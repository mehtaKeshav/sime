#include "MainComponent.h"
#include "ViewPortComponent.h"
#include "SidebarComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible(view);
    addAndMakeVisible(sidebar);
    view.setSidebarComponent(&sidebar);
    sidebar.onCollapsedChanged = [this](bool isNowCollapsed){
        isSidebarCollapsed = isNowCollapsed;
        resized();
    };
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    const int sidebarWidth = sidebar.isCollapsed() ? 50 : 220;
    auto sidebarArea = area.removeFromLeft(sidebarWidth);
    sidebar.setBounds(sidebarArea);
    view.setBounds(area);
}