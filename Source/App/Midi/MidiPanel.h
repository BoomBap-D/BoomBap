#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MidiService.h"

// =====================================================================
// The MIDI window's content (FEEDBACK item 25): device picker, the
// preset menu with its honest setup line, REC INPUT / STEP-RECORD
// arms, the LEARN toggle, the input monitor, and the mapping list
// for all three layers. The window itself follows the Pass 1 rules
// (MainComponent::showMidiSettings clamps it to the usable area).
//
// LEARN FLOW: flip LEARN on here, click any control in the main
// window (they light up - MainComponent's learn overlay), then
// wiggle a knob / fader / pad on the controller. The bind lands in
// the layer chosen here; a stolen binding shows a notice.
// =====================================================================

namespace boombap
{

class MidiPanel : public juce::Component,
                  public juce::ListBoxModel,
                  private juce::Timer
{
public:
    MidiPanel (MidiService& svc, bool hostProvidesMidi)
        : service (svc)
    {
        auto setupLabel = [this] (juce::Label& l, const juce::String& text)
        {
            l.setText (text, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, juce::Colour (0xffe8e4da));
            addAndMakeVisible (l);
        };
        auto setupToggle = [this] (juce::ToggleButton& b, const juce::String& text)
        {
            b.setButtonText (text);
            b.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffe8e4da));
            b.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffe8b23a));
            addAndMakeVisible (b);
        };

        setupLabel (titleLabel, "MIDI");
        titleLabel.setFont (juce::Font (20.0f, juce::Font::bold));

        setupLabel (deviceLabel, "Input device");
        deviceCombo.setTextWhenNothingSelected ("No device");
        deviceCombo.onChange = [this] { deviceChosen(); };
        addAndMakeVisible (deviceCombo);
        setupToggle (allInputsToggle, "All inputs");
        allInputsToggle.onClick = [this] { allInputsClicked(); };
        hostNote.setText ("MIDI comes from the host - no device setup "
                          "in the plugin.", juce::dontSendNotification);
        hostNote.setColour (juce::Label::textColourId, juce::Colour (0xffb9b2a4));
        addAndMakeVisible (hostNote);
        hostNote.setVisible (hostProvidesMidi);
        deviceLabel.setVisible (! hostProvidesMidi);
        deviceCombo.setVisible (! hostProvidesMidi);
        allInputsToggle.setVisible (! hostProvidesMidi);

        setupLabel (presetLabel, "Controller preset (factory layer)");
        presetCombo.setTextWhenNothingSelected ("No preset");
        presetCombo.onChange = [this] { presetChosen(); };
        addAndMakeVisible (presetCombo);
        setupLabel (presetNote, "");
        presetNote.setColour (juce::Label::textColourId, juce::Colour (0xffb9b2a4));

        setupToggle (recArmToggle, "REC INPUT - record controller hits into the pattern");
        recArmToggle.onClick = [this]
        {
            service.router.recArm.store (recArmToggle.getToggleState());
            changed();
        };
        setupToggle (stepRecToggle, "STEP-RECORD - stopped: each hit writes a step and advances");
        stepRecToggle.onClick = [this]
        {
            service.router.stepRecord.store (stepRecToggle.getToggleState());
            changed();
        };
        setupLabel (cursorLabel, "Step cursor: 1");

        setupToggle (learnToggle, "LEARN - then click a control in the main window and wiggle the controller");
        learnToggle.onClick = [this]
        {
            service.learnArmed.store (learnToggle.getToggleState());
            if (! learnToggle.getToggleState()) service.cancelPendingTarget();
            changed();
        };
        setupLabel (layerLabel, "New binds go to:");
        layerCombo.addItem ("Global map (all projects)", 1);
        layerCombo.addItem ("This project only", 2);
        layerCombo.setSelectedId (1, juce::dontSendNotification);
        layerCombo.onChange = [this]
        {
            service.learnLayer = layerCombo.getSelectedId() == 2
                                     ? MidiLayer::Project : MidiLayer::Global;
        };
        addAndMakeVisible (layerCombo);
        setupLabel (pendingLabel, "");
        pendingLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe8b23a));
        setupLabel (noticeLabel, "");
        noticeLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9fd48a));

        setupLabel (monitorTitle, "Input monitor");
        setupLabel (monitorLabel, "(no MIDI received yet)");
        monitorLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb9b2a4));

        setupLabel (listTitle, "Mappings - control <- source");
        listBox.setModel (this);
        listBox.setRowHeight (20);
        listBox.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1d1d20));
        listBox.setColour (juce::ListBox::textColourId, juce::Colour (0xffe8e4da));
        addAndMakeVisible (listBox);
        deleteButton.setButtonText ("Delete binding");
        deleteButton.onClick = [this] { deleteSelected(); };
        addAndMakeVisible (deleteButton);
        clearGlobalButton.setButtonText ("Clear global");
        clearGlobalButton.onClick = [this]
        {
            service.map.clearLayer (MidiLayer::Global);
            changed();
        };
        addAndMakeVisible (clearGlobalButton);
        clearProjectButton.setButtonText ("Clear project");
        clearProjectButton.onClick = [this]
        {
            service.map.clearLayer (MidiLayer::Project);
            changed();
        };
        addAndMakeVisible (clearProjectButton);

        rebuildDeviceList();
        rebuildPresetList();
        recArmToggle.setToggleState (service.router.recArm.load(),
                                     juce::dontSendNotification);
        stepRecToggle.setToggleState (service.router.stepRecord.load(),
                                      juce::dontSendNotification);
        setSize (580, 660);
        startTimer (120);
    }
    ~MidiPanel() override { stopTimer(); }

    // MainComponent: persist + mark the project dirty when a project
    // binding changes.
    std::function<void()> onChanged;

    // Test/screenshot seams.
    juce::ToggleButton& learnToggleButton() { return learnToggle; }
    juce::ToggleButton& recArmToggleButton() { return recArmToggle; }
    juce::ComboBox& deviceComboBox() { return deviceCombo; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff26262a));
        // Activity lamp beside the title.
        const auto lamp = juce::Rectangle<float> ((float) getWidth() - 34.0f, 14.0f, 14.0f, 14.0f);
        const bool hot = (juce::Time::currentTimeMillis() - lastActivityMs) < 180;
        g.setColour (hot ? juce::Colour (0xffe8b23a) : juce::Colour (0xff4a4a4e));
        g.fillEllipse (lamp);
        g.setColour (juce::Colour (0xff101012));
        g.drawEllipse (lamp, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 10);
        titleLabel.setBounds (r.removeFromTop (30));
        if (deviceCombo.isVisible())
        {
            deviceLabel.setBounds (r.removeFromTop (18));
            auto row = r.removeFromTop (26);
            deviceCombo.setBounds (row.removeFromLeft (360));
            allInputsToggle.setBounds (row);
            r.removeFromTop (6);
        }
        else
        {
            hostNote.setBounds (r.removeFromTop (22));
        }
        presetLabel.setBounds (r.removeFromTop (18));
        presetCombo.setBounds (r.removeFromTop (26));
        presetNote.setBounds (r.removeFromTop (30));
        r.removeFromTop (4);
        recArmToggle.setBounds (r.removeFromTop (24));
        auto stepRow = r.removeFromTop (24);
        stepRecToggle.setBounds (stepRow.removeFromLeft (430));
        cursorLabel.setBounds (stepRow);
        learnToggle.setBounds (r.removeFromTop (24));
        auto layerRow = r.removeFromTop (26);
        layerLabel.setBounds (layerRow.removeFromLeft (110));
        layerCombo.setBounds (layerRow.removeFromLeft (220));
        pendingLabel.setBounds (r.removeFromTop (20));
        noticeLabel.setBounds (r.removeFromTop (20));
        monitorTitle.setBounds (r.removeFromTop (18));
        monitorLabel.setBounds (r.removeFromTop (66));
        listTitle.setBounds (r.removeFromTop (18));
        auto buttons = r.removeFromBottom (28);
        deleteButton.setBounds (buttons.removeFromLeft (130));
        buttons.removeFromLeft (8);
        clearGlobalButton.setBounds (buttons.removeFromLeft (110));
        buttons.removeFromLeft (8);
        clearProjectButton.setBounds (buttons.removeFromLeft (110));
        r.removeFromBottom (6);
        listBox.setBounds (r);
    }

    // ---- ListBoxModel ------------------------------------------------
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int rowNum, juce::Graphics& g, int w, int h,
                           bool selected) override
    {
        if (rowNum < 0 || rowNum >= (int) rows.size()) return;
        if (selected) { g.fillAll (juce::Colour (0xff3a3a40)); }
        g.setColour (juce::Colour (0xffe8e4da));
        g.setFont (13.0f);
        g.drawText (rows[(size_t) rowNum].text,
                    juce::Rectangle<int> (6, 0, w - 12, h),
                    juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked (int, const juce::MouseEvent&) override {}

private:
    struct Row
    {
        MidiLayer layer;
        MidiBinding binding;
        juce::String text;
    };
    std::vector<Row> rows;
    int shownRevision = -1, shownNotice = -1, shownActivity = -1, shownCursor = -1;

    void changed() { if (onChanged) onChanged(); }

    void rebuildDeviceList()
    {
        devices = MidiService::availableInputs();
        deviceCombo.clear (juce::dontSendNotification);
        deviceCombo.addItem ("No device", 1);
        for (int i = 0; i < devices.size(); ++i)
            deviceCombo.addItem (devices[i].name, i + 2);
        int sel = 1;
        if (! service.allInputsOpen())
            for (int i = 0; i < devices.size(); ++i)
                if (devices[i].identifier == service.selectedDeviceId())
                    sel = i + 2;
        deviceCombo.setSelectedId (sel, juce::dontSendNotification);
        allInputsToggle.setToggleState (service.allInputsOpen(),
                                        juce::dontSendNotification);
    }
    void deviceChosen()
    {
        const int id = deviceCombo.getSelectedId();
        if (id >= 2 && id - 2 < devices.size())
            service.openDevice (devices[id - 2].identifier);
        else
            service.closeDevices();
        changed();
    }
    void allInputsClicked()
    {
        if (allInputsToggle.getToggleState()) service.openAllDevices();
        else { service.closeDevices(); rebuildDeviceList(); }
        changed();
    }
    void rebuildPresetList()
    {
        presets = allMidiPresets();
        presetCombo.clear (juce::dontSendNotification);
        presetCombo.addItem ("No preset (factory layer empty)", 1);
        for (int i = 0; i < (int) presets.size(); ++i)
            presetCombo.addItem (presets[(size_t) i].name, i + 2);
        presetCombo.setSelectedId (service.presetIndex + 1,
                                   juce::dontSendNotification);
        updatePresetNote();
    }
    void presetChosen()
    {
        service.applyPreset (presetCombo.getSelectedId() - 1);
        updatePresetNote();
        changed();
    }
    void updatePresetNote()
    {
        const int idx = service.presetIndex;
        presetNote.setText (idx > 0 && idx <= (int) presets.size()
                                ? presets[(size_t) idx - 1].setupNote : "",
                            juce::dontSendNotification);
    }
    void deleteSelected()
    {
        const int row = listBox.getSelectedRow();
        if (row < 0 || row >= (int) rows.size()) return;
        service.map.removeExact (rows[(size_t) row].layer,
                                 rows[(size_t) row].binding.targetId,
                                 rows[(size_t) row].binding);
        changed();
    }
    void rebuildRows()
    {
        rows.clear();
        if (service.registry == nullptr) return;
        for (auto layer : { MidiLayer::Project, MidiLayer::Global, MidiLayer::Factory })
            for (const auto& b : service.map.layerBindings (layer))
            {
                juce::String name = b.targetId;
                if (const auto* t = service.registry->find (b.targetId))
                    name = t->name;
                MidiMapEvent probe;
                probe.type = b.sourceType == MidiBinding::NoteSource
                                 ? MidiMapEvent::Note : MidiMapEvent::ControlChange;
                probe.channel = b.channel;
                probe.number = b.number;
                MidiLayer eff;
                const bool shadowed = service.map.findEffective (probe, &eff) != &b
                                   && !(eff == layer);
                juce::String text = "[" + MidiMap::layerName (layer) + "] "
                                  + name + "  <-  " + b.sourceName()
                                  + (b.takeover == MidiBinding::Pickup ? " (pickup)" : "")
                                  + (shadowed ? "  (shadowed)" : "");
                rows.push_back ({ layer, b, text });
            }
    }
    long long lastActivityMs = 0;

    void timerCallback() override
    {
        // Toggles mirror the service (preset apply, other windows).
        if (recArmToggle.getToggleState() != service.router.recArm.load())
            recArmToggle.setToggleState (service.router.recArm.load(),
                                         juce::dontSendNotification);
        if (learnToggle.getToggleState() != service.learnArmed.load())
            learnToggle.setToggleState (service.learnArmed.load(),
                                        juce::dontSendNotification);
        const int act = service.activityCount();
        if (act != shownActivity)
        {
            shownActivity = act;
            lastActivityMs = juce::Time::currentTimeMillis();
            auto lines = service.recentEvents (4);
            juce::String text;
            for (const auto& l : lines) text += l + "\n";
            monitorLabel.setText (lines.empty() ? juce::String ("(no MIDI received yet)")
                                                : text.trimEnd(),
                                  juce::dontSendNotification);
            repaint();
        }
        const auto pending = service.pendingTarget();
        pendingLabel.setText (service.learnArmed.load()
                                  ? (pending.isEmpty()
                                        ? juce::String ("LEARN: click a control in the main window...")
                                        : "Waiting for the controller: wiggle the knob / fader / pad for \""
                                              + pending + "\"")
                                  : juce::String(),
                              juce::dontSendNotification);
        if (service.learnNoticeCount() != shownNotice)
        {
            shownNotice = service.learnNoticeCount();
            noticeLabel.setText (service.learnNotice(), juce::dontSendNotification);
        }
        const int cur = service.router.cursorStep();
        if (cur != shownCursor)
        {
            shownCursor = cur;
            cursorLabel.setText ("Step cursor: " + juce::String (cur + 1),
                                 juce::dontSendNotification);
        }
        if (service.map.revision() != shownRevision)
        {
            shownRevision = service.map.revision();
            rebuildRows();
            listBox.updateContent();
            listBox.repaint();
        }
    }

    MidiService& service;
    juce::Label titleLabel, deviceLabel, presetLabel, presetNote, hostNote,
                cursorLabel, layerLabel, pendingLabel, noticeLabel,
                monitorTitle, monitorLabel, listTitle;
    juce::ComboBox deviceCombo, presetCombo, layerCombo;
    juce::ToggleButton allInputsToggle, recArmToggle, stepRecToggle, learnToggle;
    juce::ListBox listBox;
    juce::TextButton deleteButton, clearGlobalButton, clearProjectButton;
    juce::Array<juce::MidiDeviceInfo> devices;
    std::vector<MidiPreset> presets;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiPanel)
};

