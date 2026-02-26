#pragma once
#include "JuceHeader.h"
#include "PluginProcessor.h"

//==============================================================================
/**
 * SnotWebEditor
 *
 * Full HTML UI via JUCE WebBrowserComponent with Options/ResourceProvider.
 * JUCE serves SNOT_UI.html from BinaryData — no temp files, no URL hacks.
 *
 * JS → C++:  window.SNOT_sendMessage({ type, param, value })
 *              which routes through evaluateJavascript injection
 * C++ → JS:  browser->evaluateJavascript("window.SNOT.updateParam(...)")
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

    static constexpr int W = 1200;
    static constexpr int H = 720;

private:
    SnotAudioProcessor& proc;

    // ── Native fallback (always rendered underneath) ─────────────────────────
    // Shown if WebView fails to initialise
    juce::Label titleLabel, statusLabel;

    // ── WebBrowserComponent ──────────────────────────────────────────────────
    std::unique_ptr<juce::WebBrowserComponent> browser;
    bool webViewReady { false };

    void buildBrowser();
    void pushAllParamsToUI();

    void registerParamListeners();
    void unregisterParamListeners();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnotWebEditor)
};
