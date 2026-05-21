#include "TimelineComponent.h"

TimelineComponent::TimelineComponent()
{
    setWantsKeyboardFocus(true);
}

void TimelineComponent::setBlocks(const std::vector<BlockEntry>& blocks)
{
    regions_.clear();

    int trackIndex = 0;

    for (const auto& b : blocks)
    {
        regions_.push_back({ 
                b.serial,
                -1,
                b.blockType,
                b.startTimeSec,
                b.durationSec,
                trackIndex,
                juce::String(b.serial),
                b.colour
            });
        

        for (int i = 0; i < (int)b.timesList.size(); ++i)
        {
            const auto& t = b.timesList[i];
            regions_.push_back({
                b.serial,
                i,
                b.blockType,
                t.startTimeSec,
                t.durationSec,
                trackIndex,
                juce::String(b.serial),
                b.colour
            });
        }

        ++trackIndex;
    }

    repaint();
}

void TimelineComponent::setZoom(double pixelsPerSecond)
{
    pixelsPerSecond_ = std::clamp(pixelsPerSecond, 20.0, 500.0);
    repaint();
}

void TimelineComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.fillAll(juce::Colour(0xff1a1a1a));

    auto rulerArea = bounds.removeFromTop(kRulerHeight);
    auto tracksArea = bounds;

    // Tracks are clipped so they can never draw over ruler
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(tracksArea);

        paintBeatGrid(g, tracksArea);

        // Draw blocks / regions on top of grid
        paintTracks(g, tracksArea);
    }


    paintTimeRuler(g, rulerArea);

    paintPlayhead(g, getLocalBounds());
}


void TimelineComponent::paintBeatGrid(juce::Graphics& g,
                                      juce::Rectangle<int> tracksArea)
{
    const double gridStep = secondsPerSubdivision();

    if (gridStep <= 0.0 || pixelsPerSecond_ <= 0.0f)
        return;

    const double visibleStart = viewStartTime_;
    const double visibleEnd =
        viewStartTime_ + static_cast<double>(tracksArea.getWidth()) / pixelsPerSecond_;

    const double firstGridTime =
        std::floor(visibleStart / gridStep) * gridStep;

    for (double t = firstGridTime; t <= visibleEnd; t += gridStep)
    {
        const float x = timeToX(t);

        const int gridIndex =
            static_cast<int>(std::round(t / gridStep));

        if (gridIndex % 16 == 0)
        {
            // every 16th line - blue highlight
            g.setColour(juce::Colour(0xff60a5fa).withAlpha(0.45f));
        }
        else if (gridIndex % 8 == 0)
        {
            // every 8th line - light gray
            g.setColour(juce::Colours::lightgrey.withAlpha(0.28f));
        }
        else if (gridIndex % 4 == 0)
        {
            // every 4th line - dark gray
            g.setColour(juce::Colours::darkgrey.withAlpha(0.20f));
        }
        else
        {
            // small subdivisions
            g.setColour(juce::Colours::white.withAlpha(0.06f));
        }

        g.drawVerticalLine(
            static_cast<int>(x),
            static_cast<float>(tracksArea.getY()),
            static_cast<float>(tracksArea.getBottom())
        );
    }
}


void TimelineComponent::paintTimeRuler(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect(area);
    
    g.setColour(juce::Colour(0xff444444));
    g.drawLine(area.getX(), area.getBottom() - 1, 
               area.getRight(), area.getBottom() - 1);
    
    // Draw time markers
    const double majorInterval = 2.0; // Every 2 seconds
    const double minorInterval = 0.5; // Every 0.5 seconds
    
    double startTime = std::floor(viewStartTime_ / majorInterval) * majorInterval;
    double endTime = viewStartTime_ + (getWidth() / pixelsPerSecond_);
    
    g.setFont(11.0f);
    
    for (double t = startTime; t <= endTime; t += minorInterval)
    {
        int x = (int)timeToX(t);
        
        bool isMajor = (std::fmod(t, majorInterval) < 0.01);
        
        if (isMajor)
        {
            // Major tick (every 2 sec)
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawVerticalLine(x, area.getY() + 20, area.getBottom());
            
            // Time label
            int minutes = (int)(t / 60.0);
            int seconds = (int)std::fmod(t, 60.0);
            juce::String label = juce::String::formatted("%d:%02d", minutes, seconds);
            
            g.drawText(label, x - 20, area.getY() + 2, 40, 16, 
                      juce::Justification::centred);
        }
        else
        {
            // Minor tick (every 0.5 sec)
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.drawVerticalLine(x, area.getY() + 25, area.getBottom());
        }
    }
}

