#pragma once
#include <JuceHeader.h>
#include "VortexDSP.h"

// ================================================================
// VortexVocalEngine — PluginProcessor.h
// Audio Unit / VST3 compatible vocal processing plugin
// ================================================================

class VortexAudioProcessor  : public juce::AudioProcessor,
                               public juce::AudioProcessorValueTreeState::Listener
{
public:
    VortexAudioProcessor();
    ~VortexAudioProcessor() override;

    // ── AudioProcessor interface ──────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VortexVocalEngine"; }

    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void parameterChanged (const juce::String& parameterID, float newValue) override;

    // ── Parameter layout ─────────────────────────────────────
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts {
        *this, nullptr, "Parameters", createParameterLayout()
    };

    // ── Metering (read by GUI, written by audio thread) ──────
    std::atomic<float> inputLevelDb   { -60.0f };
    std::atomic<float> outputLevelDb  { -60.0f };
    std::atomic<float> anomalyScore   {   0.0f };
    std::atomic<float> currentPitchHz {   0.0f };
    std::atomic<float> deEsserGain    {   1.0f };

private:
    // ── DSP objects (one per channel, stereo) ────────────────
    VortexDSP::EnvelopeFollower envFollower[2];
    VortexDSP::PhiEMA           levelSmoother[2];
    VortexDSP::AnomalyDetector  spikeDetector[2];
    VortexDSP::DeEsser          deEsser[2];
    VortexDSP::PitchTracker     pitchTracker;   // mono pitch detection
    VortexDSP::PhiEMA           gainSmooth[2];

    // ── Cached parameter values ───────────────────────────────
    std::atomic<float>* pInputGain    = nullptr;
    std::atomic<float>* pOutputGain   = nullptr;
    std::atomic<float>* pSmoothing    = nullptr;
    std::atomic<float>* pDeEsser      = nullptr;
    std::atomic<float>* pAnomaly      = nullptr;
    std::atomic<float>* pPitchCorrect = nullptr;
    std::atomic<float>* pMix          = nullptr;

    // Gain correction driven by anomaly detector
    float anomalyGain[2] = { 1.0f, 1.0f };

    static constexpr float DB_FLOOR = -60.0f;
    static float toDb(float linear)
    {
        return (linear > 1e-7f) ? 20.0f * std::log10(linear) : DB_FLOOR;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VortexAudioProcessor)
};
