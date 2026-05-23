// ─────────────────────────────────────────────────────────────────────────────
// Main.cpp  –  JUCE application entry point for the Voxel Builder.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "MainComponent.h"
#include "MathUtils.h"
#include "BlockEntry.h"
#include "SceneFile.h"

class SIMEApp final : public juce::JUCEApplication
{
public:
    // ── JUCEApplication ───────────────────────────────────────────────────────

    const juce::String getApplicationName()    override { return "SIME"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed()          override { return false; }

    void initialise(const juce::String& /*commandLineArgs*/) override
    {
        auto* mc = new MainComponent();
        mainComponent = mc;
        mainWindow.reset(new MainWindow("SIME", mc, *this));

        // NOTE: we deliberately do NOT auto-load the autosave here.  The startup
        // menu already shows an explicit "Continue autosave.sime" button so the
        // user can opt in.  Loading silently here caused a persistence bug
        // where clicking "New Scene" on the startup menu would clear the
        // viewport, then the async autoload would fire a frame later and
        // re-populate it with the previous session's blocks.
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        if (mainComponent != nullptr)
            mainComponent->autoSave();
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

    // ── Inner window class ────────────────────────────────────────────────────

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow(const juce::String& name,
                   juce::Component*   content,
                   juce::JUCEApplication& app)
            : juce::DocumentWindow(
                  name,
                  juce::Desktop::getInstance()
                      .getDefaultLookAndFeel()
                      .findColour(juce::ResizableWindow::backgroundColourId),
                  juce::DocumentWindow::allButtons),
              application(app)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(content, true);
            setResizable(true, true);

#if JUCE_IOS || JUCE_ANDROID
            setFullScreen(true);
#else
            setResizeLimits(640, 480, 7680, 4320);
            centreWithSize(1280, 720);
#endif
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            application.systemRequestedQuit();
        }

    private:
        juce::JUCEApplication& application;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
    MainComponent* mainComponent = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// This macro generates the platform-specific main() / WinMain() entry point.
// ─────────────────────────────────────────────────────────────────────────────
START_JUCE_APPLICATION(SIMEApp)
