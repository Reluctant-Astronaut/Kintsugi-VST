#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

// Small building blocks for Kintsugi: transfer curves, simple filters and slow random movement.
namespace analog
{
    constexpr float pi = 3.14159265358979f;
    constexpr float twoPi = 6.28318530717959f;

    inline float dbToGain (float db) { return std::exp (db * 0.11512925f); }

    // Fast tanh (rational approximation, error < 1e-4), used in the 4x oversampled loop
    inline float fastTanh (float x)
    {
        x = std::max (-4.97f, std::min (4.97f, x));
        const float x2 = x * x;
        const float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
        const float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
        return std::max (-1.0f, std::min (1.0f, a / b));
    }

    //==========================================================================
    // Transfer curves. All have slope ~1 around zero, so a quiet signal passes unchanged.

    // Console line amp: a firm but rounded knee (mostly odd harmonics), plus a whisper of even.
    inline float consoleCurve (float u, float asym)
    {
        const float u2 = u * u;
        const float y = u / std::sqrt (std::sqrt (1.0f + u2 * u2));   // knee of order 4
        const float t = fastTanh (u);
        return y + asym * t * t;
    }

    // Valve curves. 'b' is the bias point, which sets how lopsided (even-harmonic) the tube is.
    inline float warmTriode (float u, float b)
    {
        const float tb = fastTanh (b);
        return (fastTanh (u + b) - tb) / (1.0f - tb * tb);
    }

    inline float cubicSoft (float u) { return u / std::cbrt (1.0f + std::abs (u * u * u)); }

    inline float gritPentode (float u, float b)
    {
        const float slope = std::pow (1.0f + std::abs (b * b * b), -4.0f / 3.0f);   // depends only on bias
        return (cubicSoft (u + b) - cubicSoft (b)) / slope;
    }

    inline float sixSoft (float u)
    {
        const float a = std::abs (u);
        const float a2 = a * a;
        return u / std::pow (1.0f + a2 * a2 * a2, 1.0f / 6.0f);
    }

    // Fuzz: hard-ish and lopsided; the negative side clips earlier as bias goes up (a starved transistor)
    inline float fuzzCurve (float u, float bias)
    {
        auto shape = [bias] (float v)
        {
            const float neg = 1.0f - 0.72f * bias;
            return v >= 0.0f ? sixSoft (v) : neg * sixSoft (v / neg);
        };
        const float off = 0.3f * bias;
        return shape (u + off) - shape (off);
    }

    // Tape: a very soft, wide knee with a little asymmetry
    inline float tapeCurve (float u)
    {
        const float y = u / std::sqrt (1.0f + u * u);   // a very soft, wide knee
        const float t = fastTanh (u * 0.5f);
        return y + 0.035f * t * t;
    }

    //==========================================================================
    struct OnePoleLP
    {
        float a = 1.0f, z = 0.0f;
        void setCutoff (float hz, float sr) { a = 1.0f - std::exp (-twoPi * std::min (hz, sr * 0.49f) / sr); }
        float process (float x) { z += a * (x - z); return z; }
        void reset() { z = 0.0f; }
    };

    struct OnePoleHP
    {
        OnePoleLP lp;
        void setCutoff (float hz, float sr) { lp.setCutoff (hz, sr); }
        float process (float x) { return x - lp.process (x); }
        void reset() { lp.reset(); }
    };

    // Envelope follower with separate attack and release
    struct Follower
    {
        float att = 0.0f, rel = 0.0f, z = 0.0f;
        void set (float attackMs, float releaseMs, float sr)
        {
            att = 1.0f - std::exp (-1.0f / (attackMs * 0.001f * sr));
            rel = 1.0f - std::exp (-1.0f / (releaseMs * 0.001f * sr));
        }
        float process (float x) { z += (x > z ? att : rel) * (x - z); return z; }
        void reset() { z = 0.0f; }
    };

    // Fast, allocation-free noise for the audio thread
    struct Noise
    {
        uint32_t s = 0x9e3779b9u;
        void seed (uint32_t v) { s = v ? v : 0x12345u; }
        uint32_t nextU() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float uni() { return (float) (nextU() >> 8) * (1.0f / 16777216.0f); }   // 0..1
        float bi()  { return uni() * 2.0f - 1.0f; }                              // -1..1
    };

    // A value that wanders smoothly between random targets (-1..1), each leg taking
    // between minSec and maxSec. Used for drift, the irregular part of wow, and azimuth.
    struct Wander
    {
        float from = 0.0f, to = 0.0f, phase = 1.0f, inc = 0.0f, minSec = 1.0f, maxSec = 2.0f, sr = 48000.0f;
        Noise rng;

        void prepare (float sampleRate, float minS, float maxS, uint32_t seed)
        {
            sr = sampleRate; minSec = minS; maxSec = maxS; rng.seed (seed);
            from = to = rng.bi() * 0.5f; phase = 1.0f;
        }

        float next (int step = 1)
        {
            phase += inc * (float) step;
            if (phase >= 1.0f)
            {
                from = to;
                to = rng.bi();
                phase = 0.0f;
                inc = 1.0f / ((minSec + (maxSec - minSec) * rng.uni()) * sr);
            }
            const float s = 0.5f - 0.5f * std::cos (pi * phase);   // smooth (cosine) easing between targets
            return from + (to - from) * s;
        }
    };
}
