#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <set>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"

// ===========================================================================
// ROUND 5 (item 105) - the KEYBOARD production tab.
//
// D's words: "one of the most important things right now for me, to
// test certain functions, even if we don't have MIDI yet." The tab
// sits in the Production Area beside STEP SEQUENCER / PAD BANK /
// AUDIO MULTITRACK, in the same grammar (docked in the bottom
// section, pop-out capable through the shared tab machinery).
//
//   (a) ON-SCREEN KEYS: three octaves (C3..C6 at shift 0) with an
//       octave shift; the middle C is C4 (item 27: his centre note).
//       Mouse: press sounds, release ends, drag glissandos; velocity
//       comes from WHERE on the key you press - top = soft (0.25),
//       bottom = hard (1.0).
//   (b) ROUTING: notes play the SELECTED track (the one global
//       selection): sample/drum rows sound pitched around their
//       root (C4 plays the sound at rest), Synth/Piano play their
//       voices at pitch. Everything goes through the engine's ONE
//       live-note funnel (BoomBapEngine::liveNoteOn / liveNoteOff),
//       so mouse, QWERTY and (Pass 5) MIDI are the same door.
//   (c) QWERTY: the two-row layout of item 27 / his Reaper VMK -
//       the Z row is the lower octave (from the board's lowest C),
//       the Q row the octave above it; sharps on S D / G H J and
//       2 3 / 5 6 7. Capture is structural: key events only reach
//       this panel while it holds keyboard focus, which is exactly
//       "the keyboard tab is active AND no text editor has focus" -
//       typing a track name can never fire a note, and losing focus
//       (or the app) releases every held note (focusLost below).
//   (d) STEP-WRITE lives with the owner (MainComponent): it reads
//       isWriteArmed() on each note-on and writes the engine's roll
//       lanes at the playhead (BoomBapEngine::stepWriteNote), one
//       undo gesture per pass. This panel is the input device; it
//       owns no project state.
//   (e) NO MIDI here (Pass 5) - by construction above: the funnel
//       is the only entry point this panel calls.
// ===========================================================================

namespace boombap
{
// The board's lowest note at octave shift 0 is C3 (48); the visible
// range is three octaves up to C6 (84). Centre C4 (60) - item 27.
inline constexpr int kKeyboardLowestNote = 48;   // C3
inline constexpr int kKeyboardHighestNote = 84;  // C6
inline constexpr int kKeyboardRootNote = 60;     // C4, the centre

inline int keyboardLowestNote (int octaveShift)
{
    return kKeyboardLowestNote + 12 * juce::jlimit (-2, 2, octaveShift);
}

// The two-row QWERTY map (item 27's VMK + FL convention): the letter
// at `rowOffset` semitones from the row's starting C. -1 = not a
// playing key. Z row starts on the board's lowest C (the lower
// octave), Q row one octave above it (the upper octave).
inline int qwertyRowOffset (juce::juce_wchar c)
{
    switch (c)
    {
        // lower row (Z...): naturals with sharps on the home row
        case 'z': return 0;  case 's': return 1;  case 'x': return 2;
        case 'd': return 3;  case 'c': return 4;  case 'v': return 5;
        case 'g': return 6;  case 'b': return 7;  case 'h': return 8;
        case 'n': return 9;  case 'j': return 10; case 'm': return 11;
        case ',': return 12; case 'l': return 13; case '.': return 14;
        case ';': return 15; case '/': return 16;
        // upper row (Q...): naturals with sharps on the number row
        case 'q': return 0;  case '2': return 1;  case 'w': return 2;
        case '3': return 3;  case 'e': return 4;  case 'r': return 5;
        case '5': return 6;  case 't': return 7;  case '6': return 8;
        case 'y': return 9;  case '7': return 10; case 'u': return 11;
        case 'i': return 12; case '9': return 13; case 'o': return 14;
        case '0': return 15; case 'p': return 16;
        default: return -1;
    }
}
inline bool qwertyIsUpperRow (juce::juce_wchar c)
{
    switch (c)
    {
        case 'q': case '2': case 'w': case '3': case 'e': case 'r':
        case '5': case 't': case '6': case 'y': case '7': case 'u':
        case 'i': case '9': case 'o': case '0': case 'p': return true;
        default: return false;
    }
}
// The MIDI note a QWERTY character plays at an octave shift, or -1.
inline int qwertyNoteForChar (juce::juce_wchar c, int octaveShift)
{
    const int off = qwertyRowOffset (c);
    if (off < 0) return -1;
    const int base = keyboardLowestNote (octaveShift) + (qwertyIsUpperRow (c) ? 12 : 0);
    return base + off;
}
// The character that plays a note (for painting on the keys), or 0.
// The rows overlap at the octave joint (the Z row reaches C4..E4,
// the Q row starts at C4): the shared notes wear the Q row's letter,
// so the centre C4 reads Q - the upper row is the board's top voice.
inline juce::juce_wchar qwertyCharForNote (int note, int octaveShift)
{
    static const juce::juce_wchar chars[] = { 'q','2','w','3','e','r','5','t','6','y','7','u','i','9','o','0','p',
                                              'z','s','x','d','c','v','g','b','h','n','j','m',',','l','.',';','/' };
    for (auto c : chars)
        if (qwertyNoteForChar (c, octaveShift) == note) return c;
    return 0;
}
} // namespace boombap