void TimelineComponent::paintTracks(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Determine max track index
    int maxTrack = 0;
    for (const auto& region : regions_)
        maxTrack = std::max(maxTrack, region.trackIndex);
    
    // Draw track backgrounds
    for (int i = 0; i <= maxTrack; ++i)
    {
        int y = area.getY() + i * (kTrackHeight + kTrackGap) - (int)verticalScroll_;
        
        juce::Colour trackBg = (i % 2 == 0)
            ? juce::Colour(0xff1e1e1e).withAlpha(0.25f)
            : juce::Colour(0xff242424).withAlpha(0.25f);

        g.setColour(trackBg);
        g.fillRect(area.getX(), y, area.getWidth(), kTrackHeight);
    }
    
    // Draw block regions
    for (const auto& region : regions_)
    {
        int x = (int)timeToX(region.startTimeSec);
        int w = (int)(region.durationSec * pixelsPerSecond_);
        int y = area.getY() + region.trackIndex * (kTrackHeight + kTrackGap) + 2 - (int)verticalScroll_;
        int h = kTrackHeight - 4;
        
        // Skip if off-screen
        if (x + w < area.getX() || x > area.getRight())
            continue;
        
        juce::Rectangle<int> blockRect(x, y, w, h);
        
        // Block color
        juce::Colour color = juce::Colour::fromFloatRGBA(region.color.x, region.color.y, region.color.z, 1.0f);
        g.setColour(color.withAlpha(0.9f));
        g.fillRoundedRectangle(blockRect.toFloat(), 3.0f);
        
        // Border
        g.setColour(color.brighter(0.3f));
        g.drawRoundedRectangle(blockRect.toFloat(), 3.0f, 1.5f);
        
        // Label
        if (w > 50) // Only show label if wide enough
        {
            g.setColour(juce::Colours::white);
            g.setFont(12.0f);
            g.drawText(region.label, blockRect.reduced(4), 
                      juce::Justification::centredLeft, true);
        }
    }
}

void TimelineComponent::paintPlayhead(juce::Graphics& g, juce::Rectangle<int> area)
{
    int x = (int) timeToX(currentTime_);

    g.setColour(juce::Colours::yellow);
    g.drawLine(x, kRulerHeight, x, area.getBottom(), 2.0f);

    juce::Path triangle;
    triangle.addTriangle(x - 6, kRulerHeight - 2,
                         x + 6, kRulerHeight - 2,
                         x,     kRulerHeight + 6);

    g.fillPath(triangle);
}

float TimelineComponent::timeToX(double timeSeconds) const
{
    return static_cast<float>((timeSeconds - viewStartTime_) * pixelsPerSecond_);
}

double TimelineComponent::xToTime(float x) const
{
    return viewStartTime_ + (x / pixelsPerSecond_);
}

void TimelineComponent::resized()
{
    playheadAnchorX_ = getWidth() / 3;
}

