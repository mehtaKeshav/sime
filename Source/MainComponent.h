#pragma once
#include <JuceHeader.h>
#include "ViewPortComponent.h"
#include "SidebarComponent.h"

class MainComponent : public juce::Component
{
public:
    MainComponent();

    void resized() override;

private:
    ViewPortComponent view;
    SidebarComponent sidebar;
    bool isSidebarCollapsed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};