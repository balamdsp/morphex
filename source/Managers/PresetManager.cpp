/* Copyright (C) 2020 Marc Sanchez Martinez
 *
 * https://github.com/MarcSM/morphex
 *
 * Morphex is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Morphex is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Morphex. If not, see <http://www.gnu.org/licenses/>.
 */

#include "PresetManager.h"

#include <cmath>

#include "../Helpers/SMTConstants.h"
#include "../Helpers/CustomLookAndFeel.h"

#include "../Helpers/SMTHelperFunctions.h"

namespace
{
    const Identifier PRESET_IDENTIFIER ("MorphexPreset");
    const Identifier PRESET_NAME_ID ("PresetName");
    const Identifier SOUND_PATHS_ID ("SoundPaths");
    const Identifier SOUND_PATH_ID ("SoundPath");
    const Identifier SLOT_INDEX_ID ("SlotIndex");
    const Identifier PARAMETERS_ID ("Parameters");
    const Identifier PARAM_ID ("Param");
    const Identifier PARAM_NAME_ID ("Name");
    const Identifier PARAM_VALUE_ID ("Value");
    const Identifier PRESET_VERSION_ID ("PresetVersion");
    // Preset schema 2: StocsGain stored dB-native (-24..+6). Schema 1 (or
    // unversioned) stored it normalized 0..1 (0..+6 dB) and is remapped.
    static constexpr int PRESET_SCHEMA_VERSION = 2;
    
    // Style alert windows to match the dark theme
    void styleAlertWindow (AlertWindow& window)
    {
        window.setLookAndFeel (new CustomLookAndFeel());
        window.setColour (AlertWindow::backgroundColourId, MorphexColors::body);
        window.setColour (AlertWindow::textColourId, MorphexColors::textPrimary);
        window.setColour (AlertWindow::outlineColourId, MorphexColors::buttonBorder);
    }
    
    // Raise alert buttons for better spacing
    void raiseAlertButtons (AlertWindow& window)
    {
        for (int i = 0; i < window.getNumButtons(); ++i)
            if (auto* b = window.getButton (i))
                b->setTopLeftPosition (b->getX(), b->getY() - 10);
    }
    
    PropertiesFile::Options getPresetSettingsOptions()
    {
        PropertiesFile::Options options;
        options.applicationName = PLUGIN_NAME;
        options.filenameSuffix = "settings";
        options.folderName = PLUGIN_NAME;
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat = PropertiesFile::storeAsXML;
        options.commonToAllUsers = false;
        options.ignoreCaseOfKeyNames = false;
        options.doNotSave = false;
        options.millisecondsBeforeSaving = 0;
        return options;
    }
}

PresetManager::PresetManager (AudioProcessor* inProcessor, MorphexSynth* inMorphexSynth)
:   mCurrentPresetIsSaved (false),
    mCurrentPresetName ("Untitled"),
    mPresetDirectory (getDefaultPresetsDirectory()),
    mProcessor (inProcessor),
    mMorphexSynth (inMorphexSynth)
{
    migrateLegacySettingsFile();
    loadPresetDirectorySettings();
    loadCollectionsDirectoriesSettings();
    loadCrtSettings();
    loadVoiceSettings();
    loadInterfaceSettings();
    loadAnalyzerSettings();
    migrateLegacyDataDirectories();
    ensurePresetDirectoryExists();
    storeLocalPreset();
    
    // Ensure default collections directory exists
    if (mCollectionsDirectories.isEmpty())
    {
        resetCollectionsDirectoriesToDefault();
    }
}

PresetManager::~PresetManager()
{
    
}

