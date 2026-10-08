#pragma once

#include <JuceHeader.h>

#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "../Components/AudioSettingsPanel.h"
#endif

#include "../Helpers/CustomLookAndFeel.h"
#include "../Helpers/InterfaceDefines.h"
#include "../AboutWindow.h"
#include "../PluginProcessor.h"

class HamburgerButton : public TextButton
{
public:
    using TextButton::TextButton;

    void paint (Graphics& g) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId), isMouseOver(), isDown());

        const auto bounds = getLocalBounds().toFloat();
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float barW = 14.0f * MorphexZoom::uiScale;
        const float barH = 2.0f * MorphexZoom::uiScale;
        const float gap = 5.0f * MorphexZoom::uiScale;

        g.setColour (isMouseOver() ? MorphexColors::textPrimary : MorphexColors::textMid);
        for (int i = -1; i <= 1; ++i)
            g.fillRect (cx - barW * 0.5f, cy + i * gap - barH * 0.5f, barW, barH);
    }
};

class PerfModeButton : public TextButton
{
public:
    using TextButton::TextButton;

    void paint (Graphics& g) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId), isMouseOver(), isDown());

        const auto bounds = getLocalBounds().toFloat();
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float s = 9.0f * MorphexZoom::uiScale;

        juce::Path tri;
        tri.addTriangle (cx - s * 0.4f, cy - s * 0.5f,
                         cx + s * 0.6f, cy,
                         cx - s * 0.4f, cy + s * 0.5f);

        g.setColour (isMouseOver() || getToggleState() ? MorphexColors::textPrimary : MorphexColors::textMid);
        g.fillPath (tri);
    }
};

class AnaModeButton : public TextButton
{
public:
    using TextButton::TextButton;

    void paint (Graphics& g) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId), isMouseOver(), isDown());

        const auto bounds = getLocalBounds().toFloat();
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float barW = 3.0f * MorphexZoom::uiScale;
        const float gap = 3.0f * MorphexZoom::uiScale;
        const float h1 = 6.0f * MorphexZoom::uiScale;
        const float h2 = 12.0f * MorphexZoom::uiScale;
        const float h3 = 9.0f * MorphexZoom::uiScale;

        g.setColour (isMouseOver() || getToggleState() ? MorphexColors::textPrimary : MorphexColors::textMid);
        g.fillRect (cx - gap - barW, cy - h1 * 0.5f, barW, h1);
        g.fillRect (cx - barW * 0.5f, cy - h2 * 0.5f, barW, h2);
        g.fillRect (cx + gap, cy - h3 * 0.5f, barW, h3);
    }
};

static constexpr int kLoadFromFileId = 0x1001;
static constexpr int kSearchPresetsId = 0x1002;

