#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace ChaosOsc {

struct Param {
    std::string name;
    float base = 0.f, chaosAmt = 0.f, smoothed = 0.f;
    float minVal = 0.f, maxVal = 1.f, defaultVal = 0.f;
    bool  isInt = false;

    Param() = default;
    Param(const std::string& n, float mn, float mx, float def, bool integer = false)
        : name(n), minVal(mn), maxVal(mx), base(def), smoothed(def),
          defaultVal(def), isInt(integer) {}

    void update(float sig) {
        float t = base + sig * chaosAmt * (maxVal - minVal) * 0.5f;
        smoothed = std::max(minVal, std::min(maxVal, t));
    }
    void resetToDefault() { base = defaultVal; chaosAmt = 0.f; smoothed = defaultVal; }
    float normBase() const { return (maxVal==minVal)?0.f:(base-minVal)/(maxVal-minVal); }
    void setNormBase(float t) {
        base = minVal + std::max(0.f,std::min(1.f,t))*(maxVal-minVal);
        if (isInt) base = std::round(base);
    }
    int iVal() const { return (int)std::round(smoothed); }
};

class ParamSystem {
public:
    // Core synthesis
    Param gain          {"Gain",        0.f,    1.f,    0.6f};
    Param pan           {"Pan",        -1.f,    1.f,    0.f};   // -1=L, 0=C, +1=R
    Param chaosGlobal   {"Chaos",       0.f,    1.f,    0.f};
    Param chaosSpeed    {"CSpd",        0.1f,   8.f,    1.f};
    Param pitch         {"Pitch",       20.f,   8000.f, 220.f};
    Param fmDepth       {"FM Dep",      0.f,    1.f,    0.f};
    Param fmRatio       {"FM Ratio",    0.5f,   8.f,    2.f};
    Param warpAmount    {"Warp",        0.f,    1.f,    0.f};
    Param warpRatio     {"Wrp Ratio",   0.5f,   8.f,    1.5f};
    Param inharmonicity {"Inharm",      0.f,    1.f,    0.f};
    Param feedbackGain  {"FB Gain",     0.f,    0.95f,  0.f};
    Param feedbackChaos {"FB Chaos",    0.f,    1.f,    0.f};
    Param fbDelayScale  {"FB Delay",    0.05f,  4.f,    1.f};   // feedback delay length scale
    Param yAmplitude    {"Y Amp",       0.f,    1.f,    0.5f};

    // Sample playback -- negative speed = reverse
    Param sampleSpeed   {"Speed",      -4.f,    4.f,    1.f};

    // Loop region (normalised 0-1 of file length)
    Param loopStart     {"Loop St",     0.f,    1.f,    0.f};
    Param loopEnd       {"Loop End",    0.f,    1.f,    1.f};

    // ADSR envelope
    Param envAttack      {"Att",         0.001f, 4.f,    0.01f};
    Param envDecay       {"Dec",         0.001f, 4.f,    0.1f};
    Param envSustain     {"Sus Lvl",     0.f,    1.f,    0.8f};
    Param envRelease     {"Rel",         0.001f, 8.f,    0.3f};
    Param envTrigRate    {"Trig Hz",     0.05f,  30.f,   1.f};

    // Effects -- filter
    Param filterCutoff  {"Flt Cut",     20.f,   20000.f,20000.f};
    Param filterRes     {"Flt Res",     0.5f,   8.f,    0.7f};
    Param filterMode    {"Flt Mode",    0.f,    2.f,    0.f, true};

    // Effects -- chorus
    Param chorusRate    {"Chr Rate",    0.05f,  8.f,    0.5f};
    Param chorusDepth   {"Chr Dep",     0.f,    20.f,   8.f};
    Param chorusWet     {"Chr Wet",     0.f,    1.f,    0.f};

    // Effects -- reverb
    Param reverbSize    {"Rev Size",    0.f,    1.f,    0.5f};
    Param reverbDamp    {"Rev Damp",    0.f,    1.f,    0.5f};
    Param reverbWet     {"Rev Wet",     0.f,    1.f,    0.f};

    // Effects -- bitcrusher
    Param crushBits     {"Crush Bit",   1.f,    16.f,   16.f};
    Param crushRate     {"Crush Spd",   0.f,    1.f,    0.f};

    std::vector<Param*> all() {
        return { &gain, &pan, &chaosGlobal, &chaosSpeed,
                 &pitch, &fmDepth, &fmRatio,
                 &warpAmount, &warpRatio, &inharmonicity,
                 &feedbackGain, &feedbackChaos, &fbDelayScale, &yAmplitude,
                 &sampleSpeed,
                 &loopStart, &loopEnd,
                 &envAttack, &envDecay, &envSustain, &envRelease, &envTrigRate,
                 &filterCutoff, &filterRes, &filterMode,
                 &chorusRate, &chorusDepth, &chorusWet,
                 &reverbSize, &reverbDamp, &reverbWet,
                 &crushBits, &crushRate };
    }

    void resetToDefaults() { for (auto* p : all()) p->resetToDefault(); }

    // Undo: save/restore a snapshot of all base values
    struct Snapshot {
        std::vector<float> bases, chaos;
        bool valid = false;
    };
    Snapshot undoSnapshot;

    void saveUndo() {
        undoSnapshot.bases.clear(); undoSnapshot.chaos.clear();
        for (auto* p : all()) { undoSnapshot.bases.push_back(p->base); undoSnapshot.chaos.push_back(p->chaosAmt); }
        undoSnapshot.valid = true;
    }
    bool restoreUndo() {
        if (!undoSnapshot.valid) return false;
        auto ps = all();
        for (int i = 0; i < (int)ps.size() && i < (int)undoSnapshot.bases.size(); ++i) {
            ps[i]->base     = undoSnapshot.bases[i];
            ps[i]->chaosAmt = undoSnapshot.chaos[i];
            ps[i]->smoothed = ps[i]->base;
        }
        return true;
    }