ValueTree PresetManager::getStateTree()
{
    ValueTree presetTree (PRESET_IDENTIFIER);
    
    // Store preset name
    presetTree.setProperty (PRESET_NAME_ID, mCurrentPresetName, nullptr);
    presetTree.setProperty (PRESET_VERSION_ID, PRESET_SCHEMA_VERSION, nullptr);
    
    // Store sound information
    getSoundInformation (presetTree);
    
    // Store plugin parameters
    ValueTree paramsTree (PARAMETERS_ID);
    auto& parameters = mProcessor->getParameters();
    
    for (int i = 0; i < parameters.size(); i++)
    {
        AudioProcessorParameterWithID* parameter =
            dynamic_cast<AudioProcessorParameterWithID*> (parameters.getUnchecked (i));
        
        if (parameter != nullptr)
        {
            ValueTree paramTree (PARAM_ID);
            paramTree.setProperty (PARAM_NAME_ID, parameter->paramID, nullptr);
            paramTree.setProperty (PARAM_VALUE_ID, (double) parameter->getValue(), nullptr);
            paramsTree.addChild (paramTree, -1, nullptr);
        }
    }
    
    presetTree.addChild (paramsTree, -1, nullptr);
    
    return presetTree;
}

bool PresetManager::loadStateFromTree (const ValueTree& tree)
{
    if (! tree.isValid() || tree.getType() != PRESET_IDENTIFIER)
        return false;

    // Parameters apply synchronously (cheap, DAW-safe); sounds stream in
    // async via setSoundInformation below.
    const int fileSchema = (int) tree.getProperty (PRESET_VERSION_ID, 0);
    ValueTree paramsTree = tree.getChildWithName (PARAMETERS_ID);
    if (paramsTree.isValid())
    {
        auto& parameters = mProcessor->getParameters();
        
        for (int i = 0; i < paramsTree.getNumChildren(); i++)
        {
            ValueTree paramTree = paramsTree.getChild (i);
            if (paramTree.getType() == PARAM_ID)
            {
                String paramId = paramTree.getProperty (PARAM_NAME_ID, "").toString();
                double value = paramTree.getProperty (PARAM_VALUE_ID, 0.0);

                // Schema 1 -> 2: StocsGain was normalized 0..1 (0..+6 dB);
                // preserve the dB, not the fraction.
                if (fileSchema < 2 && paramId == "StocsGain")
                    value = (20.0 * std::log10 (1.0 + value) + 24.0) / 30.0;
                
                for (int j = 0; j < parameters.size(); j++)
                {
                    AudioProcessorParameterWithID* parameter =
                        dynamic_cast<AudioProcessorParameterWithID*> (parameters.getUnchecked (j));
                    
                    if (parameter != nullptr && paramId == parameter->paramID)
                    {
                        parameter->setValueNotifyingHost ((float) value);
                    }
                }
            }
        }
    }
    
    // Set preset name
    mCurrentPresetName = tree.getProperty (PRESET_NAME_ID, "Untitled").toString();

    // Sounds decode off-thread, install on message; missing ones report
    // non-modally. Return = load accepted.
    bool soundsWereLoaded = setSoundInformation (tree);

    return soundsWereLoaded;
}

int PresetManager::getNumberOfPresets()
{
    return mLocalPresets.size();
}

String PresetManager::getPresetName (int inPresetIndex)
{
    return mLocalPresets[inPresetIndex].getFileNameWithoutExtension();
}

void PresetManager::createNewPreset()
{
    auto& parameters = mProcessor->getParameters();
    
    for (int i = 0; i < parameters.size(); i++)
    {
        AudioProcessorParameterWithID* parameter =
            dynamic_cast<AudioProcessorParameterWithID*> (parameters.getUnchecked(i));
        
        if (parameter != nullptr)
        {
            const float defaultValue = parameter->getDefaultValue();
            parameter->setValueNotifyingHost (defaultValue);
        }
    }
    
    mMorphexSynth->reset();
    
    mCurrentPresetIsSaved = false;
    mCurrentPresetName = "Untitled";
}

void PresetManager::savePreset()
{
    ValueTree presetTree = getStateTree();
    
    MemoryOutputStream stream;
    presetTree.writeToStream (stream);
    
    mCurrentlyLoadedPreset.deleteFile();
    mCurrentlyLoadedPreset.appendData (stream.getData(), stream.getDataSize());
    
    mCurrentPresetIsSaved = true;
}

