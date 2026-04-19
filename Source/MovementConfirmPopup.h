#pragma once
#include <JuceHeader.h>
#include "BlockEntry.h"  // ← Need this for MovementKeyframe

class MovementConfirmPopup : public juce::Component
{
public:
    MovementConfirmPopup(int blockSerial, 
                         double recordedDuration,
                         const std::vector<MovementKeyFrame>& keyframes)  // ← Add keyframes parameter
        : serial(blockSerial)
        , duration(recordedDuration)
        , recordedMovement(keyframes)  // ← Store keyframes
    {
        setSize(400, 300);  // ← Larger to show movement visualization
        
        addAndMakeVisible(messageLabel);
        messageLabel.setText(
            "Movement recorded for Block #" + juce::String(blockSerial) + "\n" +
            "Duration: " + juce::String(recordedDuration, 2) + " seconds\n" +
            "Keyframes: " + juce::String(keyframes.size()),
            juce::dontSendNotification
        );
        messageLabel.setJustificationType(juce::Justification::centred);
        
        addAndMakeVisible(confirmButton);
        confirmButton.setButtonText("Confirm");
        confirmButton.onClick = [this]() { 
            if (onConfirm) 
                onConfirm(serial, duration);
            
            if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
                dialog->closeButtonPressed();
        };
        
        addAndMakeVisible(cancelButton);
        cancelButton.setButtonText("Cancel");
        cancelButton.onClick = [this]() { 
            if (onCancel) 
                onCancel(serial);
            
            if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
                dialog->closeButtonPressed();
        };
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(10);
        
        messageLabel.setBounds(bounds.removeFromTop(60));
        
        // Leave space for visualization
        visualizationArea = bounds.removeFromTop(bounds.getHeight() - 40);
        
        auto buttonArea = bounds.removeFromBottom(30);
        confirmButton.setBounds(buttonArea.removeFromLeft(buttonArea.getWidth() / 2).reduced(5));
        cancelButton.setBounds(buttonArea.reduced(5));
    }
    
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff2a2a2a));
        g.setColour(juce::Colours::orange);
        g.drawRect(getLocalBounds(), 2);
        
        // Draw movement visualization
        if (!recordedMovement.empty())
        {
            paintMovementPath(g);
        }
    }
    
    std::function<void(int, double)> onConfirm;
    std::function<void(int)> onCancel;
    
private:
    int serial;
    double duration;
    std::vector<MovementKeyFrame> recordedMovement;
    juce::Rectangle<int> visualizationArea;
    
    juce::Label messageLabel;
    juce::TextButton confirmButton;
    juce::TextButton cancelButton;
    
    void paintMovementPath(juce::Graphics& g)
    {
        if (recordedMovement.size() < 2)
            return;
        
        auto area = visualizationArea.reduced(5);
        
        // Find bounds of movement
        int minX = recordedMovement[0].position.x;
        int maxX = minX;
        int minZ = recordedMovement[0].position.z;
        int maxZ = minZ;
        
        for (const auto& kf : recordedMovement)
        {
            minX = std::min(minX, kf.position.x);
            maxX = std::max(maxX, kf.position.x);
            minZ = std::min(minZ, kf.position.z);
            maxZ = std::max(maxZ, kf.position.z);
        }
        
        // Add padding
        int rangeX = std::max(1, maxX - minX + 2);
        int rangeZ = std::max(1, maxZ - minZ + 2);
        
        // Scale to fit visualization area
        float scaleX = area.getWidth() / (float)rangeX;
        float scaleZ = area.getHeight() / (float)rangeZ;
        float scale = std::min(scaleX, scaleZ);
        
        // Helper to convert grid pos to screen pos
        auto toScreen = [&](const Vec3i& pos) -> juce::Point<float>
        {
            float x = area.getX() + (pos.x - minX + 0.5f) * scale;
            float y = area.getBottom() - (pos.z - minZ + 0.5f) * scale;  // Flip Y
            return { x, y };
        };
        
        // Draw grid background
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillRect(area);
        
        // Draw grid lines
        g.setColour(juce::Colour(0xff333333));
        for (int x = minX; x <= maxX + 1; ++x)
        {
            auto p1 = toScreen({x, 0, minZ});
            auto p2 = toScreen({x, 0, maxZ + 1});
            g.drawLine(p1.x, p1.y, p2.x, p2.y, 0.5f);
        }
        for (int z = minZ; z <= maxZ + 1; ++z)
        {
            auto p1 = toScreen({minX, 0, z});
            auto p2 = toScreen({maxX + 1, 0, z});
            g.drawLine(p1.x, p1.y, p2.x, p2.y, 0.5f);
        }
        
        // Draw movement path
        juce::Path path;
        auto firstPoint = toScreen(recordedMovement[0].position);
        path.startNewSubPath(firstPoint);
        
        for (size_t i = 1; i < recordedMovement.size(); ++i)
        {
            auto point = toScreen(recordedMovement[i].position);
            path.lineTo(point);
        }
        
        // Draw the path with gradient
        g.setColour(juce::Colours::cyan.withAlpha(0.6f));
        g.strokePath(path, juce::PathStrokeType(2.0f));
        
        // Draw keyframe points
        for (size_t i = 0; i < recordedMovement.size(); ++i)
        {
            auto point = toScreen(recordedMovement[i].position);
            
            // Color gradient from green (start) to red (end)
            float t = (float)i / (recordedMovement.size() - 1);
            auto color = juce::Colour::fromHSV(0.33f * (1.0f - t), 0.8f, 0.9f, 1.0f);
            
            g.setColour(color);
            g.fillEllipse(point.x - 4, point.y - 4, 8, 8);
            
            // Draw Y-level indicator
            if (recordedMovement[i].position.y > 0)
            {
                g.setColour(juce::Colours::white.withAlpha(0.5f));
                g.drawText(juce::String(recordedMovement[i].position.y),
                          (int)point.x - 10, (int)point.y - 20, 20, 12,
                          juce::Justification::centred);
            }
        }
        
        // Draw start/end labels
        auto startPoint = toScreen(recordedMovement[0].position);
        auto endPoint = toScreen(recordedMovement.back().position);
        
        g.setColour(juce::Colours::white);
        g.drawText("START", (int)startPoint.x - 20, (int)startPoint.y + 8, 40, 12,
                   juce::Justification::centred);
        g.drawText("END", (int)endPoint.x - 20, (int)endPoint.y + 8, 40, 12,
                   juce::Justification::centred);
        
        // Draw coordinate labels
        g.setColour(juce::Colours::grey);
        g.setFont(10.0f);
        g.drawText("X: " + juce::String(minX) + " to " + juce::String(maxX),
                   area.getX(), area.getBottom() + 2, area.getWidth(), 12,
                   juce::Justification::centredLeft);
        g.drawText("Z: " + juce::String(minZ) + " to " + juce::String(maxZ),
                   area.getX(), area.getY() - 14, area.getWidth(), 12,
                   juce::Justification::centredLeft);
    }
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MovementConfirmPopup)
};