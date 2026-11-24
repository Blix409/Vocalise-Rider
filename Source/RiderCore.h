// RiderCore.h - real-time vocal level rider DSP. No JUCE dependency.
//
// How it decides the ride (per 10 ms frame, from the measured loudness x of the vocal):
//   1. Steer toward the TARGET:   y1 = x + (leveling/10) * (target - x)
//   2. Keep it between the lines: y2 = clamp(y1, quietLine, loudLine)
//      (loudLine = target + loudAbove, quietLine = target - quietBelow)
//   3. The ride (gain) is y2 - x, then smoothed by SPEED (pulling down is faster than coming back up).
//   SMART SILENCE: while the vocal is silent/breathing, a boost relaxes back to 0 dB instead of being held.
//   PROTECT S SOUNDS: sibilant frames (mostly very high frequencies) never move the ride.
// The audio itself is only ever MULTIPLIED by a smooth gain: no compression, EQ or filtering.
// The audio path is delayed by a fixed lookahead so the ride is already in place when a word arrives.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

struct RiderParams
{
    float targetDb     = -22.0f;  // loudness the vocal is steered toward (dBFS RMS), when autoTarget is false
    bool  autoTarget   = false;   // if true, target follows the long-term average loudness of the singing
    float loudAbove    = 6.0f;    // dB above the target: the "loud line" (loud parts are pulled down to it)
    float quietBelow   = 6.0f;    // dB below the target: the "quiet line" (quiet parts are raised up to it)
    float leveling     = 5.0f;    // 0 = only the two lines act, 10 = fully steer everything to the target
    float gateDb       = -50.0f;  // below this the vocal counts as silent (breaths, room noise)
    float speed        = 5.0f;    // 0 = slow and gentle, 10 = fast and tight
    bool  smartSilence = true;    // relax boosts back to 0 dB during silence
    bool  protectS     = true;    // sibilants don't move the ride
};

// speed 0..10 -> how long a loud part takes to be pulled down / a quiet part to be brought back up
inline void speedToMs (float speed, float& attackMs, float& releaseMs)
{
    const float s = std::clamp (speed, 0.0f, 10.0f);
    attackMs  = 160.0f * std::pow (0.1f, s / 10.0f);   // 160 ms (slow) ... 16 ms (fast)
    releaseMs = attackMs * 6.0f;                       // 960 ms ... 96 ms
}

class RiderCore
{
public:
    static constexpr double kLookaheadSeconds = 0.030;      // default
    static constexpr double kMaxLookaheadSeconds = 0.100;
    static constexpr double kHopSeconds       = 0.010;
    static constexpr int    kLookaheadFrames  = 3;
    static constexpr int    kWindowFrames     = 8;       // 8 x 10 ms = 80 ms measurement window
    static constexpr double kAutoTargetTau    = 10.0;    // seconds
    static constexpr double kGainSmoothSeconds = 0.015;  // extra smoothing of the gain (avoids grainy sound)
    static constexpr float  kMaxBoostDb       = 12.0f;   // safety caps on the ride
    static constexpr float  kMaxCutDb         = 18.0f;
    static constexpr float  kSibilantRatio    = 0.55f;   // share of energy above ~5 kHz that marks a sibilant frame
    static constexpr int    kMaxChannels      = 2;
    static constexpr int    kHistory          = 1000;    // 1000 frames x 10 ms = 10 seconds
    static constexpr int    kHistBins         = 96;      // level histogram: -95 .. 0 dB in 1 dB bins

    //==========================================================================
    // The ride decision for one frame. Used live AND by the screen to preview the "after" trace.
    struct RideDecider
    {
        float followed = 0.0f, lastDesired = 0.0f;
        void reset() { followed = lastDesired = 0.0f; }