void PresetManager::saveAsPreset (String inPresetName)
{
    File presetFile = File (mPresetDirectory + directorySeparator + inPresetName + PRESET_FILE_EXTENSION);
    
    if (! presetFile.exists())
    {
        presetFile.create();
    }
    else
    {
        presetFile.deleteFile();
    }
    
    ValueTree presetTree = getStateTree();
    
    MemoryOutputStream stream;
    presetTree.writeToStream (stream);
    
    presetFile.appendData (stream.getData(), stream.getDataSize());
    
    mCurrentPresetIsSaved = true;
    mCurrentPresetName = inPresetName;
    
    storeLocalPreset();
}

bool PresetManager::loadPreset (int inPresetIndex = 0)
{
    bool present_was_loaded = false;
    
    mCurrentlyLoadedPreset = mLocalPresets[inPresetIndex];
    
    MemoryBlock presetBinary;
    
    if (mCurrentlyLoadedPreset.loadFileAsData (presetBinary))
    {
        MemoryInputStream stream (presetBinary.getData(), presetBinary.getSize(), false);
        ValueTree presetTree = ValueTree::readFromStream (stream);
        
        if (presetTree.isValid())
        {
            present_was_loaded = loadStateFromTree (presetTree);
            
            if (present_was_loaded)
            {
                mCurrentPresetIsSaved = true;
                mCurrentPresetName = getPresetName (inPresetIndex);
            }
        }
    }
    
    return present_was_loaded;
}

bool PresetManager::loadPresetFile (const File& file)
{
    MemoryBlock presetBinary;

    if (! file.loadFileAsData (presetBinary))
        return false;

    MemoryInputStream stream (presetBinary.getData(), presetBinary.getSize(), false);
    ValueTree presetTree = ValueTree::readFromStream (stream);

    if (! presetTree.isValid())
        return false;

    mCurrentlyLoadedPreset = file;
    return loadStateFromTree (presetTree);
}

void PresetManager::getSoundInformation (ValueTree& parentTree)
{
    MorphSounds morph_sounds = this->mMorphexSynth->instrument.getMorphSounds();
    
    ValueTree soundPathsTree (SOUND_PATHS_ID);
    
    for (int i = 0; i < morph_sounds.size(); i++)
    {
        if (morph_sounds[i] != nullptr)
        {
            std::string sound_file_path = morph_sounds[i]->path;
            
            ValueTree soundPathTree (SOUND_PATH_ID);
            soundPathTree.setProperty (SLOT_INDEX_ID, i, nullptr);
            soundPathTree.setProperty (Identifier ("Path"), String (sound_file_path), nullptr);
            soundPathsTree.addChild (soundPathTree, -1, nullptr);
        }
    }
    
    parentTree.addChild (soundPathsTree, -1, nullptr);
}

