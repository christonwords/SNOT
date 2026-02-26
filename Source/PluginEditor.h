#pragma once
#include "JuceHeader.h"
#include "PluginProcessor.h"

//==============================================================================
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
    juce::Label titleLabel, statusLabel;

    //==========================================================================
    // Inner browser — intercepts snot:// navigation for JS→C++ calls
    class SnotBrowser : public juce::WebBrowserComponent
    {
    public:
        explicit SnotBrowser (SnotWebEditor& o)
            : juce::WebBrowserComponent (juce::WebBrowserComponent::Options{}),
              owner (o) {}

        bool pageAboutToLoad (const juce::String& url) override
        {
            if (url.startsWith ("snot://"))
            {
                owner.handleSnotURL (url);
                return false;
            }
            return true;
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnotBrowser)
    private:
        SnotWebEditor& owner;
    };
    //==========================================================================

    std::unique_ptr<SnotBrowser> browser;
    bool      webViewReady { false };
    juce::File htmlFile;  // temp copy of embedded HTML

    void buildBrowser();
    void handleSnotURL (const juce::String& url);
    void registerParamListeners();
    void unregisterParamListeners();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnotWebEditor)
};
