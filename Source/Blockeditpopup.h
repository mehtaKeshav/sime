#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BlockEditPopup.h
//
// Uses addToDesktop() so the popup gets its own native OS window and appears
// on top of the OpenGL context, which owns a native child window that normal
// JUCE components cannot paint over.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "BlockType.h"

class BlockEditPopup : public juce::Component
{
public:
    /// Commit returns: serial, start, duration, soundId, customFilePath
    std::function<void(int, double, double, int, const juce::String&)> onCommit;
    std::function<void()> onCancel;

    BlockEditPopup();
    ~BlockEditPopup() override;

    void showAt(int blockSerial, BlockType type,
                double startTime, double duration,
                int soundId, const juce::String& customFile,
                juce::Point<int> screenPos);

    void hide();

    void paint   (juce::Graphics&) override;
    void resized () override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void commit();
    void populateSoundCombo(BlockType type, int currentSoundId);

    int       editingSerial = -1;
    BlockType editingType   = BlockType::Violin;

    juce::Label      titleLabel;
    juce::Label      typeLabel,     typeValueLabel;
    juce::Label      startLabel,    durationLabel;
    juce::TextEditor startField,    durationField;

    // Instrument sound selector
    juce::Label      soundLabel;
    juce::ComboBox   soundCombo;

    // Custom file selector
    juce::Label      fileLabel;
    juce::TextEditor fileField;
    juce::TextButton browseButton { "Browse..." };
    juce::String     customFilePath_;

    std::unique_ptr<juce::FileChooser> fileChooser_;

    juce::TextButton applyButton  { "Apply"  };
    juce::TextButton cancelButton { "Cancel" };

    static constexpr int kWidth  = 260;
    static constexpr int kHeight = 260;
    static constexpr int kPad    = 12;
    static constexpr int kRowH   = 28;
    static constexpr int kLabelW = 76;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockEditPopup)
};
