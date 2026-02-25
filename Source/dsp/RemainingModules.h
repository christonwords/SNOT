#pragma once
#include "MacroEngine.h"

// Forward declaration — full definition is in ModuleGraph.h which includes us
class ModuleGraph;

// ─────────────────────────────────────────────────────────────────────────────
// PitchSmearDelay.h
// Delay with per-tap pitch smearing via modulated read pointer
// ─────────────────────────────────────────────────────────────────────────────
#include "../AudioNode.h"
#include "../../PluginProcessor.h"

class PitchSmearDelay : public AudioNode
{
public:
    static constexpr int MAX_DELAY_SAMPLES = 192000; // 4s at 48kHz

    explicit PitchSmearDelay (juce::AudioProcessorValueTreeState& apvts) : apvts (apvts)
    {
        pTime     = apvts.getRawParameterValue (ParamID::PSD_TIME);
        pFeedback = apvts.getRawParameterValue (ParamID::PSD_FEEDBACK);
        pSmear    = apvts.getRawParameterValue (ParamID::PSD_SMEAR);
        pMix      = apvts.getRawParameterValue (ParamID::PSD_MIX);
        pEnabled  = apvts.getRawParameterValue (ParamID::PSD_ENABLED);
    }

    juce::String getName() const override { return "Pitch Smear Delay"; }
    juce::String getType() const override { return "pitch_smear_delay"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;
        numCh = static_cast<int>(spec.numChannels);
        for (int ch = 0; ch < 2; ++ch)
        {
            delayBuf[ch].assign (MAX_DELAY_SAMPLES, 0.0f);
            writePos[ch] = 0;
            smearPhase[ch] = 0.0f;
        }
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch) std::fill(delayBuf[ch].begin(), delayBuf[ch].end(), 0.0f);
    }

    void process (juce::dsp::AudioBlock<float>& block) override
    {
        if (!isEnabled() || pEnabled->load() < 0.5f) return;

        const float delaySec  = pTime->load();
        const float feedback  = pFeedback->load();
        const float smear     = pSmear->load() * 0.02f; // max ±2% modulation
        const float mix       = pMix->load();
        const int   delayLen  = juce::jlimit(1, MAX_DELAY_SAMPLES-1,
                                 static_cast<int>(delaySec * sampleRate));

        for (int ch = 0; ch < numCh && ch < 2; ++ch)
        {
            for (int s = 0; s < (int)block.getNumSamples(); ++s)
            {
                // Smear: LFO-modulated read pointer creates pitch wobble
                smearPhase[ch] += 0.0003f;
                if (smearPhase[ch] > 1.0f) smearPhase[ch] -= 1.0f;
                const float mod = std::sin(smearPhase[ch] * juce::MathConstants<float>::twoPi);
                const float modOffset = mod * smear * delayLen;

                const float readPosF = writePos[ch] - delayLen + modOffset + MAX_DELAY_SAMPLES;
                const int   readI    = static_cast<int>(readPosF) % MAX_DELAY_SAMPLES;
                const float frac     = readPosF - std::floor(readPosF);
                const float s0 = delayBuf[ch][readI];
                const float s1 = delayBuf[ch][(readI+1) % MAX_DELAY_SAMPLES];
                const float delayed = s0 + frac * (s1 - s0);

                const float input = block.getSample(ch, s);
                delayBuf[ch][writePos[ch]] = softClip(input + delayed * feedback);
                writePos[ch] = (writePos[ch] + 1) % MAX_DELAY_SAMPLES;

                block.setSample(ch, s, eqpCrossfade(input, delayed, mix));
            }
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::array<std::vector<float>, 2> delayBuf;
    std::array<int,   2> writePos   {};
    std::array<float, 2> smearPhase {};
    double sampleRate { 44100.0 };
    int    numCh { 2 };

    std::atomic<float>* pTime, *pFeedback, *pSmear, *pMix, *pEnabled;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchSmearDelay)
};

// ─────────────────────────────────────────────────────────────────────────────
// StereoNeuralMotion.h
// Mid/Side width + smooth automated panning motion (sine lfo per channel)
// ─────────────────────────────────────────────────────────────────────────────
class StereoNeuralMotion : public AudioNode
{
public:
    explicit StereoNeuralMotion (juce::AudioProcessorValueTreeState& apvts) : apvts(apvts)
    {
        pWidth   = apvts.getRawParameterValue(ParamID::SNM_WIDTH);
        pMotion  = apvts.getRawParameterValue(ParamID::SNM_MOTION);
        pRate    = apvts.getRawParameterValue(ParamID::SNM_RATE);
        pEnabled = apvts.getRawParameterValue(ParamID::SNM_ENABLED);
    }

