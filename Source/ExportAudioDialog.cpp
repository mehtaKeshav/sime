#include "ExportAudioDialog.h"

ExportAudioDialog::ExportAudioDialog()
{
    addAndMakeVisible(title_);
    title_.setColour(juce::Label::textColourId, juce::Colour(0xffe2e6f2));
    title_.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(formatBox_);
    formatBox_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff181a24));
    formatBox_.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe2e6f2));
    formatBox_.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2f3447));
    formatBox_.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff8b94ad));

    formatBox_.addItem(SceneAudioExporter::formatDescription(SceneAudioExporter::Format::Wav), 1);
    formatBox_.addItem(SceneAudioExporter::formatDescription(SceneAudioExporter::Format::Flac), 2);
    formatBox_.addItem(SceneAudioExporter::formatDescription(SceneAudioExporter::Format::Aiff), 3);
    formatBox_.addItem(SceneAudioExporter::formatDescription(SceneAudioExporter::Format::Ogg), 4);
    formatBox_.setSelectedId(1, juce::dontSendNotification);

    addAndMakeVisible(listenerHdr_);
    listenerHdr_.setColour(juce::Label::textColourId, juce::Colour(0xffe2e6f2));
    listenerHdr_.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(listenerInfo_);
    listenerInfo_.setColour(juce::Label::textColourId, juce::Colour(0xffaac8e8));
    listenerInfo_.setJustificationType(juce::Justification::topLeft);
    listenerInfo_.setMinimumHorizontalScale(1.0f);
    listenerInfo_.setText(
        "(listener pose will be filled in when the dialog opens)",
        juce::dontSendNotification);

    addAndMakeVisible(exportBtn_);
    exportBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a5298));
    exportBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    exportBtn_.onClick = [this]
    {
        const auto fmt = selectedFormat();
        if (onExportChosen)
            onExportChosen(fmt);

        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    };

    addAndMakeVisible(cancelBtn_);
    cancelBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff242a3c));
    cancelBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    cancelBtn_.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };

    setSize(440, 220);
}

void ExportAudioDialog::setListenerInfo(bool anchored,
                                        float posX, float posY, float posZ,
                                        float yawDeg, float pitchDeg)
{
    juce::String line;
    if (anchored)
    {
        line = "ANCHORED at ("
             + juce::String(posX, 1) + ", "
             + juce::String(posY, 1) + ", "
             + juce::String(posZ, 1) + ")  yaw "
             + juce::String(yawDeg, 0) + " deg, pitch "
             + juce::String(pitchDeg, 0) + " deg.\n"
             + "Export will sound like this fixed listener pose.";
        listenerInfo_.setColour(juce::Label::textColourId,
                                juce::Colour(0xffaac8e8));
    }
    else
    {
        line = "Anchor not set. Export will use the current camera pose:\n"
             + juce::String("(")
             + juce::String(posX, 1) + ", "
             + juce::String(posY, 1) + ", "
             + juce::String(posZ, 1) + ")  yaw "
             + juce::String(yawDeg, 0) + " deg, pitch "
             + juce::String(pitchDeg, 0) + " deg.\n"
             + "Tip: hit Anchor in the toolbar to lock a pose before exporting.";
        listenerInfo_.setColour(juce::Label::textColourId,
                                juce::Colour(0xffffcc66));
    }
    listenerInfo_.setText(line, juce::dontSendNotification);
}

void ExportAudioDialog::setListenerPathInfo(int numKeyframes,
                                            double firstSec,
                                            double lastSec)
{
    const juce::String line =
        "CAMERA PATH active (" + juce::String(numKeyframes) + " keyframes, "
        + juce::String(firstSec, 1) + "s -> " + juce::String(lastSec, 1) + "s).\n"
        + "Export will animate the listener through the path.";
    listenerInfo_.setColour(juce::Label::textColourId, juce::Colour(0xff9be0a0));
    listenerInfo_.setText(line, juce::dontSendNotification);
}

SceneAudioExporter::Format ExportAudioDialog::selectedFormat() const
{
    switch (formatBox_.getSelectedId())
    {
        case 2: return SceneAudioExporter::Format::Flac;
        case 3: return SceneAudioExporter::Format::Aiff;
        case 4: return SceneAudioExporter::Format::Ogg;
        default: return SceneAudioExporter::Format::Wav;
    }
}

void ExportAudioDialog::resized()
{
    auto r = getLocalBounds().reduced(16);
    title_.setBounds(r.removeFromTop(22));
    r.removeFromTop(6);
    formatBox_.setBounds(r.removeFromTop(28));
    r.removeFromTop(14);
    listenerHdr_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    listenerInfo_.setBounds(r.removeFromTop(56));
    r.removeFromTop(12);
    auto row = r.removeFromTop(32);
    cancelBtn_.setBounds(row.removeFromRight(100));
    row.removeFromRight(8);
    exportBtn_.setBounds(row);
}
