// ─────────────────────────────────────────────────────────────────────────────
// CameraPathPopup.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "CameraPathPopup.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr juce::uint32 kBgColor      = 0xf012141cu;
    constexpr juce::uint32 kBorderColor  = 0xff2c3550u;
    constexpr juce::uint32 kAccentColor  = 0xff5b7ce6u;
    constexpr juce::uint32 kRecordColor  = 0xffc04848u;
    constexpr juce::uint32 kFieldBgColor = 0xff181a24u;
    constexpr juce::uint32 kFieldBdColor = 0xff2f3447u;
    constexpr juce::uint32 kRowBgColor   = 0xff161a26u;
    constexpr juce::uint32 kRowBdColor   = 0xff262d44u;

    constexpr int kColTimeW  = 70;
    constexpr int kColXYZW   = 54;
    constexpr int kColAngleW = 56;
    constexpr int kColModeW  = 70;
    constexpr int kColHoldW  = 60;
    constexpr int kColGap    = 6;
    constexpr int kDelW      = 24;

    constexpr float kRadToDeg = 180.f / juce::MathConstants<float>::pi;
    constexpr float kDegToRad = juce::MathConstants<float>::pi / 180.f;
}

CameraPathPopup::CameraPathPopup()
{
    setSize(kWidth, kHeight);
    setWantsKeyboardFocus(true);

    titleLabel_.setText("Camera Path", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(15.f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xfff0f2fa));
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel_);

    subtitleLabel_.setText("Hold = freeze this pose until the next keyframe (instant cut). "
                           "Lerp = smoothly interpolate to the next keyframe.",
                           juce::dontSendNotification);
    subtitleLabel_.setFont(juce::Font(11.5f));
    subtitleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8b94ad));
    subtitleLabel_.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel_.setMinimumHorizontalScale(1.f);
    addAndMakeVisible(subtitleLabel_);

    auto styleHeader = [](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(10.5f, juce::Font::bold));
        l.setColour(juce::Label::textColourId, juce::Colour(0xff6b7494));
        l.setJustificationType(juce::Justification::centredLeft);
    };
    styleHeader(headerTimeLabel_,  "TIME (s)");
    styleHeader(headerXLabel_,     "X");
    styleHeader(headerYLabel_,     "Y");
    styleHeader(headerZLabel_,     "Z");
    styleHeader(headerYawLabel_,   "YAW (deg)");
    styleHeader(headerPitchLabel_, "PITCH (deg)");
    styleHeader(headerModeLabel_,  "MODE");
    styleHeader(headerHoldLabel_,  "HOLD (s)");
    addAndMakeVisible(headerTimeLabel_);
    addAndMakeVisible(headerXLabel_);
    addAndMakeVisible(headerYLabel_);
    addAndMakeVisible(headerZLabel_);
    addAndMakeVisible(headerYawLabel_);
    addAndMakeVisible(headerPitchLabel_);
    addAndMakeVisible(headerModeLabel_);
    addAndMakeVisible(headerHoldLabel_);

    hintLabel_.setText("Tip: press R (anywhere in the window) to start/stop recording.  Works "
                       "whether the transport is playing or paused.  + Hold @ cam now drops a "
                       "fixed pose at the next free time slot.  HOLD (s) controls how long a "
                       "Hold pose occupies the timeline before the next slot opens up.",
                       juce::dontSendNotification);
    hintLabel_.setFont(juce::Font(11.f));
    hintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8b94ad));
    hintLabel_.setJustificationType(juce::Justification::topLeft);
    hintLabel_.setMinimumHorizontalScale(1.f);
    addAndMakeVisible(hintLabel_);

    captureIntervalLabel_.setText("Capture every:", juce::dontSendNotification);
    captureIntervalLabel_.setFont(juce::Font(11.f, juce::Font::bold));
    captureIntervalLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8b94ad));
    captureIntervalLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(captureIntervalLabel_);

    // Granularity options for live R-recording.  Default 1s — the user
    // explicitly asked for "every second" by default.  The 0.1s entry is
    // there for hand-flown sweeps that need fine-grained captures.
    captureIntervalBox_.addItem("Every 0.1 s",  1);
    captureIntervalBox_.addItem("Every 0.25 s", 2);
    captureIntervalBox_.addItem("Every 0.5 s",  3);
    captureIntervalBox_.addItem("Every 1 s",    4);
    captureIntervalBox_.addItem("Every 2 s",    5);
    captureIntervalBox_.addItem("Every 5 s",    6);
    captureIntervalBox_.setSelectedId(4, juce::dontSendNotification);
    captureIntervalBox_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(kFieldBgColor));
    captureIntervalBox_.setColour(juce::ComboBox::textColourId,       juce::Colour(0xfff0f2fa));
    captureIntervalBox_.setColour(juce::ComboBox::outlineColourId,    juce::Colour(kFieldBdColor));
    captureIntervalBox_.setColour(juce::ComboBox::arrowColourId,      juce::Colour(0xff8b94ad));
    captureIntervalBox_.setTooltip("How often the camera pose is sampled during a Record (R) "
                                   "session.  Lower = smoother but more keyframes to manage.");
    captureIntervalBox_.onChange = [this]
    {
        const double sec = [this]
        {
            switch (captureIntervalBox_.getSelectedId())
            {
                case 1: return 0.1;
                case 2: return 0.25;
                case 3: return 0.5;
                case 4: return 1.0;
                case 5: return 2.0;
                case 6: return 5.0;
                default: return 1.0;
            }
        }();
        if (onCaptureIntervalChanged) onCaptureIntervalChanged(sec);
    };
    addAndMakeVisible(captureIntervalBox_);

    emptyHintLabel_.setText("No keyframes yet. Click \"Hold @ cam now\" to drop a static "
                            "viewpoint, or press Record (R) to fly the camera live.",
                            juce::dontSendNotification);
    emptyHintLabel_.setFont(juce::Font(12.f, juce::Font::italic));
    emptyHintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff7a83a3));
    emptyHintLabel_.setJustificationType(juce::Justification::centred);
    addChildComponent(emptyHintLabel_);

    addHoldBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff242a3c));
    addHoldBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    addHoldBtn_.setTooltip("Capture the live camera pose and append it as a Hold keyframe "
                           "at the next free time slot (after the last segment ends).");
    addHoldBtn_.onClick = [this]
    {
        pullValuesFromRows();
        if (!getCurrentCamPose) return;

        const auto pose = getCurrentCamPose();
        CameraKeyframe kf;

        // Next available time = end of the last segment in the draft.  For
        // a draft ending in a Hold with a non-zero HoldDuration, that's
        // `time + holdDuration`; otherwise just the last keyframe time.
        // Empty draft falls back to the current playhead so the first
        // keyframe lands "where the user is in the song".
        const double tail = CameraPathUtil::effectiveEndTime(draft_);
        const double playhead = getCurrentPlayheadSec ? getCurrentPlayheadSec() : 0.0;
        kf.timeSec  = draft_.empty() ? playhead : tail;
        kf.pos      = pose.pos;
        kf.yawRad   = pose.yawRad;
        kf.pitchRad = pose.pitchRad;
        kf.mode     = CameraKeyframe::Hold;
        kf.holdDurationSec = 2.0f;   // sensible default so the next "+ Hold" lands 2s later

        draft_.push_back(kf);
        CameraPathUtil::sortByTime(draft_);
        rebuildRows();
        scrollToBottom();
        resized();
        repaint();
    };
    addAndMakeVisible(addHoldBtn_);

    recordBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff242a3c));
    recordBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    recordBtn_.setTooltip("Start / stop live recording.  Camera poses are captured ~20 Hz; "
                          "works whether transport is playing or not.");
    recordBtn_.onClick = [this]
    {
        pullValuesFromRows();
        // Commit current draft to the host BEFORE recording so the splice on
        // stop operates on the user's latest edits, not a stale snapshot.
        if (onCommitDraft) onCommitDraft(draft_);
        if (onRecordToggle) onRecordToggle();
    };
    addAndMakeVisible(recordBtn_);

    clearButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff242a3c));
    clearButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    clearButton_.onClick = [this]
    {
        draft_.clear();
        scrollY_ = 0;
        rebuildRows();
        resized();
        repaint();
    };
    addAndMakeVisible(clearButton_);

    applyButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(kAccentColor));
    applyButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    applyButton_.onClick = [this]
    {
        pullValuesFromRows();
        CameraPathUtil::sortByTime(draft_);
        if (onApply) onApply(draft_);
        hide();
    };
    addAndMakeVisible(applyButton_);

    cancelButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff242a3c));
    cancelButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    cancelButton_.onClick = [this]
    {
        hide();
        if (onDismiss) onDismiss();
    };
    addAndMakeVisible(cancelButton_);

    addToDesktop(juce::ComponentPeer::windowIsTemporary
               | juce::ComponentPeer::windowHasDropShadow);

    setVisible(false);
}