class KeyboardPanel : public juce::Component, private juce::Timer
{
public:
    explicit KeyboardPanel (boombap::BoomBapEngine& eng) : engine (eng)
    {
        titleLabel.setText ("KEYBOARD", juce::dontSendNotification);
        titleLabel.setFont (juce::Font (13.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        addAndMakeVisible (titleLabel);

        targetLabel.setColour (juce::Label::textColourId, boombap::ui::text());
        targetLabel.setFont (juce::Font (13.0f));
        addAndMakeVisible (targetLabel);

        writeToggle.setButtonText ("WRITE");
        writeToggle.setClickingTogglesState (true);
        writeToggle.setTooltip ("Step-write: while the transport plays, notes you play land "
                                "in the selected track's roll lane at the playhead");
        writeToggle.onClick = [this] { refreshWriteUi(); };
        addAndMakeVisible (writeToggle);

        octDownButton.onClick = [this] { setOctaveShift (octaveShift - 1); };
        octUpButton.onClick   = [this] { setOctaveShift (octaveShift + 1); };
        addAndMakeVisible (octDownButton);
        addAndMakeVisible (octUpButton);
        octLabel.setJustificationType (juce::Justification::centred);
        octLabel.setColour (juce::Label::textColourId, boombap::ui::text());
        octLabel.setFont (juce::Font (13.0f, juce::Font::bold));
        addAndMakeVisible (octLabel);

        hintLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        hintLabel.setFont (juce::Font (11.5f));
        hintLabel.setText ("Keys + two QWERTY rows play here - lower on a key hits harder.",
                           juce::dontSendNotification);
        addAndMakeVisible (hintLabel);

        setWantsKeyboardFocus (true);
        refreshOctaveUi();
        refreshWriteUi();
        refreshTarget();
        startTimerHz (10); // the held-note backstop + the target readout
    }

    ~KeyboardPanel() override
    {
        stopTimer();
        releaseAllNotes();
    }

    // ---- owner wiring (MainComponent) ----
    // The one selected channel (Rounds 1-4's single selection state),
    // read at press time; each held note remembers the channel it
    // started on, so a selection change mid-hold still ends it on
    // the right channel.
    std::function<int()> getSelectedChannel;
    std::function<void (int, int, float)> onNoteOn;  // (channel, midiNote, velocity)
    std::function<void (int, int)> onNoteOff;        // (channel, midiNote)

    bool isWriteArmed() const { return writeToggle.getToggleState(); }
    // Test seam (the Round 4 button-seam pattern): the real toggle.
    juce::ToggleButton& writeButtonForTest() { return writeToggle; }
    int  getOctaveShift() const { return octaveShift; }
    void setOctaveShift (int shift)
    {
        shift = juce::jlimit (-2, 2, shift);
        if (shift == octaveShift) { refreshOctaveUi(); return; }
        releaseAllNotes(); // a held note's number must never be re-read under a new shift
        octaveShift = shift;
        refreshOctaveUi();
        repaint();
    }

    // Called by the owner when the selection moves: the header names
    // the track the keys are about to play.
    void refreshTarget()
    {
        const int ch = getSelectedChannel ? getSelectedChannel() : 0;
        const bool playable = boombap::BoomBapEngine::channelIsPlayable (ch);
        targetLabel.setText ("Playing: " + juce::String (engine.channelDisplayName (ch))
                                 + (playable ? "" : "  (this channel has no notes - pick a track)"),
                             juce::dontSendNotification);
    }

    // ---- key geometry (one source for painting, mouse and tests) ----
    // The keys area sits under the header; white keys span it fully.
    juce::Rectangle<int> keysArea() const
    {
        return getLocalBounds().withTop (kHeaderH).reduced (8, 6);
    }
    static bool isBlackKey (int note)
    {
        const int pc = ((note % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }
    // White-key index of a note within the visible range (0-based
    // from the board's lowest C), or -1 when off the board.
    int whiteIndexOf (int note) const
    {
        if (note < lowestNote() || note > highestNote()) return -1;
        int idx = 0;
        for (int n = lowestNote(); n < note; ++n) if (! isBlackKey (n)) ++idx;
        return idx;
    }
    int lowestNote() const  { return boombap::keyboardLowestNote (octaveShift); }
    int highestNote() const { return lowestNote() + (boombap::kKeyboardHighestNote - boombap::kKeyboardLowestNote); }
    int whiteKeyCount() const
    {
        int n = 0;
        for (int note = lowestNote(); note <= highestNote(); ++note) if (! isBlackKey (note)) ++n;
        return n;
    }
    juce::Rectangle<float> keyRect (int note) const
    {
        const auto area = keysArea().toFloat();
        const float ww = area.getWidth() / (float) whiteKeyCount();
        if (isBlackKey (note))
        {
            // a black key hangs over the boundary after the white key below it
            const int wi = whiteIndexOf (note - 1);
            if (wi < 0) return {};
            const float x = area.getX() + (float) (wi + 1) * ww - ww * 0.31f;
            return { x, area.getY(), ww * 0.62f, area.getHeight() * 0.62f };
        }
        const int wi = whiteIndexOf (note);
        if (wi < 0) return {};
        return { area.getX() + (float) wi * ww, area.getY(), ww, area.getHeight() };
    }
    // The note at a point (black keys win - they are drawn on top),
    // plus where within the key the point fell vertically (0..1) for
    // the press velocity. -1 when the point is on no key.
    int noteAt (juce::Point<int> p, float& yFracOut) const
    {
        const auto area = keysArea();
        if (! area.contains (p)) { yFracOut = 0.0f; return -1; }
        for (int n = lowestNote(); n <= highestNote(); ++n)
            if (isBlackKey (n) && keyRect (n).contains (p.toFloat()))
            {
                const auto r = keyRect (n);
                yFracOut = juce::jlimit (0.0f, 1.0f, (p.y - r.getY()) / juce::jmax (1.0f, r.getHeight()));
                return n;
            }
        for (int n = lowestNote(); n <= highestNote(); ++n)
            if (! isBlackKey (n) && keyRect (n).contains (p.toFloat()))
            {
                const auto r = keyRect (n);
                yFracOut = juce::jlimit (0.0f, 1.0f, (p.y - r.getY()) / juce::jmax (1.0f, r.getHeight()));
                return n;
            }
        yFracOut = 0.0f;
        return -1;
    }

    bool isNoteHeld (int note) const { return heldNotes.count (note) > 0; }
    int  heldNoteCount() const { return (int) heldNotes.size(); }

    // ---- the note funnel's panel side --------------------------------
    // Mouse and QWERTY both end here: pressNote / releaseNote are the
    // ONLY paths to the owner's callbacks (asserted in the suite).
    void pressNote (int note, float velocity)
    {
        if (note < 0 || note > 127) return;
        if (heldNotes.count (note)) return; // a repeat (or a second finger) never re-triggers
        const int ch = getSelectedChannel ? getSelectedChannel() : 0;
        heldNotes[note] = ch;
        if (onNoteOn) onNoteOn (ch, note, juce::jlimit (0.05f, 1.0f, velocity));
        repaint();
    }
    void releaseNote (int note)
    {
        auto it = heldNotes.find (note);
        if (it == heldNotes.end()) return;
        const int ch = it->second;
        heldNotes.erase (it);
        if (onNoteOff) onNoteOff (ch, note);
        repaint();
    }
    void releaseAllNotes()
    {
        if (heldNotes.empty()) return;
        auto held = heldNotes;
        heldNotes.clear();
        for (auto& kv : held) if (onNoteOff) onNoteOff (kv.second, kv.first);
        repaint();
    }

    // Velocity from the press height: top of the key = soft (0.25),
    // bottom = hard (1.0). Documented in ROUND-5-NOTES.
    static float velocityForY (float yFrac) { return 0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, yFrac); }

    // ---- mouse --------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        pressAt (e.getPosition());
    }
    void mouseDrag (const juce::MouseEvent& e) override { pressAt (e.getPosition()); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (mouseNote >= 0) { releaseNote (mouseNote); mouseNote = -1; }
    }
    // The press behaviour, shared by the mouse and the test seam:
    // entering a new key ends the old one first (glissando), and a
    // press in the header (or empty space) does nothing.
    void pressAt (juce::Point<int> p)
    {
        float yFrac = 0.0f;
        const int note = noteAt (p, yFrac);
        if (note == mouseNote) return;
        if (mouseNote >= 0) releaseNote (mouseNote);
        mouseNote = note;
        if (note >= 0) pressNote (note, velocityForY (yFrac));
    }
    // Test seam: a press at the centre of a key, at a chosen height.
    void pressKeyForTest (int note, float yFrac = 1.0f)
    {
        if (mouseNote >= 0) releaseNote (mouseNote);
        mouseNote = note;
        if (note >= 0) pressNote (note, velocityForY (yFrac));
    }
    void releaseMouseForTest()
    {
        if (mouseNote >= 0) { releaseNote (mouseNote); mouseNote = -1; }
    }

    // ---- QWERTY -------------------------------------------------------
    // Key events reach this panel only while it holds keyboard focus
    // (see the class comment): an editor anywhere else consumes its
    // keys first, and a hidden tab's panel is never focused.
    bool keyPressed (const juce::KeyPress& key) override
    {
        // octave arrows (item 27's VMK grammar) under the same focus rule
        if (key.getKeyCode() == juce::KeyPress::leftKey)  { setOctaveShift (octaveShift - 1); return true; }
        if (key.getKeyCode() == juce::KeyPress::rightKey) { setOctaveShift (octaveShift + 1); return true; }
        const auto c = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase (
            (juce::juce_wchar) key.getTextCharacter());
        if (c == 0) return false;
        const int note = boombap::qwertyNoteForChar (c, octaveShift);
        if (note < 0) return false;
        if (qwertyHeld.count (c)) return true; // auto-repeat never re-triggers
        qwertyHeld[c] = note;
        pressNote (note, 0.9f); // the lanes' own velocity convention
        return true;
    }
    bool keyStateChanged (bool) override
    {
        reconcileQwerty();
        return false;
    }
    void focusLost (FocusChangeType) override { releaseAllNotes(); }
    // Defence in depth for "no stuck notes, ever": if the tab is
    // hidden (tab switch, pop-out shuffle) while a note is held by
    // any path - mouse included, which has no reconcile poll - the
    // notes end with the tab.
    void visibilityChanged() override
    {
        if (! isVisible()) releaseAllNotes();
    }

    // Test seam: the same entry the real key events use, with the
    // press built the way a real key event arrives (key code AND
    // text character set).
    bool qwertyDownForTest (juce::juce_wchar c)
    {
        const auto lc = juce::CharacterFunctions::toLowerCase (c);
        return keyPressed (juce::KeyPress ((int) lc, juce::ModifierKeys(), lc));
    }
    void qwertyUpForTest (juce::juce_wchar c)
    {
        auto it = qwertyHeld.find (c);
        if (it != qwertyHeld.end())
        {
            releaseNote (it->second);
            qwertyHeld.erase (it);
        }
    }

    // ---- painting -------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        g.fillAll (bg());
        const auto area = keysArea().toFloat();
        for (int n = lowestNote(); n <= highestNote(); ++n)
        {
            if (isBlackKey (n)) continue;
            auto r = keyRect (n);
            const bool held = isNoteHeld (n);
            const bool centre = n == lowestNote() + 12; // the Q row's C (C4 at shift 0)
            g.setColour (held ? accent() : (centre ? panelAlt().brighter (0.25f) : panelAlt()));
            g.fillRoundedRectangle (r.reduced (0.75f), 4.0f);
            g.setColour (border().withAlpha (0.8f));
            g.drawRoundedRectangle (r.reduced (0.75f), 4.0f, 1.0f);
            // the QWERTY letter (top) and the note name (bottom, C keys
            // + every white in the Q row's octave for orientation)
            const auto letter = boombap::qwertyCharForNote (n, octaveShift);
            if (letter != 0)
            {
                g.setColour (held ? juce::Colour (0xff14100a) : muted());
                g.setFont (juce::Font (juce::jmin (15.0f, r.getWidth() * 0.52f), juce::Font::bold));
                g.drawText (juce::String::charToString (letter).toUpperCase(),
                            r.reduced (2.0f).withHeight (20.0f), juce::Justification::centredTop);
            }
            if (n % 12 == 0 || centre)
            {
                g.setColour (held ? juce::Colour (0xff14100a)
                                  : (centre ? accentHi() : muted()));
                g.setFont (juce::Font (juce::jmin (14.0f, r.getWidth() * 0.5f),
                                       centre ? juce::Font::bold : juce::Font::plain));
                g.drawText (noteName (n), r.reduced (2.0f).withTrimmedTop (r.getHeight() - 24.0f),
                            juce::Justification::centredBottom);
            }
        }
        for (int n = lowestNote(); n <= highestNote(); ++n)
        {
            if (! isBlackKey (n)) continue;
            auto r = keyRect (n);
            const bool held = isNoteHeld (n);
            g.setColour (held ? accentHi() : juce::Colour (0xff101014));
            g.fillRoundedRectangle (r.reduced (0.75f), 3.0f);
            g.setColour (border().withAlpha (0.9f));
            g.drawRoundedRectangle (r.reduced (0.75f), 3.0f, 1.0f);
            const auto letter = boombap::qwertyCharForNote (n, octaveShift);
            if (letter != 0)
            {
                g.setColour (held ? juce::Colour (0xff14100a) : muted().brighter (0.4f));
                g.setFont (juce::Font (juce::jmin (12.0f, r.getWidth() * 0.5f), juce::Font::bold));
                g.drawText (juce::String::charToString (letter).toUpperCase(),
                            r.reduced (1.0f).withHeight (16.0f), juce::Justification::centredTop);
            }
        }
        (void) area;
    }

    void resized() override
    {
        titleLabel.setBounds (8, 4, 92, 26);
        targetLabel.setBounds (104, 4, 560, 26);
        hintLabel.setBounds (getWidth() / 2 - 60, 4, getWidth() / 2 - 380, 26);
        octDownButton.setBounds (getWidth() - 300, 4, 58, 26);
        octLabel.setBounds (getWidth() - 238, 4, 118, 26);
        octUpButton.setBounds (getWidth() - 116, 4, 58, 26);
        writeToggle.setBounds (getWidth() - 412, 4, 100, 26);
    }

private:
    static constexpr int kHeaderH = 36;

    static juce::String noteName (int note)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
    }

