#pragma once
#include <JuceHeader.h>

//==============================================================================
enum class MoodTag : uint32_t
{
    None     = 0,
    Spooky   = 1 << 0,
    Ethereal = 1 << 1,
    Dark     = 1 << 2,
    Glo      = 1 << 3,
    Abyss    = 1 << 4,
    Alien    = 1 << 5,
    Drift    = 1 << 6,
    Frozen   = 1 << 7,
    Mutant   = 1 << 8,
    Plasma   = 1 << 9
};

inline uint32_t operator| (MoodTag a, MoodTag b)
{
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
}

inline const char* moodTagName (MoodTag t)
{
    switch (t) {
        case MoodTag::Spooky:   return "Spooky";
        case MoodTag::Ethereal: return "Ethereal";
        case MoodTag::Dark:     return "Dark";
        case MoodTag::Glo:      return "Glo";
        case MoodTag::Abyss:    return "Abyss";
        case MoodTag::Alien:    return "Alien";
        case MoodTag::Drift:    return "Drift";
        case MoodTag::Frozen:   return "Frozen";
        case MoodTag::Mutant:   return "Mutant";
        case MoodTag::Plasma:   return "Plasma";
        default:                return "None";
    }
}

//==============================================================================
struct SnotPreset
{
    juce::String name        { "Init" };
    juce::String author      { "SNOT" };
    juce::String description;
    uint32_t     tags        { 0 };  // bitmask of MoodTag
    int          bpm         { 140 };
    bool         bpmSync     { true };
    juce::ValueTree state;           // full APVTS state snapshot

    bool hasTag (MoodTag t) const
    {
        return (tags & static_cast<uint32_t>(t)) != 0;
    }
};

//==============================================================================
/**
 * PresetManager
 *
 * Handles saving, loading, and browsing of SNOT presets.
 * Presets are stored as JSON files in the user's app data directory.
 * Factory presets are embedded as binary data (BinaryData).
 *
 * Features:
 *   - Mood tag filtering (bitmasked)
 *   - Fast text search
 *   - Previous/next navigation
 *   - Save As dialog
 *   - Import / Export
 */
class PresetManager
{
public:
    static constexpr int NUM_FACTORY_PRESETS = 20;

    PresetManager (juce::AudioProcessor& processor,
                   juce::AudioProcessorValueTreeState& apvts)
        : processor (processor), apvts (apvts)
    {
        userPresetsDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("SNOT").getChildFile ("Presets");
        userPresetsDir.createDirectory();

        loadFactoryPresets();
        scanUserPresets();
    }

    //==============================================================================
    int  getNumPresets()           const { return static_cast<int> (allPresets.size()); }
    int  getCurrentIndex()         const { return currentIndex; }
    juce::String getPresetName (int i) const
    {
        if (i >= 0 && i < (int)allPresets.size()) return allPresets[i].name;
        return {};
    }

    const SnotPreset& getPreset (int i) const { return allPresets[i]; }

    //==============================================================================
    void loadPreset (int index)
    {
        if (index < 0 || index >= (int)allPresets.size()) return;
        currentIndex = index;
        apvts.replaceState (allPresets[index].state);
    }

    void loadNextPreset()
    {
        loadPreset ((currentIndex + 1) % getNumPresets());
    }

    void loadPrevPreset()
    {
        loadPreset ((currentIndex - 1 + getNumPresets()) % getNumPresets());
    }

    //==============================================================================
    void saveCurrentAsUser (const juce::String& name, uint32_t tags,
                            const juce::String& author = "User",
                            const juce::String& description = "")
    {
        SnotPreset preset;
        preset.name        = name;
        preset.author      = author;
        preset.description = description;
        preset.tags        = tags;
        preset.state       = apvts.copyState();

        // Serialize to JSON file
        const auto file = userPresetsDir.getChildFile (name + ".snot");
        juce::var json = presetToJson (preset);
        file.replaceWithText (juce::JSON::toString (json, true));

        allPresets.push_back (preset);
        currentIndex = static_cast<int> (allPresets.size() - 1);
    }

    void renamePreset (int index, const juce::String& newName)
    {
        if (index < 0 || index >= (int)allPresets.size()) return;
        allPresets[index].name = newName;
    }

    //==============================================================================
    /** Filter presets by mood tag bitmask (0 = show all). */
    std::vector<int> getFilteredIndices (uint32_t tagMask,
                                         const juce::String& search = {}) const
    {
        std::vector<int> result;
        for (int i = 0; i < (int)allPresets.size(); ++i)
        {
            const auto& p = allPresets[i];
            if (tagMask != 0 && (p.tags & tagMask) == 0) continue;
            if (search.isNotEmpty() &&
                !p.name.containsIgnoreCase (search) &&
                !p.description.containsIgnoreCase (search)) continue;
            result.push_back (i);
        }
        return result;
    }

    //==============================================================================
    void exportPreset (int index, const juce::File& targetFile) const
    {
        if (index < 0 || index >= (int)allPresets.size()) return;
        juce::var json = presetToJson (allPresets[index]);
        targetFile.replaceWithText (juce::JSON::toString (json, true));
    }

