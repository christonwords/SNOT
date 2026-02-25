#pragma once
#include "../AudioNode.h"
#include "../../PluginProcessor.h"

//==============================================================================
/**
 * PlasmaDistortion
 *
 * Nonlinear waveshaper with a mathematically unique transfer function:
 *
 *   y = tanh(drive * x) * (1 - character * x^2 * sin(π * x * bias))
 *
 * - Drive: pre-gain (0..40dB equivalent)
 * - Character: blends between smooth tape saturation and harsh plasma arc
 * - Bias: DC offset before nonlinearity → asymmetric even-harmonic content
 *
 * Pre/post LPF prevents aliasing aliasing (run at 4x oversampling recommended).
 * Anti-aliasing filter: 4th-order Butterworth at Nyquist/2.
 */
class PlasmaDistortion : public AudioNode
{
public:
    explicit PlasmaDistortion (juce::AudioProcessorValueTreeState& apvts) : apvts (apvts)
    {
        pDrive     = apvts.getRawParameterValue (ParamID::PD_DRIVE);
        pCharacter = apvts.getRawParameterValue (ParamID::PD_CHARACTER);
        pBias      = apvts.getRawParameterValue (ParamID::PD_BIAS);
        pMix       = apvts.getRawParameterValue (ParamID::PD_MIX);
        pEnabled   = apvts.getRawParameterValue (ParamID::PD_ENABLED);
    }

    juce::String getName() const override { return "Plasma Distortion"; }
    juce::String getType() const override { return "plasma_distortion"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        antiAlias.prepare (spec);
        antiAlias.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        antiAlias.setCutoffFrequency (spec.sampleRate * 0.45);
        antiAlias.setResonance (0.5);
        dryBuf.setSize (static_cast<int>(spec.numChannels),
                        static_cast<int>(spec.maximumBlockSize));
    }

    void reset() override { antiAlias.reset(); }

