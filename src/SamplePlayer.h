#pragma once
#include <vector>
#include <atomic>
#include <string>
#include <cmath>
#include <algorithm>

namespace ChaosOsc {

struct SampleBuffer {
    std::vector<float> samples;
    double sampleRate = 44100.0;
    std::string name;
    bool  loaded   = false;
    float normGain = 1.f;

    float readLinear(double pos) const {
        if (samples.empty()) return 0.f;
        int n = (int)samples.size();
        pos = std::max(0.0, std::min((double)(n - 1), pos));
        int i0 = (int)pos, i1 = std::min(i0 + 1, n - 1);
        float t = (float)(pos - i0);
        return samples[i0] * (1.f - t) + samples[i1] * t;
    }

    void computeNormGain(float targetPeak = 0.9f) {
        float peak = 0.f;
        for (float s : samples) peak = std::max(peak, std::abs(s));
        normGain = (peak > 1e-6f) ? targetPeak / peak : 1.f;
    }
};

// ============================================================
//  SamplePlayer -- direct playback, speed changes both pitch
//  and tempo together (classic tape-machine behaviour).
//  loopStart/loopEnd: normalised [0,1] loop region
// ============================================================
class SamplePlayer {
public:
    void init(double engineSampleRate) {
        sr = engineSampleRate;
        playPos = 0.0;
        playing = false;
        activeBuffer.store(nullptr, std::memory_order_release);
    }

    void setBuffer(SampleBuffer* buf) {
        activeBuffer.store(buf, std::memory_order_release);
        playPos = 0.0;
        playing.store(buf != nullptr && buf->loaded, std::memory_order_relaxed);
        triggerPending.store(false, std::memory_order_relaxed);
    }

    float tick() {
        SampleBuffer* buf = activeBuffer.load(std::memory_order_acquire);
        if (!buf || !buf->loaded || buf->samples.empty()) return 0.f;

        // Consume pending trigger from main thread (thread-safe)
        if (triggerPending.exchange(false, std::memory_order_acq_rel)) {
            playPos = loopStart.load() * (double)buf->samples.size();
            playing.store(true, std::memory_order_relaxed);
        }

        if (!playing.load(std::memory_order_relaxed)) return 0.f;

        int    N  = (int)buf->samples.size();
        double ls = loopStart.load() * N;
        double le = loopEnd.load()   * N;
        if (le <= ls + 2) le = ls + 2;
        double loopLen = le - ls;

        double inc = speed.load() * (buf->sampleRate / sr);

        float out = buf->readLinear(playPos);

        // Crossfade near loop end to avoid click
        double xfLen = std::min(loopLen * 0.05, sr * 0.008);
        if (loop.load() && xfLen > 1.0) {
            double distFromEnd = le - playPos;
            if (inc > 0 && distFromEnd < xfLen) {
                float  t         = (float)(distFromEnd / xfLen);
                double startPos  = ls + (xfLen - distFromEnd);
                float  startSamp = buf->readLinear(startPos);
                out = out * t + startSamp * (1.f - t);
            }
        }

        playPos += inc;

        if (loop.load()) {
            if (inc > 0 && playPos >= le) playPos = ls + std::fmod(playPos - ls, loopLen);
            if (inc < 0 && playPos <  ls) playPos = le - std::fmod(ls - playPos, loopLen);
        } else {
            if (inc >= 0 && playPos >= le) { playing.store(false, std::memory_order_relaxed); playPos = ls; }
            if (inc <  0 && playPos <  ls) { playing.store(false, std::memory_order_relaxed); playPos = le; }
        }
        return out;
    }

    // Called from main thread -- safe to call any time
    void trigger() {
        triggerPending.store(true, std::memory_order_release);
    }

    bool   isPlaying()   const { return playing.load(std::memory_order_relaxed); }
    float  getNormGain() const {
        SampleBuffer* buf = activeBuffer.load(std::memory_order_acquire);
        return (buf && buf->loaded) ? buf->normGain : 1.f;
    }
    double getNormPos() const {
        SampleBuffer* buf = activeBuffer.load(std::memory_order_acquire);
        if (!buf || !buf->loaded || buf->samples.empty()) return 0.0;
        double ls = loopStart.load() * buf->samples.size();
        double le = loopEnd.load()   * buf->samples.size();
        if (le <= ls) return 0.0;
        return std::max(0.0, std::min(1.0, (playPos - ls) / (le - ls)));
    }

    std::atomic<double> speed     {1.0};
    std::atomic<double> loopStart {0.0};
    std::atomic<double> loopEnd   {1.0};
    std::atomic<bool>   loop      {true};

private:
    std::atomic<SampleBuffer*> activeBuffer  {nullptr};
    std::atomic<bool>          playing       {false};
    std::atomic<bool>          triggerPending{false};
    double playPos = 0.0;
    double sr      = 44100.0;
};

} // namespace ChaosOsc