void TimelineComponent::mouseWheelMove(const juce::MouseEvent& e, 
                                       const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        // Zoom
        double zoomFactor = 1.0 + (w.deltaY * 0.5);
        setZoom(pixelsPerSecond_ * zoomFactor);
    }
    else
    {
        const int totalContentHeight =
            (int)regions_.size() * (kTrackHeight + kTrackGap);

        const int visibleHeight = getHeight() - kRulerHeight;

        const double maxScroll =
            std::max(0, totalContentHeight - visibleHeight);

        // Trackpad: deltaY scrolls vertically, deltaX scrolls horizontally
        verticalScroll_ = std::clamp(
            verticalScroll_ - w.deltaY * 120.0,
            0.0,
            maxScroll
        );

        viewStartTime_ = std::max(0.0, viewStartTime_ - w.deltaX * 2.0);

        repaint();
    }
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingPlayhead_)
    {
        double newTime = juce::jmax(0.0, xToTime(e.x));

        currentTime_ = newTime;

        if (onPlayheadMoved)
            onPlayheadMoved(newTime);

        repaint();
        return;
    }

    if (selectedBlock_ != -1 && dragMode_ != DragMode::None)
    {
        const double currentMouseTime = xToTime(e.x);
        const double delta = currentMouseTime - dragStartTime_;

        for (auto& r : regions_)
        {
            if (r.serial != selectedBlock_ || r.timeIndex != selectedTimeIndex_)
                continue;

            double newStart = r.startTimeSec;
            double newDuration = r.durationSec;

            if (dragMode_ == DragMode::Move)
            {
                newStart = snapTime(originalStart_ + delta);
                newStart = juce::jmax(0.0, newStart);
                newDuration = originalDuration_;
            }
            else if (dragMode_ == DragMode::ResizeLeft)
            {
                const double oldEnd = originalStart_ + originalDuration_;
                newStart = snapTime(originalStart_ + delta);
                newStart = juce::jlimit(0.0, oldEnd - 0.05, newStart);
                newDuration = oldEnd - newStart;
            }
            else if (dragMode_ == DragMode::ResizeRight)
            {
                const double newEnd = snapTime(originalStart_ + originalDuration_ + delta);
                newDuration = newEnd - originalStart_;
            }

            if (!canPlaceRegion(r, newStart, newDuration))
                return;

            r.startTimeSec = newStart;
            r.durationSec = newDuration;

            selectedRegion_ = r;

            if (onBlockEdited)
                onBlockEdited(r.serial, r.timeIndex, newStart, newDuration);

            repaint();
            return;
        }
    }

    if (isPanningTimeline_)
    {
        const int dx = e.x - lastDragX_;
        const int dy = e.y - lastDragY_;

        viewStartTime_ = std::max(
            0.0,
            viewStartTime_ - static_cast<double>(dx) / pixelsPerSecond_
        );

        const int totalContentHeight =
            static_cast<int>(regions_.size()) * (kTrackHeight + kTrackGap);

        const int visibleHeight = getHeight() - kRulerHeight;

        const double maxScroll =
            std::max(0, totalContentHeight - visibleHeight);

        verticalScroll_ = std::clamp(
            verticalScroll_ - static_cast<double>(dy),
            0.0,
            maxScroll
        );

        lastDragX_ = e.x;
        lastDragY_ = e.y;

        repaint();
    }
}

void TimelineComponent::mouseUp(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);

    if (draggingPlayhead_)
    {
        draggingPlayhead_ = false;
        return;
    }

    isPanningTimeline_ = false;
    isUserPanning_ = false;

    dragMode_ = DragMode::None;
}

void TimelineComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    int playheadX = (int) timeToX(currentTime_);
    if (std::abs(e.x - playheadX) < kPlayheadHitWidth){
        draggingPlayhead_ = true;
        return;
    }
    if (e.y <= kRulerHeight)
    {
        double newTime = xToTime(e.x);
        newTime = juce::jmax(0.0, newTime);

        currentTime_ = newTime;

        if (onPlayheadMoved)
            onPlayheadMoved(newTime);

        repaint();
        return;
    }

    isPanningTimeline_ = false;

    for (auto& r : regions_)
    {
        int trackY = kRulerHeight
                   + r.trackIndex * (kTrackHeight + kTrackGap)
                   - (int)verticalScroll_;

        int x = timeToX(r.startTimeSec);
        int width = (int)(r.durationSec * pixelsPerSecond_);

        juce::Rectangle<int> rect(x, trackY, width, kTrackHeight);

        if (rect.contains(e.getPosition()))
        {
            selectedRegion_ = r;
            hasSelectedRegion_ = true;
            selectedBlock_ = r.serial;
            selectedTimeIndex_ = r.timeIndex;

            if (onRectRegionClicked)
                onRectRegionClicked(r.serial);

            originalStart_ = r.startTimeSec;
            originalDuration_ = r.durationSec;
            dragStartTime_ = xToTime(e.x);
                
            const int edgeThreshold = 5;

            if (std::abs(e.x - rect.getX()) < edgeThreshold)
                dragMode_ = DragMode::ResizeLeft;
            else if (std::abs(e.x - rect.getRight()) < edgeThreshold)
                dragMode_ = DragMode::ResizeRight;
            else{
                dragMode_ = DragMode::Move;
            }

            return;
        }
    }

    selectedBlock_ = -1;
    dragMode_ = DragMode::None;

    if (selectedBlock_ == -1)
    {
        followPlayhead_ = false;

        isPanningTimeline_ = true;
        isUserPanning_ = true;

        lastDragX_ = e.x;
        lastDragY_ = e.y;
    }
}

