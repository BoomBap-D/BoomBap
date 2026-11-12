#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

// =====================================================================
// BoomBap MIDI Learn (FEEDBACK item 25; MIDI-CONTROLLERS-REFERENCE
// Parts 4-6). Everything in this header is JUCE-light on purpose:
// the binding model, the takeover logic and the layered storage are
// headless-testable without a window or a device.
//
// LAYERS (reference Part 3): a FACTORY layer shipped with the app
// (one selectable controller preset at a time), a GLOBAL user layer
// (app settings - follows the user), and a PROJECT layer (saved in
// the .boombap file as an additive key - follows the beat). Lookup
// order is project -> global -> factory: the newest, most specific
// layer wins.
// =====================================================================

namespace boombap
{

// A normalised incoming event the map understands.
struct MidiMapEvent
{
    enum Type { Note, ControlChange } type = ControlChange;
    int channel = 0;   // 1..16
    int number = 0;    // note number or CC number
    int value = 0;     // velocity or CC value, 0..127
    bool isNoteOn = false;
};

// A learnable control. `get01/set01` work in normalised 0..1 across
// the target's own range; toggles read/write 0/1; actions fire.
struct MidiTarget
{
    enum Kind { Continuous, Toggle, Action };
    juce::String id;       // stable, e.g. "chanGain:3", "actPlay"
    juce::String name;     // human name for the mapping list
    juce::String group;    // list grouping ("Mixer", "Transport", ...)
    Kind kind = Continuous;
    std::function<float()> get01;              // Continuous + Toggle
    std::function<void (float)> set01;         // Continuous + Toggle
    std::function<void()> trigger;             // Action
};

class MidiTargetRegistry
{
public:
    void add (MidiTarget t) { targets.push_back (std::move (t)); }
    const MidiTarget* find (const juce::String& id) const
    {
        for (const auto& t : targets) if (t.id == id) return &t;
        return nullptr;
    }
    const std::vector<MidiTarget>& all() const { return targets; }
    int size() const { return (int) targets.size(); }

private:
    std::vector<MidiTarget> targets;
};

// One binding: a physical source -> a target.
struct MidiBinding
{
    enum SourceType { NoteSource, CCSource };
    enum Takeover { Absolute, Pickup };

    SourceType sourceType = CCSource;
    int channel = 0;          // 0 = any channel, else 1..16
    int number = 0;           // note or CC number
    juce::String targetId;
    Takeover takeover = Absolute;

    // Runtime takeover state (never serialised): Pickup holds the
    // control still until the physical value crosses the control's
    // current value (reference Part 4); the edge detector serves
    // toggle targets driven by CCs.
    bool pickupGrabbed = false;
    int lastPhysical = -1;

    bool matches (const MidiMapEvent& e) const
    {
        if (sourceType == NoteSource && e.type != MidiMapEvent::Note) return false;
        if (sourceType == CCSource && e.type != MidiMapEvent::ControlChange) return false;
        if (number != e.number) return false;
        return channel == 0 || channel == e.channel;
    }
    bool sameSource (const MidiBinding& o) const
    {
        return sourceType == o.sourceType && number == o.number
            && (channel == 0 || o.channel == 0 || channel == o.channel);
    }
    juce::String sourceName() const
    {
        return juce::String (sourceType == NoteSource ? "Note " : "CC ")
             + juce::String (number)
             + (channel > 0 ? " (ch " + juce::String (channel) + ")" : " (any ch)");
    }
};

enum class MidiLayer { Factory, Global, Project };

class MidiMap
{
public:
    // ---- layer contents -------------------------------------------------
    void setLayerBindings (MidiLayer layer, std::vector<MidiBinding> b)
    {
        layerRef (layer) = std::move (b);
        ++revision_;
    }
    const std::vector<MidiBinding>& layerBindings (MidiLayer layer) const
    {
        return layerRef (layer);
    }
    int layerCount (MidiLayer layer) const { return (int) layerRef (layer).size(); }
    int revision() const { return revision_; }   // UI refresh key

    static juce::String layerName (MidiLayer l)
    {
        return l == MidiLayer::Factory ? "Factory"
             : l == MidiLayer::Global  ? "Global" : "Project";
    }

