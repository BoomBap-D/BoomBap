#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "MidiLearn.h"
#include "MidiPresets.h"
#include "MidiRouter.h"
#include "../AppSettings.h"

// =====================================================================
// The MIDI service (FEEDBACK item 25): everything JUCE-shaped about
// the MIDI build in one place.
//
//   STANDALONE owns physical devices: it is a MidiInputCallback;
//   device events land in a lock-free FIFO with a hi-res timestamp and
//   the AUDIO callback drains them into the router, converting the
//   timestamp into a sample offset inside the current block - so the
//   router still times records by the engine clock + block offset,
//   never by raw arrival.
//   PLUGIN owns no devices (the host delivers); the processor hands
//   it the host MidiBuffer, whose timestamps already ARE block
//   offsets.
//
// The Learn map application and the learn-bind itself run on the
// MESSAGE thread (drainActions): the audio thread only queues raw
// events, so no binding vector or target is ever touched from the
// audio callback.
// =====================================================================

namespace boombap
{

class MidiService : private juce::MidiInputCallback
{
public:
    explicit MidiService (BoomBapEngine& e) : router (e)
    {
        router.onEvent = [this] (const MidiRouteEvent& ev) { noteMonitor (ev); };
        router.onControl = [this] (const MidiRouteEvent& ev) { queueControl (ev); };
        router.onLearnCapture = [this] (const MidiRouteEvent& ev) { return tryLearnCapture (ev); };
    }
    ~MidiService() override { closeDevices(); }

    MidiRouter router;
    MidiMap map;
    // Set by MainComponent: the controls Learn can bind. drainActions
    // drives them; null in the plugin without an editor (bound
    // controls simply do nothing until the editor opens).
    const MidiTargetRegistry* registry = nullptr;

    // -----------------------------------------------------------------
    // Devices (standalone)
    // -----------------------------------------------------------------
    static juce::Array<juce::MidiDeviceInfo> availableInputs()
    {
        return juce::MidiInput::getAvailableDevices();
    }
    juce::String selectedDeviceId() const { return deviceId; }
    bool allInputsOpen() const { return allInputs; }
    bool anyDeviceOpen() const { return ! openDevices.empty(); }

    bool openDevice (const juce::String& identifier)
    {
        closeDevices();
        allInputs = false;
        deviceId = identifier;
        if (identifier.isEmpty()) return false;
        auto dev = juce::MidiInput::openDevice (identifier, this);
        if (dev == nullptr) return false;
        dev->start();
        openDevices.push_back (std::move (dev));
        return true;
    }
    void openAllDevices()
    {
        closeDevices();
        allInputs = true;
        deviceId.clear();
        for (const auto& info : availableInputs())
        {
            auto dev = juce::MidiInput::openDevice (info.identifier, this);
            if (dev != nullptr)
            {
                dev->start();
                openDevices.push_back (std::move (dev));
            }
        }
    }
    void closeDevices()
    {
        for (auto& d : openDevices) if (d != nullptr) d->stop();
        openDevices.clear();
    }

    // -----------------------------------------------------------------
    // Audio-thread entries
    // -----------------------------------------------------------------
    // Standalone: drain the device FIFO into the router for this
    // block. blockStartTicks = Time::getHighResolutionTicks() taken
    // at the top of the audio callback.
    void pollDeviceEvents (int numSamples, double sampleRate, double blockStartTicks)
    {
        const double tps = (double) juce::Time::getHighResolutionTicksPerSecond();
        DeviceEvent ev;
        int guard = 0;
        while (deviceFifo.read (ev) && guard++ < 4096)
        {
            double offset = (ev.ticks >= blockStartTicks)
                                ? (double) juce::jmax (0, numSamples - 1)
                                : (blockStartTicks - ev.ticks) * sampleRate / tps;
            // Older than this block: it belongs to the block where it
            // arrived - play/record it at offset 0 of this one rather
            // than dropping a hit the user played.
            if (ev.ticks < blockStartTicks && offset > (double) numSamples)
                offset = 0.0;
            ev.event.sampleOffset = juce::jlimit (0, juce::jmax (0, numSamples - 1),
                                                  (int) offset);
            router.processEvent (ev.event);
        }
    }
    // Plugin: the host buffer's timestamps are already block offsets.
    void processHostBuffer (const juce::MidiBuffer& buffer)
    {
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            MidiRouteEvent ev;
            ev.sampleOffset = meta.samplePosition;
            if (! toRouteEvent (msg, ev)) continue;
            router.processEvent (ev);
        }
    }

