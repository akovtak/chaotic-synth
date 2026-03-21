#define _USE_MATH_DEFINES
#include "AudioEngine.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace ChaosOsc {

float AudioFMOsc::tick() {
    float s = (float)std::sin(2.0 * M_PI * phase);
    phase  += phaseInc;
    if (phase >= 1.0) phase -= 1.0;
    return s;
}

void Voice::init(int seed, int numPoints, double sampleRate, double baseFreq) {
    sr = sampleRate;
    buf[0].assign(numPoints, 0.f);
    buf[1].assign(numPoints, 0.f);
    for (int i = 0; i < numPoints; ++i)
        buf[0][i] = (float)std::sin(2.0 * M_PI * i / numPoints);
    readIdx.store(0); writeIdx = 1; phase = 0.0;
    fmOsc.init(sampleRate);   fmOsc.setFreq(baseFreq * 2.0);
    warpOsc.init(sampleRate); warpOsc.setFreq(baseFreq * 1.5);
    feedback.init((int)sampleRate, seed);
}

bool AudioEngine::init(const AudioConfig& cfg) {
    config = cfg;
    config.numPoints = std::max(4, std::min(64, cfg.numPoints));
    voice.init(0, config.numPoints, cfg.sampleRate, 220.0);
    for (int d = 0; d < FeedbackNetwork::NUM_DELAYS; ++d)
        fbChaosVals[d].store(0.f);
    paramBuf[0] = paramBuf[1] = EngineParams{};
    paramReadIdx.store(0); paramWriteIdx = 1;
    samplePlayer.init(cfg.sampleRate);
    sampleMode.store(false);
    envLevel = 0.f; envState = 0; envTrigPhase = 0.0;
    envTrigger.store(false); envRelTrig.store(false);
    effectsChain.init(cfg.sampleRate);
    dcX1 = dcY1 = 0.f;
    debug.blockCount.store(0);
    initialised = true;
    return true;
}

void AudioEngine::reset() {
    voice.phase = 0.0;
    voice.feedback.clear();
    dcX1 = dcY1 = 0.f;
    effectsChain.reset();
}

void AudioEngine::pushGeometry(const std::vector<float>& y) {
    if (!initialised || (int)y.size() != config.numPoints) return;
    int wi = voice.writeIdx;
    voice.buf[wi] = y;
    voice.readIdx.store(wi, std::memory_order_release);
    voice.writeIdx = 1 - wi;
}

void AudioEngine::pushParams(const EngineParams& p) {
    if (!initialised) return;
    paramBuf[paramWriteIdx] = p;
    paramReadIdx.store(paramWriteIdx, std::memory_order_release);
    paramWriteIdx = 1 - paramWriteIdx;
}

void AudioEngine::setFMFreq(double hz)   { fmFreqAtomic.store(hz,  std::memory_order_relaxed); }
void AudioEngine::setWarpFreq(double hz) { warpFreqAtomic.store(hz, std::memory_order_relaxed); }
void AudioEngine::setFeedbackChaos(const float* vals4) {
    for (int d = 0; d < FeedbackNetwork::NUM_DELAYS; ++d)
        fbChaosVals[d].store(vals4[d], std::memory_order_relaxed);
}
void AudioEngine::loadSampleBuffer(SampleBuffer* buf) { samplePlayer.setBuffer(buf); }
void AudioEngine::setSampleMode(bool on)   { sampleMode.store(on); if (on) samplePlayer.trigger(); }
void AudioEngine::setSampleSpeed(double s) { samplePlayer.speed.store(s); }
void AudioEngine::setLoopRegion(double s, double e) {
    samplePlayer.loopStart.store(s); samplePlayer.loopEnd.store(e);
}
void AudioEngine::setSampleLoop(bool lp)   { samplePlayer.loop.store(lp); }
void AudioEngine::setSampleStart(double t) { samplePlayer.loopStart.store(t); }
void AudioEngine::setSampleEnd(double t)   { samplePlayer.loopEnd.store(t); }
void AudioEngine::triggerSample()          { samplePlayer.trigger(); }
void AudioEngine::envOn()  { envTrigger.store(true, std::memory_order_relaxed); }
void AudioEngine::envOff() { envRelTrig.store(true, std::memory_order_relaxed); }