    juce::String getName() const override { return "Stereo Neural Motion"; }
    juce::String getType() const override { return "stereo_neural_motion"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;
        phase = 0.0f;
    }

    void reset() override { phase = 0.0f; }

    void process (juce::dsp::AudioBlock<float>& block) override
    {
        if (!isEnabled() || pEnabled->load() < 0.5f) return;

        const float width  = pWidth->load();   // 0..2 (1 = unity)
        const float motion = pMotion->load();
        const float rate   = pRate->load();
        const float dt     = static_cast<float>(1.0 / sampleRate);

        for (int s = 0; s < (int)block.getNumSamples(); ++s)
        {
            phase += rate * dt;
            if (phase > 1.0f) phase -= 1.0f;
            const float lfo = std::sin(phase * juce::MathConstants<float>::twoPi);

            const float L = block.getSample(0, s);
            const float R = block.getNumChannels() > 1 ? block.getSample(1, s) : L;

            // MS processing
            const float mid  = (L + R) * 0.5f;
            const float side = (L - R) * 0.5f * width;

            // Motion: add lfo-driven pan oscillation to mid
            const float panGain = 1.0f + lfo * motion * 0.3f;

            block.setSample(0, s, mid * panGain + side);
            if (block.getNumChannels() > 1)
                block.setSample(1, s, mid / panGain - side);
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    float phase { 0.0f };
    double sampleRate { 44100.0 };
    std::atomic<float>* pWidth, *pMotion, *pRate, *pEnabled;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoNeuralMotion)
};

// ─────────────────────────────────────────────────────────────────────────────
// TextureGenerator.h
// Bandlimited noise generator blended with signal for "cosmic static" texture
// ─────────────────────────────────────────────────────────────────────────────
class TextureGenerator : public AudioNode
{
public:
    explicit TextureGenerator (juce::AudioProcessorValueTreeState& apvts) : apvts(apvts)
    {
        pDensity   = apvts.getRawParameterValue(ParamID::TG_DENSITY);
        pCharacter = apvts.getRawParameterValue(ParamID::TG_CHARACTER);
        pMix       = apvts.getRawParameterValue(ParamID::TG_MIX);
        pEnabled   = apvts.getRawParameterValue(ParamID::TG_ENABLED);
    }

    juce::String getName() const override { return "Texture Generator"; }
    juce::String getType() const override { return "texture_generator"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        textureFilter.prepare(spec);
        textureFilter.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
        textureFilter.setCutoffFrequency(800.0f);
        textureFilter.setResonance(2.0f);
    }

    void reset() override { textureFilter.reset(); }

    void process (juce::dsp::AudioBlock<float>& block) override
    {
        if (!isEnabled() || pEnabled->load() < 0.5f) return;

        const float density   = pDensity->load();
        const float character = pCharacter->load();
        const float mix       = pMix->load() * 0.3f; // max 30% texture

        // Update filter based on character (brightness of texture)
        const float cutoff = juce::jmap(character, 200.0f, 8000.0f);
        textureFilter.setCutoffFrequency(cutoff);

        for (int s = 0; s < (int)block.getNumSamples(); ++s)
        {
            for (int ch = 0; ch < (int)block.getNumChannels(); ++ch)
            {
                // Sparse noise (density controls hit probability)
                float noise = 0.0f;
                if (random.nextFloat() < density * 0.1f)
                    noise = (random.nextFloat() * 2.0f - 1.0f);

                const float filtered = textureFilter.processSample(ch, noise);
                const float sig = block.getSample(ch, s);
                block.setSample(ch, s, sig + filtered * mix);
            }
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::dsp::StateVariableTPTFilter<float> textureFilter;
    juce::Random random;
    std::atomic<float>* pDensity, *pCharacter, *pMix, *pEnabled;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TextureGenerator)
};

// ─────────────────────────────────────────────────────────────────────────────
// FreezeCapture.h
// Circular buffer capture + looping playback with pitch shift
// ─────────────────────────────────────────────────────────────────────────────
class FreezeCapture : public AudioNode
{
public:
    static constexpr int CAPTURE_SIZE = 192000; // 4s at 48kHz

    explicit FreezeCapture (juce::AudioProcessorValueTreeState& apvts) : apvts(apvts)
    {
        pFreeze  = apvts.getRawParameterValue(ParamID::FC_FREEZE);
        pSize    = apvts.getRawParameterValue(ParamID::FC_SIZE);
        pPitch   = apvts.getRawParameterValue(ParamID::FC_PITCH);
        pMix     = apvts.getRawParameterValue(ParamID::FC_MIX);
        pEnabled = apvts.getRawParameterValue(ParamID::FC_ENABLED);
    }

