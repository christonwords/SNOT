#pragma once
#include <JuceHeader.h>

//==============================================================================
/**
 * AudioNode — Abstract base class for all SNOT DSP modules.
 *
 * Subclasses implement:
 *   - prepare()   — allocate resources for given spec
 *   - process()   — audio callback (called on audio thread)
 *   - reset()     — clear state
 *   - getName()   — human-readable name
 *   - getType()   — serialization type string
 *
 * Parameter access is via APVTS — nodes cache raw pointers to
 * std::atomic<float> for zero-overhead per-sample reads.
 */
class AudioNode
{
public:
    virtual ~AudioNode() = default;

    //==============================================================================
    virtual void prepare (const juce::dsp::ProcessSpec& spec) = 0;
    virtual void process (juce::dsp::AudioBlock<float>& block) = 0;
    virtual void reset() = 0;

    virtual juce::String getName() const = 0;
    virtual juce::String getType() const = 0;

    //==============================================================================
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }
    void setEnabled (bool e) noexcept { enabled.store (e, std::memory_order_relaxed); }

    //==============================================================================
    /** Called by ModuleGraph::morphTo — lerp all parameters toward target. */
    virtual void morphFrom (const AudioNode& target, float t)
    {
        // Default: no-op. Override in subclasses that support morphing.
        juce::ignoreUnused (target, t);
    }

    //==============================================================================
    virtual juce::ValueTree toValueTree() const
    {
        juce::ValueTree tree ("Node");
        tree.setProperty ("type", getType(), nullptr);
        tree.setProperty ("enabled", isEnabled(), nullptr);
        return tree;
    }

    //==============================================================================
    /** UI metadata — position in node canvas */
    juce::Point<float> canvasPosition { 0.0f, 0.0f };
    juce::Colour       orb_colour     { 0xff00ffcc };

protected:
    std::atomic<bool> enabled { true };

    //==============================================================================
    /** Utility: soft clip to prevent harsh output. */
    static inline float softClip (float x) noexcept
    {
        // Pade approximation of tanh
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    /** Utility: linear crossfade. */
    static inline float lerp (float a, float b, float t) noexcept
    {
        return a + t * (b - a);
    }

    /** Utility: equal-power crossfade. */
    static inline float eqpCrossfade (float dry, float wet, float mix) noexcept
    {
        const float angle = mix * juce::MathConstants<float>::halfPi;
        return dry * std::cos (angle) + wet * std::sin (angle);
    }
};
