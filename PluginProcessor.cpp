#include "PluginProcessor.h"
#include "PluginEditor.h"

// ================================================================
// Parameter IDs
// ================================================================
namespace ParamID
{
    const juce::String InputGain    = "inputGain";
    const juce::String OutputGain   = "outputGain";
    const juce::String Smoothing    = "smoothing";
    const juce::String DeEsser      = "deEsser";
    const juce::String Anomaly      = "anomaly";
    const juce::String PitchCorrect = "pitchCorrect";
    const juce::String Mix          = "mix";
}

// ================================================================
// Parameter Layout
// ================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VortexAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::InputGain, "Input Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, "dB"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::OutputGain, "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, "dB"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::Smoothing, "Level Smoothing",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::DeEsser, "De-Esser",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::Anomaly, "Anomaly Sensitivity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        ParamID::PitchCorrect, "Pitch Tracking", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::Mix, "Dry/Wet Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    return { params.begin(), params.end() };
}

// ================================================================
// Constructor / Destructor
// ================================================================
VortexAudioProcessor::VortexAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    pInputGain    = apvts.getRawParameterValue(ParamID::InputGain);
    pOutputGain   = apvts.getRawParameterValue(ParamID::OutputGain);
    pSmoothing    = apvts.getRawParameterValue(ParamID::Smoothing);
    pDeEsser      = apvts.getRawParameterValue(ParamID::DeEsser);
    pAnomaly      = apvts.getRawParameterValue(ParamID::Anomaly);
    pPitchCorrect = apvts.getRawParameterValue(ParamID::PitchCorrect);
    pMix          = apvts.getRawParameterValue(ParamID::Mix);

    apvts.addParameterListener(ParamID::DeEsser, this);
    apvts.addParameterListener(ParamID::Anomaly, this);
}

VortexAudioProcessor::~VortexAudioProcessor()
{
    apvts.removeParameterListener(ParamID::DeEsser, this);
    apvts.removeParameterListener(ParamID::Anomaly, this);
}

// ================================================================
// Prepare
// ================================================================
void VortexAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    for (int ch = 0; ch < 2; ++ch)
    {
        envFollower[ch].prepare(sampleRate);
        envFollower[ch].setAttackMs(5.0f);
        envFollower[ch].setReleaseMs(100.0f);

        spikeDetector[ch].setWindowSize(48);
        deEsser[ch].prepare(sampleRate);
        deEsser[ch].setThreshold(pDeEsser->load());

        levelSmoother[ch].reset();
        gainSmooth[ch].reset(1.0f);
    }

    pitchTracker.prepare(sampleRate);
}

void VortexAudioProcessor::releaseResources() {}

// ================================================================
// Process Block
// ================================================================
void VortexAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Read parameters (atomic load, once per block)
    const float inputGainLin  = juce::Decibels::decibelsToGain(pInputGain->load());
    const float outputGainLin = juce::Decibels::decibelsToGain(pOutputGain->load());
    const float deEsserAmt    = pDeEsser->load();
    const float anomalySens   = pAnomaly->load();
    const float mix           = pMix->load();
    const bool  pitchOn       = pPitchCorrect->load() > 0.5f;

    // Per-block level accumulators
    float inPeak  = 0.0f;
    float outPeak = 0.0f;
    float anomSum = 0.0f;
    float deEsGain = 1.0f;

    for (int ch = 0; ch < numChannels && ch < 2; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float dry = data[i] * inputGainLin;

            // ── Envelope ─────────────────────────────────────
            float env = envFollower[ch].process(dry);

            // Track input peak
            inPeak = std::max(inPeak, std::abs(dry));

            // ── Anomaly detection (spike suppression) ────────
            auto anom = spikeDetector[ch].process(env);
            anomSum += anom.score;

            float anoGainTarget = 1.0f;
            if (anom.isAnomaly && anom.direction == 1)
            {
                // Attenuate spike proportional to sensitivity
                anoGainTarget = 1.0f - (anom.score * anomalySens * 0.5f);
            }
            // Smooth gain changes over time (avoid clicks)
            anomalyGain[ch] = 0.995f * anomalyGain[ch] + 0.005f * anoGainTarget;

            // ── De-Esser ─────────────────────────────────────
            float deGain = deEsser[ch].process(dry, env);
            deGain = 1.0f - deEsserAmt * (1.0f - deGain); // blend by param
            deEsGain = std::min(deEsGain, deGain);

            // ── Combine gain ──────────────────────────────────
            float totalGain = anomalyGain[ch] * deGain;
            float wet = dry * totalGain;

            // ── Pitch tracking (ch 0 only for perf) ──────────
            if (pitchOn && ch == 0)
                currentPitchHz.store(pitchTracker.process(wet),
                                      std::memory_order_relaxed);

            // ── Dry/wet mix ───────────────────────────────────
            float out = wet * mix + dry * (1.0f - mix);
            out *= outputGainLin;

            outPeak = std::max(outPeak, std::abs(out));
            data[i] = out;
        }
    }

    // Update meters (relaxed — GUI reads with slight delay, that's fine)
    inputLevelDb.store  (toDb(inPeak),  std::memory_order_relaxed);
    outputLevelDb.store (toDb(outPeak), std::memory_order_relaxed);
    anomalyScore.store  (anomSum / (float)(numChannels * numSamples + 1),
                          std::memory_order_relaxed);
    deEsserGain.store   (deEsGain, std::memory_order_relaxed);
}

// ================================================================
// Parameter listener
// ================================================================
void VortexAudioProcessor::parameterChanged(const juce::String& id, float v)
{
    if (id == ParamID::DeEsser)
        for (int ch = 0; ch < 2; ++ch)
            deEsser[ch].setThreshold(v);
}

// ================================================================
// State save / restore
// ================================================================
void VortexAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void VortexAudioProcessor::setStateInformation(const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// ================================================================
// Factory
// ================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VortexAudioProcessor();
}