bool PresetManager::setSoundInformation (const ValueTree& presetTree)
{
    ValueTree soundPathsTree = presetTree.getChildWithName (SOUND_PATHS_ID);

    if (! soundPathsTree.isValid())
        return false;

    // Parse slots synchronously (fast, no IO).
    std::vector<std::pair<int, String>> requested;
    for (int i = 0; i < soundPathsTree.getNumChildren(); i++)
    {
        ValueTree soundPathTree = soundPathsTree.getChild (i);

        if (soundPathTree.getType() == SOUND_PATH_ID)
        {
            int slotIndex = (int) soundPathTree.getProperty (SLOT_INDEX_ID, -1);
            String originalPath = soundPathTree.getProperty (Identifier ("Path"), "").toString();

            if (slotIndex >= 0 && slotIndex < MorphLocation::NUM_MORPH_LOCATIONS
                && originalPath.isNotEmpty())
                requested.push_back ({ slotIndex, originalPath });
        }
    }
    if (requested.empty())
        return false;

    // Snapshot everything the worker needs (the manager may be reconfigured
    // from the UI while the worker runs).
    const File presetFile = mCurrentlyLoadedPreset;
    const String presetDir = mPresetDirectory;
    const Array<String> collectionsDirs = mCollectionsDirectories;
    const int generation = ++presetLoadGeneration;

    juce::Thread::launch ([this, requested, presetFile, presetDir, collectionsDirs, generation]
    {
        struct Loaded
        {
            int slot;
            std::shared_ptr<Sound> sound;
        };
        std::vector<Loaded> loaded;
        std::vector<MissingSound> missing;

        for (const auto& item : requested)
        {
            const String resolved = resolveSoundPath (item.second, presetFile,
                                                      presetDir, collectionsDirs);
            if (resolved.isEmpty())
            {
                missing.push_back ({ item.first, item.second });
                continue;
            }
            try
            {
                auto decoded = Core::Instrument::decodeSound (resolved.toStdString());
                if (decoded != nullptr && decoded->loaded)
                    loaded.push_back ({ item.first, std::move (decoded) });
                else
                    missing.push_back ({ item.first, item.second });
            }
            catch (const std::exception&)
            {
                missing.push_back ({ item.first, item.second });
            }
            catch (...)
            {
                missing.push_back ({ item.first, item.second });
            }
        }

        juce::MessageManager::callAsync ([this, loaded, missing, generation]
        {
            // Stale preset loads never clobber newer ones.
            if (generation != presetLoadGeneration.load())
                return;

            // Message-thread swap; earlier voice snapshots stay alive.
            this->mMorphexSynth->reset();
            for (const auto& item : loaded)
                this->mMorphexSynth->instrument.installSound (item.sound,
                                                              (MorphLocation) item.slot);

            if (! missing.empty())
            {
                String missingMessage = "The following sounds could not be found:\n";
                for (const auto& m : missing)
                    missingMessage += "\nSlot " + String (m.slotIndex + 1) + ": " + m.originalPath;
                missingMessage += "\n\nThe preset has been loaded with available sounds.";
                // Non-modal: never blocks the message thread (or a DAW host
                // thread, which routes here via loadStateFromTree).
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                        "Missing Sounds", missingMessage, "OK");
            }
        });
    });

    return true;
}

String PresetManager::resolveSoundPath (const String& originalPath, const File& presetFile,
                                         const String& presetDir, const Array<String>& collectionsDirs)
{
    // 1. Try absolute path directly
    File absolutePath (originalPath);
    if (absolutePath.existsAsFile())
        return absolutePath.getFullPathName();

    // 2. Try relative to preset file location
    if (presetFile.existsAsFile())
    {
        File presetFileDir = presetFile.getParentDirectory();
        File relativeToPreset = presetFileDir.getChildFile (originalPath);
        if (relativeToPreset.existsAsFile())
            return relativeToPreset.getFullPathName();
    }

    // 3. Try relative to preset directory
    File inPresetDir = File (presetDir).getChildFile (originalPath);
    if (inPresetDir.existsAsFile())
        return inPresetDir.getFullPathName();

    // 4. Try in all collections directories
    for (const auto& collectionsDir : collectionsDirs)
    {
        File inCollectionsDir = File (collectionsDir).getChildFile (originalPath);
        if (inCollectionsDir.existsAsFile())
            return inCollectionsDir.getFullPathName();
    }

    // 5. Try searching for just the filename in common locations
    String filename = File (originalPath).getFileName();

    // Search in preset directory
    for (DirectoryIterator di (File (presetDir), false, filename, File::findFiles); di.next();)
    {
        return di.getFile().getFullPathName();
    }

    // Search in collections directories
    for (const auto& collectionsDir : collectionsDirs)
    {
        for (DirectoryIterator di (File (collectionsDir), false, filename, File::findFiles); di.next();)
        {
            return di.getFile().getFullPathName();
        }
    }

    // 6. Try the user's Documents/Morphex directory
    File documentsDir = File::getSpecialLocation (File::userDocumentsDirectory)
                            .getChildFile (PLUGIN_NAME);
    for (DirectoryIterator di (documentsDir, true, filename, File::findFiles); di.next();)
    {
        return di.getFile().getFullPathName();
    }

    // Not found anywhere
    return {};
}

