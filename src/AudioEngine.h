#pragma once
#include <cstddef>
#include <atomic>
#include <vector>
#include <array>
#include "FeedbackNetwork.h"
#include "SamplePlayer.h"
#include "Effects.h"

namespace ChaosOsc {

struct AudioConfig {
    double sampleRate  = 44100.0;
    int    blockSize   = 512;
    int    numChannels = 2;
    int    numPoints   = 32;
};

struct EngineDebugState {
    std::atomic<float>  phase      {0.f};
    std::atomic<float>  lastSample {0.f};
    std::atomic<int>    blockCount {0};
};

struct AudioFMOsc {
    double phase = 0.0, phaseInc = 0.0, sr = 44100.0;
    void  init(double s)     { sr=s; phase=0; phaseInc=0; }
    void  setFreq(double hz) { phaseInc = hz/sr; }
    float tick();
};

struct Voice {
    double phase = 0.0;
    std::vector<float> buf[2];
    std::atomic<int>   readIdx {0};
    int                writeIdx = 1;
    AudioFMOsc fmOsc;
    AudioFMOsc warpOsc;
    FeedbackNetwork feedback;
    double sr = 44100.0;
    void init(int seed, int numPoints, double sampleRate, double baseFreq);
};

struct EngineParams {
    double frequency     = 220.0;
    float  gain          = 0.6f;
    float  pan           = 0.f;
    float  fmDepth       = 0.f;
    float  fmRatio       = 2.f;
    float  warpAmount    = 0.f;
    float  warpRatio     = 1.5f;
    float  inharmonicity = 0.f;
    float  feedbackGain  = 0.f;
    float  feedbackChaos = 0.f;
    float  fbDelayScale  = 1.f;
    float  yAmplitude    = 1.f;
    // ADSR
    float  envAttack     = 0.01f;
    float  envDecay      = 0.1f;
    float  envSustain    = 0.8f;
    float  envRelease    = 0.3f;
    float  envTrigRate   = 1.f;
    bool   envelopeOn    = false;
    bool   envelopeLoop  = false;
    bool   normalizeOn   = false;
    EffectsChain::Params fx;
};

class AudioEngine {
public:
    bool init(const AudioConfig& cfg);
    void reset();

    void pushGeometry(const std::vector<float>& y);
    void setFMFreq    (double hz);
    void setWarpFreq  (double hz);
    void setFeedbackChaos(const float* vals4);
    void pushParams(const EngineParams& p);

    void loadSampleBuffer(SampleBuffer* buf);
    void setSampleMode (bool on);
    void setSampleSpeed(double s);
    void setLoopRegion(double start, double end);
    void setSampleLoop (bool lp);
    void setSampleStart(double t);
    void setSampleEnd  (double t);
    void triggerSample();
    void envOn();
    void envOff();
    // True when envelope has fully released (safe to reuse this voice)
    bool isSilent() const { return envState == 0 && envLevel <= 0.001f; }
    double getSamplePos() const { return samplePlayer.getNormPos(); }
    bool   isSampleMode() const { return sampleMode.load(); }

    void processStereo(float& outL, float& outR);
    float processSample();
    void processBlock(float* out, std::size_t numFrames, int numChannels);

    const EngineDebugState& getDebugState() const { return debug; }
    EngineDebugState debug;

private:
    float processVoice(Voice& v, const EngineParams& p);
    float catmullRom(float p0,float p1,float p2,float p3,float t) const;
    float evaluateSpline(const std::vector<float>& buf, double phase, float inharmonicity) const;
    float dcBlock(float x);

    AudioConfig  config;
    Voice        voice;

    EngineParams paramBuf[2];
    std::atomic<int> paramReadIdx {0};
    int              paramWriteIdx = 1;

    std::atomic<float> fbChaosVals[FeedbackNetwork::NUM_DELAYS];
    std::atomic<double> fmFreqAtomic  {440.0};
    std::atomic<double> warpFreqAtomic{330.0};

    SamplePlayer      samplePlayer;

    float envLevel   = 0.f;
    int   envState   = 0;
    double envTrigPhase = 0.0;
    std::atomic<bool> envTrigger {false};
    std::atomic<bool> envRelTrig {false};

    std::atomic<bool> sampleMode {false};

    EffectsChain effectsChain;

    float aaState = 0.f;
    float lastFbScale = 1.f;
    float dcX1 = 0.f, dcY1 = 0.f;
    static constexpr float DC_COEFF = 0.9995f;
    bool initialised = false;
};

} // namespace ChaosOsc