float AudioEngine::processVoice(Voice& v, const EngineParams& p)
{
    if (v.phase >= 1.0) v.phase -= 1.0;

    double fmMod = 0.0;
    if (p.fmDepth > 0.001f) {
        v.fmOsc.setFreq(fmFreqAtomic.load(std::memory_order_relaxed));
        fmMod = p.fmDepth * v.fmOsc.tick() * 0.5;
    }
    double warpMod = 0.0;
    if (p.warpAmount > 0.001f) {
        v.warpOsc.setFreq(warpFreqAtomic.load(std::memory_order_relaxed));
        warpMod = p.warpAmount * v.warpOsc.tick() * 0.3;
    }

    float sample;
    bool sm = sampleMode.load(std::memory_order_relaxed);

    if (sm) {
        double savedSpeed = samplePlayer.speed.load(std::memory_order_relaxed);
        double modSpeed = savedSpeed;
        if (p.fmDepth    > 0.001f) modSpeed *= (1.0 + fmMod  * 2.0);
        if (p.warpAmount > 0.001f) modSpeed *= (1.0 + warpMod * 1.5);
        samplePlayer.speed.store(modSpeed, std::memory_order_relaxed);
        float raw = samplePlayer.tick();
        samplePlayer.speed.store(savedSpeed, std::memory_order_relaxed);
        int ri = v.readIdx.load(std::memory_order_acquire);
        double shapePhase = std::max(0.0, std::min(1.0 - 1e-9, (raw + 1.0) * 0.5));
        float shaped = evaluateSpline(v.buf[ri], shapePhase, p.inharmonicity);
        sample = raw * (1.f - p.yAmplitude) + shaped * p.yAmplitude;
    } else {
        double evalPhase = v.phase + fmMod;
        evalPhase -= std::floor(evalPhase);
        int ri = v.readIdx.load(std::memory_order_acquire);
        sample = evaluateSpline(v.buf[ri], evalPhase, p.inharmonicity);
    }

    {
        float chaos[FeedbackNetwork::NUM_DELAYS];
        for (int d = 0; d < FeedbackNetwork::NUM_DELAYS; ++d)
            chaos[d] = fbChaosVals[d].load(std::memory_order_relaxed);
        float fb = v.feedback.process(sample, chaos, p.feedbackGain, p.feedbackChaos);
        sample += fb * p.feedbackGain;
        sample  = std::tanh(sample * 0.7f);
    }

    if (!sm) {
        double baseInc  = p.frequency / v.sr;
        double totalInc = baseInc * (1.0 + warpMod);
        totalInc = std::max(baseInc * 0.1, std::min(baseInc * 4.0, totalInc));
        v.phase += totalInc;
    }
    return sample;
}

void AudioEngine::processStereo(float& outL, float& outR)
{
    if (!initialised) { outL = outR = 0.f; return; }
    const EngineParams& p = paramBuf[paramReadIdx.load(std::memory_order_acquire)];
    if (std::abs(p.fbDelayScale - lastFbScale) > 0.01f) {
        voice.feedback.setDelayScale(p.fbDelayScale);
        lastFbScale = p.fbDelayScale;
    }
    float s = processSample();
    float panAngle = (p.pan + 1.f) * 0.25f * 3.14159265f;
    outL = s * std::cos(panAngle);
    outR = s * std::sin(panAngle);
}

