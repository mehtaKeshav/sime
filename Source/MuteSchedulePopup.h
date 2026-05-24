#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// MuteSchedulePopup.h
//
// Floating editor for a block's `muteWindows` schedule.  Lets the user add,
// edit, and remove any number of time-based mute windows in seconds (start
// + duration).  Lives on its own native window via addToDesktop() so it can
// paint over the OpenGL viewport.
//
// Usage
//   popup->setSchedule(blockSerial, "Violin 1", windows);
//   popup->onApply = [this](int serial, std::vector<MuteWindow> w) { ... };
//   popup->showAt({ screenX, screenY });
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "BlockEntry.h"
#include <functional>
#include <vector>

class MuteSchedulePopup : public juce::Component
{
public:
    /// Called when the user clicks Apply.  Receives a copy of the edited
    /// list so the caller can push it through its normal block-update path.
    std::function<void(int blockSerial, std::vector<MuteWindow>)> onApply;

    /// Optional: called when the popup is dismissed (Cancel / Esc / outer
    /// click) without applying — used by the sidebar to drop its reference.
    std::function<void()> onDismiss;

    MuteSchedulePopup();
    ~MuteSchedulePopup() override;

    /// Replace the working list and reset scroll.  Call before showAt().
    void setSchedule(int blockSerial,
                     const juce::String& blockName,
                     const std::vector<MuteWindow>& windows);

    /// Show the popup near the given screen coordinates.  Will clamp to the
    /// primary display so it never opens off-screen.
    void showAt(juce::Point<int> screenPos);

    void hide();

    void paint   (juce::Graphics&) override;
    void resized () override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&) override;

private:
    /// One editable row in the schedule.  Owns its own start / duration
    /// editors and a delete button; the popup owns the row vector.
    struct WindowRow
    {
        std::unique_ptr<juce::TextEditor> start;
        std::unique_ptr<juce::TextEditor> duration;
        std::unique_ptr<juce::TextButton> deleteBtn;
    };

    void rebuildRows();         ///< Recreate `rows_` from `windowsDraft_`.
    void pullValuesFromRows();  ///< Sync editor text back into windowsDraft_.
    void layoutRows();          ///< Position editor / button widgets.

    int                       editingSerial_ = -1;
    juce::String              editingName_;
    std::vector<MuteWindow>   windowsDraft_;
    std::vector<WindowRow>    rows_;

    juce::Label               titleLabel_;
    juce::Label               subtitleLabel_;   ///< block name, drawn under title
    juce::Label               headerStartLabel_;
    juce::Label               headerDurLabel_;
    juce::Label               hintLabel_;
    juce::Label               emptyHintLabel_;  ///< shown in the list when no windows
    juce::TextButton          addButton_     { "+ Add Window" };
    juce::TextButton          clearButton_   { "Clear All" };
    juce::TextButton          applyButton_   { "Apply" };
    juce::TextButton          cancelButton_  { "Cancel" };

    int                       scrollY_           = 0;
    int                       listContentBottom_ = 0;

    static constexpr int kWidth    = 460;
    static constexpr int kHeight   = 460;
    static constexpr int kPad      = 16;
    // Header band: title (24) + subtitle (18) + spacing (8) + column header (16) + spacing (8)
    static constexpr int kHeaderH  = 90;
    static constexpr int kRowH     = 30;
    static constexpr int kRowGap   = 8;
    static constexpr int kFooterH  = 96;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MuteSchedulePopup)
};
