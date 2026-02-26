#include "PluginEditor.h"
#include <BinaryData.h>

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
}

//==============================================================================
void SnotWebEditor::buildBrowser()
{
    // Wrap HTML in a data URI and navigate — works on all JUCE 7.x versions,
    // no ResourceProvider API needed, no temp files, no filesystem access.
    // Disable WebView2 GPU acceleration — prevents dxgi.dll crash inside DAW plugin host.
    // WebView2 tries to create a D3D11 device which crashes in FL Studio's process.
    // Software rendering (--disable-gpu) is stable and fast enough for plugin UI.
   #if JUCE_WINDOWS
    ::SetEnvironmentVariableW (L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
                               L"--disable-gpu --disable-gpu-compositing "
                               L"--disable-software-rasterizer "
                               L"--in-process-gpu");
   #endif

    browser = std::make_unique<SnotBrowser> (*this);
    addAndMakeVisible (browser.get());

    // Build data URI: data:text/html;charset=utf-8,<url-encoded HTML>
    // For large HTML we use base64 encoding instead to avoid URL encoding overhead
    const String htmlStr (CharPointer_UTF8 (BinaryData::SNOT_UI_html),
                          (size_t) BinaryData::SNOT_UI_htmlSize);

    // base64-encode and navigate
    MemoryBlock htmlBytes (BinaryData::SNOT_UI_html, (size_t) BinaryData::SNOT_UI_htmlSize);
    const String b64 = Base64::toBase64 (BinaryData::SNOT_UI_html,
                                         (size_t) BinaryData::SNOT_UI_htmlSize);
    const String dataURI = "data:text/html;base64," + b64;

    browser->goToURL (dataURI);

    webViewReady = true;
    titleLabel.setVisible (false);
    statusLabel.setVisible (false);
    resized();
}

//==============================================================================
void SnotWebEditor::handleSnotURL (const String& url)
{
    // snot://setparam/param_id/0.750000
    if (url.startsWith ("snot://setparam/"))
    {
        const String path    = url.fromFirstOccurrenceOf ("snot://setparam/", false, false);
        const String paramID = path.upToFirstOccurrenceOf ("/", false, false);
        const float  val     = path.fromFirstOccurrenceOf ("/", false, false).getFloatValue();
        if (auto* param = proc.getAPVTS().getParameter (paramID))
            param->setValueNotifyingHost (jlimit (0.0f, 1.0f, val));
        return;
    }

    // snot://preset/prev  or  snot://preset/next
    if (url.startsWith ("snot://preset/"))
    {
        const String dir = url.fromFirstOccurrenceOf ("snot://preset/", false, false);
        auto& pm = proc.getPresetManager();
        if (dir == "prev") pm.loadPrevPreset();
        else               pm.loadNextPreset();
        if (browser != nullptr)
            browser->evaluateJavascript ("if(window.SNOT&&window.SNOT.updatePreset)"
                "{window.SNOT.updatePreset('" + pm.getPresetName (pm.getCurrentIndex()) + "');}");
        return;
    }

    // snot://module/key/1  or  snot://module/key/0
    if (url.startsWith ("snot://module/"))
    {
        const String path    = url.fromFirstOccurrenceOf ("snot://module/", false, false);
        const String key     = path.upToFirstOccurrenceOf ("/", false, false);
        const bool   enabled = path.fromFirstOccurrenceOf ("/", false, false).getIntValue() != 0;
        const String paramID = key + "_enabled";
        if (auto* param = proc.getAPVTS().getParameter (paramID))
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
        for (int x = 0; x < getWidth(); x += 40)  g.drawVerticalLine   (x, 0.0f, (float) getHeight());
        for (int y = 0; y < getHeight(); y += 40)  g.drawHorizontalLine (y, 0.0f, (float) getWidth());
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
    String js = "if(window.SNOT&&window.SNOT.updateSpectrum){window.SNOT.updateSpectrum([";
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
                "{window.SNOT.updateParam('" + paramID + "'," + String (newValue, 6) + ");}");
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
