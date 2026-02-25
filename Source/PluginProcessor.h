#pragma once
#include "JuceHeader.h"
#include "dsp/ModuleGraph.h"
#include "dsp/MacroEngine.h"
#include "dsp/ModulationMatrix.h"
#include "dsp/OversamplingChain.h"
#include "dsp/GainStager.h"
#include "dsp/MidiRouter.h"
#include "preset/PresetManager.h"

//==============================================================================
// ParamID namespace lives in ParamIDs.h (included via JuceHeader.h)
//==============================================================================
class SnotAudioProcessor : public juce::AudioProcessor,
                           public juce::AudioProcessorValueTreeState::Listener
{
public:
    SnotAudioProcessor();
    ~SnotAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "SNOT"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parameter change callback
    void parameterChanged (const juce::String& paramID, float newValue) override;

    //==============================================================================
    // Public accessors for editor
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    ModuleGraph& getModuleGraph() { return *moduleGraph; }
    MacroEngine& getMacroEngine() { return *macroEngine; }
    ModulationMatrix& getModulationMatrix() { return *modMatrix; }
    PresetManager& getPresetManager() { return *presetManager; }

    // Visualizer data (lock-free)
    const float* getSpectrumData() const { return spectrumData.data(); }
    static constexpr int SPECTRUM_SIZE = 512;

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    std::unique_ptr<ModuleGraph>       moduleGraph;
    std::unique_ptr<MacroEngine>       macroEngine;
    std::unique_ptr<ModulationMatrix>  modMatrix;
    std::unique_ptr<OversamplingChain> oversamplingChain;
    std::unique_ptr<GainStager>        gainStager;
    std::unique_ptr<MidiRouter>        midiRouter;
    std::unique_ptr<PresetManager>     presetManager;

    // Spectrum analyzer (FFT)
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> fftBuffer;
    std::array<float, SPECTRUM_SIZE> spectrumData{};
    std::atomic<bool> spectrumReady { false };

    // Wet/dry mix buffer
    juce::AudioBuffer<float> dryBuffer;

    // Current oversampling factor
    std::atomic<int> oversampleFactor { 1 };

    void updateOversamplingFromParam (float value);
    void updateSpectrum (const juce::AudioBuffer<float>& buffer);
    void applyWetDryMix (juce::AudioBuffer<float>& wet,
                         const juce::AudioBuffer<float>& dry, float mix);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnotAudioProcessor)
};
