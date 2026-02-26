#include "PluginEditor.h"
#include <BinaryData.h>

using namespace juce;

//==============================================================================
SnotWebEditor::SnotWebEditor (SnotAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      gainAttach (p.getAPVTS(), ParamID::MASTER_GAIN, gainSlider),
      mixAttach  (p.getAPVTS(), ParamID::MIX,         mixSlider)
{
    // Title
    titleLabel.setText ("SNOT", dontSendNotification);
    titleLabel.setFont (Font (48.0f, Font::bold));
    titleLabel.setColour (Label::textColourId, Colour (0xff00ffcc));
    titleLabel.setJustificationType (Justification::centred);
    addAndMakeVisible (titleLabel);

    // Status
    statusLabel.setText ("Portal Audio Engine Active", dontSendNotification);
    statusLabel.setFont (Font (14.0f));
    statusLabel.setColour (Label::textColourId, Colour (0xff888888));
    statusLabel.setJustificationType (Justification::centred);
    addAndMakeVisible (statusLabel);

    // Gain
    gainSlider.setRange (0.0, 2.0);
    gainSlider.setColour (Slider::thumbColourId,       Colour (0xff00ffcc));
    gainSlider.setColour (Slider::rotarySliderFillColourId, Colour (0xff00ffcc));
    gainLabel.setText ("Gain", dontSendNotification);
    gainLabel.setColour (Label::textColourId, Colours::white);
    gainLabel.setJustificationType (Justification::centred);
    addAndMakeVisible (gainSlider);
    addAndMakeVisible (gainLabel);

    // Mix
    mixSlider.setRange (0.0, 1.0);
    mixSlider.setColour (Slider::thumbColourId,       Colour (0xff7700ff));
    mixSlider.setColour (Slider::rotarySliderFillColourId, Colour (0xff7700ff));
    mixLabel.setText ("Mix", dontSendNotification);
    mixLabel.setColour (Label::textColourId, Colours::white);
    mixLabel.setJustificationType (Justification::centred);
    addAndMakeVisible (mixSlider);
    addAndMakeVisible (mixLabel);

    registerParamListeners();
    setSize (W, H);
    startTimerHz (30);
}

SnotWebEditor::~SnotWebEditor()
{
    stopTimer();
    unregisterParamListeners();
}

//==============================================================================
void SnotWebEditor::paint (Graphics& g)
{
    // Dark gradient background
    g.fillAll (Colour (0xff0a0a0f));

    // Glow ring behind title
    g.setColour (Colour (0xff00ffcc).withAlpha (0.06f));
    g.fillEllipse (getWidth() / 2.0f - 200, 20, 400, 200);

    // Subtle grid lines
    g.setColour (Colour (0xff1a1a2e));
    for (int x = 0; x < getWidth(); x += 40)
        g.drawVerticalLine (x, 0.0f, (float)getHeight());
    for (int y = 0; y < getHeight(); y += 40)
        g.drawHorizontalLine (y, 0.0f, (float)getWidth());

    // Bottom border accent
    g.setColour (Colour (0xff00ffcc).withAlpha (0.4f));
    g.fillRect (0, getHeight() - 2, getWidth(), 2);
}

void SnotWebEditor::resized()
{
    auto area = getLocalBounds().reduced (40);

    // Title at top
    titleLabel.setBounds  (area.removeFromTop (70));
    statusLabel.setBounds (area.removeFromTop (30));

    area.removeFromTop (40); // spacing

    // Two knobs side by side
    auto knobArea = area.removeFromTop (220);
    auto left  = knobArea.removeFromLeft (knobArea.getWidth() / 2);
    auto right = knobArea;

    gainLabel.setBounds (left.removeFromBottom (24));
    gainSlider.setBounds (left.reduced (20, 0));

    mixLabel.setBounds (right.removeFromBottom (24));
    mixSlider.setBounds (right.reduced (20, 0));
}

//==============================================================================
void SnotWebEditor::timerCallback()
{
    // Reserved for future spectrum/meter updates
}

void SnotWebEditor::parameterChanged (const String& /*paramID*/, float /*newValue*/)
{
    // Sliders update automatically via APVTS attachment
}

//==============================================================================
void SnotWebEditor::registerParamListeners()
{
    auto& apvts = proc.getAPVTS();
    const auto& params = apvts.processor.getParameters();
    for (auto* p : params)
        if (auto* rap = dynamic_cast<RangedAudioParameter*>(p))
            apvts.addParameterListener (rap->getParameterID(), this);
}

void SnotWebEditor::unregisterParamListeners()
{
    auto& apvts = proc.getAPVTS();
    const auto& params = apvts.processor.getParameters();
    for (auto* p : params)
        if (auto* rap = dynamic_cast<RangedAudioParameter*>(p))
            apvts.removeParameterListener (rap->getParameterID(), this);
}

//==============================================================================
AudioProcessorEditor* SnotAudioProcessor::createEditor()
{
    return new SnotWebEditor (*this);
}
