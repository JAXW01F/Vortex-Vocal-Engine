#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ================================================================
// Custom look-and-feel for Vortex dark theme
// ================================================================
class VortexLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VortexLookAndFeel()
    {
        setColour(juce::Slider::thumbColourId,         juce::Colour(0xFF00D4FF));
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFF00D4FF));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF1A3A4A));
        setColour(juce::Slider::trackColourId,         juce::Colour(0xFF00D4FF));
        setColour(juce::Label::textColourId,           juce::Colours::white);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider& /*slider*/) override
    {
        float cx = x + w * 0.5f;
        float cy = y + h * 0.5f;
        float r  = std::min(w, h) * 0.4f;

        // Background arc
        juce::Path bg;
        bg.addCentredArc(cx, cy, r, r, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(0xFF1A3A4A));
        g.strokePath(bg, juce::PathStrokeType(4.0f));

        // Fill arc
        float angle = startAngle + sliderPos * (endAngle - startAngle);
        juce::Path fg;
        fg.addCentredArc(cx, cy, r, r, 0.0f, startAngle, angle, true);
        g.setColour(juce::Colour(0xFF00D4FF));
        g.strokePath(fg, juce::PathStrokeType(4.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Thumb dot
        float tx = cx + (r + 6.0f) * std::sin(angle);
        float ty = cy - (r + 6.0f) * std::cos(angle);
        g.setColour(juce::Colour(0xFF00D4FF));
        g.fillEllipse(tx - 4.0f, ty - 4.0f, 8.0f, 8.0f);
    }
};

// ================================================================
// Level Meter component
// ================================================================
class LevelMeter : public juce::Component
{
public:
    void setLevel(float db) { level = db; repaint(); }
    void setColour(juce::Colour c) { colour = c; }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour(juce::Colour(0xFF0D1F2A));
        g.fillRoundedRectangle(bounds, 4.0f);

        float fraction = juce::jmap(level, -60.0f, 0.0f, 0.0f, 1.0f);
        fraction = juce::jlimit(0.0f, 1.0f, fraction);

        if (fraction > 0.0f)
        {
            auto fill = bounds.withTop(bounds.getBottom() - bounds.getHeight() * fraction);
            g.setColour(colour);
            g.fillRoundedRectangle(fill, 3.0f);
        }

        // -6 dB marker
        float marker = juce::jmap(-6.0f, -60.0f, 0.0f, 0.0f, 1.0f);
        float markerY = bounds.getBottom() - bounds.getHeight() * marker;
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawHorizontalLine((int)markerY, bounds.getX(), bounds.getRight());
    }

private:
    float        level  = -60.0f;
    juce::Colour colour = juce::Colour(0xFF00D4FF);
};

// ================================================================
// Anomaly ring indicator
// ================================================================
class AnomalyMeter : public juce::Component
{
public:
    void setScore(float s) { score = s; repaint(); }

    void paint(juce::Graphics& g) override
    {
        auto b  = getLocalBounds().toFloat().reduced(4.0f);
        float cx = b.getCentreX(), cy = b.getCentreY();
        float r  = std::min(b.getWidth(), b.getHeight()) * 0.4f;

        // Outer ring
        g.setColour(juce::Colour(0xFF1A3A4A));
        g.drawEllipse(cx - r, cy - r, r * 2, r * 2, 2.0f);

        // Fill arc
        float angle = score * juce::MathConstants<float>::twoPi;
        if (angle > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc(cx, cy, r, r, 0.0f,
                -juce::MathConstants<float>::halfPi,
                -juce::MathConstants<float>::halfPi + angle, true);

            juce::Colour c = score < 0.5f
                ? juce::Colour(0xFF00D4FF)
                : juce::Colour(0xFFFF6B35).interpolatedWith(juce::Colour(0xFFFF0040), (score - 0.5f) * 2.0f);
            g.setColour(c);
            g.strokePath(arc, juce::PathStrokeType(3.0f));
        }

        // Score text
        g.setColour(juce::Colours::white);
        g.setFont(12.0f);
        g.drawText(juce::String((int)(score * 100)) + "%",
                   getLocalBounds(), juce::Justification::centred);
    }

private:
    float score = 0.0f;
};

// ================================================================
// Main Editor
// ================================================================
class VortexAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit VortexAudioProcessorEditor(VortexAudioProcessor&);
    ~VortexAudioProcessorEditor() override;

    void paint  (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    VortexAudioProcessor& processor;
    VortexLookAndFeel     vortexLAF;

    // ── Knobs ─────────────────────────────────────────────────
    juce::Slider inputGainKnob,  outputGainKnob;
    juce::Slider smoothingKnob,  deEsserKnob;
    juce::Slider anomalyKnob,    mixKnob;
    juce::ToggleButton pitchBtn  { "Pitch Track" };

    // ── Labels ─────────────────────────────────────────────────
    juce::Label inputGainLabel   { {}, "IN GAIN" };
    juce::Label outputGainLabel  { {}, "OUT GAIN" };
    juce::Label smoothingLabel   { {}, "SMOOTH" };
    juce::Label deEsserLabel     { {}, "DE-ESS" };
    juce::Label anomalyLabel     { {}, "ANOMALY" };
    juce::Label mixLabel         { {}, "MIX" };
    juce::Label pitchReadout     { {}, "-- Hz" };

    // ── Meters ─────────────────────────────────────────────────
    LevelMeter  inputMeter, outputMeter;
    AnomalyMeter anomalyMeter;

    // ── Attachments ────────────────────────────────────────────
    std::unique_ptr<SliderAttachment> inputGainAtt, outputGainAtt;
    std::unique_ptr<SliderAttachment> smoothingAtt, deEsserAtt;
    std::unique_ptr<SliderAttachment> anomalyAtt,   mixAtt;
    std::unique_ptr<ButtonAttachment> pitchAtt;

    void makeKnob(juce::Slider& s, juce::Label& l, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VortexAudioProcessorEditor)
};