String PresetManager::resolveSoundPath (const String& originalPath, const File& presetFile)
{
    return resolveSoundPath (originalPath, presetFile, mPresetDirectory, mCollectionsDirectories);
}

bool PresetManager::getIsCurrentPresetSaved()
{
    return mCurrentPresetIsSaved;
}

String PresetManager::getCurrentPresetName()
{
    return mCurrentPresetName;
}

void PresetManager::storeLocalPreset()
{
    mLocalPresets.clear();
    
    for (DirectoryIterator di (File (mPresetDirectory),
                               false,
                               "*" + (String) PRESET_FILE_EXTENSION,
                               File::TypesOfFileToFind::findFiles); di.next();)
    {
        File preset = di.getFile();
        mLocalPresets.add (preset);
    }
}

String PresetManager::getPresetDirectory() const
{
    return mPresetDirectory;
}

bool PresetManager::setPresetDirectory (const String& inDirectory)
{
    if (inDirectory.isEmpty())
        return false;

    const File presetDirectory (inDirectory);

    if (! presetDirectory.isDirectory())
        return false;

    mPresetDirectory = presetDirectory.getFullPathName();
    savePresetDirectorySettings();
    storeLocalPreset();

    return true;
}

void PresetManager::resetPresetDirectoryToDefault()
{
    mPresetDirectory = getDefaultPresetsDirectory();
    ensurePresetDirectoryExists();
    savePresetDirectorySettings();
    storeLocalPreset();
}

File PresetManager::getSettingsFile() const
{
    return File (getDefaultPluginDataDirectory()).getChildFile ("settings.xml");
}

File PresetManager::getLegacySettingsFile() const
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile (PLUGIN_NAME)
        .getChildFile ("settings.xml");
}

void PresetManager::migrateLegacySettingsFile()
{
    // One-time legacy imports below (user folders never touched).
    const File current = getSettingsFile();
    const File legacy = getLegacySettingsFile();
    if (! current.existsAsFile() && legacy.existsAsFile())
        legacy.copyFileTo (current);
}

void PresetManager::migrateLegacyDataDirectories()
{
    // Remap only the two exact legacy default paths; contents move along.
    const String legacyRoot = File::getSpecialLocation (File::userDocumentsDirectory)
                                  .getChildFile (PLUGIN_NAME).getFullPathName();

    auto remapExactDefault = [&] (const String& stored, const String& leaf) -> String
    {
        if (stored == legacyRoot + directorySeparator + leaf)
            return getDefaultPluginDataDirectory() + directorySeparator + leaf;
        return stored;
    };

    auto moveFolderContents = [] (const File& src, const File& dst) -> bool
    {
        if (! src.isDirectory())
            return true; // nothing to move
        if (src.moveFileTo (dst))
            return true;
        // moveFileTo often fails on directories: copy + delete instead.
        if (dst.createDirectory() && src.copyDirectoryTo (dst))
        {
            src.deleteRecursively();
            return true;
        }
        return false;
    };

    const String newPresetDir = remapExactDefault (mPresetDirectory, "Presets");
    if (newPresetDir != mPresetDirectory)
    {
        if (moveFolderContents (File (mPresetDirectory), File (newPresetDir)))
        {
            mPresetDirectory = newPresetDir;
            savePresetDirectorySettings();
        }
    }

    bool collectionsChanged = false;
    for (int i = 0; i < mCollectionsDirectories.size(); ++i)
    {
        const String remapped = remapExactDefault (mCollectionsDirectories[i], "Collections");
        if (remapped != mCollectionsDirectories[i]
            && moveFolderContents (File (mCollectionsDirectories[i]), File (remapped)))
        {
            mCollectionsDirectories.set (i, remapped);
            collectionsChanged = true;
        }
    }
    if (collectionsChanged)
        saveCollectionsDirectoriesSettings();

    const File legacyRootDir (legacyRoot);
    if (legacyRootDir.isDirectory() && legacyRootDir.getNumberOfChildFiles (File::findFilesAndDirectories) == 0)
        legacyRootDir.deleteRecursively();
}

