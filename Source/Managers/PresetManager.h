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

#pragma once

#include "JuceHeader.h"

#include <atomic>
#include <memory>

#include "../Helpers/SMTConstants.h"
#include "../Entities/MorphexSynth.h"

#include "../Core/Sound.h"

#define PRESET_FILE_EXTENSION ".mpf"

class PresetManager
{
public:
    
    PresetManager (AudioProcessor* inProcessor, MorphexSynth* inMorphexSynth);
    ~PresetManager();
    
    ValueTree getStateTree();
    bool loadStateFromTree (const ValueTree& tree);
    
    int getNumberOfPresets();
    String getPresetName (int inPresetIndex);
    
    void createNewPreset();
    void savePreset();
    void saveAsPreset (String inPresetName);
    bool loadPreset (int inPresetIndex);
    bool loadPresetFile (const File& file);
    
    void getSoundInformation (ValueTree& parentTree);
    bool setSoundInformation (const ValueTree& presetTree);

    bool getIsCurrentPresetSaved();
    String getCurrentPresetName();

    String getPresetDirectory() const;
    bool setPresetDirectory (const String& inDirectory);
    void resetPresetDirectoryToDefault();

    // CRT session settings (persisted to settings.xml, restored on launch --
    // matches get/setCrtEnabled/get/setCrtStrength).
    bool getCrtEnabled() const noexcept { return mCrtEnabled; }
    void setCrtEnabled (bool enabled);
    int getCrtStrength() const noexcept { return mCrtStrength; }
    void setCrtStrength (int strength);

    // Voice session settings (persisted the same way).
    int getVoicesCount() const noexcept { return mVoicesCount; }
    void setVoicesCount (int n);
    bool getLegatoEnabled() const noexcept { return mLegatoEnabled; }
    void setLegatoEnabled (bool b);
    bool getPitchLockEnabled() const noexcept { return mPitchLockEnabled; }
    void setPitchLockEnabled (bool b);

    // Interface session settings (persisted the same way).
    int getTooltipDelayMs() const noexcept { return mTooltipDelayMs; }
    void setTooltipDelayMs (int ms);
    bool getTooltipsEnabled() const noexcept { return mTooltipsEnabled; }
    void setTooltipsEnabled (bool b);
    bool getConfirmDestructive() const noexcept { return mConfirmDestructive; }
    void setConfirmDestructive (bool b);

    // Analyzer session settings (persisted, restored in ctor).
    double getAnalyzerResolution() const noexcept { return mAnaResolution; }
    double getAnalyzerWindow() const noexcept { return mAnaWindow; }
    double getAnalyzerAmpFloor() const noexcept { return mAnaAmpFloor; }
    double getAnalyzerOnsetSens() const noexcept { return mAnaOnsetSens; }
    double getAnalyzerFundamental() const noexcept { return mAnaFundamental; }
    double getAnalyzerF0Lo() const noexcept { return mAnaF0Lo; }
    double getAnalyzerF0Hi() const noexcept { return mAnaF0Hi; }
    double getAnalyzerSrcDb() const noexcept { return mAnaSrcDb; }
    double getAnalyzerRsnDb() const noexcept { return mAnaRsnDb; }
    void setAnalyzerSettings (double resolution, double window, double ampFloor,
                              double onsetSens, double fundamental,
                              double f0Lo, double f0Hi, double srcDb, double rsnDb);
    
    // Multiple collections directories support
    Array<String> getCollectionsDirectories() const;
    void addCollectionsDirectory (const String& inDirectory);
    void removeCollectionsDirectory (int inIndex);
    void resetCollectionsDirectoriesToDefault();
    
private:
    
    struct MissingSound
    {
        int slotIndex;
        String originalPath;
    };
    
    // PathResolver: tries multiple locations to find a sound file
    String resolveSoundPath (const String& originalPath, const File& presetFile);
    static String resolveSoundPath (const String& originalPath, const File& presetFile,
                                    const String& presetDir, const Array<String>& collectionsDirs);
    
    void storeLocalPreset();
    void loadPresetDirectorySettings();
    void savePresetDirectorySettings();
    void ensurePresetDirectoryExists();
    void loadCollectionsDirectoriesSettings();
    void saveCollectionsDirectoriesSettings();
    void loadCrtSettings();
    void saveCrtSettings();
    void loadVoiceSettings();
    void saveVoiceSettings();
    void loadInterfaceSettings();
    void saveInterfaceSettings();
    File getSettingsFile() const;
    File getLegacySettingsFile() const;
    void migrateLegacySettingsFile();
    void migrateLegacyDataDirectories();    
    // Helper to find sound file across all collections directories
    String findSoundFilePath (const String& inRelativePath);
    
    bool mCurrentPresetIsSaved;
    
    File mCurrentlyLoadedPreset;
    
    Array<File> mLocalPresets;
    
    String mCurrentPresetName;
    String mPresetDirectory;
    Array<String> mCollectionsDirectories;
    
    AudioProcessor* mProcessor;
    MorphexSynth* mMorphexSynth;

    // CRT session settings (persisted, restored in ctor).
    bool mCrtEnabled = true;
    int mCrtStrength = 2;

    // Voice session settings (persisted, restored in ctor).
    int mVoicesCount = MAX_VOICES;
    bool mLegatoEnabled = true;
    bool mPitchLockEnabled = false;

    // Interface session settings (persisted, restored in ctor).
    int mTooltipDelayMs = 500;
    bool mTooltipsEnabled = true;
    bool mConfirmDestructive = true;

    // Analyzer session settings (persisted, restored in ctor).
    double mAnaResolution = 1001.0;
    double mAnaWindow = 1024.0;
    double mAnaAmpFloor = -100.0;
    double mAnaOnsetSens = 0.5;
    double mAnaFundamental = 440.0;
    double mAnaF0Lo = 100.0;
    double mAnaF0Hi = 1000.0;
    double mAnaSrcDb = 0.0;
    double mAnaRsnDb = 0.0;
    void loadAnalyzerSettings();
    void saveAnalyzerSettings();

    // Guards rapid preset switches: stale workers never install.
    std::atomic<int> presetLoadGeneration { 0 };
};
