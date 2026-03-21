#pragma once
// ============================================================
//  PolyEngine -- 8-voice polyphonic wrapper over AudioEngine.
//
//  Modes (set via setEnvelopeOn / setEnvelopeLoop from ofApp):
//
//  OSC  (envelopeOn=false):
//    Voice 0 only, runs freely. MIDI updates pitch param.
//    No triggering. Knobs affect timbre in real time.
//
//  POLY (envelopeOn=true, envelopeLoop=false):
//    MIDI note-on -> allocate voice, set freq, envOn().
//    MIDI note-off -> envOff() on that voice.
//    Voice slot freed when envelope fully silent (envState==0).
//    8 voices, oldest-note stealing when all busy.
//
//  LOOP (envelopeOn=true, envelopeLoop=true):
//    Voice 0 auto-retriggers at Trig Hz. MIDI updates pitch.
// ============================================================
#include "AudioEngine.h"
#include <algorithm>
#include <atomic>

namespace ChaosOsc {

static constexpr int NUM_POLY_VOICES = 8;

class PolyEngine {
public:
    bool init(const AudioConfig& cfg) {
        for (int i = 0; i < NUM_POLY_VOICES; ++i) {
            voices[i].init(cfg);
            voiceNote[i].store(-1);
            voiceFreq[i] = 220.f;
            voiceVel[i]  = 1.f;
            voiceAge[i]  = 0;
            voiceReleasing[i].store(false);
        }
        ageCounter   = 0;
        envelopeOn   = false;
        envelopeLoop = false;
        baseParams   = EngineParams{};
        return true;
    }

    void reset() { for (auto& v : voices) v.reset(); }

    // -- Pass-through to all voices --
    void pushGeometry(const std::vector<float>& y) {
        for (auto& v : voices) v.pushGeometry(y);
    }
    void setFMFreq(double hz)   { for (auto& v : voices) v.setFMFreq(hz); }
    void setWarpFreq(double hz) { for (auto& v : voices) v.setWarpFreq(hz); }
    void setFeedbackChaos(const float* c) { for (auto& v : voices) v.setFeedbackChaos(c); }
    void setSampleMode(bool on)  { for (auto& v : voices) v.setSampleMode(on); }
    void setSampleSpeed(double s){ for (auto& v : voices) v.setSampleSpeed(s); }
    void setLoopRegion(double s, double e) { for (auto& v : voices) v.setLoopRegion(s,e); }
    void setSampleLoop(bool lp)  { for (auto& v : voices) v.setSampleLoop(lp); }
    void loadSampleBuffer(SampleBuffer* b){ for (auto& v : voices) v.loadSampleBuffer(b); }
    bool   isSampleMode() const { return voices[0].isSampleMode(); }
    double getSamplePos() const { return voices[0].getSamplePos(); }
    const EngineDebugState& getDebugState() const { return voices[0].getDebugState(); }

    // Manual T-key trigger
    void triggerSample() { voices[0].triggerSample(); }
    void envOn()  { voices[0].envOn(); }
    void envOff() { voices[0].envOff(); }

    // -- Mode flags: call these BEFORE pushParams every frame --
    void setEnvelopeOn(bool on)   { envelopeOn   = on; }
    void setEnvelopeLoop(bool lp) { envelopeLoop = lp; }

    // -- pushParams: called every frame with shared knob values --
    void pushParams(const EngineParams& p) {
        baseParams = p;

        if (!p.envelopeOn || p.envelopeLoop) {
            // OSC / LOOP: voice 0 only
            voices[0].pushParams(p);
            EngineParams mute = p;
            mute.gain       = 0.f;
            mute.envelopeOn = false;
            for (int i = 1; i < NUM_POLY_VOICES; ++i)
                voices[i].pushParams(mute);
            return;
        }

        // POLY: per-voice params
        // Voice 0 is special: it always plays (for T-key trigger, loop, etc.)
        // Voices 1-3 only play when a MIDI note has been assigned to them
        for (int i = 0; i < NUM_POLY_VOICES; ++i) {
            EngineParams vp = p;
            if (voiceNote[i].load() >= 0) {
                // Active or releasing: use voice-specific frequency and velocity-scaled gain
                vp.frequency = voiceFreq[i];
                vp.gain      = p.gain;  // no velocity attenuation -- gain slider controls volume
            } else if (i == 0) {
                // Voice 0: always plays at full gain (handles T-key, initial sound)
                // frequency stays as p.frequency (pitch slider)
                vp.gain = p.gain;
            } else {
                // Voices 1-3: silent when no note assigned
                vp.gain = 0.f;
            }
            voices[i].pushParams(vp);
        }
    }