    void refreshOctaveUi()
    {
        const int centre = lowestNote() + 12; // the Q row's C = the board's middle C
        octLabel.setText ("OCT " + juce::String (octaveShift > 0 ? "+" : "")
                              + juce::String (octaveShift) + "  " + noteName (centre),
                          juce::dontSendNotification);
        octDownButton.setEnabled (octaveShift > -2);
        octUpButton.setEnabled (octaveShift < 2);
    }
    void refreshWriteUi()
    {
        writeToggle.setColour (juce::TextButton::buttonOnColourId, boombap::ui::accent());
        writeToggle.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff14100a));
    }

    // QWERTY releases arrive through keyStateChanged; this compares
    // the held set against the keys physically down and releases the
    // difference. The 10 Hz timer runs it as a backstop, so a release
    // the message loop somehow swallowed still ends the note.
    void reconcileQwerty()
    {
        if (qwertyHeld.empty()) return;
        std::vector<juce::juce_wchar> gone;
        for (auto& kv : qwertyHeld)
            if (! juce::KeyPress::isKeyCurrentlyDown ((int) kv.first))
                gone.push_back (kv.first);
        for (auto c : gone)
        {
            releaseNote (qwertyHeld[c]);
            qwertyHeld.erase (c);
        }
    }

    void timerCallback() override
    {
        reconcileQwerty();
        refreshTarget();
    }

    boombap::BoomBapEngine& engine;
    juce::Label titleLabel, targetLabel, octLabel, hintLabel;
    juce::TextButton octDownButton { "OCT -" }, octUpButton { "OCT +" };
    juce::ToggleButton writeToggle;
    int octaveShift = 0;
    int mouseNote = -1;
    std::map<int, int> heldNotes;              // midi note -> channel it started on
    std::map<juce::juce_wchar, int> qwertyHeld; // character -> midi note it started

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardPanel)
};