    juce::String getName() const override { return "Freeze Capture"; }
    juce::String getType() const override { return "freeze_capture"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;
        for (int ch = 0; ch < 2; ++ch)
            captureBuf[ch].assign(CAPTURE_SIZE, 0.0f);
        writePos = 0;
        readPos  = 0.0;
    }

    void reset() override { writePos = 0; readPos = 0.0; }

    void process (juce::dsp::AudioBlock<float>& block) override
    {
        if (!isEnabled() || pEnabled->load() < 0.5f) return;

        const bool frozen  = pFreeze->load() > 0.5f;
        const float sizeSec = pSize->load();
        const float pitch   = pPitch->load(); // semitones
        const float mix     = pMix->load();
        const int   captureLen = juce::jlimit(1, CAPTURE_SIZE-1,
                                  static_cast<int>(sizeSec * sampleRate));

        // Pitch ratio from semitones
        const float ratio = std::pow(2.0f, pitch / 12.0f);

        for (int s = 0; s < (int)block.getNumSamples(); ++s)
        {
            if (!frozen)
            {
                // Capture mode: write input to buffer
                for (int ch = 0; ch < 2 && ch < (int)block.getNumChannels(); ++ch)
                    captureBuf[ch][writePos % CAPTURE_SIZE] = block.getSample(ch, s);
                writePos = (writePos + 1) % CAPTURE_SIZE;
            }
            else
            {
                // Playback mode: read from captured buffer with pitch
                readPos += ratio;
                if (readPos >= captureLen) readPos -= captureLen;
                const int   ri   = static_cast<int>(readPos);
                const float frac = static_cast<float>(readPos - ri);

                for (int ch = 0; ch < 2 && ch < (int)block.getNumChannels(); ++ch)
                {
                    const float s0 = captureBuf[ch][ri % captureLen];
                    const float s1 = captureBuf[ch][(ri + 1) % captureLen];
                    const float frozen_sample = s0 + frac * (s1 - s0);
                    const float dry = block.getSample(ch, s);
                    block.setSample(ch, s, eqpCrossfade(dry, frozen_sample, mix));
                }
            }
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::array<std::vector<float>, 2> captureBuf;
    int    writePos { 0 };
    double readPos  { 0.0 };
    double sampleRate { 44100.0 };
    std::atomic<float>* pFreeze, *pSize, *pPitch, *pMix, *pEnabled;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreezeCapture)
};

// ─────────────────────────────────────────────────────────────────────────────
// MutationEngine.h
// Randomly modulates active parameters within musical bounds over time
// ─────────────────────────────────────────────────────────────────────────────
class MutationEngine : public AudioNode
{
public:
    explicit MutationEngine (juce::AudioProcessorValueTreeState& apvts) : apvts(apvts)
    {
        pAmount    = apvts.getRawParameterValue(ParamID::ME_AMOUNT);
        pRate      = apvts.getRawParameterValue(ParamID::ME_RATE);
        pCharacter = apvts.getRawParameterValue(ParamID::ME_CHARACTER);
        pEnabled   = apvts.getRawParameterValue(ParamID::ME_ENABLED);
    }

    juce::String getName() const override { return "Mutation Engine"; }
    juce::String getType() const override { return "mutation_engine"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;
        samplesUntilMutation = static_cast<int>(sampleRate / 2);
    }

    void reset() override { samplesUntilMutation = 1000; }

    /** Mutation happens on audio thread — only modulates safe parameters. */
    void process (juce::dsp::AudioBlock<float>& block) override
    {
        if (!isEnabled() || pEnabled->load() < 0.5f) return;

        samplesUntilMutation -= static_cast<int>(block.getNumSamples());
        if (samplesUntilMutation > 0) return;

        const float rate      = pRate->load();
        const float amount    = pAmount->load();
        samplesUntilMutation  = static_cast<int>(sampleRate / rate);

        // Mutate a selection of "safe" parameters
        const std::initializer_list<const char*> mutateTargets = {
            ParamID::PR_DRIFT, ParamID::PR_SHIMMER,
            ParamID::SWC_DEPTH, ParamID::SWC_WARP,
            ParamID::PSD_SMEAR, ParamID::SNM_MOTION,
            ParamID::GF_CURVE
        };

        for (auto* paramId : mutateTargets)
        {
            if (random.nextFloat() > 0.4f) continue; // not every param each time
            if (auto* param = apvts.getParameter(paramId))
            {
                const float current = param->getValue();
                const float delta   = (random.nextFloat() * 2.0f - 1.0f) * amount * 0.15f;
                param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, current + delta));
            }
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    double sampleRate { 44100.0 };
    int    samplesUntilMutation { 22050 };
    juce::Random random;
    std::atomic<float>* pAmount, *pRate, *pCharacter, *pEnabled;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MutationEngine)
};

// ─────────────────────────────────────────────────────────────────────────────
// OversamplingChain.h  (wraps JUCE dsp::Oversampling)
// ─────────────────────────────────────────────────────────────────────────────
#include <JuceHeader.h>

class OversamplingChain
{
public:
    OversamplingChain() { buildChain(2); }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        baseSpec = spec;
        chain->initProcessing(spec.maximumBlockSize);
    }