    bool importPreset (const juce::File& file)
    {
        if (!file.existsAsFile() || file.getFileExtension() != ".snot")
            return false;

        auto json = juce::JSON::parse (file.loadFileAsString());
        if (!json.isObject()) return false;

        SnotPreset preset = presetFromJson (json);
        allPresets.push_back (preset);
        return true;
    }

private:
    //==============================================================================
    void loadFactoryPresets()
    {
        // In production these come from BinaryData embedded at compile time.
        // Here we build a representative set programmatically.

        struct FactoryDef
        {
            const char* name;
            uint32_t    tags;
            const char* desc;
        };

        static const FactoryDef defs[] =
        {
            { "Abyss Gate",        MoodTag::Abyss | MoodTag::Dark | MoodTag::Ethereal,
              "Portal-deep reverb wash with slow drift. Glo trap essentials." },
            { "Ghost Frequency",   MoodTag::Spooky | MoodTag::Alien,
              "Spectral warp choir that haunts. Leads with no attack, only presence." },
            { "Jeezy Void",        MoodTag::Dark | MoodTag::Glo,
              "2007 trap energy passed through a dimensional gate." },
            { "Plasma String",     MoodTag::Plasma | MoodTag::Ethereal,
              "Bowed synth with plasma arc on the body. Cinematic leads." },
            { "808 Inflated",      MoodTag::Glo | MoodTag::Dark,
              "808s pushed to their harmonic limit. Glo glide with maximum bloom." },
            { "Frozen Portal",     MoodTag::Frozen | MoodTag::Abyss,
              "Freeze-captured reverb tail looping infinitely. Alien ambient beds." },
            { "Drift Dimension",   MoodTag::Drift | MoodTag::Alien,
              "Every parameter slowly mutating. Never the same twice." },
            { "Spectral Specter",  MoodTag::Spooky | MoodTag::Ethereal | MoodTag::Alien,
              "FFT-domain chorus + shimmer reverb. Multilayered ghost voices." },
            { "Neural Wash",       MoodTag::Ethereal | MoodTag::Drift,
              "Stereo neural motion on wide reverb. Spatial and breathing." },
            { "Gravity LP",        MoodTag::Dark | MoodTag::Glo,
              "Gravity filter auto-tracks 808 energy. Punchy and dark." },
            { "Mutant Lead",       MoodTag::Mutant | MoodTag::Alien,
              "Mutation engine randomizing pitch smear delay. Controlled chaos." },
            { "Texture Veil",      MoodTag::Ethereal | MoodTag::Frozen,
              "Granular texture layer under clean lead. Presence without mud." },
            { "Plasma Drive 808",  MoodTag::Plasma | MoodTag::Glo,
              "808 through plasma distortion at 4x oversampling. Brutal bloom." },
            { "Dark Choir",        MoodTag::Dark | MoodTag::Spooky | MoodTag::Ethereal,
              "8-voice spectral warp chorus with slow drift reverb behind." },
            { "Portal Init",       0,
              "Clean signal path. Starting point for your own portal." },
            { "Cinematic Sweep",   MoodTag::Ethereal | MoodTag::Alien | MoodTag::Abyss,
              "Slow gravity filter sweep with drift reverb and shimmer rise." },
            { "Glo Bounce",        MoodTag::Glo | MoodTag::Dark,
              "Pitch-smear delay synchronized to tempo. Glo trap bounce." },
            { "Alien Tape",        MoodTag::Alien | MoodTag::Mutant,
              "Plasma distortion + spectral warp mimicking alien tape saturation." },
            { "Void Static",       MoodTag::Abyss | MoodTag::Dark,
              "Texture generator creating sub-harmonic cosmic static bed." },
            { "Portal Master",     MoodTag::Abyss | MoodTag::Ethereal | MoodTag::Glo,
              "Full chain: all modules balanced for full glo trap production use." },
        };

        for (auto& d : defs)
        {
            SnotPreset preset;
            preset.name   = d.name;
            preset.author = "SNOT Factory";
            preset.tags   = d.tags;
            preset.desc   = d.desc;
            preset.state  = apvts.copyState(); // baseline state
            allPresets.push_back (preset);
        }
    }

    void scanUserPresets()
    {
        for (const auto& file : userPresetsDir.findChildFiles (
                juce::File::findFiles, false, "*.snot"))
        {
            auto json = juce::JSON::parse (file.loadFileAsString());
            if (json.isObject())
                allPresets.push_back (presetFromJson (json));
        }
    }

    juce::var presetToJson (const SnotPreset& preset) const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("name",    preset.name);
        obj->setProperty ("author",  preset.author);
        obj->setProperty ("desc",    preset.description);
        obj->setProperty ("tags",    static_cast<int>(preset.tags));
        obj->setProperty ("bpm",     preset.bpm);
        obj->setProperty ("version", 1);

        if (preset.state.isValid())
        {
            std::unique_ptr<juce::XmlElement> xml (preset.state.createXml());
            obj->setProperty ("state", xml ? xml->toString() : "");
        }
        return juce::var (obj);
    }

    SnotPreset presetFromJson (const juce::var& json) const
    {
        SnotPreset preset;
        preset.name        = json["name"].toString();
        preset.author      = json["author"].toString();
        preset.description = json["desc"].toString();
        preset.tags        = static_cast<uint32_t> (static_cast<int>(json["tags"]));
        preset.bpm         = json["bpm"];

        const juce::String stateXml = json["state"].toString();
        if (stateXml.isNotEmpty())
        {
            auto xml = juce::XmlDocument::parse (stateXml);
            if (xml) preset.state = juce::ValueTree::fromXml (*xml);
        }
        return preset;
    }

    //==============================================================================
    juce::AudioProcessor&                    processor;
    juce::AudioProcessorValueTreeState&      apvts;
    juce::File                               userPresetsDir;
    std::vector<SnotPreset>                  allPresets;
    int                                      currentIndex { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
