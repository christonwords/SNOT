#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
SnotAudioProcessor::SnotAudioProcessor()
    : AudioProcessor (BusesProperties()
                     .withInput  ("Input",  AudioChannelSet::stereo(), true)
                     .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SNOT_STATE", createParameterLayout()),
      fft (10),   // 1024-point FFT
      window (1 << 10, dsp::WindowingFunction<float>::hann)
{
    fftBuffer.resize (1 << 11, 0.0f); // 2x for real FFT

    moduleGraph     = std::make_unique<ModuleGraph> (apvts);
    macroEngine     = std::make_unique<MacroEngine> (apvts);
    modMatrix       = std::make_unique<ModulationMatrix> (apvts);
    oversamplingChain = std::make_unique<OversamplingChain>();
    gainStager      = std::make_unique<GainStager>();
    midiRouter      = std::make_unique<MidiRouter> (apvts);
    presetManager   = std::make_unique<PresetManager> (*this, apvts);

    // Wire macros → modulation matrix
    macroEngine->setModulationMatrix (modMatrix.get());

    // Listen to key parameters
    apvts.addParameterListener (ParamID::OVERSAMPLE, this);
    apvts.addParameterListener (ParamID::MIX, this);
}

SnotAudioProcessor::~SnotAudioProcessor()
{
    apvts.removeParameterListener (ParamID::OVERSAMPLE, this);
    apvts.removeParameterListener (ParamID::MIX, this);
}

//==============================================================================
void SnotAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const dsp::ProcessSpec spec { sampleRate,
                                  static_cast<uint32> (samplesPerBlock),
                                  static_cast<uint32> (getTotalNumOutputChannels()) };

    oversamplingChain->prepare (spec);
    moduleGraph->prepare (sampleRate, samplesPerBlock,
                          getTotalNumOutputChannels());
    modMatrix->prepare (sampleRate, samplesPerBlock);
    gainStager->prepare (spec);

    dryBuffer.setSize (getTotalNumOutputChannels(), samplesPerBlock);
    std::fill (spectrumData.begin(), spectrumData.end(), 0.0f);
}

void SnotAudioProcessor::releaseResources()
{
    moduleGraph->reset();
    oversamplingChain->reset();
}

//==============================================================================
void SnotAudioProcessor::processBlock (AudioBuffer<float>& buffer,
                                       MidiBuffer& midiMessages)
{
    ScopedNoDenormals noDenormals;

    // Capture dry signal for wet/dry mix
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, buffer.getNumSamples());

    // MIDI routing (FX switching, macro triggers)
    midiRouter->process (midiMessages, *moduleGraph, *macroEngine);

    // Modulation tick (LFOs, envelopes, macros)
    modMatrix->process (buffer.getNumSamples());

    // Oversampling upsample
    auto oversampledBlock = oversamplingChain->processSamplesUp (
        dsp::AudioBlock<float> (buffer));

    // Process through module graph
    {
        AudioBuffer<float> osBuffer (
            oversampledBlock.getChannelPointer (0),
            static_cast<int> (oversampledBlock.getNumChannels()),
            static_cast<int> (oversampledBlock.getNumSamples()));
        // Note: AudioBlock wraps data, no copy
        moduleGraph->processGraph (oversampledBlock);
    }

    // Oversampling downsample
    oversamplingChain->processSamplesDown (dsp::AudioBlock<float> (buffer));

    // Auto gain compensation
    gainStager->process (dsp::ProcessContextReplacing<float> (
        dsp::AudioBlock<float> (buffer)));

    // Master wet/dry blend
    const float mix = apvts.getRawParameterValue (ParamID::MIX)->load();
    applyWetDryMix (buffer, dryBuffer, mix);

    // Master output gain
    const float masterGain = apvts.getRawParameterValue (ParamID::MASTER_GAIN)->load();
    buffer.applyGain (masterGain);

    // Update spectrum for visualizer
    updateSpectrum (buffer);
}

