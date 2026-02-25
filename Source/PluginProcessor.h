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
// Parameter IDs — single source of truth
//==============================================================================
namespace ParamID
{
    // Master
    inline constexpr auto MASTER_GAIN   = "master_gain";
    inline constexpr auto MIX           = "master_mix";
    inline constexpr auto OVERSAMPLE    = "oversample_mode"; // 0=1x,1=2x,2=4x,3=8x

    // Macros
    inline constexpr auto MACRO_1 = "macro_1";
    inline constexpr auto MACRO_2 = "macro_2";
    inline constexpr auto MACRO_3 = "macro_3";
    inline constexpr auto MACRO_4 = "macro_4";
    inline constexpr auto MACRO_5 = "macro_5";
    inline constexpr auto MACRO_6 = "macro_6";
    inline constexpr auto MACRO_7 = "macro_7";
    inline constexpr auto MACRO_8 = "macro_8";

    // SpectralWarpChorus
    inline constexpr auto SWC_DEPTH    = "swc_depth";
    inline constexpr auto SWC_RATE     = "swc_rate";
    inline constexpr auto SWC_VOICES   = "swc_voices";
    inline constexpr auto SWC_WARP     = "swc_warp";
    inline constexpr auto SWC_MIX      = "swc_mix";
    inline constexpr auto SWC_ENABLED  = "swc_enabled";

    // PortalReverb
    inline constexpr auto PR_SIZE      = "pr_size";
    inline constexpr auto PR_DECAY     = "pr_decay";
    inline constexpr auto PR_DRIFT     = "pr_drift";
    inline constexpr auto PR_SHIMMER   = "pr_shimmer";
    inline constexpr auto PR_DAMPING   = "pr_damping";
    inline constexpr auto PR_MIX       = "pr_mix";
    inline constexpr auto PR_ENABLED   = "pr_enabled";

    // PitchSmearDelay
    inline constexpr auto PSD_TIME     = "psd_time";
    inline constexpr auto PSD_FEEDBACK = "psd_feedback";
    inline constexpr auto PSD_SMEAR    = "psd_smear";
    inline constexpr auto PSD_SYNC     = "psd_sync";
    inline constexpr auto PSD_MIX      = "psd_mix";
    inline constexpr auto PSD_ENABLED  = "psd_enabled";

    // Harmonic808Inflator
    inline constexpr auto H8_DRIVE     = "h8_drive";
    inline constexpr auto H8_PUNCH     = "h8_punch";
    inline constexpr auto H8_BLOOM     = "h8_bloom";
    inline constexpr auto H8_TUNE      = "h8_tune";
    inline constexpr auto H8_MIX       = "h8_mix";
    inline constexpr auto H8_ENABLED   = "h8_enabled";

    // GravityFilter
    inline constexpr auto GF_FREQ      = "gf_freq";
    inline constexpr auto GF_RESO      = "gf_reso";
    inline constexpr auto GF_CURVE     = "gf_curve";
    inline constexpr auto GF_MODE      = "gf_mode";
    inline constexpr auto GF_ENABLED   = "gf_enabled";

    // PlasmaDistortion
    inline constexpr auto PD_DRIVE     = "pd_drive";
    inline constexpr auto PD_CHARACTER = "pd_character";
    inline constexpr auto PD_BIAS      = "pd_bias";
    inline constexpr auto PD_MIX       = "pd_mix";
    inline constexpr auto PD_ENABLED   = "pd_enabled";

    // StereoNeuralMotion
    inline constexpr auto SNM_WIDTH    = "snm_width";
    inline constexpr auto SNM_MOTION   = "snm_motion";
    inline constexpr auto SNM_RATE     = "snm_rate";
    inline constexpr auto SNM_ENABLED  = "snm_enabled";

    // TextureGenerator
    inline constexpr auto TG_DENSITY   = "tg_density";
    inline constexpr auto TG_CHARACTER = "tg_character";
    inline constexpr auto TG_MIX       = "tg_mix";
    inline constexpr auto TG_ENABLED   = "tg_enabled";

    // FreezeCapture
    inline constexpr auto FC_FREEZE    = "fc_freeze";
    inline constexpr auto FC_SIZE      = "fc_size";
    inline constexpr auto FC_PITCH     = "fc_pitch";
    inline constexpr auto FC_MIX       = "fc_mix";
    inline constexpr auto FC_ENABLED   = "fc_enabled";

    // MutationEngine
    inline constexpr auto ME_AMOUNT    = "me_amount";
    inline constexpr auto ME_RATE      = "me_rate";
    inline constexpr auto ME_CHARACTER = "me_character";
    inline constexpr auto ME_ENABLED   = "me_enabled";
}

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