        float step (float levelDb, bool sibilant, float target, const RiderParams& p)
        {
            const bool singing = levelDb > p.gateDb;
            if (singing && ! (sibilant && p.protectS))
            {
                const float loudLine  = target + std::max (0.0f, p.loudAbove);
                const float quietLine = target - std::max (0.0f, p.quietBelow);
                const float k = std::clamp (p.leveling, 0.0f, 10.0f) * 0.1f;
                const float y1 = levelDb + k * (target - levelDb);          // steer toward the target
                const float y2 = std::clamp (y1, quietLine, loudLine);      // keep between the two lines
                lastDesired = std::clamp (y2 - levelDb, -kMaxCutDb, kMaxBoostDb);
            }
            else if (! singing && p.smartSilence)
            {
                lastDesired = std::min (lastDesired, 0.0f);                 // don't keep amplifying noise
            }
            // otherwise (breath with Smart Silence off, or a sibilant): hold the ride

            float attackMs, releaseMs;
            speedToMs (p.speed, attackMs, releaseMs);
            const float err = lastDesired - followed;
            const float tauMs = err < 0.0f ? attackMs : releaseMs;
            const float a = 1.0f - std::exp (-(float) (kHopSeconds * 1000.0) / std::max (1.0f, tauMs));
            followed = std::clamp (followed + err * a, -kMaxCutDb, kMaxBoostDb);
            return followed;
        }
    };

    // Preview for the screen: run the same decisions over the stored 10 s of measured levels.
    static void simulate (const float* levelDb, const float* sibilant, int n, const RiderParams& p,
                          float target, float* gainOut)
    {
        RideDecider d;
        for (int i = 0; i < n; ++i)
            gainOut[i] = (levelDb[i] < -99.0f) ? 0.0f : d.step (levelDb[i], sibilant[i] > 0.5f, target, p);
    }

    //==========================================================================
    void prepare (double sampleRate)
    {
        const bool rateChanged = std::abs (sampleRate - sr) > 0.5;
        sr = sampleRate;
        hop = std::max (1, (int) std::lround (kHopSeconds * sr));
        lookahead = std::max (1, (int) std::lround (lookaheadMs * 0.001 * sr));
        smoothCoef = (float) (1.0 - std::exp (-1.0 / (kGainSmoothSeconds * sr)));
        const size_t maxDelay = (size_t) std::lround (kMaxLookaheadSeconds * sr) + 2;   // sized once, so changing lookahead never allocates
        for (auto& d : delay) d.assign (maxDelay, 0.0f);
        makeFilters();
        resetDsp();
        if (rateChanged || ! analysisInitialised) clearAnalysis();
    }

