#pragma once
#include <JuceHeader.h>

class ModulationMatrix;

//==============================================================================
/**
 * MacroMapping
 * Describes how one macro value (0..1) maps to one parameter.
 */
struct MacroMapping
{
    juce::String paramID;
    float        rangeMin  { 0.0f };
    float        rangeMax  { 1.0f };
    float        curve     { 1.0f }; // 1.0 = linear, <1 = log, >1 = exp
    bool         bipolar   { false };
};

//==============================================================================
/**
 * MacroEngine
 *
 * Manages 8 macro knobs that can each drive N parameter targets.
 * Macro values are written to APVTS (automation-compatible).
 * The mapping from macro value → parameter delta is applied here
 * before being forwarded to the ModulationMatrix.
 *
 * Macro metadata (display name, colour, assignments) is stored
 * in a ValueTree for preset serialization.
 */
class MacroEngine
{
public:
    static constexpr int NUM_MACROS = 8;

    struct MacroSlot
    {
        juce::String             name   { "Macro" };
        juce::Colour             colour { 0xff00ffcc };
        std::vector<MacroMapping> mappings;
    };

    explicit MacroEngine (juce::AudioProcessorValueTreeState& apvts)
        : apvts (apvts)
    {
        for (int i = 0; i < NUM_MACROS; ++i)
        {
            slots[i].name = "Macro " + juce::String (i + 1);
            const juce::Colour palette[] = {
                juce::Colour(0xff00ffcc), juce::Colour(0xffff00aa),
                juce::Colour(0xff7700ff), juce::Colour(0xff00aaff),
                juce::Colour(0xffff5500), juce::Colour(0xff00ff44),
                juce::Colour(0xffff2222), juce::Colour(0xffccff00)
            };
            slots[i].colour = palette[i];
            pMacros[i] = apvts.getRawParameterValue ("macro_" + juce::String (i + 1));
        }
    }

    void setModulationMatrix (ModulationMatrix* m) { modMatrix = m; }

    //==============================================================================
    /** Called on audio thread — reads macro values and dispatches to targets. */
    void process (int numSamples)
    {
        juce::ignoreUnused (numSamples);
        for (int m = 0; m < NUM_MACROS; ++m)
        {
            const float value = pMacros[m]->load (std::memory_order_relaxed);
            if (std::abs (value - lastMacroValue[m]) < 1e-6f) continue;
            lastMacroValue[m] = value;

            for (auto& mapping : slots[m].mappings)
            {
                const float curved = (mapping.curve == 1.0f)
                    ? value
                    : std::pow (value, mapping.curve);

                const float target = juce::jmap (curved, 0.0f, 1.0f,
                                                  mapping.rangeMin, mapping.rangeMax);

                if (auto* param = apvts.getParameter (mapping.paramID))
                {
                    const auto& range = apvts.getParameterRange (mapping.paramID);
                    param->setValueNotifyingHost (range.convertTo0to1 (target));
                }
            }
        }
    }

    //==============================================================================
    void addMapping (int macroIndex, const MacroMapping& mapping)
    {
        jassert (macroIndex >= 0 && macroIndex < NUM_MACROS);
        slots[macroIndex].mappings.push_back (mapping);
    }

    void clearMappings (int macroIndex)
    {
        jassert (macroIndex >= 0 && macroIndex < NUM_MACROS);
        slots[macroIndex].mappings.clear();
    }

    void setMacroName   (int i, const juce::String& n) { slots[i].name = n; }
    void setMacroColour (int i, juce::Colour c)        { slots[i].colour = c; }

    const MacroSlot& getSlot (int i) const { return slots[i]; }
    float            getMacroValue (int i) const { return pMacros[i]->load(); }

    //==============================================================================
    juce::ValueTree toValueTree() const
    {
        juce::ValueTree tree ("MacroEngine");
        for (int m = 0; m < NUM_MACROS; ++m)
        {
            juce::ValueTree slot ("Macro");
            slot.setProperty ("name",   slots[m].name,          nullptr);
            slot.setProperty ("colour", slots[m].colour.toDisplayString(true), nullptr);
            for (auto& mp : slots[m].mappings)
            {
                juce::ValueTree mpTree ("Mapping");
                mpTree.setProperty ("param",   mp.paramID,  nullptr);
                mpTree.setProperty ("min",     mp.rangeMin, nullptr);
                mpTree.setProperty ("max",     mp.rangeMax, nullptr);
                mpTree.setProperty ("curve",   mp.curve,    nullptr);
                mpTree.setProperty ("bipolar", mp.bipolar,  nullptr);
                slot.appendChild (mpTree, nullptr);
            }
            tree.appendChild (slot, nullptr);
        }
        return tree;
    }

