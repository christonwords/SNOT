#include "PluginEditor.h"

// BinaryData — SNOT_UI_html is embedded at compile time by CMake
// juce_add_binary_data(SNOTResources FILES Resources/SNOT_UI.html)
// This gives us:  BinaryData::SNOT_UI_html  and  BinaryData::SNOT_UI_htmlSize
namespace BinaryData
{
    // These are declared by the juce_add_binary_data target.
    // If your IDE shows "undeclared" errors here, that's fine —
    // they resolve at link time after CMake generates them.
    extern const char*  SNOT_UI_html;
    extern const int    SNOT_UI_htmlSize;
}

using namespace juce;

//==============================================================================
SnotWebEditor::SnotWebEditor (SnotAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (W, H);
    setResizable (true, true);
    setResizeLimits (900, 540, 1920, 1080);

    // Write the embedded HTML to a temp file so the browser can load it
    // (WebBrowserComponent needs a file:// or http:// URL to load from)
    writeHTMLToTemp();

    // Create browser
    browser = std::make_unique<SnotBrowser> (*this);
    addAndMakeVisible (*browser);

    // Load the HTML
    browser->goToURL ("file://" + htmlFile.getFullPathName()
                      #if JUCE_WINDOWS
                          .replaceCharacter ('\\', '/')
                          .replace ("file://", "file:///")
                      #endif
                      );

    // Listen to all APVTS parameters so we can push automation to JS
    registerParamListeners();

    // 30fps timer: push spectrum data to JS
    startTimerHz (30);
}

SnotWebEditor::~SnotWebEditor()
{
    stopTimer();
    unregisterParamListeners();

    // Clean up temp HTML file
    htmlFile.deleteFile();
}

//==============================================================================
void SnotWebEditor::resized()
{
    browser->setBounds (getLocalBounds());
}

void SnotWebEditor::paint (Graphics& g)
{
    // The WebBrowserComponent fills everything.
    // Paint a fallback black in case the browser hasn't loaded yet.
    g.fillAll (Colour (0xff030508));
}

//==============================================================================
void SnotWebEditor::timerCallback()
{
    // Only push spectrum every 2nd frame (15fps is fine for the display)
    if (++specFrameSkip < 2) return;
    specFrameSkip = 0;

    browser->callJS (buildSpectrumJS());
}

//==============================================================================
void SnotWebEditor::parameterChanged (const String& paramID, float newValue)
{
    // Called on ANY thread — must marshal to message thread
    // newValue is already normalised 0..1 from APVTS
    const String js = "window.SNOT.updateParam('"
                    + paramID + "',"
                    + String (newValue, 6) + ")";
    browser->callJS (js);
}

//==============================================================================
String SnotWebEditor::buildSpectrumJS()
{
    const float* data = proc.getSpectrumData();

    // Build a compact JS array string: [0.12,0.45,...]
    // We sample every 4th bin to keep the string short (128 values)
    String arr = "[";
    for (int i = 0; i < SnotAudioProcessor::SPECTRUM_SIZE; i += 4)
    {
        if (i > 0) arr << ",";
        arr << String (data[i], 3);
    }
    arr << "]";

    return "window.SNOT.updateSpectrum(" + arr + ")";
}

//==============================================================================
void SnotWebEditor::writeHTMLToTemp()
{
    // Write embedded binary data to a temp file
    htmlFile = File::getSpecialLocation (File::tempDirectory)
                   .getChildFile ("SNOT_UI_" + String (Time::currentTimeMillis()) + ".html");

    htmlFile.replaceWithData (BinaryData::SNOT_UI_html,
                               static_cast<size_t> (BinaryData::SNOT_UI_htmlSize));
}

//==============================================================================
void SnotWebEditor::registerParamListeners()
{
    auto& apvts = proc.getAPVTS();

    // Register for every parameter so automation shows live in the UI
    const auto& params = apvts.processor.getParameters();
    for (auto* p : params)
        apvts.addParameterListener (p->paramID, this);
}

void SnotWebEditor::unregisterParamListeners()
{
    auto& apvts = proc.getAPVTS();
    const auto& params = apvts.processor.getParameters();
    for (auto* p : params)
        apvts.removeParameterListener (p->paramID, this);
}

//==============================================================================
// Tell the processor to use this editor
AudioProcessorEditor* SnotAudioProcessor::createEditor()
{
    return new SnotWebEditor (*this);
}
