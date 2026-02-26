#include "PluginEditor.h"
#include <BinaryData.h>

using namespace juce;

//==============================================================================
SnotWebEditor::SnotWebEditor (SnotAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    // Fallback background label (visible if WebView doesn't load)
    titleLabel.setText ("SNOT", dontSendNotification);
    titleLabel.setFont (Font (48.0f, Font::bold));
    titleLabel.setColour (Label::textColourId, Colour (0xff00ffcc));
    titleLabel.setJustificationType (Justification::centred);
    addAndMakeVisible (titleLabel);

    statusLabel.setText ("Loading portal UI...", dontSendNotification);
    statusLabel.setFont (Font (14.0f));
    statusLabel.setColour (Label::textColourId, Colour (0xff888888));
    statusLabel.setJustificationType (Justification::centred);
    addAndMakeVisible (statusLabel);

    setSize (W, H);

    // Build the WebBrowserComponent after the window is sized
    buildBrowser();

    registerParamListeners();
    startTimerHz (24);
}

SnotWebEditor::~SnotWebEditor()
{
    stopTimer();
    unregisterParamListeners();

    if (browser != nullptr)
    {
        removeChildComponent (browser.get());
        browser.reset();
    }
}

//==============================================================================
void SnotWebEditor::buildBrowser()
{
    // JUCE 7/8: Options-based constructor with native resource serving.
    // We handle the "snot://" origin by serving our HTML at that origin.
    auto options = WebBrowserComponent::Options{}
        .withResourceProvider (
            [this] (const String& url) -> std::optional<WebBrowserComponent::Resource>
            {
                // Serve SNOT_UI.html for any request to our origin
                if (url == "/" || url.isEmpty() || url.contains ("SNOT_UI"))
                {
                    return WebBrowserComponent::Resource {
                        std::vector<std::byte> (
                            reinterpret_cast<const std::byte*> (BinaryData::SNOT_UI_html),
                            reinterpret_cast<const std::byte*> (BinaryData::SNOT_UI_html)
                                + BinaryData::SNOT_UI_htmlSize),
                        "text/html"
                    };
                }
                return std::nullopt; // let everything else pass through
            },
            URL ("https://snot.plugin")  // synthetic origin
        )
        .withNativeIntegrationEnabled();

    browser = std::make_unique<WebBrowserComponent> (options);

    browser->addToDesktop (0);  // needed before navigating on some platforms
    addAndMakeVisible (browser.get());

    // Navigate to our synthetic origin — triggers the resource provider
    browser->goToURL ("https://snot.plugin/");

    webViewReady = true;
    statusLabel.setVisible (false);
    titleLabel.setVisible (false);

    resized();
}

//==============================================================================
void SnotWebEditor::paint (Graphics& g)
{
    g.fillAll (Colour (0xff0a0a0f));

    if (!webViewReady)
    {
        // Draw grid lines while loading
        g.setColour (Colour (0xff1a1a2e));
        for (int x = 0; x < getWidth(); x += 40)
            g.drawVerticalLine (x, 0.0f, (float) getHeight());
        for (int y = 0; y < getHeight(); y += 40)
            g.drawHorizontalLine (y, 0.0f, (float) getWidth());

        g.setColour (Colour (0xff00ffcc).withAlpha (0.4f));
        g.fillRect (0, getHeight() - 2, getWidth(), 2);
    }
}

void SnotWebEditor::resized()
{
    auto b = getLocalBounds();

    if (browser != nullptr)
        browser->setBounds (b);

    auto centre = b.reduced (80, 100);
    titleLabel.setBounds  (centre.removeFromTop (70));
    statusLabel.setBounds (centre.removeFromTop (30));
}

//==============================================================================
void SnotWebEditor::timerCallback()
{
    if (!webViewReady || browser == nullptr)
        return;

    // Push spectrum data to UI every ~24fps
    const float* specData = proc.getSpectrumData();
    String js = "if(window.SNOT&&window.SNOT.updateSpectrum){window.SNOT.updateSpectrum([";
    for (int i = 0; i < SnotAudioProcessor::SPECTRUM_SIZE; ++i)
    {
        if (i > 0) js += ",";
        js += String (specData[i], 3);
    }
    js += "]);}";
    browser->evaluateJavascript (js);
}

void SnotWebEditor::parameterChanged (const String& paramID, float newValue)
{
    if (!webViewReady || browser == nullptr)
        return;

    // Push param change to JS UI
    MessageManager::callAsync ([this, paramID, newValue]
    {
        if (browser != nullptr)
        {
            browser->evaluateJavascript (
                "if(window.SNOT&&window.SNOT.updateParam){"
                "window.SNOT.updateParam('" + paramID + "',"
                + String (newValue, 6) + ");}");
        }
    });
}

//==============================================================================
void SnotWebEditor::registerParamListeners()
{
    auto& apvts = proc.getAPVTS();
    for (auto* p : apvts.processor.getParameters())
        if (auto* rap = dynamic_cast<RangedAudioParameter*>(p))
            apvts.addParameterListener (rap->getParameterID(), this);
}

void SnotWebEditor::unregisterParamListeners()
{
    auto& apvts = proc.getAPVTS();
    for (auto* p : apvts.processor.getParameters())
        if (auto* rap = dynamic_cast<RangedAudioParameter*>(p))
            apvts.removeParameterListener (rap->getParameterID(), this);
}

//==============================================================================
AudioProcessorEditor* SnotAudioProcessor::createEditor()
{
    return new SnotWebEditor (*this);
}
