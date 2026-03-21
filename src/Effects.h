#pragma once
#define _USE_MATH_DEFINES
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ChaosOsc {

// ============================================================
//  SVF — State Variable Filter (low/high/band in one pass)
//  Topology-preserving TPT — no frequency warping issues.
//  mode: 0=LP, 1=HP, 2=BP
// ============================================================
struct SVFilter {
    float s1 = 0.f, s2 = 0.f;

    // Call every block (or every sample if cutoff changes fast)
    float process(float in, float cutoffHz, float resonance, float sr, int mode = 0)
    {
        // Clamp
        cutoffHz  = std::max(20.f, std::min((float)sr * 0.49f, cutoffHz));
        resonance = std::max(0.5f, std::min(20.f, resonance));

        float g  = std::tan((float)M_PI * cutoffHz / (float)sr);
        float R  = 1.f / (2.f * resonance);
        float h  = 1.f / (1.f + 2.f * R * g + g * g);

        float hp = h * (in - (2.f * R + g) * s1 - s2);
        float bp = g * hp + s1;
        float lp = g * bp + s2;

        s1 = g * hp + bp;
        s2 = g * bp + lp;

        if (mode == 0) return lp;
        if (mode == 1) return hp;
        return bp;
    }

    void reset() { s1 = s2 = 0.f; }
};

// ============================================================
//  Reverb — Schroeder design:
//  4 comb filters in parallel → 2 allpass filters in series
// ============================================================
struct Reverb {
    static constexpr int NUM_COMBS   = 4;
    static constexpr int NUM_ALLPASS = 2;

    // Prime-ish delay lengths (samples @ 44100)
    static constexpr int COMB_LENS[NUM_COMBS]   = {1557, 1617, 1491, 1422};
    static constexpr int AP_LENS  [NUM_ALLPASS]  = {225, 556};

    struct CombFilter {
        std::vector<float> buf;
        int   pos    = 0;
        float filter = 0.f;  // one-pole LP for damping
        void  init(int len) { buf.assign(len, 0.f); pos = 0; filter = 0.f; }
        float process(float in, float feedback, float damp) {
            float out = buf[pos];
            filter    = out * (1.f - damp) + filter * damp;
            buf[pos]  = in + filter * feedback;
            pos       = (pos + 1) % (int)buf.size();
            return out;
        }
    };

    struct AllpassFilter {
        std::vector<float> buf;
        int pos = 0;
        void  init(int len) { buf.assign(len, 0.f); pos = 0; }
        float process(float in, float g = 0.5f) {
            float buf_out = buf[pos];
            float out     = -in + buf_out;
            buf[pos]      = in + buf_out * g;
            pos           = (pos + 1) % (int)buf.size();
            return out;
        }
    };

    CombFilter   combs  [NUM_COMBS];
    AllpassFilter allpass[NUM_ALLPASS];

    void init() {
        for (int i = 0; i < NUM_COMBS;   ++i) combs  [i].init(COMB_LENS[i]);
        for (int i = 0; i < NUM_ALLPASS; ++i) allpass[i].init(AP_LENS  [i]);
    }

    // size: room size [0,1], damp: HF damping [0,1], wet: mix [0,1]
    float process(float in, float size, float damp, float wet)
    {
        float feedback = 0.28f + size * 0.67f;  // [0.28, 0.95]
        float dampF    = damp * 0.9f;

        float out = 0.f;
        for (int i = 0; i < NUM_COMBS; ++i)
            out += combs[i].process(in, feedback, dampF);
        out *= 0.25f;  // scale 4 combs

        for (int i = 0; i < NUM_ALLPASS; ++i)
            out = allpass[i].process(out);

        return in * (1.f - wet) + out * wet;
    }

    void reset() { init(); }
};

// ============================================================
//  Chorus — single modulated delay line
// ============================================================
struct Chorus {
    static constexpr int BUF_SIZE = 4096;
    float buf[BUF_SIZE] = {};
    int   writePos = 0;
    float lfoPhase = 0.f;

    float process(float in, float rateHz, float depthMs, float wet, float sr)
    {
        // LFO
        lfoPhase += rateHz / (float)sr;
        if (lfoPhase >= 1.f) lfoPhase -= 1.f;
        float lfo = std::sin(2.f * (float)M_PI * lfoPhase);

        // Modulated delay
        float delayMs  = 15.f + depthMs * lfo;    // centre 15ms ± depth
        float delaySmp = delayMs * 0.001f * (float)sr;
        delaySmp       = std::max(1.f, std::min((float)(BUF_SIZE - 2), delaySmp));

        int   d0  = (int)delaySmp;
        float t   = delaySmp - d0;
        int   ri0 = (writePos - d0 + BUF_SIZE) % BUF_SIZE;
        int   ri1 = (ri0 - 1 + BUF_SIZE) % BUF_SIZE;
        float delayed = buf[ri0] * (1.f - t) + buf[ri1] * t;

        buf[writePos] = in;
        writePos      = (writePos + 1) % BUF_SIZE;

        return in * (1.f - wet) + delayed * wet;
    }

    void reset() { std::fill(buf, buf + BUF_SIZE, 0.f); writePos = 0; lfoPhase = 0.f; }
};

// ============================================================
//  Bitcrusher — sample rate reduction + bit depth reduction
// ============================================================
struct Bitcrusher {
    float held     = 0.f;
    float phase    = 0.f;
    int   holdSamp = 1;

    // bits [1,16], rate [0,1] where 0=no reduction, 1=max reduction
    float process(float in, float bits, float rate, float sr)
    {
        // Sample rate reduction
        float targetSr = sr * (1.f - rate * 0.97f);
        float inc      = targetSr / sr;
        phase         += inc;
        if (phase >= 1.f) { phase -= 1.f; held = in; }

        // Bit depth reduction
        float levels = std::pow(2.f, std::max(1.f, bits));
        float crushed = std::round(held * levels) / levels;

        return crushed;
    }

    void reset() { held = phase = 0.f; }
};

// ============================================================
//  EffectsChain — one instance per slot, in order:
//  filter → chorus → reverb → bitcrusher
// ============================================================
struct EffectsChain {
    SVFilter    filter;
    Chorus      chorus;
    Reverb      reverb;
    Bitcrusher  bitcrusher;

    double sr = 44100.0;

    void init(double sampleRate) {
        sr = sampleRate;
        reverb.init();
        chorus.reset();
        filter.reset();
        bitcrusher.reset();
    }

    struct Params {
        // Filter
        float filterCutoff = 20000.f; // Hz
        float filterRes    = 0.7f;
        int   filterMode   = 0;       // 0=LP, 1=HP, 2=BP
        bool  filterOn     = false;

        // Chorus
        float chorusRate   = 0.5f;    // Hz
        float chorusDepth  = 8.f;     // ms
        float chorusWet    = 0.f;

        // Reverb
        float reverbSize   = 0.5f;
        float reverbDamp   = 0.5f;
        float reverbWet    = 0.f;

        // Bitcrusher
        float crushBits    = 16.f;
        float crushRate    = 0.f;
    };

    float process(float in, const Params& p)
    {
        float s = in;
        if (p.filterOn || p.filterCutoff < 19000.f)
            s = filter.process(s, p.filterCutoff, p.filterRes, (float)sr, p.filterMode);
        if (p.chorusWet > 0.001f)
            s = chorus.process(s, p.chorusRate, p.chorusDepth, p.chorusWet, (float)sr);
        if (p.reverbWet > 0.001f)
            s = reverb.process(s, p.reverbSize, p.reverbDamp, p.reverbWet);
        if (p.crushRate > 0.001f || p.crushBits < 15.9f)
            s = bitcrusher.process(s, p.crushBits, p.crushRate, (float)sr);
        return s;
    }

    void reset() { filter.reset(); chorus.reset(); reverb.reset(); bitcrusher.reset(); }
};

} // namespace ChaosOsc