class PresetManagerPanel
:   public Component,
    public Button::Listener,
    public juce::Value::Listener
{
public:

    enum MenuOption
    {
        None = 0,
        Init,
        Save,
        SaveAs,
        LoadFromFile,
        SetPresetFolder,
        ResetPresetFolder,
        About,
        CrtEnabled,
#if JucePlugin_Build_Standalone
        StandaloneAudioSettings,
        StandaloneSaveState,
        StandaloneLoadState,
        StandaloneReset,
#endif
        NUM_OPTIONS
    };

    static constexpr int kCrtStrengthLow = 0x2000;
    static constexpr int kCrtStrengthMedium = 0x2001;
    static constexpr int kCrtStrengthHigh = 0x2002;
    static constexpr int kZoomBaseId = 0x2100;

    PresetManagerPanel (SpectralMorphingToolAudioProcessor* inProcessor)
    :   mPresetManager (inProcessor->getPresetManager()),
        mProcessor (inProcessor)
    {
        mPresetDisplay.setButtonText ("Untitled");
        mPresetDisplay.setClickingTogglesState (false);
        mPresetDisplay.addListener (this);
        addAndMakeVisible (mPresetDisplay);

        mMenuButton.setClickingTogglesState (false);
        mMenuButton.addListener (this);
        mMenuButton.setRepaintsOnMouseActivity (false);
        addAndMakeVisible (mMenuButton);

        modePerfButton.setClickingTogglesState (true);
        modePerfButton.setRadioGroupId (7777);
        modePerfButton.setTooltip ("Performance window -- pads, slots and core controls");
        modePerfButton.onClick = [this]
        { mProcessor->setUiMode ("performance"); };
        addAndMakeVisible (modePerfButton);

        modeAnaButton.setClickingTogglesState (true);
        modeAnaButton.setRadioGroupId (7777);
        modeAnaButton.setTooltip ("Analyze window -- source waveform, partials map and resynthesis");
        modeAnaButton.onClick = [this]
        { mProcessor->setUiMode ("analyze"); };
        addAndMakeVisible (modeAnaButton);

        mProcessor->uiModeValue.addListener (this);
        syncModeButtons();
    }

    ~PresetManagerPanel() override
    {
        if (mProcessor != nullptr)
            mProcessor->uiModeValue.removeListener (this);
    }

    void paint (Graphics& g) override {}

    void resized() override
    {
        const float zs = MorphexZoom::uiScale;
        const float content_pad = GUI::Layout::ContentInset * zs;

        auto bounds = getLocalBounds();

        const int icon_size = juce::roundToInt (28.0f * zs);
        const int control_gap = juce::roundToInt (6.0f * zs);

        const int controls_width = bounds.getWidth() - (int) (content_pad * 2.0f);
        const int controls_x = bounds.getX() + (int) content_pad;
        const int controls_y = (bounds.getHeight() - icon_size) / 2;

        int x = controls_x;
        const int preset_width = controls_width - (icon_size * 3) - (control_gap * 3);
        mPresetDisplay.setBounds (x, controls_y, preset_width, icon_size);
        x += preset_width + control_gap;

        modePerfButton.setBounds (x, controls_y, icon_size, icon_size);
        x += icon_size + control_gap;

        modeAnaButton.setBounds (x, controls_y, icon_size, icon_size);
        x += icon_size + control_gap;

        mMenuButton.setBounds (x, controls_y, icon_size, icon_size);
    }

private:

    void valueChanged (juce::Value& value) override
    {
        if (value.refersToSameSourceAs (mProcessor->uiModeValue))
            syncModeButtons();
    }

    void syncModeButtons()
    {
        const bool analyze = mProcessor->getUiMode() == "analyze";
        modePerfButton.setToggleState (! analyze, juce::dontSendNotification);
        modeAnaButton.setToggleState (analyze, juce::dontSendNotification);
    }

    static void styleAlertWindow (AlertWindow& window)
    {
        static CustomLookAndFeel lnf;
        window.setLookAndFeel (&lnf);
        window.setColour (AlertWindow::backgroundColourId, MorphexColors::menuBg);
        window.setColour (AlertWindow::textColourId, MorphexColors::menuText);
        window.setColour (AlertWindow::outlineColourId, MorphexColors::menuBorder);
    }

    static void raiseAlertButtons (AlertWindow& window)
    {
        for (int i = 0; i < window.getNumButtons(); ++i)
            if (auto* b = window.getButton (i))
            {
                b->setTopLeftPosition (b->getX(), b->getY() - 10);
                b->setColour (juce::TextButton::buttonColourId, MorphexColors::menuHover);
                b->setColour (juce::TextButton::buttonOnColourId, MorphexColors::menuBorder);
                b->setColour (juce::TextButton::textColourOffId, MorphexColors::menuTextBright);
                b->setColour (juce::TextButton::textColourOnId, MorphexColors::menuTextBright);
            }
    }

    void buttonClicked (Button* button) override
    {
        if (button == &mMenuButton)
            showHamburgerMenu();
        else if (button == &mPresetDisplay)
            showPresetMenu();
    }

    void showPresetMenu()
    {
        PopupMenu menu;

        menu.addItem (kSearchPresetsId, "Search Presets...");
        menu.addSeparator();

        const int numPresets = mPresetManager->getNumberOfPresets();

        if (numPresets == 0)
            menu.addItem (1, "(no presets found)", false);
        else
            for (int i = 0; i < numPresets; ++i)
                menu.addItem (kLoadFromFileId + 1 + i, mPresetManager->getPresetName (i));

        menu.addSeparator();
        menu.addItem (kLoadFromFileId, "Load From File...");

        menu.showMenuAsync (
            PopupMenu::Options()
                .withTargetComponent (&mPresetDisplay),
            [this] (int result)
            {
                if (result == kSearchPresetsId)
                {
                    showPresetSearch();
                }
                else if (result == kLoadFromFileId)
                {
                    loadPresetFileDialog();
                }
                else if (result > kLoadFromFileId)
                {
                    const int index = result - kLoadFromFileId - 1;
                    if (mPresetManager->loadPreset (index))
                        currentPresetIndex = index;
                    else
                        updatePresetDisplay();
                }
            });
    }

    void loadPresetFileDialog()
    {
        auto chooser = std::make_shared<FileChooser> ("Load Preset File",
                                                      File (mPresetManager->getPresetDirectory()),
                                                      "*" + String (PRESET_FILE_EXTENSION));

        chooser->launchAsync (FileBrowserComponent::openMode
                              | FileBrowserComponent::canSelectFiles,
                              [this, chooser] (const FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                  {
                                      if (mPresetManager->loadPresetFile (fc.getResult()))
                                          currentPresetIndex = -1;
                                      updatePresetDisplay();
                                  }
                              });
    }

    struct PresetSearchBox : public Component,
                             private juce::TextEditor::Listener,
                             private juce::ListBoxModel
    {
        PresetSearchBox (PresetManager* mgr, std::function<void (int)> onPick)
        :   manager (mgr), pick (std::move (onPick))
        {
            search.setTextToShowWhenEmpty ("Search presets...", MorphexColors::menuTextDim);
            search.setFont (CustomLookAndFeel::makeFont (24.0f));
            search.setIndents (juce::roundToInt (8.0f * MorphexZoom::uiScale),
                               juce::roundToInt (10.0f * MorphexZoom::uiScale));
            search.setColour (juce::TextEditor::backgroundColourId, MorphexColors::menuBg);
            search.setColour (juce::TextEditor::textColourId, MorphexColors::menuTextBright);
            search.setColour (juce::TextEditor::outlineColourId, MorphexColors::menuBorder);
            search.addListener (this);
            addAndMakeVisible (search);

            list.setModel (this);
            list.setRowHeight (juce::roundToInt (36.0f * MorphexZoom::uiScale));
            list.setColour (juce::ListBox::backgroundColourId, MorphexColors::menuBg);
            list.setColour (juce::ListBox::textColourId, MorphexColors::menuText);
            list.setColour (juce::ListBox::outlineColourId, MorphexColors::menuBorder);
            addAndMakeVisible (list);

            refilter();
        }

        void resized() override
        {
            const int searchH = juce::roundToInt (44.0f * MorphexZoom::uiScale);
            auto area = getLocalBounds();
            search.setBounds (area.removeFromTop (searchH));
            list.setBounds (area.withTrimmedTop (juce::roundToInt (6.0f * MorphexZoom::uiScale)));
        }

        void visibilityChanged() override
        {
            if (isShowing())
                search.grabKeyboardFocus();
        }

        int getNumRows() override { return filtered.size(); }

        void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
        {
            if (selected)
            {
                g.setColour (MorphexColors::menuHover);
                g.fillAll();
            }
            g.setColour (selected ? MorphexColors::menuTextBright : MorphexColors::menuText);
            g.setFont (CustomLookAndFeel::makeFont (20.0f));
            const int idx = filtered[(size_t) row];
            g.drawText (manager->getPresetName (idx),
                        10, 0, w - 20, h, juce::Justification::centredLeft, true);
        }

        void listBoxItemClicked (int row, const juce::MouseEvent&) override
        {
            if (juce::isPositiveAndBelow (row, filtered.size()))
                pick (filtered[(size_t) row]);
        }

        void textEditorTextChanged (juce::TextEditor&) override { refilter(); }

        void textEditorEscapeKeyPressed (juce::TextEditor&) override
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);
        }

        void textEditorReturnKeyPressed (juce::TextEditor&) override
        {
            if (filtered.size() == 1)
                pick (filtered[0]);
            else if (list.getSelectedRow() >= 0)
                pick (filtered[(size_t) list.getSelectedRow()]);
        }

    private:
        void refilter()
        {
            filtered.clear();
            const auto needle = search.getText().trim().toLowerCase();
            for (int i = 0, n = manager->getNumberOfPresets(); i < n; ++i)
                if (needle.isEmpty() || manager->getPresetName (i).toLowerCase().contains (needle))
                    filtered.add (i);
            list.updateContent();
            if (filtered.size() > 0)
                list.selectRow (0);
            repaint();
        }

        PresetManager* manager;
        std::function<void (int)> pick;
        juce::TextEditor search;
        juce::ListBox list;
        juce::Array<int> filtered;
    };

    void showPresetSearch()
    {
        auto* box = new PresetSearchBox (mPresetManager, [this] (int index)
        {
            if (mPresetManager->loadPreset (index))
                currentPresetIndex = index;
            updatePresetDisplay();
        });
        box->setSize (juce::roundToInt (420.0f * MorphexZoom::uiScale),
                      juce::roundToInt (360.0f * MorphexZoom::uiScale));
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (box);
        options.dialogTitle = "Search Presets";
        options.dialogBackgroundColour = MorphexColors::background;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = false;
        options.resizable = false;
        options.launchAsync();
    }

    static bool isStandaloneApp (const SpectralMorphingToolAudioProcessor* processor)
    {
        return processor != nullptr
            && processor->wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    }

    void showHamburgerMenu()
    {
        PopupMenu menu;

        menu.addItem (MenuOption::Init, "Init");
        menu.addSeparator();
        menu.addItem (MenuOption::Save, "Save");
        menu.addItem (MenuOption::SaveAs, "Save As...");
        menu.addItem (MenuOption::LoadFromFile, "Load From File...");
        menu.addSeparator();
        menu.addItem (MenuOption::SetPresetFolder, "Set Preset Folder");
        menu.addItem (MenuOption::ResetPresetFolder, "Reset Preset Folder");
        menu.addSeparator();
        PopupMenu crtSub;
        crtSub.addItem (MenuOption::CrtEnabled,
                      juce::String ("CRT Enabled - ") + (mProcessor->isCrtEnabled() ? "[X]" : "[ ]"));

        PopupMenu strengthSub;
        const int strength = mProcessor->getCrtStrength();
        strengthSub.addItem (kCrtStrengthLow, "Low", true, strength == 0);
        strengthSub.addItem (kCrtStrengthMedium, "Medium", true, strength == 1);
        strengthSub.addItem (kCrtStrengthHigh, "High", true, strength == 2);
        crtSub.addSubMenu (">> Strength", strengthSub);
        menu.addSubMenu (">> CRT Layout", crtSub);

        PopupMenu zoomSub;
        int zoomCurrent = 0;
        if (auto* zoomParam = mProcessor->parameters.getParameter (Morphex::Zoom::UI_SCALE_ID))
            zoomCurrent = juce::jlimit (0, Morphex::Zoom::ZOOM_COUNT - 1,
                                        juce::roundToInt (zoomParam->getValue() * (float) (Morphex::Zoom::ZOOM_COUNT - 1)));
        for (int i = 0; i < Morphex::Zoom::ZOOM_COUNT; ++i)
            zoomSub.addItem (kZoomBaseId + i,
                             juce::String (Morphex::Zoom::ZOOM_PERCENTS[i], 0) + "%",
                             true, i == zoomCurrent);
        menu.addSubMenu (">> Zoom", zoomSub);

        menu.addSeparator();
        menu.addItem (MenuOption::About, "About");

#if JucePlugin_Build_Standalone
        if (isStandaloneApp (mProcessor))
        {
            menu.addSeparator();
            PopupMenu standaloneSub;
            standaloneSub.addItem (MenuOption::StandaloneAudioSettings, "Audio/MIDI Settings...");
            standaloneSub.addItem (MenuOption::StandaloneSaveState, "Save State...");
            standaloneSub.addItem (MenuOption::StandaloneLoadState, "Load State...");
            standaloneSub.addItem (MenuOption::StandaloneReset, "Reset to Default");
            menu.addSubMenu (">> Standalone", standaloneSub);
        }
#endif

        menu.showMenuAsync (
            PopupMenu::Options()
                .withTargetComponent (&mMenuButton),
            [this] (int result) { handleMenuResult (result); });
    }

    void handleMenuResult (int selected_id)
    {
        if (selected_id >= kZoomBaseId && selected_id < kZoomBaseId + Morphex::Zoom::ZOOM_COUNT)
        {
            const int idx = selected_id - kZoomBaseId;
            if (auto* zoomParam = mProcessor->parameters.getParameter (Morphex::Zoom::UI_SCALE_ID))
                zoomParam->setValueNotifyingHost ((float) idx / (float) (Morphex::Zoom::ZOOM_COUNT - 1));
            return;
        }

        switch (selected_id)
        {
            case MenuOption::None:                                 break;
            case MenuOption::Init:   displayInitPopup();           break;
            case MenuOption::Save:   mPresetManager->savePreset(); break;
            case MenuOption::SaveAs: displaySaveAsPopup();         break;
            case MenuOption::LoadFromFile: loadPresetFileDialog(); break;
            case MenuOption::SetPresetFolder: displaySetPresetFolderPopup(); break;
            case MenuOption::ResetPresetFolder: resetPresetFolder(); break;
            case MenuOption::About: displayAboutPopup(); break;
            case MenuOption::CrtEnabled:
                mProcessor->setCrtEnabled (! mProcessor->isCrtEnabled());
                break;
            case kCrtStrengthLow:
                mProcessor->setCrtStrength (0);
                break;
            case kCrtStrengthMedium:
                mProcessor->setCrtStrength (1);
                break;
            case kCrtStrengthHigh:
                mProcessor->setCrtStrength (2);
                break;
#if JucePlugin_Build_Standalone
            case MenuOption::StandaloneAudioSettings:
                if (auto* tl = getTopLevelComponent())
                    if (auto* sfw = dynamic_cast<juce::StandaloneFilterWindow*> (tl))
                        new SettingsWindow (sfw->getPluginHolder()->deviceManager);
                break;
            case MenuOption::StandaloneSaveState:
                if (auto* tl = getTopLevelComponent())
                    if (auto* sfw = dynamic_cast<juce::StandaloneFilterWindow*> (tl))
                        sfw->getPluginHolder()->askUserToSaveState();
                break;
            case MenuOption::StandaloneLoadState:
                if (auto* tl = getTopLevelComponent())
                    if (auto* sfw = dynamic_cast<juce::StandaloneFilterWindow*> (tl))
                        sfw->getPluginHolder()->askUserToLoadState();
                break;
            case MenuOption::StandaloneReset:
                if (auto* tl = getTopLevelComponent())
                    if (auto* sfw = dynamic_cast<juce::StandaloneFilterWindow*> (tl))
                        juce::MessageManager::callAsync ([sfw]
                        {
                            sfw->resetToDefaultState();
                        });
                break;
#endif
            default:                 jassertfalse;                 break;
        }
    }

    void displayInitPopup()
    {
        AlertWindow window ("Init", "Are you sure you want to initialize this preset?", AlertWindow::NoIcon);
        styleAlertWindow (window);

        window.centreAroundComponent (this, getWidth(), getHeight());
        window.addButton ("Confirm", 1);
        window.addButton ("Cancel", 0);
        raiseAlertButtons (window);

        if (window.runModalLoop())
        {
            mPresetManager->createNewPreset();
            updatePresetDisplay();
            // Init lands on the performance window with the Core panel up.
            mProcessor->setUiMode ("performance");
            mProcessor->setCenterSection (0);
        }
    }

    void displaySaveAsPopup()
    {
        String currentPresetName = mPresetManager->getCurrentPresetName();

        AlertWindow window ("Save As", "Please enter a name for your preset", AlertWindow::NoIcon);
        styleAlertWindow (window);

        window.centreAroundComponent (this, getWidth(), getHeight());
        window.addTextEditor ("presetName", currentPresetName, "Preset Name: ");
        if (auto* presetNameEditor = window.getTextEditor ("presetName"))
        {
            presetNameEditor->setFont (CustomLookAndFeel::makeFont (24.0f));
            presetNameEditor->setIndents (4, 7);
        }
        window.addButton ("Confirm", 1);
        window.addButton ("Cancel", 0);
        raiseAlertButtons (window);

        if (window.runModalLoop())
        {
            String presetName = window.getTextEditor ("presetName")->getText();
            mPresetManager->saveAsPreset (presetName);
            updatePresetDisplay();
        }
    }

    void displaySetPresetFolderPopup()
    {
        FileChooser chooser ("Select Preset Folder",
                             File (mPresetManager->getPresetDirectory()),
                             "*",
                             true,
                             false,
                             this);

        if (chooser.browseForDirectory())
        {
            if (mPresetManager->setPresetDirectory (chooser.getResult().getFullPathName()))
            {
                currentPresetIndex = -1;
                updatePresetDisplay();
            }
        }
    }

    void resetPresetFolder()
    {
        mPresetManager->resetPresetDirectoryToDefault();
        currentPresetIndex = -1;
        updatePresetDisplay();
    }

    void displayAboutPopup()
    {
        new AboutWindow();
    }

    void updatePresetDisplay()
    {
        String presetName = mPresetManager->getCurrentPresetName();
        mPresetDisplay.setButtonText (presetName);
    }

    PresetManager* mPresetManager;

    TextButton mPresetDisplay;
    HamburgerButton mMenuButton;
    PerfModeButton modePerfButton;
    AnaModeButton modeAnaButton;

    int currentPresetIndex = 0;

    SpectralMorphingToolAudioProcessor* mProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManagerPanel)
};
