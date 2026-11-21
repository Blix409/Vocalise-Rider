#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "RiderCore.h"

class VocalRiderProcessor : public juce::AudioProcessor
{
public:
    VocalRiderProcessor();
    ~VocalRiderProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { endRideWrite(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vocalise-Rider"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    RiderCore& getCoreForEditor() { return core; }
    const RiderCore& getCore() const { return core; }

    // Peak of the plugin's output since the last call (linear, 0..). ch = 0 left, 1 right.
    float takePeak (int ch) { return peak[juce::jlimit (0, 1, ch)].exchange (0.0f); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    RiderParams readParams() const;
    void endRideWrite();

    RiderCore core;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outGain;
    std::atomic<float> peak[2] { { 0.0f }, { 0.0f } };

    std::atomic<float>* pAuto = nullptr;
    std::atomic<float>* pTarget = nullptr;
    std::atomic<float>* pLoud = nullptr;
    std::atomic<float>* pQuiet = nullptr;
    std::atomic<float>* pLeveling = nullptr;
    std::atomic<float>* pGate = nullptr;
    std::atomic<float>* pSpeed = nullptr;
    std::atomic<float>* pSmartSilence = nullptr;
    std::atomic<float>* pProtectS = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pMode = nullptr;        // 0 = Live, 1 = Write, 2 = Read
    std::atomic<float>* pRideOut = nullptr;
    juce::AudioParameterFloat* rideParam = nullptr;
    bool writeGestureOpen = false;
    float lastWritten = 1000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalRiderProcessor)
};
