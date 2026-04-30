#include "PluginEditor.h"

// ================================================================
// Constructor
// ================================================================
VortexAudioProcessorEditor::VortexAudioProcessorEditor(VortexAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&vortexLAF);
    setSize(600, 420);

    // ── Knobs ─────────────────────────────────────────────────
    makeKnob(inputGainKnob,  inputGainLabel,  "IN GAIN");
    makeKnob(outputGainKnob, outputGainLabel, "OUT GAIN");
    makeKnob(smoothingKnob,  smoothingLabel,  "SMOOTH");
    makeKnob(deEsserKnob,    deEsserLabel,    "DE-ESS");
    makeKnob(anomalyKnob,    anomalyLabel,    "ANOMALY");
    makeKnob(mixKnob,        mixLabel,        "MIX");

    // ── Pitch toggle ──────────────────────────────────────────
    addAndMakeVisible(pitchBtn);
    pitchBtn.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    pitchBtn.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xFF00D4FF));

    addAndMakeVisible(pitchReadout);
    pitchReadout.setColour(juce::Label::textColourId, juce::Colour(0xFF00D4FF));
    pitchReadout.setJustificationType(juce::Justification::centred);
    pitchReadout.setFont(juce::Font(18.0f, juce::Font::bold));

    // ── Meters ────────────────────────────────────────────────
    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);
    inputMeter.setColour (juce::Colour(0xFF00D4FF));
    outputMeter.setColour(juce::Colour(0xFF39FF14));

    addAndMakeVisible(anomalyMeter);

    // ── Attachments ────────────────────────────────────────────
    auto& apvts = processor.apvts;
    inputGainAtt  = std::make_unique<SliderAttachment>(apvts, "inputGain",    inputGainKnob);
    outputGainAtt = std::make_unique<SliderAttachment>(apvts, "outputGain",   outputGainKnob);
    smoothingAtt  = std::make_unique<SliderAttachment>(apvts, "smoothing",    smoothingKnob);
    deEsserAtt    = std::make_unique<SliderAttachment>(apvts, "deEsser",      deEsserKnob);
    anomalyAtt    = std::make_unique<SliderAttachment>(apvts, "anomaly",      anomalyKnob);
    mixAtt        = std::make_unique<SliderAttachment>(apvts, "mix",          mixKnob);
    pitchAtt      = std::make_unique<ButtonAttachment>(apvts, "pitchCorrect", pitchBtn);

    startTimerHz(30); // 30 fps meter refresh
}

VortexAudioProcessorEditor::~VortexAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

// ================================================================
// Timer — update meters from audio thread values
// ================================================================
void VortexAudioProcessorEditor::timerCallback()
{
    inputMeter.setLevel (processor.inputLevelDb.load());
    outputMeter.setLevel(processor.outputLevelDb.load());
    anomalyMeter.setScore(processor.anomalyScore.load());

    float hz = processor.currentPitchHz.load();
    pitchReadout.setText(hz > 50.0f
        ? juce::String((int)hz) + " Hz"
        : "-- Hz",
        juce::dontSendNotification);
}

// ================================================================
// Paint
// ================================================================
void VortexAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background gradient
    g.fillAll(juce::Colour(0xFF0A1520));

    // Header bar
    g.setColour(juce::Colour(0xFF0D2030));
    g.fillRect(0, 0, getWidth(), 50);

    // Title
    g.setColour(juce::Colour(0xFF00D4FF));
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.drawText("VORTEX VOCAL ENGINE", 0, 0, getWidth(), 50,
               juce::Justification::centred);

    // Subtitle
    g.setFont(10.0f);
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.drawText("AI-ASSISTED VOCAL PROCESSING", 0, 30, getWidth(), 20,
               juce::Justification::centred);

    // Dividers
    g.setColour(juce::Colour(0xFF1A3A4A));
    g.drawLine(0, 50, (float)getWidth(), 50, 1.0f);
    g.drawLine(50, 55, 50, (float)getHeight() - 10, 1.0f);
    g.drawLine((float)getWidth() - 50, 55, (float)getWidth() - 50, (float)getHeight() - 10, 1.0f);

    // Section labels
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.setFont(9.0f);
    g.drawText("IN",  0,   52, 50, 15, juce::Justification::centred);
    g.drawText("OUT", getWidth() - 50, 52, 50, 15, juce::Justification::centred);

    // Meter label
    g.drawText("ANOMALY", getWidth() / 2 - 35, 340, 70, 15,
               juce::Justification::centred);
}

// ================================================================
// Layout
// ================================================================
void VortexAudioProcessorEditor::resized()
{
    const int W = getWidth();
    const int H = getHeight();

    // Level meters (left and right strips)
    inputMeter.setBounds (5,  60, 35, H - 70);
    outputMeter.setBounds(W - 45, 60, 35, H - 70);

    // Knob area (centre)
    const int knobW   = 90;
    const int knobH   = 90;
    const int knobY1  = 65;
    const int knobY2  = 185;
    const int startX  = 60;
    const int spacing = (W - 120 - knobW) / 2;

    // Row 1: In gain | Smooth | Anomaly
    inputGainKnob.setBounds (startX,                  knobY1, knobW, knobH);
    smoothingKnob.setBounds (startX + knobW + spacing, knobY1, knobW, knobH);
    anomalyKnob.setBounds   (startX + (knobW + spacing) * 2, knobY1, knobW, knobH);

    // Row 1 labels
    const int labelH = 18;
    inputGainLabel.setBounds (startX,                  knobY1 + knobH, knobW, labelH);
    smoothingLabel.setBounds (startX + knobW + spacing, knobY1 + knobH, knobW, labelH);
    anomalyLabel.setBounds   (startX + (knobW + spacing) * 2, knobY1 + knobH, knobW, labelH);

    // Row 2: Out gain | De-esser | Mix
    outputGainKnob.setBounds(startX,                   knobY2, knobW, knobH);
    deEsserKnob.setBounds   (startX + knobW + spacing,  knobY2, knobW, knobH);
    mixKnob.setBounds       (startX + (knobW + spacing) * 2, knobY2, knobW, knobH);

    outputGainLabel.setBounds(startX,                   knobY2 + knobH, knobW, labelH);
    deEsserLabel.setBounds   (startX + knobW + spacing,  knobY2 + knobH, knobW, labelH);
    mixLabel.setBounds       (startX + (knobW + spacing) * 2, knobY2 + knobH, knobW, labelH);

    // Anomaly meter (bottom centre)
    anomalyMeter.setBounds(W / 2 - 40, 310, 80, 80);

    // Pitch toggle and readout (bottom)
    pitchBtn.setBounds    (startX, 330, 120, 25);
    pitchReadout.setBounds(startX, 360, 120, 30);
}

// ================================================================
// Knob helper
// ================================================================
void VortexAudioProcessorEditor::makeKnob(juce::Slider& s,
                                           juce::Label&  l,
                                           const juce::String& text)
{
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF0A1520));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(s);

    l.setText(text, juce::dontSendNotification);
    l.setFont(juce::Font(10.0f, juce::Font::bold));
    l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    l.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(l);
}