    // Clears the DSP state only (the screen's history and the level histogram are kept).
    void resetDsp()
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        delayPos = 0;
        for (auto& f : hp) f.z1 = f.z2 = 0.0;
        for (auto& f : hf) f.z1 = f.z2 = 0.0;
        std::fill (std::begin (frameSq), std::end (frameSq), 0.0);
        frameIdx = 0; framesSeen = 0;
        accum = hfAccum = beforeAccum = 0.0; samplesInFrame = 0;
        gainStartDb = gainEndDb = smoothedDb = currentRideDb = 0.0f;
        rampPos = 0;
        decider.reset();
    }

    void clearAnalysis()
    {
        analysisInitialised = true;
        frameCount.store (0);
        clearLevelHistogram();
        autoLevel = -30.0f; haveAuto = false;
    }

    int getLatencySamples() const { return lookahead; }
    int getLookaheadMs() const { return lookaheadMs; }
    // Advanced setting: how far the ride leads the audio (0 = off). Safe on the audio thread.
    void setLookaheadMs (int ms)
    {
        ms = std::max (0, std::min (ms, (int) (kMaxLookaheadSeconds * 1000.0)));
        if (ms == lookaheadMs) return;
        lookaheadMs = ms;
        lookahead = std::max (1, (int) std::lround (lookaheadMs * 0.001 * sr));
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        delayPos = 0;
    }
    float getCurrentRideDb() const { return currentRideDb; }
    // Read mode: ignore the analysis and apply this gain (the ride Logic plays back from automation).
    void setFollow (bool on, float db) { if (! on) followPrev = db; followOn = on; followDb = db; }

    // In-place processing. numChannels = 1 or 2. Works for any block size.
    void process (float* const* ch, int numChannels, int numSamples, const RiderParams& p)
    {
        numChannels = std::min (numChannels, kMaxChannels);
        for (int n = 0; n < numSamples; ++n)
        {
            // ---- measurement (on the undelayed input) ----
            double energy = 0.0, hfEnergy = 0.0;
            for (int c = 0; c < numChannels; ++c)
            {
                const double y = hp[c].run (ch[c][n]);
                energy += y * y;
                if (hfOn) { const double h = hf[c].run (ch[c][n]); hfEnergy += h * h; }
            }
            accum += energy / numChannels;
            hfAccum += hfEnergy / numChannels;

            // ---- gain for this sample: ramp across the hop, then smoothed ----
            const float t = (float) rampPos / (float) hop;
            const float rampDb = followOn ? followDb : gainStartDb + (gainEndDb - gainStartDb) * std::min (1.0f, t);
            if (rampPos < hop) ++rampPos;
            if (followOn)                                         // Read: glide straight to the recorded value over this block
            {
                const float f = (float) (n + 1) / (float) numSamples;
                smoothedDb = followPrev + (followDb - followPrev) * f;
            }
            else
                smoothedDb += smoothCoef * (rampDb - smoothedDb);
            currentRideDb = smoothedDb;
            const float g = std::pow (10.0f, smoothedDb * 0.05f);

            // ---- delayed audio path ----
            double inE = 0.0;
            for (int c = 0; c < numChannels; ++c)
            {
                float& slot = delay[c][(size_t) delayPos];
                const float delayed = slot;
                slot = ch[c][n];
                ch[c][n] = delayed * g;
                inE += (double) delayed * delayed;
            }
            beforeAccum += inE / numChannels;
            if (++delayPos >= lookahead) delayPos = 0;

            if (++samplesInFrame >= hop)
                closeFrame (p);
        }
        followPrev = followDb;
    }

    // ---- read-only info for the plugin screen (safe to call from the UI thread) ----
    // Last 'kHistory' frames, oldest first. before = level of the audio (dB), level = what the detector
    // measured, sibilant = 1 for sibilant frames. -100 means "nothing yet".
    void readHistory (float* before, float* level, float* sibilant) const
    {
        const uint32_t cnt = frameCount.load (std::memory_order_acquire);
        for (int i = 0; i < kHistory; ++i)
        {
            const int64_t age = (int64_t) kHistory - 1 - i;           // 0 = newest
            const int64_t idx = (int64_t) cnt - 1 - age;
            if (idx < 0) { before[i] = level[i] = -100.0f; sibilant[i] = 0.0f; continue; }
            const size_t s = (size_t) (idx % kHistory);
            before[i]    = histBefore[s].load (std::memory_order_relaxed);
            level[i]     = histLevel[s].load (std::memory_order_relaxed);
            sibilant[i]  = histSib[s].load (std::memory_order_relaxed);
        }
    }
    float getShownTargetDb() const { return shownTarget.load (std::memory_order_relaxed); }
    float getAutoLevelDb() const { return autoLevelShown.load (std::memory_order_relaxed); }

    // Histogram of every measured level since it was last cleared (used by AUTO SET).
    void readLevelHistogram (uint32_t* out) const
    {
        for (int i = 0; i < kHistBins; ++i) out[i] = levelHist[i].load (std::memory_order_relaxed);
    }
    void clearLevelHistogram()
    {
        for (auto& b : levelHist) b.store (0, std::memory_order_relaxed);
    }
    static float histBinToDb (int bin) { return (float) bin - 95.0f + 0.5f; }