    void update(const std::vector<float>& sigs) {
        auto ps = all();
        for (int i = 0; i < (int)ps.size(); ++i)
            ps[i]->update(sigs.empty() ? 0.f : sigs[i % sigs.size()]);
    }

    void randomize(bool subtle, unsigned int seed) {
        unsigned int r = seed;
        auto rng = [&]() -> float {
            r ^= r << 13; r ^= r >> 17; r ^= r << 5;
            return (r & 0x7FFFFFFFu) / float(0x7FFFFFFFu);
        };
        for (int i = 0; i < 7; ++i) rng();

        auto randParam = [&](Param& p, float rangeMul = 1.f) {
            float range = (p.maxVal - p.minVal) * rangeMul;
            if (subtle)
                p.base = std::max(p.minVal, std::min(p.maxVal, p.base + (rng()*2.f-1.f)*range*0.25f));
            else
                p.base = p.minVal + rng() * (p.maxVal - p.minVal);
            if (p.isInt) p.base = std::round(p.base);
        };

        // Pan
        randParam(pan);

        // Chaos system
        chaosGlobal.base = subtle
            ? std::max(0.f, std::min(1.f, chaosGlobal.base + (rng()*2.f-1.f)*0.2f))
            : rng() * rng();  // bias toward low
        randParam(chaosSpeed);

        // Pitch: random Hz, biased toward lower frequencies using sqrt
        // sqrt(rng()) concentrates results in lower part of range
        {
            float t = subtle
                ? std::max(0.f, std::min(1.f, (pitch.base - pitch.minVal)/(pitch.maxVal - pitch.minVal) + (rng()*2.f-1.f)*0.15f))
                : std::sqrt(rng());  // sqrt bias: more picks in lower freq range
            pitch.base = pitch.minVal + t * (pitch.maxVal - pitch.minVal);
        }

        // FM
        randParam(fmDepth);  randParam(fmRatio);

        // Warp
        randParam(warpAmount); randParam(warpRatio);

        // Inharmonicity
        randParam(inharmonicity);

        // Feedback
        feedbackGain.base = subtle
            ? std::max(0.f, std::min(0.95f, feedbackGain.base + (rng()*2.f-1.f)*0.2f))
            : rng() * rng() * 0.8f;
        randParam(feedbackChaos); if (!subtle) feedbackChaos.base *= rng();
        randParam(fbDelayScale);

        // Y Amplitude
        randParam(yAmplitude);

        // Sample speed
        sampleSpeed.base = subtle
            ? std::max(-4.f, std::min(4.f, sampleSpeed.base + (rng()*2.f-1.f)*0.5f))
            : (rng()*2.f - 1.f) * 4.f;
        if (std::abs(sampleSpeed.base) < 0.15f)
            sampleSpeed.base = sampleSpeed.base >= 0 ? 0.15f : -0.15f;

        // Envelope
        randParam(envAttack); randParam(envDecay);
        envSustain.base = subtle
            ? std::max(0.f, std::min(1.f, envSustain.base + (rng()*2.f-1.f)*0.2f))
            : rng();
        if (!subtle) envRelease.base = rng() * 3.f + 0.01f;
        else         randParam(envRelease);

        // Effects
        if (!subtle) {
            filterCutoff.base = rng() > 0.4f ? 200.f + rng()*rng()*19800.f : 20000.f;
            filterRes.base    = 0.5f + rng() * 5.f;
            chorusWet.base    = rng() > 0.65f ? 0.1f + rng()*0.5f : 0.f;
            chorusRate.base   = 0.05f + rng() * 4.f;
            chorusDepth.base  = rng() * 15.f;
            reverbWet.base    = rng() > 0.6f  ? 0.05f + rng()*0.45f : 0.f;
            reverbSize.base   = 0.1f + rng() * 0.9f;
            reverbDamp.base   = rng();
            crushBits.base    = rng() > 0.8f  ? 2.f + rng()*12.f : 16.f;
            crushRate.base    = rng() > 0.8f  ? rng()*0.7f : 0.f;
        } else {
            auto nudge = [&](Param& p, float amt) {
                p.base = std::max(p.minVal, std::min(p.maxVal,
                         p.base + (rng()*2.f-1.f)*(p.maxVal-p.minVal)*amt));
            };
            nudge(filterCutoff, 0.08f);
            nudge(chorusWet, 0.08f);
            nudge(reverbWet, 0.08f);
        }
    }

    // Randomize only chaos amounts -- leaves base values untouched
    void randomizeChaos(bool subtle, unsigned int seed) {
        unsigned int r = seed ^ 0xDEADBEEFu;
        auto rng = [&]() -> float {
            r ^= r << 13; r ^= r >> 17; r ^= r << 5;
            return (r & 0x7FFFFFFFu) / float(0x7FFFFFFFu);
        };
        for (int i = 0; i < 5; ++i) rng();

        for (auto* p : all()) {
            // Skip non-modulatable params
            if (p == &gain || p == &filterMode) continue;
            if (subtle)
                p->chaosAmt = std::max(0.f, std::min(1.f, p->chaosAmt + (rng()*2.f-1.f)*0.15f));
            else
                p->chaosAmt = rng() > 0.55f ? rng() * 0.6f : 0.f;  // 45% chance of chaos, max 0.6
        }
    }
};

struct CrossfadeParam {
    Param x {"X-Fade", 0.f, 1.f, 0.5f};
};

} // namespace ChaosOsc