    // Effective binding for a source: project beats global beats
    // factory. Returns nullptr when unbound.
    const MidiBinding* findEffective (const MidiMapEvent& e, MidiLayer* layerOut = nullptr) const
    {
        for (auto l : { MidiLayer::Project, MidiLayer::Global, MidiLayer::Factory })
            for (const auto& b : layerRef (l))
                if (b.matches (e)) { if (layerOut) *layerOut = l; return &b; }
        return nullptr;
    }
    const MidiBinding* findByTarget (const juce::String& targetId) const
    {
        for (auto l : { MidiLayer::Project, MidiLayer::Global, MidiLayer::Factory })
            for (const auto& b : layerRef (l))
                if (b.targetId == targetId) return &b;
        return nullptr;
    }

    // LEARN: bind a source to a target in the given layer. A source
    // can only drive ONE control: an existing binding for the same
    // source (in ANY layer) is stolen - removed - and its target id
    // reported so the UI can show the notice (reference Part 4's
    // steal rule; the brief trims the [Control both] option).
    // Re-binding a source to the target it already drives is a no-op.
    bool learnBind (MidiLayer layer, MidiBinding binding, juce::String& stolenTargetId)
    {
        stolenTargetId.clear();
        MidiMapEvent probe;
        probe.type = binding.sourceType == MidiBinding::NoteSource
                         ? MidiMapEvent::Note : MidiMapEvent::ControlChange;
        probe.channel = binding.channel;
        probe.number = binding.number;
        const MidiBinding* existing = findEffective (probe);
        if (existing != nullptr && existing->targetId == binding.targetId
            && existing->channel == binding.channel)
            return false;   // already bound exactly so
        if (existing != nullptr)
        {
            stolenTargetId = existing->targetId;
            removeSourceEverywhere (binding);
        }
        // A target keeps one binding per layer slot too: drop any old
        // binding for the same target in the SAME layer (re-learn).
        auto& vec = layerRef (layer);
        vec.erase (std::remove_if (vec.begin(), vec.end(),
                                   [&] (const MidiBinding& b)
                                   { return b.targetId == binding.targetId; }),
                   vec.end());
        binding.pickupGrabbed = false;
        binding.lastPhysical = -1;
        vec.push_back (std::move (binding));
        ++revision_;
        return true;
    }

    // Remove the effective binding for a source (whichever layer owns
    // it). Used by the mapping list's per-binding delete.
    bool removeEffective (const MidiBinding& probeSource)
    {
        for (auto l : { MidiLayer::Project, MidiLayer::Global, MidiLayer::Factory })
        {
            auto& vec = layerRef (l);
            for (auto it = vec.begin(); it != vec.end(); ++it)
                if (it->sameSource (probeSource))
                {
                    vec.erase (it);
                    ++revision_;
                    return true;
                }
        }
        return false;
    }
    // Remove one exact binding (layer + target + source) - the list
    // delete when the row shown is NOT the effective one.
    bool removeExact (MidiLayer layer, const juce::String& targetId,
                      const MidiBinding& probeSource)
    {
        auto& vec = layerRef (layer);
        for (auto it = vec.begin(); it != vec.end(); ++it)
            if (it->targetId == targetId && it->sameSource (probeSource))
            {
                vec.erase (it);
                ++revision_;
                return true;
            }
        return false;
    }
    void clearLayer (MidiLayer layer)
    {
        layerRef (layer).clear();
        ++revision_;
    }
    void clearUserLayers() { clearLayer (MidiLayer::Global); clearLayer (MidiLayer::Project); }

