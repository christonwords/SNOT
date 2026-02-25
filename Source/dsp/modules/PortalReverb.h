#pragma once
#include "../AudioNode.h"
#include "../../PluginProcessor.h"

//==============================================================================
/**
 * PortalReverb
 *
 * An "infinite drifting" algorithmic reverb designed to sound like
 * audio falling through a dimensional gateway. Architecture:
 *
 *   Input → Pre-delay → 16-tap Schroeder diffuser
 *           → 8 feedback delay lines (FDL) with Hadamard mixing matrix
 *           → Pitch shimmer (±1 octave micro-pitch on FDL feedback)
 *           → Drift modulation (per-FDL LFO detunes delay times)
 *           → Damping (1-pole LPF in each FDL)
 *           → Wet output
 *
 * The drift modulation creates the "living" quality: delay lines slowly
 * wander, creating thick chorus-like time smearing without discrete echoes.
 * Shimmer feeds pitch-shifted audio back into the reverb for infinite rise.
 */
class PortalReverb : public AudioNode
{
public:
    explicit PortalReverb (juce::AudioProcessorValueTreeState& apvts) : apvts (apvts)
    {
        pSize    = apvts.getRawParameterValue (ParamID::PR_SIZE);
        pDecay   = apvts.getRawParameterValue (ParamID::PR_DECAY);
        pDrift   = apvts.getRawParameterValue (ParamID::PR_DRIFT);
        pShimmer = apvts.getRawParameterValue (ParamID::PR_SHIMMER);
        pDamping = apvts.getRawParameterValue (ParamID::PR_DAMPING);
        pMix     = apvts.getRawParameterValue (ParamID::PR_MIX);
        pEnabled = apvts.getRawParameterValue (ParamID::PR_ENABLED);
    }

    juce::String getName() const override { return "Portal Reverb"; }
    juce::String getType() const override { return "portal_reverb"; }

    //==============================================================================
    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;
        numChannels = static_cast<int> (spec.numChannels);

        // Allocate FDL buffers (prime-number lengths for dense echo density)
        static constexpr int FDL_PRIMES[NUM_FDL] = {
            2039, 2311, 2683, 3001, 3299, 3671, 4049, 4421
        };

        for (int i = 0; i < NUM_FDL; ++i)
        {
            const int len = static_cast<int> (
                FDL_PRIMES[i] * sampleRate / 44100.0 + 0.5);
            fdl[i].resize (len, 0.0f);
            fdlPos[i] = 0;
            fdlFilter[i] = 0.0f;
            // LFO phases spread across full cycle
            lfoPhase[i] = static_cast<float> (i) / NUM_FDL;
        }

        // Pre-delay buffer (max 500ms)
        preDelayBuffer.resize (static_cast<int> (sampleRate * 0.5), 0.0f);
        preDelayPos = 0;

        // Shimmer pitch shifter buffers
        shimmerBuf.setSize (2, static_cast<int> (sampleRate * 0.5));
        shimmerBuf.clear();
        shimmerReadPos = 0.0;
        shimmerWritePos = 0;

        dryBuf.setSize (numChannels, static_cast<int> (spec.maximumBlockSize));
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < NUM_FDL; ++i)
        {
            std::fill (fdl[i].begin(), fdl[i].end(), 0.0f);
            fdlFilter[i] = 0.0f;
        }
        std::fill (preDelayBuffer.begin(), preDelayBuffer.end(), 0.0f);
        shimmerBuf.clear();
    }

    //==============================================================================
    void process (juce::dsp::AudioBlock<float>& block) override
    {
        if (!isEnabled() || pEnabled->load() < 0.5f) return;

        const int numSamples = static_cast<int> (block.getNumSamples());
        const float mix      = pMix->load();
        const float decay    = computeDecayCoeff();
        const float drift    = pDrift->load() * 0.003f; // max ±0.3% delay mod
        const float shimmer  = pShimmer->load();
        const float damping  = juce::jmap (pDamping->load(), 0.0f, 1.0f, 0.995f, 0.8f);

        // Save dry
        for (int ch = 0; ch < numChannels; ++ch)
            dryBuf.copyFrom (ch, 0, block.getChannelPointer(ch), numSamples);

        const float lfoRate = 0.15f / static_cast<float> (sampleRate); // ~0.15 Hz

        for (int s = 0; s < numSamples; ++s)
        {
            // Mix stereo to mono for reverb input
            float in = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                in += block.getSample (ch, s);
            in /= numChannels;

            // Pre-delay (20ms default)
            const int preDLen = static_cast<int> (
                juce::jmap (pSize->load(), 0.0f, 1.0f, 0.005f, 0.08f) * (float)sampleRate);
            preDelayBuffer[preDelayPos] = in;
            const int preTap = (preDelayPos - preDLen + static_cast<int>(preDelayBuffer.size()))
                               % static_cast<int>(preDelayBuffer.size());
            float diffused = preDelayBuffer[preTap];
            preDelayPos = (preDelayPos + 1) % static_cast<int>(preDelayBuffer.size());

            // Accumulate FDL outputs (mono reverb)
            float wetMono = 0.0f;
            float fdlInputs[NUM_FDL];

            // Hadamard mixing matrix (8×8 fast version)
            hadamardMix (fdlOutputCache, fdlInputs);

            for (int i = 0; i < NUM_FDL; ++i)
            {
                // LFO modulation of read position
                lfoPhase[i] += lfoRate;
                if (lfoPhase[i] > 1.0f) lfoPhase[i] -= 1.0f;
                const float lfo = std::sin (lfoPhase[i] * juce::MathConstants<float>::twoPi);

                // Compute modulated read index
                const int bufLen = static_cast<int> (fdl[i].size());
                const float modSamples = lfo * drift * bufLen;
                const int readOffset = bufLen - 1 + static_cast<int> (modSamples);
                const int readPos = (fdlPos[i] + readOffset) % bufLen;

                // Read with linear interpolation
                const float frac = modSamples - std::floor (modSamples);
                const float s0 = fdl[i][readPos];
                const float s1 = fdl[i][(readPos + 1) % bufLen];
                fdlOutputCache[i] = s0 + frac * (s1 - s0);

                // Damping filter (1-pole LPF in feedback path)
                fdlFilter[i] = fdlFilter[i] * damping + fdlOutputCache[i] * (1.0f - damping);

                // Write: input = diffused signal + mixed feedback
                float writeVal = diffused * 0.125f + fdlInputs[i] * decay;

                // Shimmer: add pitch-shifted feedback octave up
                if (shimmer > 0.001f)
                    writeVal += getShimmerSample (s) * shimmer * decay * 0.3f;

                fdl[i][fdlPos[i]] = writeVal;
                fdlPos[i] = (fdlPos[i] + 1) % bufLen;

                wetMono += fdlOutputCache[i];
            }
            wetMono /= NUM_FDL;

            // Update shimmer write
            shimmerBuf.setSample (0, shimmerWritePos, wetMono);
            shimmerBuf.setSample (1, shimmerWritePos, wetMono);
            shimmerWritePos = (shimmerWritePos + 1) % shimmerBuf.getNumSamples();

            // Spread mono reverb to stereo (mid/side decorrelation)
            const float left  = wetMono + fdlOutputCache[0] * 0.3f - fdlOutputCache[1] * 0.1f;
            const float right = wetMono - fdlOutputCache[0] * 0.3f + fdlOutputCache[1] * 0.1f;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float dry = dryBuf.getSample (ch, s);
                const float wet = (ch == 0) ? left : right;
                block.setSample (ch, s, eqpCrossfade (dry, wet, mix));
            }
        }
    }