CameraPathPopup::~CameraPathPopup()
{
    stopTimer();
    removeFromDesktop();
}

// ─────────────────────────────────────────────────────────────────────────────

void CameraPathPopup::setPath(const std::vector<CameraKeyframe>& path)
{
    draft_        = path;
    scrollY_      = 0;
    wasRecording_ = false;

    // Sync the capture-interval dropdown to the host's current setting.
    if (getCaptureIntervalSec)
    {
        const double s = getCaptureIntervalSec();
        int id = 4;   // default = 1s
        if      (s <= 0.15)  id = 1;
        else if (s <= 0.35)  id = 2;
        else if (s <= 0.75)  id = 3;
        else if (s <= 1.5)   id = 4;
        else if (s <= 3.0)   id = 5;
        else                 id = 6;
        captureIntervalBox_.setSelectedId(id, juce::dontSendNotification);
    }

    rebuildRows();
}

void CameraPathPopup::showAt(juce::Point<int> screenPos)
{
    auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    juce::Rectangle<int> screen = display
        ? display->userArea
        : juce::Rectangle<int>(0, 0, 1920, 1080);

    int px = screenPos.x + 16;
    int py = screenPos.y - kHeight / 2;
    px = juce::jlimit(screen.getX(), screen.getRight()  - kWidth,  px);
    py = juce::jlimit(screen.getY(), screen.getBottom() - kHeight, py);

    setVisible(true);
    setBounds(px, py, kWidth, kHeight);
    resized();
    toFront(true);
    grabKeyboardFocus();

    startTimerHz(10);   // poll for recording state + live path snapshot
}

