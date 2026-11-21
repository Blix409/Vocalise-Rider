#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using Param = juce::AudioParameterFloat;
    using Range = juce::NormalisableRange<float>;
}

juce::AudioProcessorValueTreeState::ParameterLayout VocalRiderProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    // Every one of these shows up in Logic's automation menu.
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "autoTarget", 1 }, "Auto target", true));
    l.add (std::make_unique<Param> (juce::ParameterID { "target", 1 }, "Target loudness (dBFS)",
                                    Range (-50.0f, -5.0f, 0.1f), -22.0f));
    l.add (std::make_unique<Param> (juce::ParameterID { "loudAbove", 1 }, "Loud line: dB above target",
                                    Range (0.0f, 24.0f, 0.1f), 6.0f));
    l.add (std::make_unique<Param> (juce::ParameterID { "quietBelow", 1 }, "Quiet line: dB below target",
                                    Range (0.0f, 24.0f, 0.1f), 6.0f));
    l.add (std::make_unique<Param> (juce::ParameterID { "leveling", 1 }, "Leveling (0-10)",
                                    Range (0.0f, 10.0f, 0.1f), 5.0f));
    l.add (std::make_unique<Param> (juce::ParameterID { "gate", 1 }, "Silence threshold (dBFS)",
                                    Range (-80.0f, -30.0f, 0.1f), -50.0f));
    l.add (std::make_unique<Param> (juce::ParameterID { "speed", 1 }, "Speed (0-10)",
                                    Range (0.0f, 10.0f, 0.1f), 5.0f));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "smartSilence", 1 }, "Smart silence", true));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "protectS", 1 }, "Protect S sounds", true));
    l.add (std::make_unique<Param> (juce::ParameterID { "output", 1 }, "Output volume (dB)",
                                    Range (-24.0f, 12.0f, 0.1f), 0.0f));
    // Ride-recording workflow. In WRITE mode the plugin moves "Ride (dB)" by itself, so Logic's
    // Latch/Touch/Write records it as automation. In READ mode the plugin applies whatever "Ride (dB)" says.
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Mode (Live/Write/Read)",
                                                         juce::StringArray { "Live", "Write", "Read" }, 0));
    l.add (std::make_unique<Param> (juce::ParameterID { "rideOut", 1 }, "Ride (dB)",
                                    Range (-18.0f, 12.0f, 0.01f), 0.0f));
    return l;
}

VocalRiderProcessor::VocalRiderProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pAuto         = apvts.getRawParameterValue ("autoTarget");
    pTarget       = apvts.getRawParameterValue ("target");
    pLoud         = apvts.getRawParameterValue ("loudAbove");
    pQuiet        = apvts.getRawParameterValue ("quietBelow");
    pLeveling     = apvts.getRawParameterValue ("leveling");
    pGate         = apvts.getRawParameterValue ("gate");
    pSpeed        = apvts.getRawParameterValue ("speed");
    pSmartSilence = apvts.getRawParameterValue ("smartSilence");
    pProtectS     = apvts.getRawParameterValue ("protectS");
    pOutput       = apvts.getRawParameterValue ("output");
    pMode         = apvts.getRawParameterValue ("mode");
    pRideOut      = apvts.getRawParameterValue ("rideOut");
    rideParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("rideOut"));
}

void VocalRiderProcessor::endRideWrite()
{
    if (writeGestureOpen && rideParam != nullptr) rideParam->endChangeGesture();
    writeGestureOpen = false;
    lastWritten = 1000.0f;
}

bool VocalRiderProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void VocalRiderProcessor::prepareToPlay (double sampleRate, int)
{
    core.prepare (sampleRate);
    setLatencySamples (core.getLatencySamples());  // Logic compensates for the lookahead delay
    outGain.reset (sampleRate, 0.03);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
}

RiderParams VocalRiderProcessor::readParams() const
{
    RiderParams p;
    p.autoTarget   = pAuto->load() > 0.5f;
    p.targetDb     = pTarget->load();
    p.loudAbove    = pLoud->load();
    p.quietBelow   = pQuiet->load();
    p.leveling     = pLeveling->load();
    p.gateDb       = pGate->load();
    p.speed        = pSpeed->load();
    p.smartSilence = pSmartSilence->load() > 0.5f;
    p.protectS     = pProtectS->load() > 0.5f;
    return p;
}

void VocalRiderProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = std::min (buffer.getNumChannels(), RiderCore::kMaxChannels);
    const int n = buffer.getNumSamples();
    if (numCh < 1) return;

    const int mode = juce::jlimit (0, 2, (int) std::lround (pMode->load()));
    core.setFollow (mode == 2, pRideOut->load());                 // READ: apply the recorded ride
    core.process (buffer.getArrayOfWritePointers(), numCh, n, readParams());

    // WRITE: publish the ride as the "Ride (dB)" parameter while the transport runs
    bool playing = true;                                          // standalone has no transport
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            playing = pos->getIsPlaying();
    if (mode == 1 && playing && rideParam != nullptr)
    {
        if (! writeGestureOpen) { rideParam->beginChangeGesture(); writeGestureOpen = true; }
        const float v = juce::jlimit (-18.0f, 12.0f, core.getCurrentRideDb());
        if (std::abs (v - lastWritten) > 0.05f)
        {
            rideParam->setValueNotifyingHost (rideParam->convertTo0to1 (v));
            lastWritten = v;
        }
    }
    else if (writeGestureOpen)
        endRideWrite();

    // main (output) volume, smoothed so moving it never clicks
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    if (outGain.isSmoothing())
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = outGain.getNextValue();
            for (int c = 0; c < numCh; ++c) buffer.getWritePointer (c)[i] *= g;
        }
    }
    else buffer.applyGain (outGain.getTargetValue());

    // peak meter (after the output volume, i.e. what actually leaves the plugin)
    for (int c = 0; c < numCh; ++c)
    {
        const float mag = buffer.getMagnitude (c, 0, n);
        auto& pk = peak[c];
        float cur = pk.load (std::memory_order_relaxed);
        while (mag > cur && ! pk.compare_exchange_weak (cur, mag, std::memory_order_relaxed)) {}
    }
    if (numCh == 1)                                            // mono: show the same peak on both bars
        peak[1].store (peak[0].load (std::memory_order_relaxed), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* VocalRiderProcessor::createEditor()
{
    return new VocalRiderEditor (*this);
}

void VocalRiderProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void VocalRiderProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalRiderProcessor();
}
