#pragma once
#include <JuceHeader.h>
#include "ViewPortComponent.h"
#include "SidebarComponent.h"
#include "BlockEditPopup.h"
#include "TransportBarComponent.h"
#include "BlockType.h"
#include "MovementConfirmPopup.h"
#include "StartupMenuComponent.h"
#include "SceneAudioExporter.h"

class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    void saveScene(const juce::String& path = {});
    void openScene();
    void newScene();
    void autoSave();
    void loadSceneFromFile(const juce::String& path);

private:
    // ── Startup menu ──────────────────────────────────────────────────────────
    StartupMenuComponent startupMenu_;
    bool                 showingStartup_ = true;
    void                 dismissStartupMenu();

    // ── Main app components ───────────────────────────────────────────────────
    ViewPortComponent     view;
    SidebarComponent      sidebar;
    BlockEditPopup        editPopup;
    TransportBarComponent transportBar;
    std::unique_ptr<MovementConfirmPopup> movementPopup;
    bool isSidebarCollapsed = false;

    // ── Block type toolbar ────────────────────────────────────────────────────
    // Single grouped ComboBox listing all 23 BlockTypes, plus a color "pill"
    // that visually echoes the active selection.
    juce::ComboBox blockTypeCombo;

    /// Compact color-swatch + active type name shown left of the combo.
    class TypePill : public juce::Component
    {
    public:
        void setActive(BlockType t) { type_ = t; repaint(); }
        void paint(juce::Graphics& g) override;
    private:
        BlockType type_ = BlockType::Violin;
    } typePill_;

    BlockType activeType_ = BlockType::Violin;
    void setActiveBlockType(BlockType t);
    void rebuildBlockTypeCombo();
    void syncComboToActive();

    // ── View toggle buttons (top toolbar) ────────────────────────────────────
    // Floor = XZ plane (y=0).  WallX = YZ plane (x=0).  WallZ = XY plane (z=0).
    // Plane buttons are labelled by the two axes that lie INSIDE the plane,
    // matching common 3D-software convention:
    //   * Floor   = XZ plane (y = 0)         – horizontal ground
    //   * YZ Wall = vertical wall at x = 0   – contains the Y and Z axes
    //   * XY Wall = vertical wall at z = 0   – contains the X and Y axes
    juce::TextButton showFloorBtn_   { "Floor" };
    juce::TextButton showWallXBtn_   { "YZ Wall" };
    juce::TextButton showWallZBtn_   { "XY Wall" };
    juce::TextButton showArrowsBtn_  { "Arrows" };
    juce::TextButton dopplerBtn_     { "Doppler" };

    void configureToggleButton(juce::TextButton& b);

    // ── File / View / Mute menus (DAW-style) ────────────────────────────────
    juce::TextButton fileMenuBtn_;
    juce::TextButton viewMenuBtn_;
    juce::TextButton muteMenuBtn_;

    void showViewMenu();
    void showMuteMenu();

    juce::String currentFilePath_;
    std::unique_ptr<juce::FileChooser> fileChooser_;

    void showFileMenu();
    void handleFileMenu(int result);
    void showExportAudioDialog();
    void launchExportSaveChooser(SceneAudioExporter::Format format);

    static constexpr int kToolbarH = 34;
    void showMovementConfirmPopup(int serial, double duration,
                                  const std::vector<MovementKeyFrame>& keyframes,
                                  juce::Point<int> position);

    void setPlaybackUiState(bool playing, bool paused, double currentTime);
    void stopPlaybackAndResetUi();
    void timerCallback() override;

    // ── Title bar + dirty tracking ────────────────────────────────────────────
    bool hasUnsavedChanges_  = false;
    bool suppressNextDirty_  = false;   ///< Set before load/new to ignore the
                                         ///  onBlockListChanged that follows them.
    void updateWindowTitle();
    void markDirty();                    ///< Set dirty + refresh title bar

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