void CameraPathPopup::hide()
{
    stopTimer();
    setVisible(false);
}

// ─────────────────────────────────────────────────────────────────────────────

void CameraPathPopup::timerCallback()
{
    if (!isVisible()) return;

    const bool rec = isRecording ? isRecording() : false;
    recordBtn_.setColour(juce::TextButton::buttonColourId,
                         juce::Colour(rec ? kRecordColor : 0xff242a3c));
    recordBtn_.setButtonText(rec ? juce::String("Stop recording (R)")
                                 : juce::String("Record (R)"));

    // Only refresh the draft from the host when recording JUST stopped.
    // The rest of the time the popup is authoritative — Clear, delete, and
    // hand-typed edits won't be overwritten by polling.
    if (wasRecording_ && !rec)
    {
        if (fetchLivePath)
        {
            draft_ = fetchLivePath();
            rebuildRows();
            resized();
            repaint();
        }
    }
    wasRecording_ = rec;
}

// ─────────────────────────────────────────────────────────────────────────────

void CameraPathPopup::rebuildRows()
{
    for (auto& r : rows_)
    {
        if (r.time)      removeChildComponent(r.time.get());
        if (r.x)         removeChildComponent(r.x.get());
        if (r.y)         removeChildComponent(r.y.get());
        if (r.z)         removeChildComponent(r.z.get());
        if (r.yawDeg)    removeChildComponent(r.yawDeg.get());
        if (r.pitchDeg)  removeChildComponent(r.pitchDeg.get());
        if (r.modeBox)   removeChildComponent(r.modeBox.get());
        if (r.holdSec)   removeChildComponent(r.holdSec.get());
        if (r.deleteBtn) removeChildComponent(r.deleteBtn.get());
    }
    rows_.clear();
    rows_.reserve(draft_.size());

    auto styleField = [](juce::TextEditor& f, const char* restrict_)
    {
        f.setFont(juce::Font(12.5f));
        f.setColour(juce::TextEditor::backgroundColourId,     juce::Colour(kFieldBgColor));
        f.setColour(juce::TextEditor::textColourId,           juce::Colour(0xfff0f2fa));
        f.setColour(juce::TextEditor::outlineColourId,        juce::Colour(kFieldBdColor));
        f.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(kAccentColor));
        f.setColour(juce::TextEditor::highlightColourId,      juce::Colour(0xff3a5fbf));
        f.setIndents(6, 3);
        f.setInputRestrictions(16, restrict_);
        f.setSelectAllWhenFocused(true);
    };

    for (size_t i = 0; i < draft_.size(); ++i)
    {
        KeyRow r;
        r.time      = std::make_unique<juce::TextEditor>();
        r.x         = std::make_unique<juce::TextEditor>();
        r.y         = std::make_unique<juce::TextEditor>();
        r.z         = std::make_unique<juce::TextEditor>();
        r.yawDeg    = std::make_unique<juce::TextEditor>();
        r.pitchDeg  = std::make_unique<juce::TextEditor>();
        r.modeBox   = std::make_unique<juce::ComboBox>();
        r.holdSec   = std::make_unique<juce::TextEditor>();
        r.deleteBtn = std::make_unique<juce::TextButton>("X");

        styleField(*r.time,     "0123456789.");
        styleField(*r.x,        "-0123456789.");
        styleField(*r.y,        "-0123456789.");
        styleField(*r.z,        "-0123456789.");
        styleField(*r.yawDeg,   "-0123456789.");
        styleField(*r.pitchDeg, "-0123456789.");
        styleField(*r.holdSec,  "0123456789.");

        r.time   ->setText(juce::String(draft_[i].timeSec, 2), juce::dontSendNotification);
        r.x      ->setText(juce::String(draft_[i].pos.x, 2),    juce::dontSendNotification);
        r.y      ->setText(juce::String(draft_[i].pos.y, 2),    juce::dontSendNotification);
        r.z      ->setText(juce::String(draft_[i].pos.z, 2),    juce::dontSendNotification);
        r.yawDeg ->setText(juce::String(draft_[i].yawRad * kRadToDeg, 0),
                           juce::dontSendNotification);
        r.pitchDeg->setText(juce::String(draft_[i].pitchRad * kRadToDeg, 0),
                            juce::dontSendNotification);

        r.modeBox->addItem("Hold", 1);
        r.modeBox->addItem("Lerp", 2);
        r.modeBox->setSelectedId(draft_[i].mode == CameraKeyframe::Lerp ? 2 : 1,
                                 juce::dontSendNotification);
        r.modeBox->setColour(juce::ComboBox::backgroundColourId, juce::Colour(kFieldBgColor));
        r.modeBox->setColour(juce::ComboBox::textColourId,       juce::Colour(0xfff0f2fa));
        r.modeBox->setColour(juce::ComboBox::outlineColourId,    juce::Colour(kFieldBdColor));
        r.modeBox->setColour(juce::ComboBox::arrowColourId,      juce::Colour(0xff8b94ad));

        // Hold(s) editor — only meaningful when mode == Hold.  When the
        // user switches the mode, we toggle enable+greyout so the column
        // isn't a trap on Lerp rows.
        r.holdSec->setText(juce::String(draft_[i].holdDurationSec, 2),
                           juce::dontSendNotification);
        r.holdSec->setTooltip("How long this Hold pose occupies the timeline.  "
                              "0 = hold until the next keyframe.  Adds to the next "
                              "free slot used by + Hold @ cam now.");
        auto syncHoldEnabled = [boxPtr = r.modeBox.get(),
                                holdPtr = r.holdSec.get()]
        {
            const bool isHold = boxPtr->getSelectedId() != 2;
            holdPtr->setReadOnly(!isHold);
            holdPtr->setAlpha(isHold ? 1.0f : 0.45f);
            if (!isHold)
                holdPtr->setText("-", juce::dontSendNotification);
        };
        syncHoldEnabled();
        r.modeBox->onChange = [holdPtr = r.holdSec.get(),
                               boxPtr  = r.modeBox.get()]
        {
            const bool isHold = boxPtr->getSelectedId() != 2;
            holdPtr->setReadOnly(!isHold);
            holdPtr->setAlpha(isHold ? 1.0f : 0.45f);
            if (!isHold)
                holdPtr->setText("-", juce::dontSendNotification);
            else if (holdPtr->getText() == "-")
                holdPtr->setText("0.00", juce::dontSendNotification);
        };

        r.deleteBtn->setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff3a1c24));
        r.deleteBtn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff0c8c8));
        r.deleteBtn->setTooltip("Remove this keyframe");

        const int rowIndex = static_cast<int>(i);
        r.deleteBtn->onClick = [this, rowIndex]
        {
            pullValuesFromRows();
            if (rowIndex >= 0 && rowIndex < (int) draft_.size())
            {
                draft_.erase(draft_.begin() + rowIndex);
                rebuildRows();
                resized();
                repaint();
            }
        };

        addAndMakeVisible(*r.time);
        addAndMakeVisible(*r.x);
        addAndMakeVisible(*r.y);
        addAndMakeVisible(*r.z);
        addAndMakeVisible(*r.yawDeg);
        addAndMakeVisible(*r.pitchDeg);
        addAndMakeVisible(*r.modeBox);
        addAndMakeVisible(*r.holdSec);
        addAndMakeVisible(*r.deleteBtn);

        rows_.push_back(std::move(r));
    }

    layoutRows();
    repaint();
}