void SnotAudioProcessor::processBlockBypassed (AudioBuffer<float>& buffer,
                                                MidiBuffer&)
{
    // Pass through clean
}

//==============================================================================
void SnotAudioProcessor::applyWetDryMix (AudioBuffer<float>& wet,
                                          const AudioBuffer<float>& dry,
                                          float mix)
{
    const float wetGain = mix;
    const float dryGain = 1.0f - mix;

    for (int ch = 0; ch < wet.getNumChannels(); ++ch)
    {
        wet.applyGain (ch, 0, wet.getNumSamples(), wetGain);
        wet.addFrom (ch, 0, dry, ch, 0, dry.getNumSamples(), dryGain);
    }
}

void SnotAudioProcessor::updateSpectrum (const AudioBuffer<float>& buffer)
{
    const int fftSize = 1 << 10;
    if (buffer.getNumSamples() < fftSize) return;

    // Mix to mono for spectrum display
    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* src = buffer.getReadPointer (ch);
        for (int i = 0; i < fftSize; ++i)
            fftBuffer[i] += src[i];
    }
    FloatVectorOperations::multiply (fftBuffer.data(),
                                     1.0f / buffer.getNumChannels(), fftSize);

    window.multiplyWithWindowingTable (fftBuffer.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform (fftBuffer.data());

    // Smooth spectrum into spectrumData (log-scale bin mapping)
    for (int i = 0; i < SPECTRUM_SIZE; ++i)
    {
        const float mapped = std::pow (static_cast<float>(i) / SPECTRUM_SIZE, 2.5f);
        const int   bin    = static_cast<int> (mapped * (fftSize / 2));
        const float level  = Decibels::gainToDecibels (fftBuffer[bin] / fftSize + 1e-9f);
        const float norm   = jmap (level, -80.0f, 0.0f, 0.0f, 1.0f);
        // Smooth with previous
        spectrumData[i] = spectrumData[i] * 0.85f + jlimit (0.0f, 1.0f, norm) * 0.15f;
    }
    spectrumReady.store (true, std::memory_order_release);
}

//==============================================================================
void SnotAudioProcessor::parameterChanged (const String& paramID, float newValue)
{
    if (paramID == ParamID::OVERSAMPLE)
        updateOversamplingFromParam (newValue);
}

void SnotAudioProcessor::updateOversamplingFromParam (float value)
{
    const int factors[] = { 1, 2, 4, 8 };
    const int idx = jlimit (0, 3, static_cast<int> (value));
    oversamplingChain->setFactor (factors[idx]);
    oversampleFactor.store (factors[idx]);
    setLatencySamples (static_cast<int> (oversamplingChain->getLatencyInSamples()));
}

//==============================================================================
double SnotAudioProcessor::getTailLengthSeconds() const
{
    // Portal reverb can tail for up to 30 seconds
    return 30.0;
}

//==============================================================================
AudioProcessorEditor* SnotAudioProcessor::createEditor()
{
    return new SnotAudioProcessorEditor (*this);
}

//==============================================================================
void SnotAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // Embed graph topology and macro mappings
    auto graphXml  = moduleGraph->toValueTree();
    auto macroXml  = macroEngine->toValueTree();
    auto modXml    = modMatrix->toValueTree();
    state.appendChild (graphXml,  nullptr);
    state.appendChild (macroXml,  nullptr);
    state.appendChild (modXml,    nullptr);

    std::unique_ptr<XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SnotAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr)
    {
        auto state = ValueTree::fromXml (*xml);
        apvts.replaceState (state);

        if (auto graphTree = state.getChildWithName ("ModuleGraph"); graphTree.isValid())
            moduleGraph->fromValueTree (graphTree);
        if (auto macroTree = state.getChildWithName ("MacroEngine"); macroTree.isValid())
            macroEngine->fromValueTree (macroTree);
        if (auto modTree = state.getChildWithName ("ModulationMatrix"); modTree.isValid())
            modMatrix->fromValueTree (modTree);
    }
}

