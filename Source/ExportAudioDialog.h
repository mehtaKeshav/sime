#pragma once

#include <JuceHeader.h>
#include "SceneAudioExporter.h"

// ---------------------------------------------------------------------------
// Small modal content: pick lossless / lossy container, then the host opens
// a save dialog and runs SceneAudioExporter.
// ---------------------------------------------------------------------------
class ExportAudioDialog : public juce::Component
{
public:
    ExportAudioDialog();

    /// Tell the dialog about the listener pose the bounce will use, so the
    /// user can see (and verify) whether the export will sound like the
    /// anchored pose, the live camera, or something else entirely.
    void setListenerInfo(bool anchored,
                         float posX, float posY, float posZ,
                         float yawDeg, float pitchDeg);

    /// Override the listener info line with a path-active summary.  When a
    /// camera path is set, the bounce animates the listener through it and
    /// the static anchor / camera pose is irrelevant.
    void setListenerPathInfo(int numKeyframes,
                             double firstSec,
                             double lastSec);

    void resized() override;

    /// Invoked when the user confirms; host should close the dialog and open
    /// a FileChooser (or call export directly).
    std::function<void(SceneAudioExporter::Format)> onExportChosen;

private:
    juce::Label       title_       { {}, "Format" };
    juce::ComboBox    formatBox_;
    juce::Label       listenerHdr_ { {}, "Listener pose" };
    juce::Label       listenerInfo_;
    juce::TextButton exportBtn_ { "Choose file and export" };
    juce::TextButton cancelBtn_ { "Cancel" };

    SceneAudioExporter::Format selectedFormat() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportAudioDialog)
};
