#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace VortexDSP
{
    constexpr float PHI   = 1.618033988f;
    constexpr float ALPHA = 1.0f / PHI;   // ~0.618 — EMA smoothing factor

    // =========================================================
    // 1. Phi-EMA  –  real-time exponential moving average
    //    Maps to: smooth_series()
    // =========================================================
    class PhiEMA
    {
    public:
        void reset(float initial = 0.0f) { ema = initial; }

        inline float process(float input)
        {
            ema = ALPHA * input + (1.0f - ALPHA) * ema;
            return ema;
        }

        float current() const { return ema; }

    private:
        float ema = 0.0f;
    };

    // =========================================================
    // 2. Circular Rolling Window  –  lock-free, no heap alloc
    //    Maps to: rolling_window()
    // =========================================================
    template <int MaxSize>
    class CircularWindow
    {
    public:
        void reset()  { head = 0; count = 0; }
        void setSize(int n) { size = std::min(n, MaxSize); reset(); }

        void push(float v)
        {
            buf[head] = v;
            head = (head + 1) % size;
            if (count < size) ++count;
        }

        int  filled()        const { return count; }
        bool isFull()        const { return count == size; }

        // Iterate in insertion order (oldest first)
        float operator[](int i) const
        {
            int oldest = isFull() ? head : 0;
            return buf[(oldest + i) % size];
        }

        float mean() const
        {
            if (count == 0) return 0.0f;
            float s = 0.0f;
            for (int i = 0; i < count; ++i) s += (*this)[i];
            return s / count;
        }

        float variance() const
        {
            if (count < 2) return 0.0f;
            float m = mean();
            float s = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                float d = (*this)[i] - m;
                s += d * d;
            }
            return s / count;
        }

        float stddev() const { return std::sqrt(variance()); }

        float phiWeightedMean() const
        {
            if (count == 0) return 0.0f;
            float wSum = 0.0f, total = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                float w = std::pow(PHI, (float)i);
                wSum  += (*this)[i] * w;
                total += w;
            }
            return wSum / total;
        }

    private:
        float buf[MaxSize] = {};
        int   head  = 0;
        int   count = 0;
        int   size  = MaxSize;
    };

    // =========================================================
    // 3. Anomaly Detector  –  maps to: anomaly_score()
    // =========================================================
    struct AnomalyResult
    {
        float score;      // 0-1, higher = more anomalous
        float zScore;
        bool  isAnomaly;
        int   direction;  // -1=low, 0=normal, +1=high
    };

    class AnomalyDetector
    {
    public:
        void setWindowSize(int n) { window.setSize(n); }

        AnomalyResult process(float input)
        {
            window.push(input);

            if (window.filled() < 3)
                return { 0.0f, 0.0f, false, 0 };

            float m   = window.mean();
            float std = window.stddev();
            if (std < 1e-9f) std = 1e-9f;

            float z     = (input - m) / std;
            float score = std::tanh(std::abs(z) / 2.0f);

            int dir = (z > 1.5f) ? 1 : (z < -1.5f) ? -1 : 0;

            return { score, z, score > 0.7f, dir };
        }

    private:
        CircularWindow<64> window;
    };

    // =========================================================
    // 4. Envelope Follower  –  attack/release style
    // =========================================================
    class EnvelopeFollower
    {
    public:
        void prepare(double sampleRate)
        {
            sr = (float)sampleRate;
            setAttackMs(5.0f);
            setReleaseMs(80.0f);
        }

        void setAttackMs(float ms)
        {
            attackCoeff = std::exp(-1.0f / (sr * ms * 0.001f));
        }

        void setReleaseMs(float ms)
        {
            releaseCoeff = std::exp(-1.0f / (sr * ms * 0.001f));
        }

        float process(float input)
        {
            float rect = std::abs(input);
            float coeff = (rect > env) ? attackCoeff : releaseCoeff;
            env = coeff * env + (1.0f - coeff) * rect;
            return env;
        }

        float current() const { return env; }

    private:
        float sr           = 44100.0f;
        float attackCoeff  = 0.0f;
        float releaseCoeff = 0.0f;
        float env          = 0.0f;
    };

    // =========================================================
    // 5. De-Esser  –  anomaly-driven sibilance detector
    // =========================================================
    class DeEsser
    {
    public:
        void prepare(double sampleRate)
        {
            sr = (float)sampleRate;
            hfEnv.prepare(sampleRate);
            hfEnv.setAttackMs(1.0f);
            hfEnv.setReleaseMs(30.0f);
            anomaly.setWindowSize(32);
        }

        // Call once per sample; returns gain multiplier (0-1)
        float process(float input, float fullEnv)
        {
            // Simple 1-pole high-shelf approximation for sibilance band (~6kHz+)
            // A proper implementation would use a biquad; this is the concept
            float hf = input - hpState;
            hpState  = hpState + hpCoeff * (input - hpState);

            float hfLevel = hfEnv.process(hf);

            // Ratio of HF energy vs full signal
            float ratio = (fullEnv > 1e-6f) ? (hfLevel / fullEnv) : 0.0f;

            auto result = anomaly.process(ratio);

            if (result.isAnomaly && result.direction == 1)
            {
                // Smoothly reduce gain when sibilance spike detected
                gainTarget = 1.0f - (result.score * threshold);
            }
            else
            {
                gainTarget = 1.0f;
            }

            // Smooth gain changes
            gainSmooth = 0.99f * gainSmooth + 0.01f * gainTarget;
            return gainSmooth;
        }

        void setThreshold(float t) { threshold = t; } // 0-1

    private:
        float           sr          = 44100.0f;
        float           hpCoeff     = 0.85f;   // hi-pass pole
        float           hpState     = 0.0f;
        float           threshold   = 0.6f;
        float           gainTarget  = 1.0f;
        float           gainSmooth  = 1.0f;
        EnvelopeFollower hfEnv;
        AnomalyDetector  anomaly;
    };

    // =========================================================
    // 6. Pitch Tracker  –  autocorrelation (YIN-lite)
    //    Returns smoothed pitch in Hz
    // =========================================================
    class PitchTracker
    {
    public:
        static constexpr int FRAME_SIZE = 1024;

        void prepare(double sampleRate)
        {
            sr = (float)sampleRate;
            framePos = 0;
            pitchSmooth.reset(0.0f);
        }

        // Feed one sample; returns smoothed pitch Hz (0 = unvoiced)
        float process(float input)
        {
            frame[framePos++] = input;

            if (framePos >= FRAME_SIZE)
            {
                framePos = 0;
                float detectedHz = detectPitch();
                if (detectedHz > 50.0f && detectedHz < 1000.0f)
                    pitchSmooth.process(detectedHz);
            }

            return pitchSmooth.current();
        }

    private:
        float detectPitch()
        {
            // Simplified autocorrelation
            int   minLag = (int)(sr / 800.0f);
            int   maxLag = (int)(sr / 60.0f);
            float bestCorr = -1.0f;
            int   bestLag  = 0;

            for (int lag = minLag; lag <= maxLag && lag < FRAME_SIZE / 2; ++lag)
            {
                float corr = 0.0f;
                for (int n = 0; n < FRAME_SIZE / 2; ++n)
                    corr += frame[n] * frame[n + lag];

                if (corr > bestCorr)
                {
                    bestCorr = corr;
                    bestLag  = lag;
                }
            }

            return (bestLag > 0) ? (sr / bestLag) : 0.0f;
        }

        float  frame[FRAME_SIZE] = {};
        int    framePos = 0;
        float  sr = 44100.0f;
        PhiEMA pitchSmooth;
    };

} // namespace VortexDSP