void PresetManager::loadPresetDirectorySettings()
{
    const File settingsFile = getSettingsFile();

    if (! settingsFile.existsAsFile())
        return;

    PropertiesFile settings (settingsFile, getPresetSettingsOptions());
    const String savedDirectory = settings.getValue (PRESET_DIRECTORY_SETTINGS_KEY);

    if (savedDirectory.isNotEmpty() && File (savedDirectory).isDirectory())
        mPresetDirectory = savedDirectory;
}

void PresetManager::savePresetDirectorySettings()
{
    const File settingsFile = getSettingsFile();
    settingsFile.getParentDirectory().createDirectory();

    PropertiesFile settings (settingsFile, getPresetSettingsOptions());
    settings.setValue (PRESET_DIRECTORY_SETTINGS_KEY, mPresetDirectory);
    settings.saveIfNeeded();
}

void PresetManager::ensurePresetDirectoryExists()
{
    File (mPresetDirectory).createDirectory();
}

void PresetManager::loadCrtSettings()
{
    const File settingsFile = getSettingsFile();

    if (! settingsFile.existsAsFile())
        return;

    PropertiesFile settings (settingsFile, getPresetSettingsOptions());
    mCrtEnabled = settings.getBoolValue (CRT_ENABLED_SETTINGS_KEY, true);
    mCrtStrength = juce::jlimit (0, 2, settings.getIntValue (CRT_STRENGTH_SETTINGS_KEY, 2));
}

void PresetManager::saveCrtSettings()
{
    const File settingsFile = getSettingsFile();
    settingsFile.getParentDirectory().createDirectory();

    PropertiesFile settings (settingsFile, getPresetSettingsOptions());
    settings.setValue (CRT_ENABLED_SETTINGS_KEY, mCrtEnabled);
    settings.setValue (CRT_STRENGTH_SETTINGS_KEY, mCrtStrength);
    settings.saveIfNeeded();
}

void PresetManager::setCrtEnabled (bool enabled)
{
    mCrtEnabled = enabled;
    saveCrtSettings();
}

void PresetManager::setCrtStrength (int strength)
{
    mCrtStrength = juce::jlimit (0, 2, strength);
    saveCrtSettings();
}

void PresetManager::loadVoiceSettings()
{
    const File settingsFile = getSettingsFile();

    if (! settingsFile.existsAsFile())
        return;

    PropertiesFile settings (settingsFile, getPresetSettingsOptions());
    mVoicesCount = juce::jlimit (1, MAX_VOICES, settings.getIntValue (VOICES_COUNT_SETTINGS_KEY, MAX_VOICES));
    mLegatoEnabled = settings.getBoolValue (LEGATO_ENABLED_SETTINGS_KEY, true);
    mPitchLockEnabled = settings.getBoolValue (PITCH_LOCK_SETTINGS_KEY, false);
}

void PresetManager::saveVoiceSettings()
{
    const File settingsFile = getSettingsFile();
    settingsFile.getParentDirectory().createDirectory();

    PropertiesFile settings (settingsFile, getPresetSettingsOptions());
    settings.setValue (VOICES_COUNT_SETTINGS_KEY, mVoicesCount);
    settings.setValue (LEGATO_ENABLED_SETTINGS_KEY, mLegatoEnabled);
    settings.setValue (PITCH_LOCK_SETTINGS_KEY, mPitchLockEnabled);
    settings.saveIfNeeded();
}

void PresetManager::setVoicesCount (int n)
{
    mVoicesCount = juce::jlimit (1, MAX_VOICES, n);
    saveVoiceSettings();
}

void PresetManager::setLegatoEnabled (bool b)
{
    mLegatoEnabled = b;
    saveVoiceSettings();
}

void PresetManager::setPitchLockEnabled (bool b)
{
    mPitchLockEnabled = b;
    saveVoiceSettings();
}

