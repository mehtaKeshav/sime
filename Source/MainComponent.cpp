// ─────────────────────────────────────────────────────────────────────────────
// MainComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible(view);
    addAndMakeVisible(sidebar);
    addAndMakeVisible(transportBar);

    // ── Wire sidebar collapse ─────────────────────────────────────────────────
    view.setSidebarComponent(&sidebar);
    sidebar.onCollapsedChanged = [this](bool isNowCollapsed)
    {
        isSidebarCollapsed = isNowCollapsed;
        resized();
    };

    // ── Transport bar ─────────────────────────────────────────────────────────
    transportBar.onPlay  = [this] { view.transportPlay(); };
    transportBar.onPause = [this] { view.transportPause(); };
    transportBar.onStop  = [this]
    {
        view.transportStop();
        transportBar.setTransportState(false, false, 0.0,
                                       view.getTransportDuration());
    };

    // ── Block type toolbar ────────────────────────────────────────────────────
    addAndMakeVisible(violinBtn);
    addAndMakeVisible(pianoBtn);
    addAndMakeVisible(drumBtn);
    addAndMakeVisible(customBtn);

    violinBtn.onClick = [this] { setActiveBlockType(BlockType::Violin); };
    pianoBtn .onClick = [this] { setActiveBlockType(BlockType::Piano);  };
    drumBtn  .onClick = [this] { setActiveBlockType(BlockType::Drum);   };
    customBtn.onClick = [this] { setActiveBlockType(BlockType::Custom); };

    refreshToolbarColors();

    // ── Wire edit popup ───────────────────────────────────────────────────────
    view.onRequestBlockEdit = [this](int serial, BlockType type,
                                     double start, double dur,
                                     int soundId, const juce::String& customFile,
                                     juce::Point<int> posInView)
    {
        juce::Point<int> screenPos = view.localPointToGlobal(posInView);
        editPopup.showAt(serial, type, start, dur, soundId, customFile, screenPos);
    };

    editPopup.onCommit = [this](int serial, double start, double dur,
                                int sid, const juce::String& customFile)
    {
        view.applyBlockEdit(serial, start, dur, sid, customFile);
    };

    editPopup.onCancel = [this]()
    {
        view.clearSelectedBlock();
    };

    // ── Poll transport state at 30 Hz ─────────────────────────────────────────
    startTimerHz(30);
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::setActiveBlockType(BlockType t)
{
    activeType_ = t;
    view.setActiveBlockType(t);
    refreshToolbarColors();
}

void MainComponent::refreshToolbarColors()
{
    auto style = [&](juce::TextButton& btn, BlockType t,
                     juce::Colour activeCol)
    {
        if (activeType_ == t)
        {
            btn.setColour(juce::TextButton::buttonColourId,  activeCol);
            btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        }
        else
        {
            btn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff222233));
            btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888899));
        }
    };

    style(violinBtn, BlockType::Violin, juce::Colour(0xffc03528));
    style(pianoBtn,  BlockType::Piano,  juce::Colour(0xff3366cc));
    style(drumBtn,   BlockType::Drum,   juce::Colour(0xff2eaa44));
    style(customBtn, BlockType::Custom, juce::Colour(0xff666688));

    violinBtn.repaint();
    pianoBtn .repaint();
    drumBtn  .repaint();
    customBtn.repaint();
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::timerCallback()
{
    const double currentTime = view.getTransportTime();
    const double duration    = view.getTransportDuration();

    if (view.isTransportPlaying() && duration > 0.0 && currentTime >= duration)
    {
        view.transportStop();
        transportBar.setTransportState(false, false, 0.0, duration);
        return;
    }

    transportBar.setTransportState(
        view.isTransportPlaying(),
        view.isTransportPaused(),
        currentTime,
        duration);
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0e1018));
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Sidebar — full height, left side
    const int sidebarWidth = sidebar.isCollapsed() ? 50 : 220;
    sidebar.setBounds(area.removeFromLeft(sidebarWidth));

    // Transport bar — bottom
    transportBar.setBounds(area.removeFromBottom(TransportBarComponent::kHeight));

    // Block type toolbar — top of viewport area
    auto toolbarArea = area.removeFromTop(kToolbarH);
    const int btnW = 80;
    const int gap  = 4;
    int tx = toolbarArea.getX() + 8;
    int ty = toolbarArea.getY() + (kToolbarH - 26) / 2;
    violinBtn.setBounds(tx, ty, btnW, 26);  tx += btnW + gap;
    pianoBtn .setBounds(tx, ty, btnW, 26);  tx += btnW + gap;
    drumBtn  .setBounds(tx, ty, btnW, 26);  tx += btnW + gap;
    customBtn.setBounds(tx, ty, btnW, 26);

    // 3D viewport — whatever remains
    view.setBounds(area);
}