//==============================================================================
// Preset stubs
int  SnotAudioProcessor::getNumPrograms() { return presetManager->getNumPresets(); }
int  SnotAudioProcessor::getCurrentProgram() { return presetManager->getCurrentIndex(); }
void SnotAudioProcessor::setCurrentProgram (int i) { presetManager->loadPreset (i); }
const String SnotAudioProcessor::getProgramName (int i) { return presetManager->getPresetName (i); }
void SnotAudioProcessor::changeProgramName (int i, const String& n) { presetManager->renamePreset (i, n); }

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout SnotAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto addFloat = [&](const char* id, const char* name,
                        float min, float max, float def,
                        float skew = 1.0f)
    {
        NormalisableRange<float> range (min, max);
        range.skew = skew;
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID {id, 1}, name, range, def));
    };

    auto addBool = [&](const char* id, const char* name, bool def)
    {
        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID {id, 1}, name, def));
    };

    auto addChoice = [&](const char* id, const char* name,
                         StringArray choices, int def)
    {
        params.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID {id, 1}, name, choices, def));
    };

    // Master
    addFloat (ParamID::MASTER_GAIN, "Master Gain", 0.0f, 2.0f, 1.0f);
    addFloat (ParamID::MIX,         "Mix",         0.0f, 1.0f, 1.0f);
    addChoice(ParamID::OVERSAMPLE,  "Oversampling",
              {"1x", "2x", "4x", "8x"}, 1);

    // Macros
    for (int i = 0; i < 8; ++i)
    {
        const String id   = "macro_" + String (i + 1);
        const String name = "Macro " + String (i + 1);
        addFloat (id.toRawUTF8(), name.toRawUTF8(), 0.0f, 1.0f, 0.0f);
    }

    // Spectral Warp Chorus
    addFloat  (ParamID::SWC_DEPTH,  "SWC Depth",  0.0f, 1.0f,   0.5f);
    addFloat  (ParamID::SWC_RATE,   "SWC Rate",   0.01f, 10.0f, 0.5f, 0.4f);
    addFloat  (ParamID::SWC_VOICES, "SWC Voices", 1.0f, 8.0f,   4.0f);
    addFloat  (ParamID::SWC_WARP,   "SWC Warp",   0.0f, 1.0f,   0.3f);
    addFloat  (ParamID::SWC_MIX,    "SWC Mix",    0.0f, 1.0f,   0.6f);
    addBool   (ParamID::SWC_ENABLED,"SWC Enable",  true);

    // Portal Reverb
    addFloat (ParamID::PR_SIZE,    "Reverb Size",    0.0f, 1.0f, 0.7f);
    addFloat (ParamID::PR_DECAY,   "Reverb Decay",   0.1f, 60.0f, 8.0f, 0.3f);
    addFloat (ParamID::PR_DRIFT,   "Reverb Drift",   0.0f, 1.0f, 0.4f);
    addFloat (ParamID::PR_SHIMMER, "Reverb Shimmer", 0.0f, 1.0f, 0.2f);
    addFloat (ParamID::PR_DAMPING, "Reverb Damping", 0.0f, 1.0f, 0.3f);
    addFloat (ParamID::PR_MIX,     "Reverb Mix",     0.0f, 1.0f, 0.4f);
    addBool  (ParamID::PR_ENABLED, "Reverb Enable",  true);

    // Pitch Smear Delay
    addFloat (ParamID::PSD_TIME,     "Delay Time",     0.01f, 4.0f, 0.25f, 0.4f);
    addFloat (ParamID::PSD_FEEDBACK, "Delay Feedback", 0.0f,  0.99f, 0.4f);
    addFloat (ParamID::PSD_SMEAR,    "Delay Smear",    0.0f,  1.0f, 0.3f);
    addBool  (ParamID::PSD_SYNC,     "Delay Sync",     true);
    addFloat (ParamID::PSD_MIX,      "Delay Mix",      0.0f,  1.0f, 0.4f);
    addBool  (ParamID::PSD_ENABLED,  "Delay Enable",   true);

    // 808 Inflator
    addFloat (ParamID::H8_DRIVE,  "808 Drive",  0.0f, 1.0f, 0.3f);
    addFloat (ParamID::H8_PUNCH,  "808 Punch",  0.0f, 1.0f, 0.5f);
    addFloat (ParamID::H8_BLOOM,  "808 Bloom",  0.0f, 1.0f, 0.2f);
    addFloat (ParamID::H8_TUNE,   "808 Tune",   -24.0f, 24.0f, 0.0f);
    addFloat (ParamID::H8_MIX,    "808 Mix",    0.0f, 1.0f, 0.8f);
    addBool  (ParamID::H8_ENABLED,"808 Enable", false);

    // Gravity Filter
    addFloat  (ParamID::GF_FREQ,   "Filter Freq",  20.0f, 20000.0f, 2000.0f, 0.25f);
    addFloat  (ParamID::GF_RESO,   "Filter Reso",  0.0f, 1.0f, 0.3f);
    addFloat  (ParamID::GF_CURVE,  "Filter Curve", -1.0f, 1.0f, 0.0f);
    addChoice (ParamID::GF_MODE,   "Filter Mode",
               {"LP", "HP", "BP", "Notch", "Gravity"}, 4);
    addBool   (ParamID::GF_ENABLED,"Filter Enable", true);

    // Plasma Distortion
    addFloat (ParamID::PD_DRIVE,    "Plasma Drive",     0.0f, 1.0f, 0.4f);
    addFloat (ParamID::PD_CHARACTER,"Plasma Character", 0.0f, 1.0f, 0.5f);
    addFloat (ParamID::PD_BIAS,     "Plasma Bias",      -1.0f, 1.0f, 0.0f);
    addFloat (ParamID::PD_MIX,      "Plasma Mix",       0.0f, 1.0f, 0.5f);
    addBool  (ParamID::PD_ENABLED,  "Plasma Enable",    false);

    // Stereo Neural Motion
    addFloat (ParamID::SNM_WIDTH,  "SNM Width",  0.0f, 2.0f, 1.0f);
    addFloat (ParamID::SNM_MOTION, "SNM Motion", 0.0f, 1.0f, 0.3f);
    addFloat (ParamID::SNM_RATE,   "SNM Rate",   0.01f, 4.0f, 0.2f, 0.4f);
    addBool  (ParamID::SNM_ENABLED,"SNM Enable", true);

    // Texture Generator
    addFloat (ParamID::TG_DENSITY,   "Texture Density",   0.0f, 1.0f, 0.2f);
    addFloat (ParamID::TG_CHARACTER, "Texture Character", 0.0f, 1.0f, 0.5f);
    addFloat (ParamID::TG_MIX,       "Texture Mix",       0.0f, 1.0f, 0.15f);
    addBool  (ParamID::TG_ENABLED,   "Texture Enable",    false);

    // Freeze Capture
    addBool  (ParamID::FC_FREEZE,  "Freeze",       false);
    addFloat (ParamID::FC_SIZE,    "Freeze Size",  0.01f, 4.0f, 0.5f, 0.5f);
    addFloat (ParamID::FC_PITCH,   "Freeze Pitch", -24.0f, 24.0f, 0.0f);
    addFloat (ParamID::FC_MIX,     "Freeze Mix",   0.0f, 1.0f, 1.0f);
    addBool  (ParamID::FC_ENABLED, "Freeze Enable",false);

    // Mutation Engine
    addFloat (ParamID::ME_AMOUNT,   "Mutation Amount",   0.0f, 1.0f, 0.2f);
    addFloat (ParamID::ME_RATE,     "Mutation Rate",     0.01f, 8.0f, 0.5f, 0.4f);
    addFloat (ParamID::ME_CHARACTER,"Mutation Character",0.0f, 1.0f, 0.5f);
    addBool  (ParamID::ME_ENABLED,  "Mutation Enable",   false);

    return { params.begin(), params.end() };
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SnotAudioProcessor();
}
