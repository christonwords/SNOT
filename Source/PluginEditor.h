#pragma once
#include "JuceHeader.h"
#include "PluginProcessor.h"

//==============================================================================
/**
 * SnotWebEditor
 *
 * The actual plugin window inside your DAW.
 * It contains one component: a JUCE WebBrowserComponent that displays
 * SNOT_UI.html — the full animated portal interface.
 *
 * ┌─────────────────────────────────────────────────┐
 * │  DAW plugin window                              │
 * │  ┌───────────────────────────────────────────┐  │
 * │  │  WebBrowserComponent (Chromium / WKWebView)│  │
 * │  │                                           │  │
 * │  │       SNOT_UI.html renders here           │  │
 * │  │  Portal rings, orbs, knobs, spectrum...   │  │
 * │  │                                           │  │
 * │  └───────────────────────────────────────────┘  │
 * └─────────────────────────────────────────────────┘
 *
 * JS → C++ :   iframe navigates to  snot://setparam/param_id/0.750000
 * C++ → JS :   browser->evaluateJavascript("window.SNOT.updateParam(...)")
 */
class SnotWebEditor : public juce::AudioProcessorEditor,
                       public juce::AudioProcessorValueTreeState::Listener,
                       public juce::Timer
{
public:
    explicit SnotWebEditor (SnotAudioProcessor& p);
    ~SnotWebEditor() override;

    void resized()       override;
    void paint (juce::Graphics&) override;
    void timerCallback() override;
    void parameterChanged (const juce::String& paramID, float newValue) override;

    static constexpr int W = 1200;
    static constexpr int H = 720;

private:
    SnotAudioProcessor& proc;

    // ── Inner browser class ──────────────────────────────────────────────────
    class SnotBrowser : public juce::WebBrowserComponent
    {
    public:
        explicit SnotBrowser (SnotWebEditor& owner)
            : juce::WebBrowserComponent (false), owner (owner) {}

        // Intercept snot:// scheme before the browser tries to navigate there
        bool pageAboutToLoad (const juce::String& url) override
        {
            if (url.startsWith ("snot://"))
            {
                handleSnotURL (url);
                return false; // block navigation — we handled it
            }
            return true; // allow everything else (fonts, etc.)
        }

        void callJS (const juce::String& script)
        {
            // MessageManager ensures this runs on the message thread
            juce::MessageManager::callAsync ([this, script] {
                evaluateJavascript (script);
            });
        }

    private:
        SnotWebEditor& owner;

        void handleSnotURL (const juce::String& url)
        {
            // snot://setparam/param_id/0.750000
            if (url.startsWith ("snot://setparam/"))
            {
                const juce::String path = url.fromFirstOccurrenceOf ("snot://setparam/", false, false);
                const juce::String paramID = path.upToFirstOccurrenceOf ("/", false, false);
                const float  normVal  = path.fromFirstOccurrenceOf ("/", false, false).getFloatValue();

                if (auto* param = owner.proc.getAPVTS().getParameter (paramID))
                    param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normVal));
                return;
            }

            // snot://preset/prev  or  snot://preset/next
            if (url.startsWith ("snot://preset/"))
            {
                const juce::String dir = url.fromFirstOccurrenceOf ("snot://preset/", false, false);
                auto& pm = owner.proc.getPresetManager();
                if (dir == "prev") pm.loadPrevPreset();
                else               pm.loadNextPreset();

                const juce::String name = pm.getPresetName (pm.getCurrentIndex());
                callJS ("window.SNOT.updatePreset('" + name + "')");
                return;
            }

            // snot://module/key/1  or  snot://module/key/0
            if (url.startsWith ("snot://module/"))
            {
                const juce::String path    = url.fromFirstOccurrenceOf ("snot://module/", false, false);
                const juce::String key     = path.upToFirstOccurrenceOf ("/", false, false);
                const bool         enabled = path.fromFirstOccurrenceOf ("/", false, false).getIntValue() != 0;

                // Map module key to enabled param ID
                const juce::String paramID = key + "_enabled";
                if (auto* param = owner.proc.getAPVTS().getParameter (paramID))
                    param->setValueNotifyingHost (enabled ? 1.0f : 0.0f);
                return;
            }
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnotBrowser)
    };

    // ────────────────────────────────────────────────────────────────────────
    std::unique_ptr<SnotBrowser> browser;
    juce::File                   htmlFile;  // temp copy of embedded HTML

    std::array<float, SnotAudioProcessor::SPECTRUM_SIZE> lastSpectrum {};
    int specFrameSkip { 0 };

    void   writeHTMLToTemp();
    void   registerParamListeners();
    void   unregisterParamListeners();
    juce::String buildSpectrumJS();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnotWebEditor)
};
