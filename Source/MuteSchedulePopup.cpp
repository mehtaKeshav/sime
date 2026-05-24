// ─────────────────────────────────────────────────────────────────────────────
// MuteSchedulePopup.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "MuteSchedulePopup.h"

namespace
{
    constexpr juce::uint32 kBgColor      = 0xf012141cu;
    constexpr juce::uint32 kBorderColor  = 0xff2c3550u;
    constexpr juce::uint32 kAccentColor  = 0xff5b7ce6u;
    constexpr juce::uint32 kFieldBgColor = 0xff181a24u;
    constexpr juce::uint32 kFieldBdColor = 0xff2f3447u;
    constexpr juce::uint32 kRowBgColor   = 0xff161a26u;
    constexpr juce::uint32 kRowBdColor   = 0xff262d44u;
}

MuteSchedulePopup::MuteSchedulePopup()
{
    setSize(kWidth, kHeight);
    setWantsKeyboardFocus(true);

    titleLabel_.setText("Mute Schedule", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(15.f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xfff0f2fa));
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel_);

    subtitleLabel_.setFont(juce::Font(11.5f));
    subtitleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8b94ad));
    subtitleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(subtitleLabel_);

    auto styleHeader = [](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(10.5f, juce::Font::bold));
        l.setColour(juce::Label::textColourId, juce::Colour(0xff6b7494));
        l.setJustificationType(juce::Justification::centredLeft);
    };
    styleHeader(headerStartLabel_, "START (S)");
    styleHeader(headerDurLabel_,   "DURATION (S)");
    addAndMakeVisible(headerStartLabel_);
    addAndMakeVisible(headerDurLabel_);

    hintLabel_.setText("Each window silences this block while the playhead is inside it. "
                       "Movement and visuals keep playing.",
                       juce::dontSendNotification);
    hintLabel_.setFont(juce::Font(11.f));
    hintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8b94ad));
    hintLabel_.setJustificationType(juce::Justification::centredLeft);
    hintLabel_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(hintLabel_);

    emptyHintLabel_.setText("No mute windows yet. Click \"+ Add Window\" to schedule one.",
                            juce::dontSendNotification);
    emptyHintLabel_.setFont(juce::Font(12.f, juce::Font::italic));
    emptyHintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff7a83a3));
    emptyHintLabel_.setJustificationType(juce::Justification::centred);
    addChildComponent(emptyHintLabel_);

    addButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff242a3c));
    addButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    addButton_.onClick = [this]
    {
        pullValuesFromRows();
        windowsDraft_.push_back({ 0.0, 1.0 });
        rebuildRows();
        // Scroll to the bottom so the new row is visible.
        const int listH    = kHeight - kHeaderH - kFooterH - 4;
        const int rowsTotal = static_cast<int>(rows_.size()) * (kRowH + kRowGap);
        scrollY_ = std::max(0, rowsTotal - listH);
        resized();
        repaint();
    };
    addAndMakeVisible(addButton_);

    clearButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff242a3c));
    clearButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    clearButton_.onClick = [this]
    {
        windowsDraft_.clear();
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

        // Drop empty / non-positive-duration entries so we never persist
        // bogus rows the user left blank.
        std::vector<MuteWindow> cleaned;
        cleaned.reserve(windowsDraft_.size());
        for (const auto& w : windowsDraft_)
            if (w.durationSec > 0.0)
                cleaned.push_back({ std::max(0.0, w.startSec), w.durationSec });

        if (onApply)
            onApply(editingSerial_, cleaned);

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

MuteSchedulePopup::~MuteSchedulePopup()
{
    removeFromDesktop();
}

// ─────────────────────────────────────────────────────────────────────────────

void MuteSchedulePopup::setSchedule(int blockSerial,
                                    const juce::String& blockName,
                                    const std::vector<MuteWindow>& windows)
{
    editingSerial_ = blockSerial;
    editingName_   = blockName;
    windowsDraft_  = windows;
    scrollY_       = 0;

    titleLabel_.setText("Mute Schedule", juce::dontSendNotification);
    subtitleLabel_.setText(blockName.isEmpty() ? juce::String("(no block selected)")
                                               : blockName,
                           juce::dontSendNotification);

    rebuildRows();
}

void MuteSchedulePopup::showAt(juce::Point<int> screenPos)
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
}