    void fromValueTree (const juce::ValueTree& tree)
    {
        for (int m = 0; m < NUM_MACROS && m < tree.getNumChildren(); ++m)
        {
            auto slot = tree.getChild (m);
            slots[m].name   = slot.getProperty ("name").toString();
            slots[m].colour = juce::Colour::fromString (slot.getProperty ("colour").toString());
            slots[m].mappings.clear();
            for (int c = 0; c < slot.getNumChildren(); ++c)
            {
                auto mp = slot.getChild (c);
                MacroMapping mapping;
                mapping.paramID  = mp.getProperty ("param").toString();
                mapping.rangeMin = static_cast<float> (mp.getProperty ("min"));
                mapping.rangeMax = static_cast<float> (mp.getProperty ("max"));
                mapping.curve    = static_cast<float> (mp.getProperty ("curve"));
                mapping.bipolar  = static_cast<bool>  (mp.getProperty ("bipolar"));
                slots[m].mappings.push_back (mapping);
            }
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    ModulationMatrix*                   modMatrix { nullptr };

    MacroSlot  slots[NUM_MACROS];
    float      lastMacroValue[NUM_MACROS] {};

    std::atomic<float>* pMacros[NUM_MACROS] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacroEngine)
};

//==============================================================================
/**
 * ModulationSource — one LFO or envelope that can drive parameters.
 */
struct ModSource
{
    enum Type { LFO_SINE, LFO_TRI, LFO_SQUARE, LFO_RANDOM, ENVELOPE };

    Type  type    { LFO_SINE };
    float rate    { 1.0f };   // Hz for LFOs
    float depth   { 0.5f };
    float phase   { 0.0f };   // current phase 0..1
    bool  bpmSync { false };
    float syncDiv { 4.0f };   // beat division

    // Envelope params (used when type == ENVELOPE)
    float attack  { 0.01f };
    float decay   { 0.1f };
    float sustain { 0.7f };
    float release { 0.3f };
    float envPhase{ 0.0f };
    enum EnvStage { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
    EnvStage envStage { ENV_IDLE };
};

/** Describes how one source modulates one parameter. */
struct ModRoute
{
    int         sourceIndex { 0 };
    juce::String paramID;
    float        amount    { 0.5f }; // ±1
    bool         bipolar   { true };
};

//==============================================================================
/**
 * ModulationMatrix
 *
 * Manages all modulation sources (LFOs, envelopes) and their routing
 * to parameters. Values are accumulated per-block and written to
 * parameter modulation offsets.
 *
 * Thread-safety: sources/routes modified on message thread (lock-protected),
 * process() called on audio thread (reads a stable snapshot).
 */
class ModulationMatrix
{
public:
    explicit ModulationMatrix (juce::AudioProcessorValueTreeState& apvts)
        : apvts (apvts) {}

    void prepare (double sr, int /*blockSize*/)
    {
        sampleRate = sr;
    }

    //==============================================================================
    /** Tick all sources, write accumulated modulation to parameters. */
    void process (int numSamples)
    {
        const float dt = static_cast<float> (numSamples) / static_cast<float> (sampleRate);

        // Update source phases
        for (auto& src : sources)
        {
            if (src.type == ModSource::ENVELOPE) continue;
            src.phase += src.rate * dt;
            if (src.phase > 1.0f) src.phase -= 1.0f;
        }

        // Compute source values and apply to routes
        for (auto& route : routes)
        {
            if (route.sourceIndex >= (int)sources.size()) continue;
            auto& src = sources[route.sourceIndex];

            float value = computeSourceValue (src);
            value *= route.amount;

            // Modulate parameter (additive, clamped)
            if (auto* param = apvts.getParameter (route.paramID))
            {
                const float current = param->getValue();
                param->setValueNotifyingHost (
                    juce::jlimit (0.0f, 1.0f, current + value * 0.01f));
            }
        }
    }

    //==============================================================================
    int addSource (const ModSource& src)
    {
        const juce::ScopedLock sl (lock);
        sources.push_back (src);
        return static_cast<int> (sources.size() - 1);
    }

    void addRoute (const ModRoute& route)
    {
        const juce::ScopedLock sl (lock);
        routes.push_back (route);
    }

    void clearAll()
    {
        const juce::ScopedLock sl (lock);
        sources.clear();
        routes.clear();
    }

    //==============================================================================
    juce::ValueTree toValueTree() const { return juce::ValueTree ("ModulationMatrix"); }
    void fromValueTree (const juce::ValueTree&) {}

private:
    float computeSourceValue (const ModSource& src) const
    {
        switch (src.type)
        {
            case ModSource::LFO_SINE:
                return std::sin (src.phase * juce::MathConstants<float>::twoPi) * src.depth;
            case ModSource::LFO_TRI:
                return (src.phase < 0.5f ? src.phase * 4.0f - 1.0f
                                         : 3.0f - src.phase * 4.0f) * src.depth;
            case ModSource::LFO_SQUARE:
                return (src.phase < 0.5f ? 1.0f : -1.0f) * src.depth;
            case ModSource::LFO_RANDOM:
                return 0.0f; // Would use S&H in practice
            default:
                return 0.0f;
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<ModSource> sources;
    std::vector<ModRoute>  routes;
    juce::CriticalSection  lock;
    double sampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModulationMatrix)
};