    juce::dsp::AudioBlock<float> processSamplesUp (juce::dsp::AudioBlock<float> input)
    {
        return chain->processSamplesUp(input);
    }

    void processSamplesDown (juce::dsp::AudioBlock<float> output)
    {
        chain->processSamplesDown(output);
    }

    void setFactor (int factor)
    {
        if (factor == currentFactor) return;
        currentFactor = factor;
        const int order = factor == 1 ? 0 : factor == 2 ? 1 : factor == 4 ? 2 : 3;
        buildChain(order);
        if (baseSpec.sampleRate > 0)
            chain->initProcessing(baseSpec.maximumBlockSize);
    }

    float getLatencyInSamples() const { return chain->getLatencyInSamples(); }
    void  reset() { chain->reset(); }

private:
    void buildChain (int order)
    {
        chain = std::make_unique<juce::dsp::Oversampling<float>>(
            2, order,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> chain;
    juce::dsp::ProcessSpec baseSpec {};
    int currentFactor { 2 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OversamplingChain)
};

// ─────────────────────────────────────────────────────────────────────────────
// GainStager.h  — auto gain compensation using RMS measurement
// ─────────────────────────────────────────────────────────────────────────────
class GainStager
{
public:
    GainStager() = default;
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        gain.prepare(spec);
        gain.setGainLinear(1.0f);
        gain.setRampDurationSeconds(0.05);
        rmsSmooth = 0.0f;
        const float tc = std::exp(-1.0f / (0.3f * static_cast<float>(spec.sampleRate)));
        rmsCoeff = tc;
    }

    void process (juce::dsp::ProcessContextReplacing<float> ctx)
    {
        // Measure RMS
        const auto& block = ctx.getInputBlock();
        float rms = 0.0f;
        for (int ch = 0; ch < (int)block.getNumChannels(); ++ch)
        {
            for (int s = 0; s < (int)block.getNumSamples(); ++s)
            {
                const float x = block.getSample(ch, s);
                rms += x * x;
            }
        }
        rms = std::sqrt(rms / (block.getNumChannels() * block.getNumSamples()));
        rmsSmooth = rmsSmooth * rmsCoeff + rms * (1.0f - rmsCoeff);

        // Target RMS: -18dBFS = 0.126
        constexpr float TARGET_RMS = 0.126f;
        if (rmsSmooth > 1e-6f)
        {
            const float correction = TARGET_RMS / rmsSmooth;
            gain.setGainLinear(juce::jlimit(0.1f, 4.0f, correction));
        }
        gain.process(ctx);
    }

    void reset() { gain.reset(); }

private:
    juce::dsp::Gain<float> gain;
    float rmsSmooth { 0.0f };
    float rmsCoeff  { 0.99f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainStager)
};

// ─────────────────────────────────────────────────────────────────────────────
// MidiRouter.h  — MIDI CC → macro, note-on → FX switch
// ─────────────────────────────────────────────────────────────────────────────
#include "ModuleGraph.h"
#include "MacroEngine.h"

class MidiRouter
{
public:
    explicit MidiRouter (juce::AudioProcessorValueTreeState& apvts) : apvts(apvts) {}

    void process (juce::MidiBuffer& midi, MacroEngine& macros)
    {
        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();

            // CC 1-8 → Macros 1-8
            if (msg.isController())
            {
                const int cc  = msg.getControllerNumber();
                const int val = msg.getControllerValue();
                if (cc >= 1 && cc <= 8)
                {
                    const int macroIdx = cc - 1;
                    const float norm   = val / 127.0f;
                    if (auto* p = apvts.getParameter("macro_" + juce::String(macroIdx + 1)))
                        p->setValueNotifyingHost(norm);
                }
            }

            // Note C1 (36) → Freeze toggle
            if (msg.isNoteOn() && msg.getNoteNumber() == 36)
            {
                if (auto* p = apvts.getParameter(ParamID::FC_FREEZE))
                    p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f);
            }
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRouter)
};