void MuteSchedulePopup::hide()
{
    setVisible(false);
}

// ─────────────────────────────────────────────────────────────────────────────

void MuteSchedulePopup::rebuildRows()
{
    // Tear down old editors so removeChildComponent fires for each one
    // before we drop them; safer than letting destruction race with paint.
    for (auto& r : rows_)
    {
        if (r.start)     removeChildComponent(r.start.get());
        if (r.duration)  removeChildComponent(r.duration.get());
        if (r.deleteBtn) removeChildComponent(r.deleteBtn.get());
    }
    rows_.clear();
    rows_.reserve(windowsDraft_.size());

    for (size_t i = 0; i < windowsDraft_.size(); ++i)
    {
        WindowRow r;
        r.start     = std::make_unique<juce::TextEditor>();
        r.duration  = std::make_unique<juce::TextEditor>();
        r.deleteBtn = std::make_unique<juce::TextButton>("X");

        auto styleField = [](juce::TextEditor& f)
        {
            f.setFont(juce::Font(13.f));
            f.setColour(juce::TextEditor::backgroundColourId,     juce::Colour(kFieldBgColor));
            f.setColour(juce::TextEditor::textColourId,           juce::Colour(0xfff0f2fa));
            f.setColour(juce::TextEditor::outlineColourId,        juce::Colour(kFieldBdColor));
            f.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(kAccentColor));
            f.setColour(juce::TextEditor::highlightColourId,      juce::Colour(0xff3a5fbf));
            f.setIndents(8, 4);
            f.setInputRestrictions(16, "0123456789.");
            f.setSelectAllWhenFocused(true);
        };
        styleField(*r.start);
        styleField(*r.duration);

        r.start->setText(juce::String(windowsDraft_[i].startSec, 3),
                         juce::dontSendNotification);
        r.duration->setText(juce::String(windowsDraft_[i].durationSec, 3),
                            juce::dontSendNotification);

        r.deleteBtn->setColour(juce::TextButton::buttonColourId,
                               juce::Colour(0xff3a1c24));
        r.deleteBtn->setColour(juce::TextButton::textColourOffId,
                               juce::Colour(0xfff0c8c8));
        r.deleteBtn->setTooltip("Remove this window");

        const int rowIndex = static_cast<int>(i);
        r.deleteBtn->onClick = [this, rowIndex]
        {
            pullValuesFromRows();
            if (rowIndex >= 0 && rowIndex < (int) windowsDraft_.size())
            {
                windowsDraft_.erase(windowsDraft_.begin() + rowIndex);
                rebuildRows();
                resized();
                repaint();
            }
        };

        addAndMakeVisible(*r.start);
        addAndMakeVisible(*r.duration);
        addAndMakeVisible(*r.deleteBtn);

        rows_.push_back(std::move(r));
    }

    layoutRows();
    repaint();
}

void MuteSchedulePopup::pullValuesFromRows()
{
    for (size_t i = 0; i < rows_.size() && i < windowsDraft_.size(); ++i)
    {
        windowsDraft_[i].startSec    = rows_[i].start
            ? rows_[i].start->getText().getDoubleValue() : 0.0;
        windowsDraft_[i].durationSec = rows_[i].duration
            ? rows_[i].duration->getText().getDoubleValue() : 0.0;
    }
}

