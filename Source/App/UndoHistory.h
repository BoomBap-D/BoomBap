#pragma once

#include <juce_core/juce_core.h> // UndoManager + UndoableAction live in juce_core
#include <functional>

namespace boombap
{
// ============================================================================
// UndoHistory (Pass 2, item 38): UNLIMITED undo/redo over project-changing
// actions. Every action is a whole-project snapshot pair (the JSON the
// project file would write): undoable gestures capture the state before
// and after, and undo/redo re-apply a snapshot through the owner's normal
// project-load path, so EVERY kind of edit - steps, clips, mixer and strip
// settings, structure, kit loads, trims - is covered by construction.
// Transport, playback and live performance never touch the project JSON,
// so they are never captured. History is unlimited (no transaction cap).
// ============================================================================
class UndoHistory
{
public:
    using SnapshotFn = std::function<juce::String()>;
    using ApplyFn = std::function<void (const juce::String&)>;

    void setCallbacks (SnapshotFn snap, ApplyFn apply)
    {
        snapshotFn = std::move (snap);
        applyFn = std::move (apply);
    }

    // A gesture spans one user action that may fire many small changes
    // (a drag, a split gesture): begin captures the "before" state once,
    // end commits one transaction if anything actually changed.
    void beginGesture()
    {
        if (gestureOpen || snapshotFn == nullptr) return;
        gestureOpen = true;
        gestureBefore = snapshotFn();
    }
    void endGesture()
    {
        if (! gestureOpen) return;
        gestureOpen = false;
        if (snapshotFn == nullptr || applying) return;
        const juce::String after = snapshotFn();
        if (after != gestureBefore)
            push (gestureBefore, after);
    }
    bool gestureIsOpen() const { return gestureOpen; }

    // One-shot: run fn as a single undoable action.
    template <typename Fn>
    void perform (Fn&& fn)
    {
        if (gestureOpen) { fn(); return; } // inside a gesture: the gesture owns it
        beginGesture();
        fn();
        endGesture();
    }

    bool canUndo() const { return undoManager.canUndo(); }
    bool canRedo() const { return undoManager.canRedo(); }

    void undo()
    {
        if (snapshotFn == nullptr || applyFn == nullptr) return;
        applying = true;
        undoManager.undo();
        applying = false;
    }
    void redo()
    {
        if (snapshotFn == nullptr || applyFn == nullptr) return;
        applying = true;
        undoManager.redo();
        applying = false;
    }

    void clear() { undoManager.clearUndoHistory(); gestureOpen = false; totalRecorded = 0; }
    bool isApplying() const { return applying; }
    // Actions recorded since the last clear (JUCE 7's UndoManager keeps no
    // running total of its own; the tests use this to prove what did and
    // did not enter the history).
    int totalItems() const { return totalRecorded; }

private:
    struct SnapshotAction : public juce::UndoableAction
    {
        SnapshotAction (UndoHistory& o, juce::String b, juce::String a)
            : owner (o), before (std::move (b)), after (std::move (a)) {}
        bool perform() override { owner.applyFn (after); return true; }
        bool undo() override { owner.applyFn (before); return true; }
        int getSizeInUnits() override { return (int) (before.length() + after.length()) / 32 + 2; }
        UndoHistory& owner;
        juce::String before, after;
    };

    void push (const juce::String& before, const juce::String& after)
    {
        undoManager.beginNewTransaction();
        undoManager.perform (new SnapshotAction (*this, before, after));
        ++totalRecorded;
    }

    juce::UndoManager undoManager; // unlimited: no transaction limit set
    int totalRecorded = 0;
    SnapshotFn snapshotFn;
    ApplyFn applyFn;
    bool gestureOpen = false;
    bool applying = false;
    juce::String gestureBefore;

    friend struct SnapshotAction;
};
} // namespace boombap
