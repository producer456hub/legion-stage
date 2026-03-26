#pragma once

#include <JuceHeader.h>

// Handles preset browsing for plugins.
// Strategy 1: Use standard program API (works for u-he, etc.)
// Strategy 2: Scan plugin parameters for a preset-like parameter (some plugins)
// Strategy 3: Scan Arturia preset folder for names (display only, no loading)
class PresetBrowser
{
public:
    struct PresetInfo {
        juce::String name;
        int index;
    };

    // Scan a plugin and build a preset list
    static juce::Array<PresetInfo> getPresets(juce::AudioProcessor* plugin)
    {
        juce::Array<PresetInfo> presets;
        if (plugin == nullptr) return presets;

        int numPrograms = plugin->getNumPrograms();

        if (numPrograms > 1)
        {
            // Plugin exposes presets through program API
            for (int i = 0; i < numPrograms; ++i)
            {
                PresetInfo p;
                p.name = plugin->getProgramName(i);
                if (p.name.isEmpty())
                    p.name = "Preset " + juce::String(i + 1);
                p.index = i;
                presets.add(p);
            }
        }
        else
        {
            // Try scanning Arturia preset folder for names
            juce::String pluginName = plugin->getName();
            auto names = scanArturiaPresetNames(pluginName);

            for (int i = 0; i < names.size(); ++i)
            {
                PresetInfo p;
                p.name = names[i];
                p.index = i;
                presets.add(p);
            }
        }

        return presets;
    }

    // Load preset by index using program change
    static void loadPreset(juce::AudioProcessor* plugin, int index)
    {
        if (plugin == nullptr) return;

        if (plugin->getNumPrograms() > 1)
        {
            plugin->setCurrentProgram(index);
        }
    }

    // Get list of Arturia preset names from disk (for display)
    static juce::StringArray scanArturiaPresetNames(const juce::String& pluginName)
    {
        juce::StringArray names;

        // Try common paths
        juce::Array<juce::File> searchDirs;
        searchDirs.add(juce::File("C:/ProgramData/Arturia/Presets/" + pluginName + "/Factory/Factory"));
        searchDirs.add(juce::File("C:/ProgramData/Arturia/Presets/" + pluginName + "/Factory"));
        searchDirs.add(juce::File("C:/ProgramData/Arturia/Presets/" + pluginName));

        for (auto& dir : searchDirs)
        {
            if (dir.isDirectory())
            {
                auto files = dir.findChildFiles(juce::File::findFiles, false);
                files.sort();

                for (auto& f : files)
                {
                    if (f.getFileExtension().isEmpty()) // Arturia presets have no extension
                        names.add(f.getFileName());
                }

                if (!names.isEmpty()) break;
            }
        }

        return names;
    }
};
