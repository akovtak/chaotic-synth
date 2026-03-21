#pragma once
#include <vector>
#include <array>
#include <cmath>

namespace ChaosOsc {

// ============================================================
//  FeedbackNetwork
//  4 delay lines with chaos-controlled feedback gains.
//  Delay lengths are chosen to be mutually prime (ms) to avoid
//  comb filtering artefacts.
//  Call process(input, chaosValues) once per sample.
//  Returns delayed+feedback sum to mix with voice output.
// ============================================================
class FeedbackNetwork {
public:
    static constexpr int NUM_DELAYS = 4;

    void init(int sampleRate, int seed = 0)
    {
        sr = sampleRate;

        // Prime-ish delay lengths in ms — different per seed so voices differ
        float baseLengths[NUM_DELAYS] = {23.f, 31.f, 47.f, 67.f};
        float seedOffset = seed * 7.3f;

        for (int d = 0; d < NUM_DELAYS; ++d) {
            float ms    = baseLengths[d] + std::fmod(seedOffset * (d+1), 13.f);
            int   len   = std::max(8, (int)(ms * 0.001f * sampleRate));
            buf[d].assign(len, 0.f);
            pos[d] = 0;
            gain[d] = 0.f;
        }
    }

    // Process one sample.
    // chaosVals: NUM_DELAYS values in [-1,1] from chaos system
    // baseGain:  user-set base feedback gain [0, 0.95]
    // chaosAmt:  how much chaos modulates gains [0,1]
    float process(float input, const float* chaosVals,
                  float baseGain, float chaosAmt)
    {
        float output = 0.f;

        for (int d = 0; d < NUM_DELAYS; ++d) {
            // Read from delay
            float delayed = buf[d][pos[d]];

            // Chaos-modulated gain — clamp hard below 0.95 to prevent runaway
            float g = baseGain + chaosVals[d] * chaosAmt * 0.4f;
            g = std::max(-0.95f, std::min(0.95f, g));
            gain[d] = g;

            // Write input + feedback into delay
            buf[d][pos[d]] = input + delayed * g;

            // Soft clip to prevent blowup
            if (buf[d][pos[d]] > 2.f)  buf[d][pos[d]] =  2.f;
            if (buf[d][pos[d]] < -2.f) buf[d][pos[d]] = -2.f;

            // Advance write head
            pos[d] = (pos[d] + 1) % (int)buf[d].size();

            output += delayed;
        }

        return output * (1.f / NUM_DELAYS);
    }

    // Scale all delay lengths (0.1=short/metallic, 1=default, 3=long/spacious)
    void setDelayScale(float scale) {
        scale = std::max(0.05f, std::min(4.f, scale));
        float baseLengths[NUM_DELAYS] = {23.f, 31.f, 47.f, 67.f};
        for (int d = 0; d < NUM_DELAYS; ++d) {
            float ms  = baseLengths[d] * scale;
            int   len = std::max(8, (int)(ms * 0.001f * sr));
            if (len != (int)buf[d].size()) {
                buf[d].assign(len, 0.f);
                pos[d] = 0;
            }
        }
    }

    void clear()
    {
        for (int d = 0; d < NUM_DELAYS; ++d)
            std::fill(buf[d].begin(), buf[d].end(), 0.f);
    }

    float getGain(int d) const { return (d >= 0 && d < NUM_DELAYS) ? gain[d] : 0.f; }

private:
    int sr = 44100;
    std::array<std::vector<float>, NUM_DELAYS> buf;
    std::array<int,   NUM_DELAYS> pos   {};
    std::array<float, NUM_DELAYS> gain  {};
};

} // namespace ChaosOsc
