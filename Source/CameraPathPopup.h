#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CameraPathPopup.h
//
// Floating editor for the scene's camera (listener) path.  Each row is one
// CameraKeyframe — time, position, yaw / pitch (in degrees for the UI),
// and mode (Hold / Lerp).  The buttons "Add Hold at cam now" and "Record"
// drive the two authoring modes the user asked for:
//
//   * Hold  : instant teleport to a fixed pose for some duration.
//   * Lerp  : Recorded segment captured live by flying the camera while the
//             transport is playing (R key, mirrored on the popup button).
//
// Apply commits the whole list to ViewPortComponent via applyCameraPath.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "CameraPath.h"
#include <functional>
#include <memory>
#include <vector>

class CameraPathPopup : public juce::Component, private juce::Timer
{
public:
    /// Apply pushes the cleaned, sorted list back to the host and closes.
    std::function<void(std::vector<CameraKeyframe>)>            onApply;
    /// Dismiss notifies the host if the user cancels.
    std::function<void()>                                       onDismiss;

    /// Push the current draft to the host WITHOUT closing.  Called before a
    /// recording session so the host splice operates on the user's latest
    /// edits, not a stale snapshot.
    std::function<void(std::vector<CameraKeyframe>)>            onCommitDraft;

    /// Record button asks the host to toggle camera-path recording.
    std::function<void()>                                       onRecordToggle;
    /// Polled each timer tick — true while the host is actively recording.
    std::function<bool()>                                       isRecording;
    /// Fetch the latest path snapshot from the host (called once on open and
    /// once each time recording transitions from active → stopped).
    std::function<std::vector<CameraKeyframe>()>                fetchLivePath;

    /// Live camera pose + playhead time for the "+ Hold @ cam now" button.
    /// The popup adds the row directly to its draft — no host round-trip —
    /// so user edits aren't overwritten by the polling timer.
    std::function<CameraPose()>                                 getCurrentCamPose;
    std::function<double()>                                     getCurrentPlayheadSec;

    /// Capture-interval selector wiring.  setter is invoked when the user
    /// changes the dropdown; getter is queried on open so the dropdown
    /// reflects the host's current setting.
    std::function<void(double)>                                 onCaptureIntervalChanged;
    std::function<double()>                                     getCaptureIntervalSec;

    CameraPathPopup();
    ~CameraPathPopup() override;

    void setPath(const std::vector<CameraKeyframe>& path);

    void showAt(juce::Point<int> screenPos);
    void hide();

    void paint   (juce::Graphics&) override;
    void resized () override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&) override;

private:
    struct KeyRow
    {
        std::unique_ptr<juce::TextEditor> time;
        std::unique_ptr<juce::TextEditor> x;
        std::unique_ptr<juce::TextEditor> y;
        std::unique_ptr<juce::TextEditor> z;
        std::unique_ptr<juce::TextEditor> yawDeg;
        std::unique_ptr<juce::TextEditor> pitchDeg;
        std::unique_ptr<juce::ComboBox>   modeBox;
        std::unique_ptr<juce::TextEditor> holdSec;   ///< Visible/active only when mode == Hold
        std::unique_ptr<juce::TextButton> deleteBtn;
    };

    void rebuildRows();
    void pullValuesFromRows();
    void layoutRows();
    void scrollToBottom();

    void timerCallback() override;

    std::vector<CameraKeyframe>  draft_;
    std::vector<KeyRow>          rows_;

    juce::Label      titleLabel_;
    juce::Label      subtitleLabel_;
    juce::Label      headerTimeLabel_;
    juce::Label      headerXLabel_;
    juce::Label      headerYLabel_;
    juce::Label      headerZLabel_;
    juce::Label      headerYawLabel_;
    juce::Label      headerPitchLabel_;
    juce::Label      headerModeLabel_;
    juce::Label      headerHoldLabel_;
    juce::Label      hintLabel_;
    juce::Label      emptyHintLabel_;
    juce::Label      captureIntervalLabel_;
    juce::ComboBox   captureIntervalBox_;
    juce::TextButton addHoldBtn_  { "+ Hold @ cam now" };
    juce::TextButton recordBtn_   { "Record (R)" };
    juce::TextButton clearButton_ { "Clear All" };
    juce::TextButton applyButton_ { "Apply" };
    juce::TextButton cancelButton_{ "Cancel" };

    int  scrollY_       = 0;
    bool wasRecording_  = false;   ///< For edge detection in timerCallback.

    static constexpr int kWidth   = 820;
    static constexpr int kHeight  = 580;
    static constexpr int kPad     = 16;
    static constexpr int kHeaderH = 96;
    static constexpr int kRowH    = 30;
    static constexpr int kRowGap  = 8;
    static constexpr int kFooterH = 138;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CameraPathPopup)
};