float AudioEngine::processSample()
{
    if (!initialised) return 0.f;
    const EngineParams& p = paramBuf[paramReadIdx.load(std::memory_order_acquire)];
    float s = processVoice(voice, p);
    s = dcBlock(s);
    s *= p.gain;

    if (p.envelopeOn) {
        // Auto-retrigger (loop mode)
        if (p.envelopeLoop) {
            double phaseInc = p.envTrigRate / config.sampleRate;
            envTrigPhase += phaseInc;
            if (envTrigPhase >= 1.0) {
                envTrigPhase -= 1.0;
                envLevel = 0.f;
                envState = 1;
                samplePlayer.trigger();
            }
        } else {
            envTrigPhase = 0.0;
        }

        // Manual/MIDI trigger -- start attack from zero (no click)
        if (envTrigger.exchange(false, std::memory_order_relaxed)) {
            envLevel = 0.f;
            envState = 1;
        }
        // Release
        if (envRelTrig.exchange(false, std::memory_order_relaxed)) {
            if (envState >= 1 && envState <= 3) envState = 4;
        }

        float sr_f       = (float)config.sampleRate;
        float attackInc  = 1.f / std::max(1.f, p.envAttack  * sr_f);
        float decayInc   = 1.f / std::max(1.f, p.envDecay   * sr_f);
        float releaseInc = 1.f / std::max(1.f, p.envRelease * sr_f);

        switch (envState) {
            case 1:
                envLevel += attackInc;
                if (envLevel >= 1.f) { envLevel = 1.f; envState = 2; }
                break;
            case 2:
                envLevel -= decayInc;
                if (envLevel <= p.envSustain) {
                    envLevel = p.envSustain;
                    envState = (p.envSustain < 0.001f) ? 4 : 3;
                }
                break;
            case 3:
                envLevel = p.envSustain;
                break;
            case 4:
                envLevel -= releaseInc;
                if (envLevel <= 0.f) { envLevel = 0.f; envState = 0; }
                break;
            default:
                envLevel = 0.f; break;
        }
        s *= envLevel;
    } else {
        // No envelope: clear triggers, keep level at 1 for free-running OSC
        envTrigger.store(false, std::memory_order_relaxed);
        envRelTrig.store(false, std::memory_order_relaxed);
        envState = 0;
        // Only set full level if this voice is actually supposed to produce sound.
        // If gain==0 (idle poly voice), leave envLevel alone to avoid clicks.
        if (p.gain > 0.f) envLevel = 1.f;
        envTrigPhase = 0.0;
    }

    if (p.normalizeOn) {
        s *= samplePlayer.getNormGain();
        s  = std::tanh(s * 0.95f);
    }

    s = effectsChain.process(s, p.fx);
    return s;
}

void AudioEngine::processBlock(float* out, std::size_t numFrames, int numChannels)
{
    for (std::size_t f = 0; f < numFrames; ++f) {
        float s = processSample();
        for (int ch = 0; ch < numChannels; ++ch)
            out[f * numChannels + ch] = s;
    }
    debug.phase.store((float)voice.phase);
    debug.lastSample.store(out[0]);
    debug.blockCount.fetch_add(1);
}

float AudioEngine::catmullRom(float p0,float p1,float p2,float p3,float t) const {
    float t2=t*t, t3=t2*t;
    return 0.5f*((2.f*p1)+(-p0+p2)*t+(2.f*p0-5.f*p1+4.f*p2-p3)*t2+(-p0+3.f*p1-3.f*p2+p3)*t3);
}

float AudioEngine::evaluateSpline(const std::vector<float>& buf, double phase, float inharmonicity) const {
    int n = (int)buf.size();
    if (n == 0) return 0.f;
    if (n == 1) return buf[0];
    double sp = phase;
    if (inharmonicity > 0.001f) {
        sp = std::pow(sp, 1.0 / (1.0 + inharmonicity * 1.5));
        sp -= std::floor(sp);
    }
    double pos = sp * n;
    int i1 = (int)pos % n;
    float t  = (float)(pos - (int)pos);
    int i0=(i1-1+n)%n, i2=(i1+1)%n, i3=(i1+2)%n;
    return catmullRom(buf[i0],buf[i1],buf[i2],buf[i3],t);
}

float AudioEngine::dcBlock(float x) {
    float y = x - dcX1 + DC_COEFF * dcY1;
    dcX1 = x; dcY1 = y;
    return y;
}

} // namespace ChaosOsc