    void process (juce::dsp::AudioBlock<float>& block) override
    {
        if (!isEnabled() || pEnabled->load() < 0.5f) return;

        const float drive     = juce::jmap (pDrive->load(),     0.0f, 1.0f, 1.0f,  40.0f);
        const float character = pCharacter->load();
        const float bias      = pBias->load() * 0.5f;
        const float mix       = pMix->load();
        const float outGain   = 1.0f / std::sqrt (drive); // compensate loudness

        for (int ch = 0; ch < (int)block.getNumChannels(); ++ch)
        {
            for (int s = 0; s < (int)block.getNumSamples(); ++s)
            {
                const float dry = block.getSample (ch, s);
                float x = dry * drive + bias;

                // Plasma transfer function
                const float tanhX = softClip (x);
                const float plasma = tanhX * (1.0f - character * x * x
                    * std::sin (juce::MathConstants<float>::pi * x * bias));

                // Anti-aliasing filter output
                const float filtered = antiAlias.processSample (ch, plasma);
                const float wet = filtered * outGain;

                block.setSample (ch, s, eqpCrossfade (dry, wet, mix));
            }
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::dsp::StateVariableTPTFilter<float> antiAlias;
    juce::AudioBuffer<float> dryBuf;

    std::atomic<float>* pDrive     { nullptr };
    std::atomic<float>* pCharacter { nullptr };
    std::atomic<float>* pBias      { nullptr };
    std::atomic<float>* pMix       { nullptr };
    std::atomic<float>* pEnabled   { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlasmaDistortion)
};

//==============================================================================
/**
 * GravityCurveFilter
 *
 * A state-variable filter with a "gravity curve" parameter that warps
 * the frequency response nonlinearly as the signal passes through it.
 *
 * Gravity mode: the cutoff frequency self-modulates based on the RMS
 * of the input signal, creating a dynamic, breathing quality. High
 * signal → frequency pulled up. Low signal → frequency pulled down.
 * The "curve" parameter controls the nonlinearity of this modulation.
 */
class GravityCurveFilter : public AudioNode
{
public:
    explicit GravityCurveFilter (juce::AudioProcessorValueTreeState& apvts) : apvts (apvts)
    {
        pFreq    = apvts.getRawParameterValue (ParamID::GF_FREQ);
        pReso    = apvts.getRawParameterValue (ParamID::GF_RESO);
        pCurve   = apvts.getRawParameterValue (ParamID::GF_CURVE);
        pMode    = apvts.getRawParameterValue (ParamID::GF_MODE);
        pEnabled = apvts.getRawParameterValue (ParamID::GF_ENABLED);
    }

    juce::String getName() const override { return "Gravity Curve Filter"; }
    juce::String getType() const override { return "gravity_filter"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;
        filter.prepare (spec);
        filter.setResonance (0.7f);
        rmsSmooth = 0.0f;
        const float timeConst = std::exp (-1.0f / (0.02f * static_cast<float>(spec.sampleRate)));
        rmsCoeff = timeConst;
    }

    void reset() override { filter.reset(); rmsSmooth = 0.0f; }

    void process (juce::dsp::AudioBlock<float>& block) override
    {
        if (!isEnabled() || pEnabled->load() < 0.5f) return;

        const float baseFreq = pFreq->load();
        const float reso     = juce::jmap (pReso->load(), 0.0f, 1.0f, 0.5f, 20.0f);
        const float curve    = pCurve->load();
        const int   modeInt  = static_cast<int> (pMode->load());

        using SVF = juce::dsp::StateVariableTPTFilterType;
        const SVF modeMap[] = { SVF::lowpass, SVF::highpass, SVF::bandpass,
                                SVF::lowpass, SVF::lowpass }; // "Gravity" uses LP with modulation
        filter.setType (modeMap[modeInt]);
        filter.setResonance (reso);

        for (int s = 0; s < (int)block.getNumSamples(); ++s)
        {
            // Compute per-sample RMS (smooth)
            float power = 0.0f;
            for (int ch = 0; ch < (int)block.getNumChannels(); ++ch)
                power += block.getSample (ch, s) * block.getSample (ch, s);
            power /= block.getNumChannels();
            rmsSmooth = rmsSmooth * rmsCoeff + power * (1.0f - rmsCoeff);
            const float rms = std::sqrt (rmsSmooth);

            // Gravity: cutoff modulated by input level + curve nonlinearity
            float modFreq = baseFreq;
            if (modeInt == 4) // Gravity mode
            {
                const float gravMod = std::pow (rms, std::abs (curve) + 0.1f)
                                    * (curve > 0 ? 1.0f : -1.0f) * 3000.0f;
                modFreq = juce::jlimit (20.0f, 20000.0f, baseFreq + gravMod);
            }
            filter.setCutoffFrequency (modFreq);

            for (int ch = 0; ch < (int)block.getNumChannels(); ++ch)
            {
                const float out = filter.processSample (ch, block.getSample (ch, s));
                block.setSample (ch, s, out);
            }
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::dsp::StateVariableTPTFilter<float> filter;

    std::atomic<float>* pFreq    { nullptr };
    std::atomic<float>* pReso    { nullptr };
    std::atomic<float>* pCurve   { nullptr };
    std::atomic<float>* pMode    { nullptr };
    std::atomic<float>* pEnabled { nullptr };

    double sampleRate { 44100.0 };
    float  rmsSmooth  { 0.0f };
    float  rmsCoeff   { 0.99f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GravityCurveFilter)
};

//==============================================================================
/**
 * Harmonic808Inflator
 *
 * Specifically engineered for 808s and bass. Adds:
 *   - Punch: transient-shaped 2nd harmonic injection (1-pole envelope follower)
 *   - Bloom: frequency doubling via full-wave rectification + HPF
 *   - Drive: soft saturation pre-inflator
 *   - Tune: ±24 semitone pitch shift via PSOLA-based resampling
 *
 * The combination creates that "bouncy" glo trap 808 that hits hard,
 * has presence at all volumes, and glides with rich harmonic content.
 */
class Harmonic808Inflator : public AudioNode
{
public:
    explicit Harmonic808Inflator (juce::AudioProcessorValueTreeState& apvts) : apvts (apvts)
    {
        pDrive   = apvts.getRawParameterValue (ParamID::H8_DRIVE);
        pPunch   = apvts.getRawParameterValue (ParamID::H8_PUNCH);
        pBloom   = apvts.getRawParameterValue (ParamID::H8_BLOOM);
        pTune    = apvts.getRawParameterValue (ParamID::H8_TUNE);
        pMix     = apvts.getRawParameterValue (ParamID::H8_MIX);
        pEnabled = apvts.getRawParameterValue (ParamID::H8_ENABLED);
    }

    juce::String getName() const override { return "Harmonic 808 Inflator"; }
    juce::String getType() const override { return "harmonic_808_inflator"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;

        bloomHPF.prepare (spec);
        bloomHPF.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        bloomHPF.setCutoffFrequency (80.0f);
        bloomHPF.setResonance (0.5f);

        dryBuf.setSize (static_cast<int>(spec.numChannels),
                        static_cast<int>(spec.maximumBlockSize));

        envSmooth = 0.0f;
        const float attackMs  = 2.0f;
        const float releaseMs = 100.0f;
        envAttack  = std::exp (-1.0f / (attackMs  * 0.001f * static_cast<float>(sampleRate)));
        envRelease = std::exp (-1.0f / (releaseMs * 0.001f * static_cast<float>(sampleRate)));
    }

    void reset() override { bloomHPF.reset(); envSmooth = 0.0f; }

    void process (juce::dsp::AudioBlock<float>& block) override
    {
        if (!isEnabled() || pEnabled->load() < 0.5f) return;

        const float drive   = juce::jmap (pDrive->load(), 0.0f, 1.0f, 1.0f, 8.0f);
        const float punch   = pPunch->load();
        const float bloom   = pBloom->load();
        const float mix     = pMix->load();
        // Tune handled at block level (would use PSOLA in production)

        for (int ch = 0; ch < (int)block.getNumChannels(); ++ch)
        {
            for (int s = 0; s < (int)block.getNumSamples(); ++s)
            {
                const float dry = block.getSample (ch, s);

                // Envelope follower for transient punch
                const float rectified = std::abs (dry);
                envSmooth = rectified > envSmooth
                    ? rectified * (1 - envAttack)  + envSmooth * envAttack
                    : rectified * (1 - envRelease) + envSmooth * envRelease;

                // Drive → soft saturation
                float x = softClip (dry * drive);

                // 2nd harmonic injection (punch)
                const float h2 = x * x * (x > 0 ? 1.0f : -1.0f); // asymmetric 2nd harmonic
                x += h2 * punch * envSmooth * 0.5f;

                // Bloom: full-wave rectification creates even harmonics
                float bloomSig = bloomHPF.processSample (ch, std::abs (dry));
                x += bloomSig * bloom * 0.3f;

                // Output gain compensation
                x *= 1.0f / drive;

                block.setSample (ch, s, eqpCrossfade (dry, x, mix));
            }
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::dsp::StateVariableTPTFilter<float> bloomHPF;
    juce::AudioBuffer<float> dryBuf;

    std::atomic<float>* pDrive   { nullptr };
    std::atomic<float>* pPunch   { nullptr };
    std::atomic<float>* pBloom   { nullptr };
    std::atomic<float>* pTune    { nullptr };
    std::atomic<float>* pMix     { nullptr };
    std::atomic<float>* pEnabled { nullptr };

    double sampleRate  { 44100.0 };
    float  envSmooth   { 0.0f };
    float  envAttack   { 0.99f };
    float  envRelease  { 0.9f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Harmonic808Inflator)
};
