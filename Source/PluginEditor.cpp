#include "PluginEditor.h"
#include <BinaryData.h>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

using namespace juce;

//==============================================================================
SnotWebEditor::SnotWebEditor (SnotAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    titleLabel.setText ("SNOT", dontSendNotification);
    titleLabel.setFont (Font (48.0f, Font::bold));
    titleLabel.setColour (Label::textColourId, Colour (0xff00ffcc));
    titleLabel.setJustificationType (Justification::centred);
    addAndMakeVisible (titleLabel);

    statusLabel.setText ("Loading portal...", dontSendNotification);
    statusLabel.setFont (Font (14.0f));
    statusLabel.setColour (Label::textColourId, Colour (0xff888888));
    statusLabel.setJustificationType (Justification::centred);
    addAndMakeVisible (statusLabel);

    setSize (W, H);
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
    // Clean up temp file
    if (htmlFile.existsAsFile())
        htmlFile.deleteFile();
}

//==============================================================================
void SnotWebEditor::buildBrowser()
{
   #if JUCE_WINDOWS
    // Disable GPU compositing — prevents dxgi.dll crash in DAW host process
    ::SetEnvironmentVariableW (L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
                               L"--disable-gpu --disable-gpu-compositing --in-process-gpu");
   #endif

    browser = std::make_unique<SnotBrowser> (*this);
    addAndMakeVisible (browser.get());

    // Write HTML to a temp file and navigate via file:// URL.
    // More reliable than data: URIs with WebView2 software rendering.
    htmlFile = File::getSpecialLocation (File::tempDirectory)
                   .getChildFile ("SNOT_UI.html");

    htmlFile.replaceWithData (BinaryData::SNOT_UI_html,
                              (size_t) BinaryData::SNOT_UI_htmlSize);

    browser->goToURL (htmlFile.getFullPathName());

    webViewReady = true;
    titleLabel.setVisible (false);
    statusLabel.setVisible (false);
    resized();
}

//==============================================================================
void SnotWebEditor::handleSnotURL (const String& url)
{
    if (url.startsWith ("snot://setparam/"))
    {
        const String path    = url.fromFirstOccurrenceOf ("snot://setparam/", false, false);
        const String paramID = path.upToFirstOccurrenceOf ("/", false, false);
        const float  val     = path.fromFirstOccurrenceOf ("/", false, false).getFloatValue();
        if (auto* param = proc.getAPVTS().getParameter (paramID))
            param->setValueNotifyingHost (jlimit (0.0f, 1.0f, val));
        return;
    }

    if (url.startsWith ("snot://preset/"))
    {
        const String dir = url.fromFirstOccurrenceOf ("snot://preset/", false, false);
        auto& pm = proc.getPresetManager();
        if (dir == "prev") pm.loadPrevPreset();
        else               pm.loadNextPreset();
        if (browser != nullptr)
            browser->evaluateJavascript (
                "if(window.SNOT&&window.SNOT.updatePreset)"
                "{window.SNOT.updatePreset('"
                + pm.getPresetName (pm.getCurrentIndex()) + "');}");
        return;
    }

    if (url.startsWith ("snot://module/"))
    {
        const String path    = url.fromFirstOccurrenceOf ("snot://module/", false, false);
        const String key     = path.upToFirstOccurrenceOf ("/", false, false);
        const bool   enabled = path.fromFirstOccurrenceOf ("/", false, false).getIntValue() != 0;
        if (auto* param = proc.getAPVTS().getParameter (key + "_enabled"))
            param->setValueNotifyingHost (enabled ? 1.0f : 0.0f);
        return;
    }
}

//==============================================================================
void SnotWebEditor::paint (Graphics& g)
{
    g.fillAll (Colour (0xff0a0a0f));
    if (!webViewReady)
    {
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
    if (browser != nullptr)
        browser->setBounds (getLocalBounds());

    auto b = getLocalBounds().reduced (80, 100);
    titleLabel.setBounds  (b.removeFromTop (70));
    statusLabel.setBounds (b.removeFromTop (30));
}

//==============================================================================
void SnotWebEditor::timerCallback()
{
    if (!webViewReady || browser == nullptr) return;

    const float* spec = proc.getSpectrumData();
    String js = "if(window.SNOT&&window.SNOT.updateSpectrum)"
                "{window.SNOT.updateSpectrum([";
    for (int i = 0; i < SnotAudioProcessor::SPECTRUM_SIZE; ++i)
    {
        if (i > 0) js += ",";
        js += String (spec[i], 3);
    }
    js += "]);}";
    browser->evaluateJavascript (js);
}

void SnotWebEditor::parameterChanged (const String& paramID, float newValue)
{
    if (!webViewReady || browser == nullptr) return;
    MessageManager::callAsync ([this, paramID, newValue]
    {
        if (browser != nullptr)
            browser->evaluateJavascript (
                "if(window.SNOT&&window.SNOT.updateParam)"
                "{window.SNOT.updateParam('" + paramID + "',"
                + String (newValue, 6) + ");}");
    });
}

//==============================================================================
void SnotWebEditor::registerParamListeners()
{
    auto& apvts = proc.getAPVTS();
    for (auto* p : apvts.processor.getParameters())
        if (auto* rap = dynamic_cast<RangedAudioParameter*> (p))
            apvts.addParameterListener (rap->getParameterID(), this);
}

void SnotWebEditor::unregisterParamListeners()
{
    auto& apvts = proc.getAPVTS();
    for (auto* p : apvts.processor.getParameters())
        if (auto* rap = dynamic_cast<RangedAudioParameter*> (p))
            apvts.removeParameterListener (rap->getParameterID(), this);
}

//==============================================================================
AudioProcessorEditor* SnotAudioProcessor::createEditor()
{
    return new SnotWebEditor (*this);
}
