#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "PluginProcessor.h"

/**
    The board: the input selector along the top, and below it each pedal's own
    editor, laid out left to right in signal order. Scrolls sideways once the
    chain is wider than the window.
*/
class VirtualPedalboardEditor final : public juce::AudioProcessorEditor,
                                      private VirtualPedalboardProcessor::ChainListener
{
public:
    explicit VirtualPedalboardEditor (VirtualPedalboardProcessor&);
    ~VirtualPedalboardEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** How many pedal panels are showing - one per pedal, if all is well. */
    int getNumPedalPanels() const noexcept { return (int) panels.size(); }

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    /** A pedal's editor must be deleted the way a plugin host deletes one:
        tell the pedal first, so it stops pointing at the editor. */
    struct PanelDeleter
    {
        void operator() (juce::AudioProcessorEditor* editor) const
        {
            editor->processor.editorBeingDeleted (editor);
            delete editor;
        }
    };

    using Panel = std::unique_ptr<juce::AudioProcessorEditor, PanelDeleter>;

    void pedalChainChanged() override;
    void rebuildPanels();

    VirtualPedalboardProcessor& board;

    juce::Label    titleLabel, inputLabel, emptyLabel;
    juce::ComboBox inputBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> inputAttachment;

    juce::Viewport     viewport;
    juce::Component    pedalRow;
    std::vector<Panel> panels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualPedalboardEditor)
};