    // ---- applying events (message thread) -------------------------------
    // If the event is bound, drive its target through the registry.
    // Continuous: absolute maps the full range; Pickup holds until
    // the physical value crosses the control's current position
    // (then tracks, including direction changes after the cross).
    // Toggle: a note-on or a CC rising edge (>= 64) flips it.
    // Action: fires on a note-on or a CC rising edge.
    // Returns the target id driven, or "" when unbound.
    juce::String applyEvent (const MidiMapEvent& e, const MidiTargetRegistry& registry)
    {
        MidiLayer layer;
        const MidiBinding* found = findEffective (e, &layer);
        if (found == nullptr) return {};
        const MidiTarget* target = registry.find (found->targetId);
        if (target == nullptr) return {};
        // Mutable runtime state lives on the stored binding.
        MidiBinding& b = const_cast<MidiBinding&> (*found);
        const int prev = b.lastPhysical;
        b.lastPhysical = e.value;
        const bool pressEdge = e.type == MidiMapEvent::Note ? e.isNoteOn
                                                           : (e.value >= 64 && prev < 64);

        switch (target->kind)
        {
            case MidiTarget::Continuous:
            {
                if (e.type == MidiMapEvent::Note)
                {
                    // A note momentarily drives a continuous control:
                    // velocity on press, back to 0 on release.
                    if (target->set01)
                        target->set01 (e.isNoteOn ? e.value / 127.0f : 0.0f);
                    b.pickupGrabbed = true;
                    return target->id;
                }
                const float phys = e.value / 127.0f;
                if (b.takeover == MidiBinding::Pickup && target->get01)
                {
                    const float cur = target->get01();
                    const float curPhys = cur * 127.0f;
                    if (! b.pickupGrabbed)
                    {
                        // Grabbed when the physical control crosses
                        // (or reaches) the control's current value
                        // between the previous and this reading.
                        if (prev < 0
                                ? std::abs (e.value - curPhys) <= 1.0f
                                : (prev <= curPhys && e.value >= curPhys)
                                   || (prev >= curPhys && e.value <= curPhys))
                            b.pickupGrabbed = true;
                        else
                            return target->id;   // bound, but holding
                    }
                }
                if (target->set01) target->set01 (phys);
                return target->id;
            }
            case MidiTarget::Toggle:
                if (pressEdge && target->get01 && target->set01)
                    target->set01 (target->get01() >= 0.5f ? 0.0f : 1.0f);
                return target->id;
            case MidiTarget::Action:
                if (pressEdge && target->trigger) target->trigger();
                return target->id;
        }
        return {};
    }

    // ---- serialisation (one layer <-> JSON text) ------------------------
    static juce::String layerToJson (const std::vector<MidiBinding>& bindings)
    {
        juce::Array<juce::var> arr;
        for (const auto& b : bindings)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("src", b.sourceType == MidiBinding::NoteSource ? "note" : "cc");
            o->setProperty ("ch", b.channel);
            o->setProperty ("num", b.number);
            o->setProperty ("target", b.targetId);
            o->setProperty ("take", b.takeover == MidiBinding::Pickup ? "pickup" : "abs");
            arr.add (juce::var (o));
        }
        return juce::JSON::toString (juce::var (arr), true);
    }
    static std::vector<MidiBinding> layerFromJson (const juce::String& text)
    {
        std::vector<MidiBinding> out;
        const auto v = juce::JSON::parse (text);
        if (! v.isArray()) return out;
        for (const auto& item : *v.getArray())
        {
            if (! item.isObject()) continue;
            MidiBinding b;
            b.sourceType = item.getProperty ("src", "cc").toString() == "note"
                               ? MidiBinding::NoteSource : MidiBinding::CCSource;
            b.channel = (int) item.getProperty ("ch", 0);
            b.number = (int) item.getProperty ("num", 0);
            b.targetId = item.getProperty ("target", "").toString();
            b.takeover = item.getProperty ("take", "abs").toString() == "pickup"
                             ? MidiBinding::Pickup : MidiBinding::Absolute;
            if (b.targetId.isNotEmpty()) out.push_back (b);
        }
        return out;
    }
    juce::String layerToJson (MidiLayer layer) const
    {
        return layerToJson (layerRef (layer));
    }
    void layerFromJson (MidiLayer layer, const juce::String& text)
    {
        layerRef (layer) = layerFromJson (text);
        ++revision_;
    }

private:
    void removeSourceEverywhere (const MidiBinding& probe)
    {
        for (auto l : { MidiLayer::Factory, MidiLayer::Global, MidiLayer::Project })
        {
            auto& vec = layerRef (l);
            vec.erase (std::remove_if (vec.begin(), vec.end(),
                                       [&] (const MidiBinding& b)
                                       { return b.sameSource (probe); }),
                       vec.end());
        }
    }
    std::vector<MidiBinding>& layerRef (MidiLayer l)
    {
        return l == MidiLayer::Factory ? factory_ : l == MidiLayer::Global ? global_ : project_;
    }
    const std::vector<MidiBinding>& layerRef (MidiLayer l) const
    {
        return l == MidiLayer::Factory ? factory_ : l == MidiLayer::Global ? global_ : project_;
    }
    std::vector<MidiBinding> factory_, global_, project_;
    int revision_ = 0;
};

} // namespace boombap
