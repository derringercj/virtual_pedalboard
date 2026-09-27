#include "PluginEditor.h"

#include "ui/Theme.h"

namespace
{
    constexpr int margin       = 16;
    constexpr int headerHeight = 34;
    constexpr int headerGap    = 16;
    constexpr int pedalGap     = 12;
    constexpr int emptyHeight  = 120;
    constexpr int minWidth     = 640;
    constexpr int maxWidth     = 1400;
}

//==============================================================================
VirtualPedalboardEditor::VirtualPedalboardEditor (VirtualPedalboardProcessor& p)
    : juce::AudioProcessorEditor (&p), board (p)
{
    juce::Font titleFont { juce::FontOptions (22.0f) };
    titleFont.setBold (true);

    titleLabel.setText ("VIRTUAL PEDALBOARD", juce::dontSendNotification);
    titleLabel.setFont (titleFont);
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    inputLabel.setText ("Input", juce::dontSendNotification);
    inputLabel.setJustificationType (juce::Justification::centredRight);
    inputLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (inputLabel);

    inputBox.addItemList ({ "In 1 (left)", "In 2 (right)", "Sum 1 + 2" }, 1);
    addAndMakeVisible (inputBox);
    inputAttachment = std::make_unique<APVTS::ComboBoxAttachment> (board.getValueTreeState(),
                                                                  "input", inputBox);

    emptyLabel.setText ("No pedals on the board - the bass goes straight through.",
                        juce::dontSendNotification);
    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    addChildComponent (emptyLabel);

    viewport.setViewedComponent (&pedalRow, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    board.addChainListener (this);
    rebuildPanels();
}

VirtualPedalboardEditor::~VirtualPedalboardEditor()
{
    board.removeChainListener (this);
}

//==============================================================================
void VirtualPedalboardEditor::pedalChainChanged()
{
    rebuildPanels();
}

void VirtualPedalboardEditor::rebuildPanels()
{
    // Every pedal's editor is thrown away and made again. Simple, and chains
    // change rarely enough that there's nothing to gain from being clever.
    panels.clear();

    int rowWidth = 0, rowHeight = 0;

    for (int i = 0; i < board.getNumPedals(); ++i)
    {
        auto* pedal = board.getPedal (i);

        if (pedal == nullptr)
            continue;

        Panel panel { pedal->createEditorIfNeeded() };

        if (panel == nullptr)
            continue;

        if (rowWidth > 0)
            rowWidth += pedalGap;

        panel->setTopLeftPosition (rowWidth, 0);
        pedalRow.addAndMakeVisible (*panel);

        rowWidth += panel->getWidth();
        rowHeight = juce::jmax (rowHeight, panel->getHeight());
        panels.push_back (std::move (panel));
    }

    pedalRow.setSize (rowWidth, rowHeight);
    emptyLabel.setVisible (panels.empty());

    const auto width      = juce::jlimit (minWidth, maxWidth, rowWidth + 2 * margin);
    const auto scrolls    = rowWidth + 2 * margin > maxWidth;
    const auto pedalsArea = juce::jmax (emptyHeight, rowHeight)
                            + (scrolls ? viewport.getScrollBarThickness() : 0);

    setSize (width, margin + headerHeight + headerGap + pedalsArea + margin);
}

//==============================================================================
void VirtualPedalboardEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::board);

    const auto separatorY = (float) (margin + headerHeight + headerGap / 2);
    g.setColour (theme::hairline);
    g.drawLine ((float) margin, separatorY, (float) (getWidth() - margin), separatorY, 1.0f);
}

void VirtualPedalboardEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);

    auto header = area.removeFromTop (headerHeight);
    inputBox.setBounds (header.removeFromRight (180).reduced (0, 4));
    inputLabel.setBounds (header.removeFromRight (60));
    titleLabel.setBounds (header);

    area.removeFromTop (headerGap);
    viewport.setBounds (area);
    emptyLabel.setBounds (area);
}