void CameraPathPopup::pullValuesFromRows()
{
    for (size_t i = 0; i < rows_.size() && i < draft_.size(); ++i)
    {
        auto& r = rows_[i];
        auto& d = draft_[i];
        if (r.time)    d.timeSec  = r.time->getText().getDoubleValue();
        if (r.x)       d.pos.x    = (float) r.x->getText().getDoubleValue();
        if (r.y)       d.pos.y    = (float) r.y->getText().getDoubleValue();
        if (r.z)       d.pos.z    = (float) r.z->getText().getDoubleValue();
        if (r.yawDeg)  d.yawRad   = (float) r.yawDeg->getText().getDoubleValue()   * kDegToRad;
        if (r.pitchDeg)d.pitchRad = (float) r.pitchDeg->getText().getDoubleValue() * kDegToRad;
        if (r.modeBox) d.mode = (r.modeBox->getSelectedId() == 2)
                              ? CameraKeyframe::Lerp
                              : CameraKeyframe::Hold;

        if (r.holdSec && d.mode == CameraKeyframe::Hold)
        {
            const auto txt = r.holdSec->getText();
            d.holdDurationSec = (txt == "-")
                ? 0.0f
                : juce::jmax(0.0f, (float) txt.getDoubleValue());
        }
        else
        {
            d.holdDurationSec = 0.0f;
        }
    }
}

