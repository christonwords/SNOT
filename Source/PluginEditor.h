#pragma once
#include "JuceHeader.h"
#include "PluginProcessor.h"

//==============================================================================
/**
 * SnotWebEditor — minimal native JUCE UI
 * 
 * Temporarily replaces the WebBrowserComponent to ensure the plugin
 * loads cleanly. The full HTML UI will be restored once load is confirmed.
 */
class SnotWebEditor : public juce::AudioProcessorEditor,
                      public juce::AudioProcessorValueTreeState::Listener,
                      public juce::Timer
{
public:
    explicit SnotWebEditor (SnotAudioProcessor& p);
    ~SnotWebEditor() override;

    void resized()    override;
    void paint (juce::Graphics&) override;
    void timerCallback() override;
    void parameterChanged (const juce::String& paramID, float newValue) override;

    static constexpr int W = 800;
    static constexpr int H = 500;

private:
    SnotAudioProcessor& proc;

    // Simple knob-style slider for master controls
    juce::Slider gainSlider  { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider mixSlider   { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label  gainLabel, mixLabel, titleLabel, statusLabel;

    juce::AudioProcessorValueTreeState::SliderAttachment gainAttach, mixAttach;

    void registerParamListeners();
    void unregisterParamListeners();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnotWebEditor)
};
