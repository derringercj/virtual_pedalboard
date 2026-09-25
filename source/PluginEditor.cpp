#include "PluginEditor.h"

namespace
{
    constexpr int   margin      = 16;
    constexpr int   titleHeight = 34;
    constexpr int   separatorY  = margin + titleHeight + 7;

    const juce::Colour background { 0xff23262b };
    const juce::Colour trough     { 0xff15171a };
    const juce::Colour accent     { 0xffe8b23a };
    const juce::Colour hairline   { 0xff35393f };
}

//==============================================================================
GainReductionMeter::GainReductionMeter (VirtualPedalboardProcessor& p)
    : processor (p)
{
    startTimerHz (30);
}

void GainReductionMeter::timerCallback()
{
    const auto target = processor.getGainReductionDb();

    // Snap upward so nothing is missed, fall back smoothly so it stays readable.
    displayedDb = target > displayedDb ? target
                                       : juce::jmax (target, displayedDb - fallPerTickDb);
    repaint();
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (trough);
    g.fillRoundedRectangle (bounds, 4.0f);

    const auto proportion = juce::jlimit (0.0f, 1.0f, displayedDb / fullScaleDb);

    if (proportion > 0.0f)
    {
        g.setColour (accent);
        g.fillRoundedRectangle (bounds.withWidth (juce::jmax (8.0f, bounds.getWidth() * proportion)),
                                4.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (13.0f);
    g.drawText (displayedDb < 0.05f ? juce::String ("0.0 dB")
                                    : "-" + juce::String (displayedDb, 1) + " dB",
                getLocalBounds().reduced (10, 0), juce::Justification::centredRight);
}

//==============================================================================
VirtualPedalboardEditor::VirtualPedalboardEditor (VirtualPedalboardProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), meter (p)
{
    auto& apvts = processor.getValueTreeState();

    juce::Font titleFont { juce::FontOptions (22.0f) };
    titleFont.setBold (true);

    titleLabel.setText ("COMPRESSOR", juce::dontSendNotification);
    titleLabel.setFont (titleFont);
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    meterLabel.setText ("Gain reduction", juce::dontSendNotification);
    meterLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (meterLabel);
    addAndMakeVisible (meter);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::darkred);
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<APVTS::ButtonAttachment> (apvts, "bypass", bypassButton);

    inputLabel.setText ("Input", juce::dontSendNotification);
    inputLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (inputLabel);

    inputBox.addItemList ({ "In 1 (left)", "In 2 (right)", "Sum 1 + 2" }, 1);
    addAndMakeVisible (inputBox);
    inputAttachment = std::make_unique<APVTS::ComboBoxAttachment> (apvts, "input", inputBox);

    addKnob ("threshold", "Threshold", " dB");
    addKnob ("ratio",     "Ratio",     ":1");
    addKnob ("knee",      "Knee",      " dB");
    addKnob ("attack",    "Attack",    " ms");
    addKnob ("release",   "Release",   " ms");
    addKnob ("makeup",    "Makeup",    " dB");

    setSize (780, 380);
}

void VirtualPedalboardEditor::addKnob (const juce::String& parameterID, const juce::String& text,
                                       const juce::String& suffix)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 20);
    knob->slider.setTextValueSuffix (suffix);
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (knob->slider);

    knob->label.setText (text, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
    addAndMakeVisible (knob->label);

    knob->attachment = std::make_unique<APVTS::SliderAttachment> (processor.getValueTreeState(),
                                                                 parameterID, knob->slider);
    knobs.push_back (std::move (knob));
}

void VirtualPedalboardEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (hairline);
    g.drawLine ((float) margin, (float) separatorY,
                (float) (getWidth() - margin), (float) separatorY, 1.0f);
}

void VirtualPedalboardEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);

    titleLabel.setBounds (area.removeFromTop (titleHeight));
    area.removeFromTop (14);

    auto meterRow = area.removeFromTop (30);
    meterLabel.setBounds (meterRow.removeFromLeft (120));
    meter.setBounds (meterRow);
    area.removeFromTop (14);

    auto bottom = area.removeFromBottom (44);
    bypassButton.setBounds (bottom.removeFromLeft (140).reduced (0, 6));
    bottom.removeFromLeft (24);
    inputLabel.setBounds (bottom.removeFromLeft (52).reduced (0, 6));
    inputBox.setBounds (bottom.removeFromLeft (180).reduced (0, 10));
    area.removeFromBottom (10);

    const auto count = (int) knobs.size();

    if (count == 0)
        return;

    const auto cellWidth = area.getWidth() / count;

    for (auto& knob : knobs)
    {
        auto cell = area.removeFromLeft (cellWidth);
        knob->label.setBounds (cell.removeFromTop (20));
        knob->slider.setBounds (cell);
    }
}