private:
    bool followOn = false;
    float followDb = 0.0f, followPrev = 0.0f;
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    static Biquad highpass (double fc, double sr)
    {
        const double pi = 3.14159265358979323846;
        const double w0 = 2.0 * pi * fc / sr, cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.70710678);
        const double a0 = 1.0 + alpha;
        Biquad k;
        k.b0 = ((1.0 + cw) * 0.5) / a0;
        k.b1 = -(1.0 + cw) / a0;
        k.b2 = k.b0;
        k.a1 = (-2.0 * cw) / a0;
        k.a2 = (1.0 - alpha) / a0;
        return k;
    }

    void makeFilters()
    {
        for (auto& f : hp) f = highpass (100.0, sr);              // detector: ignore rumble/plosive thumps
        hfOn = sr >= 32000.0;                                     // sibilance detector: energy above ~5 kHz
        for (auto& f : hf) f = highpass (5000.0, sr);
    }

    static float toDb (double ms) { return (float) (10.0 * std::log10 (std::max (ms, 1e-12))); }

    void closeFrame (const RiderParams& p)
    {
        // store this 10 ms frame's energy, compute mean-square over the last up-to-8 frames
        frameSq[frameIdx] = accum;
        frameIdx = (frameIdx + 1) % kWindowFrames;
        framesSeen = std::min (framesSeen + 1, kWindowFrames);

        double sum = 0.0;
        for (int i = 0; i < framesSeen; ++i) sum += frameSq[i];
        const float level = toDb (sum / ((double) framesSeen * hop));
        const bool sibilant = hfOn && accum > 1e-12 && (hfAccum / accum) > (double) kSibilantRatio;

        const bool singing = level > p.gateDb;
        if (singing && ! sibilant)
        {
            if (! haveAuto) { autoLevel = level; haveAuto = true; }
            else
            {
                const float a = (float) (1.0 - std::exp (-kHopSeconds / kAutoTargetTau));
                autoLevel += a * (level - autoLevel);
            }
        }
        const float target = p.autoTarget && haveAuto ? autoLevel : p.targetDb;

        gainStartDb = gainEndDb;
        gainEndDb = decider.step (level, sibilant, target, p);
        rampPos = 0;

        // ---- record for the screen ----
        const uint32_t cnt = frameCount.load (std::memory_order_relaxed);
        const size_t s = (size_t) (cnt % (uint32_t) kHistory);
        histBefore[s].store (toDb (beforeAccum / hop), std::memory_order_relaxed);
        histLevel[s].store (level, std::memory_order_relaxed);
        histSib[s].store (sibilant ? 1.0f : 0.0f, std::memory_order_relaxed);
        frameCount.store (cnt + 1, std::memory_order_release);
        shownTarget.store (target, std::memory_order_relaxed);
        autoLevelShown.store (autoLevel, std::memory_order_relaxed);
        const int bin = std::clamp ((int) std::floor (level + 95.0f), 0, kHistBins - 1);
        levelHist[bin].fetch_add (1, std::memory_order_relaxed);

        accum = hfAccum = beforeAccum = 0.0; samplesInFrame = 0;
    }

    double sr = 0.0;
    int hop = 441, lookahead = 1323, lookaheadMs = 30;
    float smoothCoef = 0.001f;
    bool hfOn = true, analysisInitialised = false;
    std::vector<float> delay[kMaxChannels];
    int delayPos = 0;
    Biquad hp[kMaxChannels], hf[kMaxChannels];
    double frameSq[kWindowFrames] = {};
    int frameIdx = 0, framesSeen = 0;
    double accum = 0.0, hfAccum = 0.0, beforeAccum = 0.0;
    int samplesInFrame = 0;
    std::atomic<float> histBefore[kHistory], histLevel[kHistory], histSib[kHistory];
    std::atomic<uint32_t> frameCount { 0 };
    std::atomic<uint32_t> levelHist[kHistBins] = {};
    std::atomic<float> shownTarget { -22.0f }, autoLevelShown { -30.0f };
    float gainStartDb = 0, gainEndDb = 0, smoothedDb = 0, currentRideDb = 0;
    int rampPos = 0;
    RideDecider decider;
    float autoLevel = -30.0f;
    bool haveAuto = false;
};
