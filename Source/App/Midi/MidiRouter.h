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
    explicit MidiRouter (BoomBapEngine& e) : engine (e)
    {
        for (int ch = 1; ch <= 16; ++ch)
            for (int n = 0; n < 128; ++n)
            {
                learnedPadSnap[ch][n].store (-1);
                learnedKeySnap[ch][n].store (-1);
            }
    }

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
    // Correspondence (item 147): a melodic pitch the controller is
    // holding - the UI lights that key on the board (display only).
    std::function<void (int, bool)> onVisualNote;

    // -----------------------------------------------------------------
    // Learned note routing (item 147 fix). The MidiMap lives on the
    // message thread; the router runs on the audio thread, so the
    // service rebuilds this snapshot whenever the map changes
    // (MidiService::syncRouterSnapshot): for every (channel, note),
    // the pad or key pitch the EFFECTIVE note binding (project >
    // global > factory) drives, or -1. Bindings to CONTROLS never
    // appear here - they keep the message-thread path they always had.
    // -----------------------------------------------------------------
    std::atomic<int> learnedPadSnap[17][128];
    std::atomic<int> learnedKeySnap[17][128];

    void clearLearnedNotes()
    {
        for (int ch = 1; ch <= 16; ++ch)
            for (int n = 0; n < 128; ++n)
            {
                learnedPadSnap[ch][n].store (-1);
                learnedKeySnap[ch][n].store (-1);
            }
    }
    void setLearnedNote (bool isPad, int channel, int note, int value)
    {
        if (channel < 1 || channel > 16 || note < 0 || note > 127) return;
        (isPad ? learnedPadSnap[channel][note] : learnedKeySnap[channel][note])
            .store (value);
    }
    int learnedPadFor (int channel, int note) const
    {
        if (channel < 1 || channel > 16 || note < 0 || note > 127) return -1;
        return learnedPadSnap[channel][note].load();
    }
    int learnedKeyFor (int channel, int note) const
    {
        if (channel < 1 || channel > 16 || note < 0 || note > 127) return -1;
        return learnedKeySnap[channel][note].load();
    }

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

    // A pad hit, fixed or learned: trigger + (record | step-record)
    // with the step resolved from the engine clock, never arrival.
    void playPad (int pad, const MidiRouteEvent& ev)
    {
        const float vel = ev.value / 127.0f;
        const auto clk = engine.getMidiClock();
        const bool playingNow = clk.playing && ! clk.paused;
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

    // A melodic note at a given pitch (the note itself, or the pitch
    // a learned key binding maps it to). The pitch that SOUNDS is the
    // pitch recorded and the pitch shown lit on the board.
    void playMelodic (int pitch, const MidiRouteEvent& ev)
    {
        const float vel = ev.value / 127.0f;
        const auto clk = engine.getMidiClock();
        const bool playingNow = clk.playing && ! clk.paused;
        const int ch = melodicChannel.load();
        noteChannel[ev.number & 127] = ch;
        notePitch[ev.number & 127] = pitch;
        engine.liveNoteOn (ch, pitch, vel);
        if (onVisualNote) onVisualNote (pitch, true);
        if (recArm.load() && playingNow)
        {
            const int step = resolveStep (clk, ev.sampleOffset);
            if (step >= 0)
                engine.recordNoteAtStep (ch, pitch, vel, step);
        }
        else if (stepRecord.load() && ! clk.playing)
        {
            if (engine.stepRecordNoteAt (ch, pitch, vel, stepCursor.load()))
                advanceCursor();
        }
    }

    void noteOn (const MidiRouteEvent& ev)
    {
        // Item 147 fix: a LEARNED note plays what it was bound to - a
        // pad or a key - in place of the fixed map below. A learned hit
        // is consumed here: one source, one job, so it does not also
        // fall through to the fixed routing or the control map.
        const int lPad = learnedPadFor (ev.channel, ev.number);
        if (lPad >= 0) { playPad (lPad, ev); return; }
        const int lKey = learnedKeyFor (ev.channel, ev.number);
        if (lKey >= 0) { playMelodic (lKey, ev); return; }

        if (ev.number >= 36 && ev.number <= 51)
            // Pads 1-16 on ANY channel (the MPK Mini mk1's factory
            // Program 1 layout - zero setup, reference Part 5.1).
            playPad (ev.number - 36, ev);
        else
            playMelodic (ev.number, ev);
        // A bound note (mute, Play, pattern...) fires as well as
        // playing - unless Learn swallowed it above.
        if (onControl) onControl (ev);
    }

    void noteOff (const MidiRouteEvent& ev)
    {
        const int idx = ev.number & 127;
        if (learnedPadFor (ev.channel, ev.number) >= 0)
            return; // a learned pad is a one-shot, like a fixed pad
        if (noteChannel[idx] >= 0)
        {
            // Release the pitch this note actually started (a learned
            // key may have remapped it), on the channel it started on.
            const int pitch = notePitch[idx] >= 0 ? notePitch[idx] : ev.number;
            engine.liveNoteOff (noteChannel[idx], pitch);
            if (onVisualNote) onVisualNote (pitch, false);
            noteChannel[idx] = -1;
            notePitch[idx] = -1;
            if (onControl) onControl (ev);
            return;
        }
        if (ev.number >= 36 && ev.number <= 51) return; // fixed pads are one-shots
        engine.liveNoteOff (melodicChannel.load(), ev.number);
        if (onVisualNote) onVisualNote (ev.number, false);
        if (onControl) onControl (ev);
    }

    BoomBapEngine& engine;
    // Which channel (and pitch) each held note was started on, so its
    // note-off lands right even if the selection - or the learned map
    // - moves mid-hold.
    int noteChannel[128];
    int notePitch[128];
    struct NoteMapInit
    {
        NoteMapInit (int* a, int* b)
        {
            for (int i = 0; i < 128; ++i) { a[i] = -1; b[i] = -1; }
        }
    };
    NoteMapInit noteMapInit { noteChannel, notePitch };
};

} // namespace boombap