void TimelineComponent::setCurrentTime(double timeSec)
{
    currentTime_ = juce::jmax(0.0, timeSec);
    if (isPlaying_)
    {
        float x = timeToX(currentTime_);

        if (x > playheadAnchorX_)
        {
            viewStartTime_ = currentTime_ - (playheadAnchorX_ / pixelsPerSecond_);
            viewStartTime_ = juce::jmax(0.0, viewStartTime_);
        }
    }

    repaint();
}

void TimelineComponent::enableFollowPlayhead()
{
    followPlayhead_ = true;
}

void TimelineComponent::setPlaying(bool playing)
{
    isPlaying_ = playing;

    if (playing)
    {
        followPlayhead_ = true;
        isUserPanning_ = false;
        isPanningTimeline_ = false;
    }
}

double TimelineComponent::secondsPerBeat() const
{
    return 60.0 / bpm_;
}

double TimelineComponent::secondsPerSubdivision() const
{
    return secondsPerBeat() * (4.0 / subdivision_);
}


double TimelineComponent::snapTime(double timeSeconds) const
{
    if (!snapToGrid_)
        return timeSeconds;

    const double grid = secondsPerSubdivision();

    if (grid <= 0.0)
        return timeSeconds;

    return std::round(timeSeconds / grid) * grid;
}

void TimelineComponent::setBpm(double newBpm)
{
    bpm_ = juce::jlimit(40.0, 240.0, newBpm);
    repaint();
}

int TimelineComponent::yToTrackIndex(int y)
{
    int localY = y + (int)verticalScroll_ - kRulerHeight;

    if (localY < 0)
        return -1;

    int rowHeight = kTrackHeight + kTrackGap;
    int track = localY / rowHeight;

    int yInsideTrack = localY % rowHeight;

    if (yInsideTrack >= kTrackHeight)
        return -1;

    return track;
};

void TimelineComponent::mouseMove(const juce::MouseEvent& e)
{
    mouseCursorTime_ = juce::jmax(0.0, xToTime(e.x));
    mouseTrackIndex_ = yToTrackIndex(e.y);
}

bool TimelineComponent::keyPressed(const juce::KeyPress& key)
{
    const bool isCtrlC =
        (key.getModifiers().isCommandDown() &&
        (key.getKeyCode() == 'c' || key.getKeyCode() == 'C'));

    const bool isCtrlV =
        (key.getModifiers().isCommandDown() &&
        (key.getKeyCode() == 'v' || key.getKeyCode() == 'V'));

    if (isCtrlC)
    {
        if (hasSelectedRegion_)
            copiedRegion_ = selectedRegion_;

        return true;
    }

    if (isCtrlV)
    {
        if (!copiedRegion_)
        return true;
        
        const auto& copy = *copiedRegion_;
        if (mouseTrackIndex_ != copy.trackIndex)
            return true;

        double newStart = juce::jmax(0.0, mouseCursorTime_);
        double duration = copy.durationSec;

        BlockRegion test = copy;
        test.startTimeSec = newStart;

        if (!canPlaceRegion(test, newStart, duration))
            return true;

        if (onRegionDuplicated)
            onRegionDuplicated(copy.serial, newStart, duration);

        return true;
    }

    return false;
}

bool TimelineComponent::canPlaceRegion(const BlockRegion& moving,
                                       double newStart,
                                       double newDuration) const
{
    if (newStart < 0.0)
        return false;

    if (newDuration <= 0.05)
        return false;

    const double newEnd = newStart + newDuration;

    for (const auto& other : regions_)
    {
        if (other.trackIndex != moving.trackIndex)
            continue;

        if (other.serial == moving.serial &&
            other.timeIndex == moving.timeIndex)
            continue;

        const double otherStart = other.startTimeSec;
        const double otherEnd   = other.startTimeSec + other.durationSec;

        const bool overlaps =
            newStart < otherEnd && newEnd > otherStart;

        if (overlaps)
            return false;
    }

    return true;
}