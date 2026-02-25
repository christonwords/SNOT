#pragma once
#include "../AudioNode.h"
#include "../../PluginProcessor.h"

//==============================================================================
/**
 * SpectralWarpChorus
 *
 * A chorus operating in the frequency domain using overlap-add STFT processing.
 * Each "voice" applies a unique combination of:
 *   1. Fractional bin shift (spectral frequency warp — not pitch shift)
 *   2. Phase randomization (adds alien inharmonicity)
 *   3. LFO-modulated magnitude scaling per frequency band
 *
 * This produces a chorus that's thicker and more dimensionally alien than
 * any time-domain approach — voices don't sound like copies, they sound
 * like parallel versions of the audio from different dimensions.
 *
 * FFT size: 2048 samples, hop: 512 (75% overlap), Hann window.
 */
class SpectralWarpChorus : public AudioNode
{
public:
    static constexpr int FFT_ORDER  = 11;  // 2048
    static constexpr int FFT_SIZE   = 1 << FFT_ORDER;
    static constexpr int HOP_SIZE   = FFT_SIZE / 4;  // 75% overlap
    static constexpr int MAX_VOICES = 8;

    explicit SpectralWarpChorus (juce::AudioProcessorValueTreeState& apvts)
        : apvts (apvts),
          fft (FFT_ORDER),
          window (FFT_SIZE, juce::dsp::WindowingFunction<float>::hann)
    {
        pDepth   = apvts.getRawParameterValue (ParamID::SWC_DEPTH);
        pRate    = apvts.getRawParameterValue (ParamID::SWC_RATE);
        pVoices  = apvts.getRawParameterValue (ParamID::SWC_VOICES);
        pWarp    = apvts.getRawParameterValue (ParamID::SWC_WARP);
        pMix     = apvts.getRawParameterValue (ParamID::SWC_MIX);
        pEnabled = apvts.getRawParameterValue (ParamID::SWC_ENABLED);

        // Seed per-voice random state
        for (int v = 0; v < MAX_VOICES; ++v)
        {
            voiceLfoPhase[v] = static_cast<float>(v) / MAX_VOICES;
            voiceDetune[v]   = (v % 2 == 0 ? 1.0f : -1.0f)
                             * (0.1f + 0.15f * static_cast<float>(v));
            random.setSeed (v * 0x9e3779b9 + 12345678);
            for (int b = 0; b < FFT_SIZE / 2; ++b)
                voicePhaseRand[v][b % 512] = random.nextFloat() * juce::MathConstants<float>::twoPi;
        }
    }

    juce::String getName() const override { return "Spectral Warp Chorus"; }
    juce::String getType() const override { return "spectral_warp_chorus"; }

    //==============================================================================
    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        sampleRate  = spec.sampleRate;
        numChannels = static_cast<int> (spec.numChannels);

        for (int ch = 0; ch < 2; ++ch)
        {
            inFifo[ch].assign (FFT_SIZE, 0.0f);
            outFifo[ch].assign (FFT_SIZE, 0.0f);
            fftData[ch].assign (FFT_SIZE * 2, 0.0f);
            outputAccum[ch].assign (FFT_SIZE + HOP_SIZE, 0.0f);
        }
        fifoIndex = 0;
        reset();
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (inFifo[ch].begin(),     inFifo[ch].end(),     0.0f);
            std::fill (outFifo[ch].begin(),    outFifo[ch].end(),    0.0f);
            std::fill (outputAccum[ch].begin(),outputAccum[ch].end(),0.0f);
        }
        fifoIndex = 0;
        for (int v = 0; v < MAX_VOICES; ++v) voiceLfoPhase[v] = 0.0f;
    }

    //==============================================================================
    void process (juce::dsp::AudioBlock<float>& block) override
    {
        if (!isEnabled() || pEnabled->load() < 0.5f) return;

        const int numSamples = static_cast<int> (block.getNumSamples());
        const float mix      = pMix->load();

        for (int s = 0; s < numSamples; ++s)
        {
            for (int ch = 0; ch < numChannels && ch < 2; ++ch)
            {
                inFifo[ch][fifoIndex] = block.getSample (ch, s);
                block.setSample (ch, s,
                    eqpCrossfade (block.getSample (ch, s),
                                  outFifo[ch][fifoIndex], mix));
            }

            ++fifoIndex;
            if (fifoIndex >= HOP_SIZE)
            {
                fifoIndex = 0;
                processSpectralFrame();
            }
        }
    }