    // -----------------------------------------------------------------
    // Learn state (UI thread writes; capture runs on the audio thread
    // and only QUEUES - the bind lands in drainActions)
    // -----------------------------------------------------------------
    std::atomic<bool> learnArmed { false };
    void setPendingTarget (const juce::String& targetId)
    {
        std::strncpy (pendingTargetBuf, targetId.toRawUTF8(), sizeof (pendingTargetBuf) - 1);
        pendingTargetBuf[sizeof (pendingTargetBuf) - 1] = 0;
        hasPendingTarget.store (targetId.isNotEmpty());
    }
    juce::String pendingTarget() const
    {
        return hasPendingTarget.load() ? juce::String (pendingTargetBuf) : juce::String();
    }
    void cancelPendingTarget() { hasPendingTarget.store (false); }
    // Bumped when a bind happened; the string carries the notice
    // ("CC 16 now drives Glue Threshold - it was bound to ...").
    int learnNoticeCount() const { return learnNoticeCountA.load(); }
    juce::String learnNotice() const { return lastLearnNotice; }
    // Which layer the panel's clicks bind into (Global default).
    MidiLayer learnLayer = MidiLayer::Global;

    // -----------------------------------------------------------------
    // Monitor (written on the audio thread via atomics + a small
    // lock-free text ring; read by the window's timer)
    // -----------------------------------------------------------------
    int activityCount() const { return activityCountA.load(); }
    juce::String lastEventText() const
    {
        return juce::String (lastEventBuf);
    }
    std::vector<juce::String> recentEvents (int maxLines) const
    {
        std::vector<juce::String> out;
        const int n = monitorCount.load();
        for (int i = 0; i < n && (int) out.size() < maxLines; ++i)
        {
            const int idx = (monitorHead.load() - 1 - i + kMonitorLines * 2) % kMonitorLines;
            out.push_back (juce::String (monitorBuf[idx]));
        }
        return out;
    }

    // -----------------------------------------------------------------
    // Message-thread: apply queued control events to the map, land
    // queued learn binds, and return true if anything changed (the
    // caller refreshes UI + saves).
    // -----------------------------------------------------------------
    bool drainActions()
    {
        bool changed = false;
        // Learn binds first, so the wiggle that made them does not
        // also drive the control it just bound.
        PendingBind pb;
        while (bindFifo.read (pb))
        {
            MidiBinding b;
            b.sourceType = pb.isNote ? MidiBinding::NoteSource : MidiBinding::CCSource;
            b.channel = pb.channel;
            b.number = pb.number;
            b.targetId = juce::String (pb.targetId);
            juce::String stolen;
            map.learnBind (learnLayer, b, stolen);
            juce::String notice = b.sourceName() + " now drives "
                                + targetName (b.targetId);
            if (stolen.isNotEmpty())
                notice += " - it was bound to " + targetName (stolen)
                        + ", which it replaces";
            lastLearnNotice = notice;
            learnNoticeCountA.fetch_add (1);
            changed = true;
        }
        if (registry != nullptr)
        {
            MidiRouteEvent ev;
            int guard = 0;
            while (controlFifo.read (ev) && guard++ < 2048)
            {
                MidiMapEvent me;
                me.type = ev.type == MidiRouteEvent::ControlChange
                              ? MidiMapEvent::ControlChange : MidiMapEvent::Note;
                me.channel = ev.channel;
                me.number = ev.number;
                me.value = ev.value;
                me.isNoteOn = ev.type == MidiRouteEvent::NoteOn && ev.value > 0;
                if (map.applyEvent (me, *registry).isNotEmpty())
                    changed = true;
            }
        }
        return changed;
    }