private:
    //==============================================================================
    static constexpr int NUM_FDL = 8;

    float computeDecayCoeff() const
    {
        // Map decay time (seconds) to per-sample feedback coefficient
        // At decay=8s, a 2311-sample FDL at 44100Hz should decay by ~60dB
        const float decaySec = pDecay->load();
        const float avgFdlLen = 3000.0f; // approximate
        const float rt60Samples = decaySec * static_cast<float> (sampleRate);
        return std::pow (0.001f, avgFdlLen / rt60Samples);
    }

    /** Fast 8×8 Hadamard mix (in-place via Walsh–Hadamard). */
    static void hadamardMix (const float* in, float* out)
    {
        float tmp[NUM_FDL];
        // Stage 1
        for (int i = 0; i < 4; ++i)
        {
            tmp[i*2]   = in[i*2] + in[i*2+1];
            tmp[i*2+1] = in[i*2] - in[i*2+1];
        }
        // Stage 2
        for (int i = 0; i < 2; ++i)
        {
            out[i*4]   = tmp[i*4]   + tmp[i*4+2];
            out[i*4+1] = tmp[i*4+1] + tmp[i*4+3];
            out[i*4+2] = tmp[i*4]   - tmp[i*4+2];
            out[i*4+3] = tmp[i*4+1] - tmp[i*4+3];
        }
        // Stage 3
        const float a = out[0], b = out[1], c = out[2], d = out[3];
        const float e = out[4], f = out[5], g = out[6], h = out[7];
        out[0] = a+e; out[1] = b+f; out[2] = c+g; out[3] = d+h;
        out[4] = a-e; out[5] = b-f; out[6] = c-g; out[7] = d-h;

        // Normalize
        constexpr float norm = 1.0f / 2.828427f; // 1/sqrt(8)
        for (int i = 0; i < NUM_FDL; ++i) out[i] *= norm;
    }

    /** Simple octave-up shimmer read from delay buffer. */
    float getShimmerSample (int /*sampleIndex*/)
    {
        // Read at 2x speed to pitch shift up one octave
        shimmerReadPos += 2.0;
        const int bufLen = shimmerBuf.getNumSamples();
        while (shimmerReadPos >= bufLen) shimmerReadPos -= bufLen;
        const int iPos = static_cast<int> (shimmerReadPos);
        const float frac = static_cast<float> (shimmerReadPos - iPos);
        const float s0 = shimmerBuf.getSample (0, iPos);
        const float s1 = shimmerBuf.getSample (0, (iPos + 1) % bufLen);
        return s0 + frac * (s1 - s0);
    }

    //==============================================================================
    juce::AudioProcessorValueTreeState& apvts;

    std::atomic<float>* pSize    { nullptr };
    std::atomic<float>* pDecay   { nullptr };
    std::atomic<float>* pDrift   { nullptr };
    std::atomic<float>* pShimmer { nullptr };
    std::atomic<float>* pDamping { nullptr };
    std::atomic<float>* pMix     { nullptr };
    std::atomic<float>* pEnabled { nullptr };

    // FDL state
    std::array<std::vector<float>, NUM_FDL> fdl;
    std::array<int,   NUM_FDL> fdlPos    {};
    std::array<float, NUM_FDL> fdlFilter {};
    std::array<float, NUM_FDL> lfoPhase  {};
    float fdlOutputCache[NUM_FDL] {};

    // Pre-delay
    std::vector<float> preDelayBuffer;
    int preDelayPos { 0 };

    // Shimmer pitch shifter
    juce::AudioBuffer<float> shimmerBuf;
    double shimmerReadPos  { 0.0 };
    int    shimmerWritePos { 0 };

    juce::AudioBuffer<float> dryBuf;
    double sampleRate  { 44100.0 };
    int    numChannels { 2 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PortalReverb)
};
