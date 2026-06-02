#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// HelpPopup.h
//
// Modal cheat-sheet of every keyboard / mouse / toolbar / sidebar action in
// SIME.  Opened from the toolbar's "Help" button.
//
// The body lives inside a juce::Viewport so it can grow beyond the panel's
// fixed height — the user can scroll with the wheel, arrow keys, PageUp /
// PageDown, or Home / End.  Each section is rendered by drawing one
// AttributedString into a Body child component sized to the layout's
// computed height.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>

class HelpPopup : public juce::Component
{
public:
    HelpPopup();

    void paint   (juce::Graphics&) override;
    void resized () override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    /// Inner component that owns the AttributedString and reports its
    /// laid-out height up to HelpPopup so the viewport scrollbar sizes
    /// correctly.
    class Body : public juce::Component
    {
    public:
        Body() { setOpaque(false); }

        void setBody(juce::AttributedString s) { body_ = std::move(s); }

        /// Re-runs the text layout for the current width, returns the
        /// height in pixels.  Call from HelpPopup::resized() so the body's
        /// bounds match its content.
        int  measureHeight(int width);

        void paint(juce::Graphics&) override;

    private:
        juce::AttributedString body_;
    };

    juce::TextButton closeBtn_ { "Close" };
    juce::Viewport   viewport_;
    Body             body_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpPopup)
};