void MuteSchedulePopup::layoutRows()
{
    const int listTop = kHeaderH;
    const int listBot = kHeight - kFooterH - 4;
    const int listH   = listBot - listTop;

    const int colStartX = kPad;
    const int colStartW = 130;
    const int colDurX   = colStartX + colStartW + 12;
    const int colDurW   = 130;
    const int delW      = 28;
    const int delX      = kWidth - kPad - delW;

    int y = listTop - scrollY_;
    for (auto& r : rows_)
    {
        const bool offscreen = (y + kRowH <= listTop || y >= listBot);
        if (offscreen)
        {
            if (r.start)     r.start->setBounds(0, 0, 0, 0);
            if (r.duration)  r.duration->setBounds(0, 0, 0, 0);
            if (r.deleteBtn) r.deleteBtn->setBounds(0, 0, 0, 0);
        }
        else
        {
            r.start    ->setBounds(colStartX, y, colStartW, kRowH - 4);
            r.duration ->setBounds(colDurX,   y, colDurW,   kRowH - 4);
            r.deleteBtn->setBounds(delX,      y, delW,      kRowH - 4);
        }
        y += kRowH + kRowGap;
    }

    listContentBottom_ = y + scrollY_;

    // Clamp scroll if everything fit / overshot.
    const int totalH = std::max(0, (int) rows_.size() * (kRowH + kRowGap));
    const int maxScroll = std::max(0, totalH - listH);
    if (scrollY_ > maxScroll)
    {
        scrollY_ = maxScroll;
        layoutRows();
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void MuteSchedulePopup::resized()
{
    // ── Header ─────────────────────────────────────────────────────────────
    const int titleY    = kPad + 4;
    titleLabel_   .setBounds(kPad, titleY,      kWidth - 2 * kPad, 22);
    subtitleLabel_.setBounds(kPad, titleY + 22, kWidth - 2 * kPad, 18);

    // Column headers sit just above the list area (kHeaderH boundary).
    const int colHeaderY = kHeaderH - 22;
    const int colStartX  = kPad;
    const int colStartW  = 130;
    const int colDurX    = colStartX + colStartW + 12;
    const int colDurW    = 130;

    headerStartLabel_.setBounds(colStartX, colHeaderY, colStartW, 14);
    headerDurLabel_  .setBounds(colDurX,   colHeaderY, colDurW,   14);

    // Empty-state hint covers the list area when there are no rows.
    emptyHintLabel_.setBounds(kPad, kHeaderH + 12,
                              kWidth - 2 * kPad, 32);

    // ── Footer ─────────────────────────────────────────────────────────────
    const int footerTop = kHeight - kFooterH + 6;
    hintLabel_.setBounds(kPad, footerTop, kWidth - 2 * kPad, 32);

    const int rowY     = footerTop + 36;
    const int actionH  = 26;
    const int halfW    = (kWidth - 2 * kPad - 8) / 2;
    addButton_  .setBounds(kPad,            rowY, halfW, actionH);
    clearButton_.setBounds(kPad + halfW + 8, rowY, halfW, actionH);

    const int btnY = rowY + actionH + 6;
    cancelButton_.setBounds(kPad,            btnY, halfW, actionH);
    applyButton_ .setBounds(kPad + halfW + 8, btnY, halfW, actionH);

    layoutRows();
}

void MuteSchedulePopup::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Card background + outer stroke.
    g.setColour(juce::Colour(kBgColor));
    g.fillRoundedRectangle(bounds, 10.f);
    g.setColour(juce::Colour(kBorderColor));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.f, 1.f);

    // Accent stripe under the very top of the card.
    g.setColour(juce::Colour(kAccentColor));
    g.fillRoundedRectangle(bounds.withHeight(3.f).reduced(2.f, 0.f), 1.5f);

    // Divider between the header band and the list area.
    g.setColour(juce::Colour(0xff222a3e));
    g.fillRect(kPad, kHeaderH - 4, kWidth - 2 * kPad, 1);

    // Divider above the footer.
    g.setColour(juce::Colour(0xff222a3e));
    g.fillRect(kPad, kHeight - kFooterH - 2, kWidth - 2 * kPad, 1);

    // Row backgrounds — drawn in paint so we don't have to add a Component
    // per row purely for visuals.  Mirrors layoutRows() math exactly.
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

            // Row number badge on the far left of each row.
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

    // Tiny scrollbar indicator on the right edge of the list area.
    const int listH = listBot - listTop;
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

bool MuteSchedulePopup::keyPressed(const juce::KeyPress& k)
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
    return false;
}

void MuteSchedulePopup::mouseWheelMove(const juce::MouseEvent&,
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