    // -----------------------------------------------------------------
    // Persistence: device choice + preset + the GLOBAL layer live in
    // the app PropertiesFile. The PROJECT layer travels in the
    // .boombap file (ProjectData::midiLearnJson).
    // -----------------------------------------------------------------
    // The plugin's service never touches physical devices (the host
    // owns them) and must not overwrite the standalone's saved
    // device choice in the shared properties file.
    bool manageDevices = true;

    void loadState()
    {
        auto props = appPropertiesFile();
        if (props == nullptr) return;
        presetIndex = props->getIntValue ("midiPresetIndex", 0);
        const auto presets = allMidiPresets();
        if (presetIndex > 0 && presetIndex <= (int) presets.size())
            map.setLayerBindings (MidiLayer::Factory,
                                  presets[(size_t) presetIndex - 1].bindings);
        map.layerFromJson (MidiLayer::Global,
                           props->getValue ("midiGlobalMap", {}));
        if (manageDevices)
        {
            const auto devId = props->getValue ("midiDeviceId", {});
            const bool all = props->getBoolValue ("midiAllInputs", false);
            if (all) openAllDevices();
            else if (devId.isNotEmpty()) openDevice (devId);
        }
        router.recArm.store (props->getBoolValue ("midiRecArm", false));
    }
    void saveState()
    {
        auto props = appPropertiesFile();
        if (props == nullptr) return;
        if (manageDevices)
        {
            props->setValue ("midiDeviceId", deviceId);
            props->setValue ("midiAllInputs", allInputs);
        }
        props->setValue ("midiPresetIndex", presetIndex);
        props->setValue ("midiGlobalMap", map.layerToJson (MidiLayer::Global));
        props->setValue ("midiRecArm", router.recArm.load());
        props->saveIfNeeded();
    }
    int presetIndex = 0;   // 0 = none, else 1..presets
    void applyPreset (int idx)
    {
        presetIndex = idx;
        const auto presets = allMidiPresets();
        if (idx > 0 && idx <= (int) presets.size())
            map.setLayerBindings (MidiLayer::Factory,
                                  presets[(size_t) idx - 1].bindings);
        else
            map.setLayerBindings (MidiLayer::Factory, {});
    }
    juce::String projectLayerJson() const
    {
        return map.layerCount (MidiLayer::Project) > 0
                   ? map.layerToJson (MidiLayer::Project) : juce::String();
    }
    void setProjectLayerJson (const juce::String& json)
    {
        map.layerFromJson (MidiLayer::Project, json);
    }

private:
    // ---- MidiInputCallback: stamp and queue, nothing else (no
    // locks, no allocation beyond the message copy JUCE hands us).
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& msg) override
    {
        MidiRouteEvent ev;
        if (! toRouteEvent (msg, ev)) return;
        DeviceEvent de;
        de.event = ev;
        de.ticks = (double) juce::Time::getHighResolutionTicks();
        deviceFifo.push (de);
    }
    static bool toRouteEvent (const juce::MidiMessage& msg, MidiRouteEvent& ev)
    {
        ev.channel = msg.getChannel();
        if (msg.isNoteOn())       { ev.type = MidiRouteEvent::NoteOn;  ev.number = msg.getNoteNumber(); ev.value = msg.getVelocity(); }
        else if (msg.isNoteOff()) { ev.type = MidiRouteEvent::NoteOff; ev.number = msg.getNoteNumber(); ev.value = 0; }
        else if (msg.isController()) { ev.type = MidiRouteEvent::ControlChange; ev.number = msg.getControllerNumber(); ev.value = msg.getControllerValue(); }
        else return false;
        return true;
    }

    void noteMonitor (const MidiRouteEvent& ev)
    {
        activityCountA.fetch_add (1);
        char text[96];
        if (ev.type == MidiRouteEvent::ControlChange)
            std::snprintf (text, sizeof text, "CC %d = %d (ch %d)",
                           ev.number, ev.value, ev.channel);
        else if (ev.type == MidiRouteEvent::NoteOn)
            std::snprintf (text, sizeof text, "Note %d on, vel %d (ch %d)",
                           ev.number, ev.value, ev.channel);
        else
            std::snprintf (text, sizeof text, "Note %d off (ch %d)",
                           ev.number, ev.channel);
        std::strncpy (lastEventBuf, text, sizeof (lastEventBuf) - 1);
        lastEventBuf[sizeof (lastEventBuf) - 1] = 0;
        const int head = monitorHead.load();
        std::strncpy (monitorBuf[head % kMonitorLines], text,
                      sizeof (monitorBuf[0]) - 1);
        monitorBuf[head % kMonitorLines][sizeof (monitorBuf[0]) - 1] = 0;
        monitorHead.store (head + 1);
        monitorCount.store (juce::jmin (kMonitorLines, monitorCount.load() + 1));
    }

    void queueControl (const MidiRouteEvent& ev) { controlFifo.push (ev); }

    bool tryLearnCapture (const MidiRouteEvent& ev)
    {
        if (! learnArmed.load() || ! hasPendingTarget.load()) return false;
        PendingBind pb;
        pb.isNote = ev.type == MidiRouteEvent::NoteOn;
        pb.channel = ev.channel;
        pb.number = ev.number;
        std::strncpy (pb.targetId, pendingTargetBuf, sizeof (pb.targetId) - 1);
        pb.targetId[sizeof (pb.targetId) - 1] = 0;
        if (! bindFifo.push (pb)) return false;
        hasPendingTarget.store (false);
        return true;
    }
    juce::String targetName (const juce::String& id) const
    {
        if (registry != nullptr)
            if (const auto* t = registry->find (id)) return t->name;
        return id;
    }

    // A tiny single-producer/single-consumer ring (audio-safe).
    template <typename T, int N>
    struct Fifo
    {
        std::atomic<int> w { 0 }, r { 0 };
        T buf[N];
        bool push (const T& v)
        {
            const int wi = w.load(), ri = r.load();
            if ((wi + 1) % N == ri) return false;   // full: drop
            buf[wi] = v;
            w.store ((wi + 1) % N);
            return true;
        }
        bool read (T& out)
        {
            const int ri = r.load();
            if (ri == w.load()) return false;
            out = buf[ri];
            r.store ((ri + 1) % N);
            return true;
        }
    };
    struct DeviceEvent { MidiRouteEvent event; double ticks = 0.0; };
    struct PendingBind { bool isNote = false; int channel = 0, number = 0; char targetId[64] = {}; };

    Fifo<DeviceEvent, 2048> deviceFifo;
    Fifo<MidiRouteEvent, 2048> controlFifo;
    Fifo<PendingBind, 64> bindFifo;

    std::vector<std::unique_ptr<juce::MidiInput>> openDevices;
    juce::String deviceId;
    bool allInputs = false;

    std::atomic<bool> hasPendingTarget { false };
    char pendingTargetBuf[64] = {};
    juce::String lastLearnNotice;               // message thread
    std::atomic<int> learnNoticeCountA { 0 };

    std::atomic<int> activityCountA { 0 };
    char lastEventBuf[96] = {};
    static constexpr int kMonitorLines = 10;
    char monitorBuf[kMonitorLines][96] = {};
    std::atomic<int> monitorHead { 0 }, monitorCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiService)
};

} // namespace boombap
