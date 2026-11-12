#pragma once
#include "../Engine/BoomBapEngine.h"
#include <atomic>
#include <functional>

// =====================================================================
// The MIDI input router (FEEDBACK item 25). Pure C++ over the engine
// on purpose: the routing, the live-record step maths and the
// step-record cursor are headless-testable with synthetic events.
//
// TIMING RULE (D's, from testing other apps): a recorded event's step
// is resolved from the ENGINE's clock - the MidiClock the engine
// publishes each block - plus the event's own timestamp inside the
// audio block (its MidiBuffer sample position). Callback arrival
// time is never consulted, so a late or early OS callback cannot
// drag a hit onto the wrong step.
// =====================================================================

namespace boombap
{

struct MidiRouteEvent
{
    enum Type { NoteOn, NoteOff, ControlChange } type = NoteOff;
    int channel = 1;        // 1..16
    int number = 0;         // note or CC number
    int value = 0;          // velocity / CC value 0..127
    int sampleOffset = 0;   // position inside the audio block
};

class MidiRouter
{
public:
    explicit MidiRouter (BoomBapEngine& e) : engine (e) {}

    // Arms / modes (set from the UI thread; read on the audio thread).
    std::atomic<bool> recArm { false };       // REC INPUT
    std::atomic<bool> stepRecord { false };   // STEP-RECORD mode
    // The melodic target for non-pad notes: BoomBapEngine channel
    // indices (Kick/Snare/... rows play melodic on their ghost lanes,
    // Synth = 32, Piano = 33). The UI keeps this on the selected
    // playable channel.
    std::atomic<int> melodicChannel { BoomBapEngine::kSynthChannel };
    std::atomic<int> stepCursor { 0 };        // step-record write head

    // Hooks (wired by the service; called on the audio thread):
    // every event, for the input monitor.
    std::function<void (const MidiRouteEvent&)> onEvent;
    // CCs and notes offered to the Learn map (after playing/recording
    // is done - a bound note still plays unless Learn swallows it).
    std::function<void (const MidiRouteEvent&)> onControl;
    // Learn capture: when it returns true the event is being bound
    // and must NOT play or record.
    std::function<bool (const MidiRouteEvent&)> onLearnCapture;

    // MIDI velocities -> the pattern's step tiers (0 off / 90 normal
    // / 127 accent - the engine's own convention). Only hits quieter
    // than the normal tier keep their raw value (a ghost note).
    static int velocityTier (int vel127)
    {
        if (vel127 >= 100) return 127;
        if (vel127 >= 40) return 90;
        return juceClamp127 (vel127);
    }

    // Which step does an event at `sampleOffset` in this block land
    // on? Uses the published clock: the step sounding at block start,
    // distance to the next step, and the step length. Nearest step
    // wins, halves round to the step already sounding - a hit a hair
    // before the next step's midpoint still belongs to the groove it
    // was played against.
    static int resolveStep (const BoomBapEngine::MidiClock& clk, int sampleOffset)
    {
        if (clk.step < 0 || clk.samplesPerStep <= 0.0) return -1;
        const double pos = (double) clk.intoStepSamples + (double) sampleOffset;
        if (pos < clk.samplesPerStep * 0.5) return clk.step;
        // Steps after the sounding one, counted from the next step's
        // start boundary (nextStepInSamples from block start).
        const double pastNext = (double) sampleOffset
                              - (double) clk.nextStepInSamples
                              + clk.samplesPerStep * 0.5;
        int advance = 1;
        if (pastNext > 0.0)
            advance += (int) (pastNext / clk.samplesPerStep);
        return (clk.step + advance) % BoomBapEngine::kSteps;
    }

    void processEvent (const MidiRouteEvent& ev)
    {
        if (onEvent) onEvent (ev);
        // Learn capture first: a binding wiggle must not play, record
        // or drive the control it is about to replace.
        if ((ev.type == MidiRouteEvent::ControlChange
             || (ev.type == MidiRouteEvent::NoteOn && ev.value > 0))
            && onLearnCapture && onLearnCapture (ev))
            return;

        switch (ev.type)
        {
            case MidiRouteEvent::NoteOn:
                if (ev.value <= 0) { noteOff (ev); break; }
                noteOn (ev);
                break;
            case MidiRouteEvent::NoteOff:
                noteOff (ev);
                break;
            case MidiRouteEvent::ControlChange:
                if (onControl) onControl (ev);
                break;
        }
    }

    // The step-record cursor, exposed for the UI/tests.
    int cursorStep() const { return stepCursor.load(); }
    void setCursorStep (int s) { stepCursor.store (clampStep (s)); }

private:
    static int juceClamp127 (int v) { return v < 0 ? 0 : v > 127 ? 127 : v; }
    static int clampStep (int s)
    {
        const int k = BoomBapEngine::kSteps;
        return ((s % k) + k) % k;
    }

    void advanceCursor() { stepCursor.store (clampStep (stepCursor.load() + 1)); }

    void noteOn (const MidiRouteEvent& ev)
    {
        const float vel = ev.value / 127.0f;
        const auto clk = engine.getMidiClock();
        const bool playingNow = clk.playing && ! clk.paused;

        if (ev.number >= 36 && ev.number <= 51)
        {
            // Pads 1-16 on ANY channel (the MPK Mini mk1's factory
            // Program 1 layout - zero setup, reference Part 5.1).
            const int pad = ev.number - 36;
            engine.triggerPad (pad, vel);
            if (recArm.load() && playingNow)
            {
                const int step = resolveStep (clk, ev.sampleOffset);
                if (step >= 0)
                    engine.recordPadStep (pad, step, velocityTier (ev.value));
            }
            else if (stepRecord.load() && ! clk.playing)
            {
                engine.stepRecordPadStep (pad, stepCursor.load(),
                                          velocityTier (ev.value));
                advanceCursor();
            }
        }
        else
        {
            const int ch = melodicChannel.load();
            noteChannel[ev.number & 127] = ch;
            engine.liveNoteOn (ch, ev.number, vel);
            if (recArm.load() && playingNow)
            {
                const int step = resolveStep (clk, ev.sampleOffset);
                if (step >= 0)
                    engine.recordNoteAtStep (ch, ev.number, vel, step);
            }
            else if (stepRecord.load() && ! clk.playing)
            {
                if (engine.stepRecordNoteAt (ch, ev.number, vel, stepCursor.load()))
                    advanceCursor();
            }
        }
        // A bound note (mute, Play, pattern...) fires as well as
        // playing - unless Learn swallowed it above.
        if (onControl) onControl (ev);
    }

    void noteOff (const MidiRouteEvent& ev)
    {
        if (ev.number >= 36 && ev.number <= 51) return; // pads are one-shots
        const int ch = noteChannel[ev.number & 127];
        engine.liveNoteOff (ch >= 0 ? ch : melodicChannel.load(), ev.number);
        if (onControl) onControl (ev);
    }

    BoomBapEngine& engine;
    // Which channel each held note was started on, so its note-off
    // lands right even if the selection moves mid-hold.
    int noteChannel[128];
    struct NoteChannelInit
    {
        NoteChannelInit (int* p) { for (int i = 0; i < 128; ++i) p[i] = -1; }
    };
    NoteChannelInit noteChannelInit { noteChannel };
};

} // namespace boombap