private:
    //==============================================================================
    void processSpectralFrame()
    {
        const int   numVoices = static_cast<int> (pVoices->load());
        const float depth     = pDepth->load();
        const float warp      = pWarp->load();
        const float lfoRate   = pRate->load() / static_cast<float> (sampleRate);

        for (int ch = 0; ch < numChannels && ch < 2; ++ch)
        {
            // Copy fifo with windowing into fftData
            std::copy (inFifo[ch].begin() + HOP_SIZE, inFifo[ch].end(),   fftData[ch].begin());
            std::copy (inFifo[ch].begin(),             inFifo[ch].begin() + HOP_SIZE,
                       fftData[ch].begin() + (FFT_SIZE - HOP_SIZE));
            window.multiplyWithWindowingTable (fftData[ch].data(), FFT_SIZE);

            // Forward FFT (real → complex interleaved)
            fft.performRealOnlyForwardTransform (fftData[ch].data(), true);

            // Accumulate voices in frequency domain
            std::fill (voiceAccum.begin(), voiceAccum.end(), 0.0f);

            for (int v = 0; v < numVoices; ++v)
            {
                voiceLfoPhase[v] += lfoRate * HOP_SIZE;
                if (voiceLfoPhase[v] > 1.0f) voiceLfoPhase[v] -= 1.0f;
                const float lfo = std::sin (voiceLfoPhase[v] * juce::MathConstants<float>::twoPi);

                // Fractional bin shift (spectral warp)
                const float shift = voiceDetune[v] * depth * warp * 3.0f; // ±3 bins max
                const float mag_mod = 1.0f + lfo * depth * 0.4f;

                for (int bin = 1; bin < FFT_SIZE / 2 - 1; ++bin)
                {
                    const float re = fftData[ch][bin * 2];
                    const float im = fftData[ch][bin * 2 + 1];

                    // Phase rotation (creates alien shimmer)
                    const float phi = voicePhaseRand[v][bin % 512] * depth * 0.3f
                                    + static_cast<float>(bin) * shift * 0.01f;
                    const float cosP = std::cos (phi);
                    const float sinP = std::sin (phi);

                    voiceAccum[bin * 2]     += (re * cosP - im * sinP) * mag_mod;
                    voiceAccum[bin * 2 + 1] += (re * sinP + im * cosP) * mag_mod;
                }
            }

            // Mix original + voices
            const float voiceScale = 1.0f / (numVoices + 1);
            for (int i = 0; i < FFT_SIZE; ++i)
                fftData[ch][i] = (fftData[ch][i] + voiceAccum[i]) * voiceScale;

            // Inverse FFT
            fft.performRealOnlyInverseTransform (fftData[ch].data());
            window.multiplyWithWindowingTable (fftData[ch].data(), FFT_SIZE);

            // Overlap-add into output accumulator
            for (int i = 0; i < FFT_SIZE; ++i)
                outputAccum[ch][i] += fftData[ch][i];

            // Copy hop to outFifo and shift accumulator
            std::copy (outputAccum[ch].begin(), outputAccum[ch].begin() + HOP_SIZE,
                       outFifo[ch].begin());
            std::copy (outputAccum[ch].begin() + HOP_SIZE, outputAccum[ch].end(),
                       outputAccum[ch].begin());
            std::fill (outputAccum[ch].end() - HOP_SIZE, outputAccum[ch].end(), 0.0f);
        }
    }

    //==============================================================================
    juce::AudioProcessorValueTreeState& apvts;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    std::atomic<float>* pDepth   { nullptr };
    std::atomic<float>* pRate    { nullptr };
    std::atomic<float>* pVoices  { nullptr };
    std::atomic<float>* pWarp    { nullptr };
    std::atomic<float>* pMix     { nullptr };
    std::atomic<float>* pEnabled { nullptr };

    std::array<std::vector<float>, 2> inFifo, outFifo, fftData, outputAccum;
    std::vector<float> voiceAccum = std::vector<float> (FFT_SIZE * 2, 0.0f);

    int fifoIndex { 0 };
    double sampleRate  { 44100.0 };
    int    numChannels { 2 };

    float voiceLfoPhase[MAX_VOICES] {};
    float voiceDetune[MAX_VOICES]   {};
    float voicePhaseRand[MAX_VOICES][512] {};

    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralWarpChorus)
};