void PresetManager::loadInterfaceSettings()
{
    const File settingsFile = getSettingsFile();

    if (! settingsFile.existsAsFile())
        return;

    PropertiesFile settings (settingsFile, getPresetSettingsOptions());
    mTooltipDelayMs = juce::jlimit (0, 2000, settings.getIntValue (TOOLTIP_DELAY_MS_SETTINGS_KEY, 500));
    mTooltipsEnabled = settings.getBoolValue (TOOLTIPS_ENABLED_SETTINGS_KEY, true);
    mConfirmDestructive = settings.getBoolValue (CONFIRM_DESTRUCTIVE_SETTINGS_KEY, true);
}

void PresetManager::saveInterfaceSettings()
{
    const File settingsFile = getSettingsFile();
    settingsFile.getParentDirectory().createDirectory();

    PropertiesFile settings (settingsFile, getPresetSettingsOptions());
    settings.setValue (TOOLTIP_DELAY_MS_SETTINGS_KEY, mTooltipDelayMs);
    settings.setValue (TOOLTIPS_ENABLED_SETTINGS_KEY, mTooltipsEnabled);
    settings.setValue (CONFIRM_DESTRUCTIVE_SETTINGS_KEY, mConfirmDestructive);
    settings.saveIfNeeded();
}

void PresetManager::setTooltipDelayMs (int ms)
{
    mTooltipDelayMs = juce::jlimit (0, 2000, ms);
    saveInterfaceSettings();
}

void PresetManager::setTooltipsEnabled (bool b)
{
    mTooltipsEnabled = b;
    saveInterfaceSettings();
}

void PresetManager::setConfirmDestructive (bool b)
{
    mConfirmDestructive = b;
    saveInterfaceSettings();
}

// ============================================================================
// Multiple Collections Directories Support
// ============================================================================

Array<String> PresetManager::getCollectionsDirectories() const
{
    return mCollectionsDirectories;
}

void PresetManager::addCollectionsDirectory (const String& inDirectory)
{
    if (inDirectory.isEmpty())
        return;
    
    const File dir (inDirectory);
    
    if (! dir.isDirectory())
        return;
    
    // Avoid duplicates
    const String fullPath = dir.getFullPathName();
    
    for (const auto& existingDir : mCollectionsDirectories)
    {
        if (existingDir == fullPath)
            return;
    }
    
    mCollectionsDirectories.add (fullPath);
    saveCollectionsDirectoriesSettings();
}

void PresetManager::removeCollectionsDirectory (int inIndex)
{
    if (inIndex < 0 || inIndex >= mCollectionsDirectories.size())
        return;
    
    mCollectionsDirectories.remove (inIndex);
    saveCollectionsDirectoriesSettings();
}

void PresetManager::resetCollectionsDirectoriesToDefault()
{
    mCollectionsDirectories.clear();
    mCollectionsDirectories.add (getDefaultCollectionsDirectory());
    
    // Ensure default directory exists
    File (getDefaultCollectionsDirectory()).createDirectory();
    
    saveCollectionsDirectoriesSettings();
}

void PresetManager::loadCollectionsDirectoriesSettings()
{
    const File settingsFile = getSettingsFile();
    
    if (! settingsFile.existsAsFile())
        return;
    
    PropertiesFile settings (settingsFile, getPresetSettingsOptions());
    const String savedDirectories = settings.getValue (COLLECTIONS_DIRECTORIES_SETTINGS_KEY);
    
    if (savedDirectories.isNotEmpty())
    {
        // Parse semicolon-separated list of directories
        StringArray dirArray;
        dirArray.addTokens (savedDirectories, ";", "");
        
        for (const auto& dir : dirArray)
        {
            String trimmedDir = dir.trim();
            if (trimmedDir.isNotEmpty() && File (trimmedDir).isDirectory())
            {
                mCollectionsDirectories.add (trimmedDir);
            }
        }
    }
}

void PresetManager::saveCollectionsDirectoriesSettings()
{
    const File settingsFile = getSettingsFile();
    settingsFile.getParentDirectory().createDirectory();
    
    PropertiesFile settings (settingsFile, getPresetSettingsOptions());
    
    // Join directories with semicolon
    String directoriesString;
    for (int i = 0; i < mCollectionsDirectories.size(); i++)
    {
        if (i > 0)
            directoriesString += ";";
        directoriesString += mCollectionsDirectories[i];
    }
    
    settings.setValue (COLLECTIONS_DIRECTORIES_SETTINGS_KEY, directoriesString);
    settings.saveIfNeeded();
}