// =====================================================================
// While LEARN is armed, this transparent overlay sits over the main
// window: every learnable control is outlined in amber with its
// name, and clicking one chooses it as the bind target (the wiggle
// on the controller then completes the bind). Clicks that miss every
// outlined control pass through to the window underneath, so the app
// stays usable while LEARN is on.
// =====================================================================
class LearnGlowOverlay : public juce::Component
{
public:
    struct Spot
    {
        juce::Component* comp = nullptr;
        juce::String targetId;
        juce::String name;
    };
    std::vector<Spot> spots;
    std::function<void (const juce::String&)> onPick;

    LearnGlowOverlay() { setInterceptsMouseClicks (true, false); }

    juce::Rectangle<int> spotRect (const Spot& s) const
    {
        if (s.comp == nullptr || ! s.comp->isVisible()) return {};
        return getLocalArea (s.comp, s.comp->getLocalBounds());
    }
    const Spot* spotAt (juce::Point<int> p) const
    {
        for (int i = (int) spots.size() - 1; i >= 0; --i)
        {
            const auto r = spotRect (spots[(size_t) i]);
            if (! r.isEmpty() && r.contains (p)) return &spots[(size_t) i];
        }
        return nullptr;
    }
    bool hitTest (int x, int y) override { return spotAt ({ x, y }) != nullptr; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const Spot* s = spotAt (e.getPosition()))
            if (onPick) onPick (s->targetId);
    }
    void paint (juce::Graphics& g) override
    {
        for (const auto& s : spots)
        {
            const auto r = spotRect (s);
            if (r.isEmpty()) continue;
            g.setColour (juce::Colour (0xffe8b23a));
            g.drawRect (r.toFloat(), 2.0f);
            const auto tag = juce::Rectangle<int> (r.getX(), juce::jmax (0, r.getY() - 15),
                                                   juce::jmin (r.getWidth(), 150), 14);
            g.setColour (juce::Colour (0xcc1d1d20));
            g.fillRect (tag);
            g.setColour (juce::Colour (0xffe8b23a));
            g.setFont (10.0f);
            g.drawText (s.name, tag.reduced (3, 0),
                        juce::Justification::centredLeft, true);
        }
    }
};

} // namespace boombap