void CameraPathPopup::scrollToBottom()
{
    const int listTop = kHeaderH;
    const int listBot = kHeight - kFooterH - 4;
    const int listH   = listBot - listTop;
    const int totalH  = static_cast<int>(rows_.size()) * (kRowH + kRowGap);
    scrollY_ = std::max(0, totalH - listH);
}

void CameraPathPopup::layoutRows()
{
    const int listTop = kHeaderH;
    const int listBot = kHeight - kFooterH - 4;
    const int listH   = listBot - listTop;

    const int colTimeX  = kPad;
    const int colXX     = colTimeX + kColTimeW + kColGap;
    const int colYX     = colXX    + kColXYZW  + kColGap;
    const int colZX     = colYX    + kColXYZW  + kColGap;
    const int colYawX   = colZX    + kColXYZW  + kColGap;
    const int colPitchX = colYawX  + kColAngleW + kColGap;
    const int colModeX  = colPitchX + kColAngleW + kColGap;
    const int colHoldX  = colModeX  + kColModeW  + kColGap;
    const int delX      = kWidth - kPad - kDelW;

    int y = listTop - scrollY_;
    for (auto& r : rows_)
    {
        const bool offscreen = (y + kRowH <= listTop || y >= listBot);
        if (offscreen)
        {
            if (r.time)      r.time->setBounds(0, 0, 0, 0);
            if (r.x)         r.x->setBounds(0, 0, 0, 0);
            if (r.y)         r.y->setBounds(0, 0, 0, 0);
            if (r.z)         r.z->setBounds(0, 0, 0, 0);
            if (r.yawDeg)    r.yawDeg->setBounds(0, 0, 0, 0);
            if (r.pitchDeg)  r.pitchDeg->setBounds(0, 0, 0, 0);
            if (r.modeBox)   r.modeBox->setBounds(0, 0, 0, 0);
            if (r.holdSec)   r.holdSec->setBounds(0, 0, 0, 0);
            if (r.deleteBtn) r.deleteBtn->setBounds(0, 0, 0, 0);
        }
        else
        {
            r.time     ->setBounds(colTimeX,  y, kColTimeW,  kRowH - 4);
            r.x        ->setBounds(colXX,     y, kColXYZW,   kRowH - 4);
            r.y        ->setBounds(colYX,     y, kColXYZW,   kRowH - 4);
            r.z        ->setBounds(colZX,     y, kColXYZW,   kRowH - 4);
            r.yawDeg   ->setBounds(colYawX,   y, kColAngleW, kRowH - 4);
            r.pitchDeg ->setBounds(colPitchX, y, kColAngleW, kRowH - 4);
            r.modeBox  ->setBounds(colModeX,  y, kColModeW,  kRowH - 4);
            r.holdSec  ->setBounds(colHoldX,  y, kColHoldW,  kRowH - 4);
            r.deleteBtn->setBounds(delX,      y, kDelW,      kRowH - 4);
        }
        y += kRowH + kRowGap;
    }

    const int totalH = static_cast<int>(rows_.size()) * (kRowH + kRowGap);
    const int maxScroll = std::max(0, totalH - listH);
    if (scrollY_ > maxScroll)
    {
        scrollY_ = maxScroll;
        layoutRows();
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void CameraPathPopup::resized()
{
    const int titleY = kPad + 4;
    titleLabel_   .setBounds(kPad, titleY,      kWidth - 2 * kPad, 22);
    subtitleLabel_.setBounds(kPad, titleY + 22, kWidth - 2 * kPad, 32);

    const int colHeaderY = kHeaderH - 22;
    const int colTimeX  = kPad;
    const int colXX     = colTimeX + kColTimeW + kColGap;
    const int colYX     = colXX    + kColXYZW  + kColGap;
    const int colZX     = colYX    + kColXYZW  + kColGap;
    const int colYawX   = colZX    + kColXYZW  + kColGap;
    const int colPitchX = colYawX  + kColAngleW + kColGap;
    const int colModeX  = colPitchX + kColAngleW + kColGap;
    const int colHoldX  = colModeX  + kColModeW  + kColGap;

    headerTimeLabel_ .setBounds(colTimeX,  colHeaderY, kColTimeW,  14);
    headerXLabel_    .setBounds(colXX,     colHeaderY, kColXYZW,   14);
    headerYLabel_    .setBounds(colYX,     colHeaderY, kColXYZW,   14);
    headerZLabel_    .setBounds(colZX,     colHeaderY, kColXYZW,   14);
    headerYawLabel_  .setBounds(colYawX,   colHeaderY, kColAngleW, 14);
    headerPitchLabel_.setBounds(colPitchX, colHeaderY, kColAngleW, 14);
    headerModeLabel_ .setBounds(colModeX,  colHeaderY, kColModeW,  14);
    headerHoldLabel_ .setBounds(colHoldX,  colHeaderY, kColHoldW,  14);

    emptyHintLabel_.setBounds(kPad, kHeaderH + 16,
                              kWidth - 2 * kPad, 48);

    const int footerTop = kHeight - kFooterH + 6;
    hintLabel_.setBounds(kPad, footerTop, kWidth - 2 * kPad, 44);

    // Row 0: capture-interval selector (right-aligned)
    const int row0Y       = footerTop + 50;
    const int comboH      = 24;
    const int comboW      = 130;
    const int comboLblW   = 90;
    captureIntervalLabel_.setBounds(kWidth - kPad - comboW - comboLblW - 4,
                                    row0Y, comboLblW, comboH);
    captureIntervalBox_.setBounds(kWidth - kPad - comboW, row0Y, comboW, comboH);

    // Row 1: the three action buttons
    const int rowY    = row0Y + comboH + 4;
    const int actionH = 26;
    const int third   = (kWidth - 2 * kPad - 16) / 3;
    addHoldBtn_ .setBounds(kPad,                       rowY, third, actionH);
    recordBtn_  .setBounds(kPad + third + 8,           rowY, third, actionH);
    clearButton_.setBounds(kPad + 2 * (third + 8),     rowY, third, actionH);

    // Row 2: Cancel / Apply
    const int btnY = rowY + actionH + 6;
    const int half = (kWidth - 2 * kPad - 8) / 2;
    cancelButton_.setBounds(kPad,            btnY, half, actionH);
    applyButton_ .setBounds(kPad + half + 8, btnY, half, actionH);

    layoutRows();
}

void CameraPathPopup::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(kBgColor));
    g.fillRoundedRectangle(bounds, 10.f);
    g.setColour(juce::Colour(kBorderColor));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.f, 1.f);

    g.setColour(juce::Colour(kAccentColor));
    g.fillRoundedRectangle(bounds.withHeight(3.f).reduced(2.f, 0.f), 1.5f);

    g.setColour(juce::Colour(0xff222a3e));
    g.fillRect(kPad, kHeaderH - 4, kWidth - 2 * kPad, 1);
    g.fillRect(kPad, kHeight - kFooterH - 2, kWidth - 2 * kPad, 1);

    const int listTop = kHeaderH;
    const int listBot = kHeight - kFooterH - 4;

    g.saveState();
    g.reduceClipRegion(0, listTop, kWidth, listBot - listTop);

    int y = listTop - scrollY_;
    for (size_t i = 0; i < rows_.size(); ++i)
    {
        const bool offscreen = (y + kRowH <= listTop || y >= listBot);
        if (!offscreen)
        {
            juce::Rectangle<float> rowR((float) kPad - 6.f, (float) y - 3.f,
                                        (float) (kWidth - 2 * kPad) + 12.f,
                                        (float) (kRowH - 4) + 6.f);
            g.setColour(juce::Colour(kRowBgColor));
            g.fillRoundedRectangle(rowR, 5.f);
            g.setColour(juce::Colour(kRowBdColor));
            g.drawRoundedRectangle(rowR, 5.f, 1.f);

            g.setColour(juce::Colour(0xff5b6685));
            g.setFont(juce::Font(10.5f, juce::Font::bold));
            g.drawText("#" + juce::String((int) i + 1),
                       0, y, kPad + 2, kRowH - 4,
                       juce::Justification::centredRight);
        }
        y += kRowH + kRowGap;
    }

    g.restoreState();

    emptyHintLabel_.setVisible(rows_.empty());

    const int listH  = listBot - listTop;
    const int totalH = std::max(0, (int) rows_.size() * (kRowH + kRowGap));
    if (totalH > listH)
    {
        const float trackX = (float) (kWidth - 7);
        const float trackY = (float) listTop + 2.f;
        const float trackH = (float) listH - 4.f;
        g.setColour(juce::Colour(0x33ffffff));
        g.fillRoundedRectangle(trackX, trackY, 3.f, trackH, 1.5f);

        const float thumbH = juce::jmax(24.f,
                                        trackH * (float) listH / (float) totalH);
        const float thumbY = trackY
            + (trackH - thumbH) * (float) scrollY_
                                / (float) juce::jmax(1, totalH - listH);
        g.setColour(juce::Colour(0xffaac8e8).withAlpha(0.6f));
        g.fillRoundedRectangle(trackX, thumbY, 3.f, thumbH, 1.5f);
    }
}

bool CameraPathPopup::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        hide();
        if (onDismiss) onDismiss();
        return true;
    }
    if (k == juce::KeyPress::returnKey)
    {
        applyButton_.triggerClick();
        return true;
    }
    if (k.getKeyCode() == 'r' || k.getKeyCode() == 'R')
    {
        if (onRecordToggle) onRecordToggle();
        return true;
    }
    return false;
}

void CameraPathPopup::mouseWheelMove(const juce::MouseEvent&,
                                     const juce::MouseWheelDetails& wheel)
{
    const int listTop = kHeaderH;
    const int listBot = kHeight - kFooterH - 4;
    const int listH   = listBot - listTop;
    const int totalH  = std::max(0, (int) rows_.size() * (kRowH + kRowGap));
    const int maxScroll = std::max(0, totalH - listH);
    if (maxScroll <= 0) return;

    const int step = static_cast<int>(wheel.deltaY * -80.f);
    scrollY_ = std::clamp(scrollY_ + step, 0, maxScroll);
    layoutRows();
    repaint();
}