    // -- Note on/off --
    void noteOn(int midiNote, float velocity, float freqHz) {
        // Use the dedicated flags (always current, set by setEnvelopeOn before this call)
        if (!envelopeOn || envelopeLoop) {
            // OSC / LOOP: just let ofApp's pitch param update handle it
            return;
        }

        int idx = findVoice(midiNote);

        // If stealing an active voice, envOff it first (avoids click from hard cutoff)
        if (voiceNote[idx].load() >= 0 && !voiceReleasing[idx].load()) {
            voices[idx].envOff();
        }

        voiceNote[idx].store(midiNote);
        voiceFreq[idx]     = freqHz;
        voiceVel[idx]      = velocity;
        voiceAge[idx]      = ++ageCounter;
        voiceReleasing[idx].store(false);

        // Push this voice's specific params immediately
        EngineParams vp = baseParams;
        vp.frequency = freqHz;
        vp.gain      = baseParams.gain;  // full gain -- velocity does not attenuate
        voices[idx].pushParams(vp);
        voices[idx].triggerSample();
        voices[idx].envOn();
    }

    void noteOff(int midiNote) {
        for (int i = 0; i < NUM_POLY_VOICES; ++i) {
            if (voiceNote[i].load() == midiNote && !voiceReleasing[i].load()) {
                voices[i].envOff();
                voiceReleasing[i].store(true);
                // Keep voiceNote[i] set so pushParams feeds params during release tail.
            }
        }
    }

    // -- Audio --
    void processStereo(float& outL, float& outR) {
        // First, free any voices whose release has finished
        // (must be called from audio thread -- safe because voiceNote is only written
        //  from main thread during noteOff, and we only read isSilent here)
        for (int i = 0; i < NUM_POLY_VOICES; ++i) {
            if (voiceReleasing[i].load() && voices[i].isSilent()) {
                voiceNote[i].store(-1);
                voiceReleasing[i].store(false);
            }
        }

        outL = outR = 0.f;
        for (int i = 0; i < NUM_POLY_VOICES; ++i) {
            float vL, vR;
            voices[i].processStereo(vL, vR);
            outL += vL;
            outR += vR;
        }
        // No scaling -- each voice uses the gain slider directly.
        // With 8 voices all sounding simultaneously this could clip, but
        // typical polyphonic playing has 2-4 notes so tanh in processVoice handles peaks.
    }

    float processSample() {
        for (int i = 0; i < NUM_POLY_VOICES; ++i) {
            if (voiceReleasing[i].load() && voices[i].isSilent()) {
                voiceNote[i].store(-1);
                voiceReleasing[i].store(false);
            }
        }
        float s = 0.f;
        for (auto& v : voices) s += v.processSample();
        // no scaling
        return s;
    }

    void processBlock(float* out, std::size_t nF, int nCh) {
        for (std::size_t f = 0; f < nF; ++f) {
            float L, R; processStereo(L, R);
            if (nCh >= 2) { out[f*nCh]=L; out[f*nCh+1]=R; }
            else out[f*nCh] = (L+R)*0.5f;
        }
    }

    int activeVoiceCount() const {
        int n = 0;
        for (int i = 0; i < NUM_POLY_VOICES; ++i)
            if (voiceNote[i].load() >= 0) ++n;
        return n;
    }

private:
    AudioEngine  voices[NUM_POLY_VOICES];
    EngineParams baseParams{};

    // voiceNote and voiceReleasing are read from audio thread + written from main thread
    // Use atomic to avoid data races (the click source)
    std::atomic<int>  voiceNote[NUM_POLY_VOICES];
    float             voiceFreq[NUM_POLY_VOICES];
    float             voiceVel[NUM_POLY_VOICES];
    int               voiceAge[NUM_POLY_VOICES];
    std::atomic<bool> voiceReleasing[NUM_POLY_VOICES];

    int  ageCounter   = 0;
    bool envelopeOn   = false;
    bool envelopeLoop = false;

    int findVoice(int midiNote) {
        // 1. Same note already playing -- retrigger it
        for (int i = 0; i < NUM_POLY_VOICES; ++i)
            if (voiceNote[i].load() == midiNote) return i;
        // 2. Free slot (silent)
        for (int i = 0; i < NUM_POLY_VOICES; ++i)
            if (voiceNote[i].load() < 0) return i;
        // 3. Steal: prefer releasing voices first (they're already fading)
        for (int i = 0; i < NUM_POLY_VOICES; ++i)
            if (voiceReleasing[i].load()) return i;
        // 4. Steal oldest active note
        int oldest = 0;
        for (int i = 1; i < NUM_POLY_VOICES; ++i)
            if (voiceAge[i] < voiceAge[oldest]) oldest = i;
        return oldest;
    }
};

} // namespace ChaosOsc