String PresetManager::findSoundFilePath (const String& inRelativePath)
{
    // Search in all collections directories
    for (const auto& collectionsDir : mCollectionsDirectories)
    {
        String fullPath = collectionsDir + directorySeparator + inRelativePath;
        File soundFile (fullPath);
        
        if (soundFile.existsAsFile())
        {
            return fullPath;
        }
    }
    
    // Not found
    return "";
}

void PresetManager::loadAnalyzerSettings()
{
    const File settingsFile = getSettingsFile();
    if (! settingsFile.existsAsFile())
        return;

    PropertiesFile settings (settingsFile, getPresetSettingsOptions());
    mAnaResolution = settings.getDoubleValue (ANA_RESOLUTION_SETTINGS_KEY, mAnaResolution);
    mAnaWindow = settings.getDoubleValue (ANA_WINDOW_SETTINGS_KEY, mAnaWindow);
    mAnaAmpFloor = settings.getDoubleValue (ANA_AMP_FLOOR_SETTINGS_KEY, mAnaAmpFloor);
    mAnaOnsetSens = settings.getDoubleValue (ANA_ONSET_SENS_SETTINGS_KEY, mAnaOnsetSens);
    mAnaFundamental = settings.getDoubleValue (ANA_FUNDAMENTAL_SETTINGS_KEY, mAnaFundamental);
    mAnaF0Lo = settings.getDoubleValue (ANA_F0_LO_SETTINGS_KEY, mAnaF0Lo);
    mAnaF0Hi = settings.getDoubleValue (ANA_F0_HI_SETTINGS_KEY, mAnaF0Hi);
    mAnaSrcDb = settings.getDoubleValue (ANA_SRC_DB_SETTINGS_KEY, mAnaSrcDb);
    mAnaRsnDb = settings.getDoubleValue (ANA_RSN_DB_SETTINGS_KEY, mAnaRsnDb);
}

void PresetManager::saveAnalyzerSettings()
{
    const File settingsFile = getSettingsFile();
    settingsFile.getParentDirectory().createDirectory();

    PropertiesFile settings (settingsFile, getPresetSettingsOptions());
    settings.setValue (ANA_RESOLUTION_SETTINGS_KEY, mAnaResolution);
    settings.setValue (ANA_WINDOW_SETTINGS_KEY, mAnaWindow);
    settings.setValue (ANA_AMP_FLOOR_SETTINGS_KEY, mAnaAmpFloor);
    settings.setValue (ANA_ONSET_SENS_SETTINGS_KEY, mAnaOnsetSens);
    settings.setValue (ANA_FUNDAMENTAL_SETTINGS_KEY, mAnaFundamental);
    settings.setValue (ANA_F0_LO_SETTINGS_KEY, mAnaF0Lo);
    settings.setValue (ANA_F0_HI_SETTINGS_KEY, mAnaF0Hi);
    settings.setValue (ANA_SRC_DB_SETTINGS_KEY, mAnaSrcDb);
    settings.setValue (ANA_RSN_DB_SETTINGS_KEY, mAnaRsnDb);
    settings.saveIfNeeded();
}

void PresetManager::setAnalyzerSettings (double resolution, double window, double ampFloor,
                                         double onsetSens, double fundamental,
                                         double f0Lo, double f0Hi, double srcDb, double rsnDb)
{
    mAnaResolution = resolution;
    mAnaWindow = window;
    mAnaAmpFloor = ampFloor;
    mAnaOnsetSens = onsetSens;
    mAnaFundamental = fundamental;
    mAnaF0Lo = f0Lo;
    mAnaF0Hi = f0Hi;
    mAnaSrcDb = srcDb;
    mAnaRsnDb = rsnDb;
    saveAnalyzerSettings();
}
