#define _USE_MATH_DEFINES
#include "ofApp.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstring>
#include <numeric>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

static constexpr int NV = Slot::NUM_VIS;

// ================================================================
//  Slot
// ================================================================
void Slot::init(int slotIdx, double sampleRate, int numPoints)
{
    ChaosOsc::AudioConfig cfg;
    cfg.sampleRate  = sampleRate;
    cfg.blockSize   = 512;
    cfg.numChannels = 2;
    cfg.numPoints   = numPoints;
    engine.init(cfg);

    waveLib.init(numPoints);
    categories = waveLib.getCategories();
    baseY.resize(numPoints, 0.f);
    geom.resize(numPoints, 0.f);
    waveLib.getMorphed(baseY);
    geom = baseY;
    engine.pushGeometry(geom);

    for (int i = 0; i < NV; ++i) {
        double dt = 1.0 - (i / float(NV)) * 0.3;
        thomas[i].init(i + slotIdx * NV, dt);
        thomas[i].setChaosAmount(0.f);
        towel[i].init(i + slotIdx * NV);
    }
}

void Slot::update()
{
    float ca  = params.chaosGlobal.smoothed;
    float spd = params.chaosSpeed.smoothed;
    for (int i = 0; i < NV; ++i) { thomas[i].setSpeed(spd); thomas[i].setChaosAmount(ca); }

    if (ca > 0.001f)
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i < NV; ++i) { thomas[i].step(); towel[i].step(); }

    waveLib.getMorphed(baseY);
    for (int i = 0; i < NV; ++i) {
        float cy       = ca > 0.001f ? thomas[i].getY() : 0.f;
        float maxDrift = params.yAmplitude.smoothed * ca;
        float room     = 1.f - std::abs(baseY[i]);
        geom[i]        = ofClamp(baseY[i] + cy * maxDrift * room, -0.98f, 0.98f);
    }

    for (int i = 0; i < NV; ++i) {
        float baseX = i / float(NV - 1);
        if (ca > 0.001f) {
            float cx      = thomas[i].getX();
            float maxDrift = params.yAmplitude.smoothed * ca * (1.f / float(NV)) * 3.f;
            ptX[i] = baseX + cx * maxDrift;
        } else {
            ptX[i] = baseX;
        }
        ptY[i] = geom[i];
    }

    const float minGap = 1.f / float(NV) * 0.3f;
    for (int i = 1;     i < NV;  ++i) if (ptX[i] < ptX[i-1] + minGap) ptX[i] = ptX[i-1] + minGap;
    for (int i = NV-2;  i >= 0;  --i) if (ptX[i] > ptX[i+1] - minGap) ptX[i] = ptX[i+1] - minGap;
    ptX[0]    = std::max(0.f, ptX[0]);
    ptX[NV-1] = std::min(1.f, ptX[NV-1]);

    engine.pushGeometry(geom);
    displayPhase = engine.getDebugState().phase.load();
}

void Slot::resetAll()
{
    params.resetToDefaults();
    engine.reset();
    for (int i = 0; i < NV; ++i) {
        double dt = 1.0 - (i / float(NV)) * 0.3;
        thomas[i].init(i, dt);
        thomas[i].setChaosAmount(0.f);
        towel[i].init(i);
    }
    waveLib.setMorphT(0.f);
    waveLib.clearCustomShape();
}

void Slot::buildWaveDisplay()
{
    if (sampleBuf.samples.empty()) { waveDisplay.clear(); return; }
    waveDisplay.resize(WAVE_DISP);
    int N = (int)sampleBuf.samples.size();
    for (int i = 0; i < WAVE_DISP; ++i) {
        // Average a block of samples per display pixel
        int lo = (int)( i      / float(WAVE_DISP) * N);
        int hi = (int)((i + 1) / float(WAVE_DISP) * N);
        hi     = std::min(hi, N);
        float mx = 0.f;
        for (int j = lo; j < hi; ++j) mx = std::max(mx, std::abs(sampleBuf.samples[j]));
        waveDisplay[i] = mx;
    }
}


// ================================================================
//  Theme
// ================================================================
void ofApp::applyTheme()
{
    if (darkMode) {
        theme.bg       = ofColor(10,  11,  16);
        theme.panel    = ofColor(5,   7,   11);
        theme.text     = ofColor(200, 210, 230);
        theme.textDim  = ofColor(50,  55,  75);
        theme.accent   = ofColor(45,  210, 115);
        theme.scopeLine= ofColor(45,  210, 115);
        theme.waveA    = ofColor(40,  155, 75);
        theme.waveB    = ofColor(155, 90,  40);
    } else {
        theme.bg       = ofColor(220, 224, 236);
        theme.panel    = ofColor(200, 206, 222);
        theme.text     = ofColor(5,   8,   20);
        theme.textDim  = ofColor(25,  30,  50);
        theme.accent   = ofColor(10,  120, 55);
        theme.scopeLine= ofColor(10,  120, 55);
        theme.waveA    = ofColor(15,  90,  40);
        theme.waveB    = ofColor(110, 50,  15);
    }
    // Propagate darkMode to all slider UIs for theme-aware rendering
    for (int s = 0; s < 2; ++s) {
        sliderUI[s].darkMode = darkMode;
        fxUI[s].darkMode     = darkMode;
    }
}

// ================================================================
//  ofApp::setup
// ================================================================
void ofApp::setup()
{
    ofSetWindowTitle("ChaosOsc");
    ofSetFrameRate(60);
    ofSetFullscreen(true);
    applyTheme();
    recorder.init();
    refreshPresetList();

    for (int s = 0; s < 2; ++s) slots[s].init(s, 44100.0, NV);

    setupLayout();
    for (int s = 0; s < 2; ++s) { setupSliders(s); setupFxSliders(s); }

    xfadeUI.addSlider(&xfade.x, ofColor(180, 140, 220), false);
    xfadeUI.layout(xfadeRect.x, xfadeRect.y, xfadeRect.width, xfadeRect.height, 1);

    // MIDI / OSC
    midiOsc.init(8000);
    // Auto-load saved MIDI map if it exists
    loadMidiMap(ofToDataPath("midi_map.ini"));
    midiOsc.onNote = [this](ChaosOsc::MidiNoteEvent e) {
        float hz  = midiNoteToHz(e.note);
        float vel = e.velocity / 127.f;
        for (int s = 0; s < 2; ++s) {
            if (slots[s].midiChannel != -1 && slots[s].midiChannel != e.channel)
                continue;
            if (e.isOn) {
                slots[s].params.pitch.base = hz;
                slots[s].engine.noteOn(e.note, vel, hz);
            } else {
                slots[s].engine.noteOff(e.note);
            }
        }
    };
    midiOsc.onXfade = [this](float v) { xfade.x.base = v; };
    midiOsc.onSampleMode = [this](int s, bool on) {
        slots[s].sampleMode = on;
        slots[s].engine.setSampleMode(on);
    };
    midiOsc.onTrigger = [this](int s) { slots[s].engine.triggerSample(); };

    // Audio
    ofSoundStreamSettings ss;
    ss.setOutListener(this);
    ss.sampleRate        = 44100;
    ss.bufferSize        = 256;   // smaller buffer = lower latency
    ss.numOutputChannels = 2;
    ss.numInputChannels  = 0;
    ofSoundStream tmp;
    // Try WASAPI exclusive first (lower latency, not affected by window drag)
    bool started = false;
    auto devsEx = tmp.getDeviceList(ofSoundDevice::Api::MS_WASAPI);
    for (auto& d : devsEx) {
        if (d.isDefaultOutput) {
            ss.setOutDevice(d);
            // ofSoundStream doesn't expose exclusive mode flag directly --
            // set via environment hint if supported by driver
            break;
        }
    }
    started = soundStream.setup(ss);
    if (!started) {
        // Fallback: default device, larger buffer
        ss.bufferSize = 512;
        soundStream.setup(ss);
    }
}

void ofApp::setupLayout()
{
    float w = ofGetWidth(), h = ofGetHeight();
    xfadeRect = ofRectangle(0, 0,        w, h * 0.035f);
    scopeRect = ofRectangle(0, h*0.035f, w, h * 0.085f);
    waveRect  = ofRectangle(0, h*0.12f,  w, h * 0.13f);
    paramRect = ofRectangle(0, h*0.25f,  w, h * 0.57f);  // 57% of screen height
    libRect   = ofRectangle(0, h*0.82f,  w, h * 0.18f);
}

void ofApp::setupSliders(int s)
{
    using namespace ChaosOsc;
    auto& p  = slots[s].params;
    auto& ui = sliderUI[s];
    float x  = paramRect.x + s * paramRect.width * 0.5f;
    float w  = paramRect.width * 0.5f;
    ui.addSlider(&p.gain,          ofColor(80,  200, 120));
    ui.addSlider(&p.pan,           ofColor(180, 200,  80));
    ui.addSlider(&p.chaosGlobal,   ofColor(220, 100,  60));
    ui.addSlider(&p.chaosSpeed,    ofColor(200,  80,  40));
    ui.addSlider(&p.pitch,         ofColor(160, 220, 100));
    ui.addSlider(&p.fmDepth,       ofColor(210,  75, 115));
    ui.addSlider(&p.fmRatio,       ofColor(190,  55,  95));
    ui.addSlider(&p.warpAmount,    ofColor(115,  75, 210));
    ui.addSlider(&p.warpRatio,     ofColor( 95,  55, 190));
    ui.addSlider(&p.inharmonicity, ofColor(185, 145,  55));
    ui.addSlider(&p.feedbackGain,  ofColor( 55, 185, 185));
    ui.addSlider(&p.feedbackChaos, ofColor( 35, 165, 165));
    ui.addSlider(&p.fbDelayScale,  ofColor( 45, 145, 155));
    ui.addSlider(&p.yAmplitude,    ofColor( 55, 195, 155));
    ui.addSlider(&p.sampleSpeed,   ofColor(200, 160,  80), false);
    ui.layout(x, paramRect.y, w, paramRect.height, 2);
}

void ofApp::setupFxSliders(int s)
{
    using namespace ChaosOsc;
    auto& p  = slots[s].params;
    auto& ui = fxUI[s];
    float x  = paramRect.x + s * paramRect.width * 0.5f;
    float w  = paramRect.width * 0.5f;
    ui.addSlider(&p.filterCutoff,  ofColor(100, 180, 240));
    ui.addSlider(&p.filterRes,     ofColor( 80, 155, 210));
    ui.addSlider(&p.filterMode,    ofColor( 60, 120, 180), false);
    ui.addSlider(&p.chorusRate,    ofColor(180, 220, 100));
    ui.addSlider(&p.chorusDepth,   ofColor(160, 200,  80));
    ui.addSlider(&p.chorusWet,     ofColor(140, 180,  60));
    ui.addSlider(&p.reverbSize,    ofColor(200, 140, 220));
    ui.addSlider(&p.reverbDamp,    ofColor(180, 120, 200));
    ui.addSlider(&p.reverbWet,     ofColor(160, 100, 180));
    ui.addSlider(&p.crushBits,     ofColor(220, 160,  80));
    ui.addSlider(&p.crushRate,     ofColor(200, 140,  60));
    ui.addSlider(&p.loopStart,     ofColor(100, 180, 240));
    ui.addSlider(&p.loopEnd,       ofColor( 80, 155, 210));
    ui.addSlider(&p.envAttack,     ofColor(160, 240, 160));
    ui.addSlider(&p.envDecay,      ofColor(140, 220, 140));
    ui.addSlider(&p.envSustain,    ofColor(120, 200, 120));
    ui.addSlider(&p.envRelease,    ofColor( 80, 160, 100));
    ui.addSlider(&p.envTrigRate,   ofColor( 60, 200, 180), false);
    ui.layout(x, paramRect.y, w, paramRect.height, 2);
}

// ================================================================
//  update
// ================================================================
void ofApp::update()
{
    // Poll MIDI/OSC
    midiOsc.poll({ &slots[0].params, &slots[1].params });

    // Drain recorder ring buffer to disk
    recorder.drain();

    // MIDI learn: if waiting for CC, check for any incoming CC
    if (midiLearnTarget) {
        int learnKey = midiOsc.getLastCCKey();   // (channel<<8)|cc, or -1
        if (learnKey >= 0) {
            int ch = ChaosOsc::ccKeyChannel(learnKey);
            int cc = ChaosOsc::ccKeyCC(learnKey);
            // Find which slot owns this param
            int slot = 0;
            for (int s = 0; s < 2; ++s)
                for (auto* p : slots[s].params.all())
                    if (p == midiLearnTarget) slot = s;
            midiCCMap[learnKey] = {slot, midiLearnTarget, ch, cc};
            setStatus("Mapped Ch" + std::to_string(ch+1) + " CC" + std::to_string(cc)
                      + " -> " + midiLearnTarget->name + "  (auto-saved)");
            midiLearnTarget = nullptr;
            saveMidiMap(ofToDataPath("midi_map.ini"));
        }
    }
    // Apply mapped CCs
    for (auto& kv : midiCCMap) {
        float val = midiOsc.getCCValue(kv.first);
        if (val >= 0.f) kv.second.param->setNormBase(val);
    }

    // Clear status message when expired
    if (!statusMsg.empty() && ofGetElapsedTimeMillis() > statusExpiry)
        statusMsg.clear();

    // Apply pitch bend to current edit slot
    if (std::abs(midiOsc.pitchBendOffset) > 0.01f) {
        float& pb = slots[editSlot].params.pitch.base;
        pb = pb * std::pow(2.f, midiOsc.pitchBendOffset / 12.f);
        midiOsc.pitchBendOffset = 0.f;
    }

    for (int s = 0; s < 2; ++s) {
        auto& sl = slots[s];
        float ca = sl.params.chaosGlobal.base;
        std::vector<float> chaosSignals;
        auto allP = sl.params.all();
        for (int i = 0; i < (int)allP.size(); ++i) {
            int ci = i % NV;
            chaosSignals.push_back(ca > 0.001f ? sl.thomas[ci].getY() * ca : 0.f);
        }
        sl.params.update(chaosSignals);
        sl.update();
        updateSlotEngineParams(s);
    }
    xfade.x.update(0.f);
}

void ofApp::updateSlotEngineParams(int s)
{
    auto& sl = slots[s];
    auto& p  = sl.params;

    double baseFreq = std::max(20.0, (double)p.pitch.smoothed);
    double ca       = p.chaosGlobal.smoothed;
    double fmFreq   = baseFreq * p.fmRatio.smoothed
                    * (1.0 + (ca > 0.001 ? sl.thomas[0].getX() * ca * 0.4 : 0.0));
    double warpFreq = baseFreq * p.warpRatio.smoothed
                    * (1.0 + (ca > 0.001 ? sl.thomas[1].getY() * ca * 0.3 : 0.0));
    sl.engine.setFMFreq  (std::max(10.0, fmFreq));
    sl.engine.setWarpFreq(std::max(10.0, warpFreq));

    float fbC[ChaosOsc::FeedbackNetwork::NUM_DELAYS];
    for (int d = 0; d < ChaosOsc::FeedbackNetwork::NUM_DELAYS; ++d)
        fbC[d] = ca > 0.001f ? sl.thomas[(d * 3) % NV].getX() : 0.f;
    sl.engine.setFeedbackChaos(fbC);
    sl.engine.setSampleSpeed(p.sampleSpeed.smoothed);

    ChaosOsc::EngineParams ep;
    ep.frequency     = baseFreq;
    ep.gain          = p.gain.smoothed;
    ep.pan           = p.pan.smoothed;
    ep.fmDepth       = p.fmDepth.smoothed;
    ep.fmRatio       = p.fmRatio.smoothed;
    ep.warpAmount    = p.warpAmount.smoothed;
    ep.warpRatio     = p.warpRatio.smoothed;
    ep.inharmonicity = p.inharmonicity.smoothed;
    ep.feedbackGain  = p.feedbackGain.smoothed;
    ep.feedbackChaos = p.feedbackChaos.smoothed;
    ep.fbDelayScale  = p.fbDelayScale.smoothed;
    ep.yAmplitude    = p.yAmplitude.smoothed;
    ep.envAttack     = p.envAttack.smoothed;
    ep.envDecay      = p.envDecay.smoothed;
    ep.envSustain    = p.envSustain.smoothed;
    ep.envRelease    = p.envRelease.smoothed;
    ep.envTrigRate   = p.envTrigRate.smoothed;
    ep.envelopeOn    = slots[s].envelopeOn;
    ep.envelopeLoop  = slots[s].envelopeLoop;
    slots[s].engine.setLoopRegion(p.loopStart.smoothed, p.loopEnd.smoothed);
    slots[s].engine.setSampleLoop(slots[s].loopOn);
    ep.normalizeOn   = slots[s].normalizeOn;

    // Effects params
    ep.fx.filterCutoff = p.filterCutoff.smoothed;
    ep.fx.filterRes    = p.filterRes.smoothed;
    ep.fx.filterMode   = p.filterMode.iVal();
    ep.fx.filterOn     = p.filterCutoff.smoothed < 19500.f;
    ep.fx.chorusRate   = p.chorusRate.smoothed;
    ep.fx.chorusDepth  = p.chorusDepth.smoothed;
    ep.fx.chorusWet    = p.chorusWet.smoothed;
    ep.fx.reverbSize   = p.reverbSize.smoothed;
    ep.fx.reverbDamp   = p.reverbDamp.smoothed;
    ep.fx.reverbWet    = p.reverbWet.smoothed;
    ep.fx.crushBits    = p.crushBits.smoothed;
    ep.fx.crushRate    = p.crushRate.smoothed;

    sl.engine.setEnvelopeOn(slots[s].envelopeOn);
    sl.engine.setEnvelopeLoop(slots[s].envelopeLoop);
    sl.engine.pushParams(ep);
}

// ================================================================
//  audioOut -- triggered scope + crossfade
// ================================================================
void ofApp::updateScope(float s)
{
    // Triggered: wait for rising zero crossing, then capture SCOPE_SIZE samples
    static int  captureCount = 0;
    static bool capturing    = false;

    if (!capturing) {
        if (scopePrev < 0.f && s >= 0.f)   // rising zero crossing
            capturing = true;
        scopePrev = s;
    }
    if (capturing) {
        scopeBuf[captureCount++] = s;
        if (captureCount >= SCOPE_SIZE) {
            std::copy(scopeBuf, scopeBuf + SCOPE_SIZE, scopeDisplay);
            captureCount = 0;
            capturing    = false;
        }
    }
}

void ofApp::audioOut(ofSoundBuffer& buffer)
{
    // Boost audio thread priority once on first call
    static std::once_flag audioThreadOnce;
    std::call_once(audioThreadOnce, [](){
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
    });
    std::size_t nF = buffer.getNumFrames();
    int nCh = (int)buffer.getNumChannels();
    float* data = buffer.getBuffer().data();
    float xf = xfade.x.smoothed;

    for (std::size_t f = 0; f < nF; ++f) {
        float aL, aR, bL, bR;
        slots[0].engine.processStereo(aL, aR);
        slots[1].engine.processStereo(bL, bR);
        // Equal-power mix: xf=0 -> slot A only, xf=1 -> slot B only, xf=0.5 -> both full
        float gainA = (float)std::cos(xf * 3.14159265f * 0.5);
        float gainB = (float)std::sin(xf * 3.14159265f * 0.5);
        float outL = aL * gainA + bL * gainB;
        float outR = aR * gainA + bR * gainB;
        float outM = (outL + outR) * 0.5f;  // mono for scope
        updateScope(outM);
        recorder.push(outL, outR);
        if (nCh >= 2) {
            data[f * nCh + 0] = outL;
            data[f * nCh + 1] = outR;
        } else {
            data[f * nCh] = outM;
        }
    }
}

void ofApp::exit()
{
    if (recorder.isRecording()) recorder.stop();
    soundStream.stop();
    soundStream.close();
    midiOsc.close();
    for (int s = 0; s < 2; ++s)
        if (slots[s].loadThread.joinable()) slots[s].loadThread.join();
}

void ofApp::savePreset()
{
    ofFileDialogResult res = ofSystemSaveDialog("preset.cpreset", "Save Preset");
    if (!res.bSuccess) return;
    std::string path = res.getPath();
    if (path.find('.') == std::string::npos) path += ".cpreset";

    std::vector<ChaosOsc::PresetSlotState> states(2);
    for (int s = 0; s < 2; ++s) {
        states[s].params      = &slots[s].params;
        states[s].waveLib     = &slots[s].waveLib;
        states[s].sampleMode  = &slots[s].sampleMode;
        states[s].normalizeOn = &slots[s].normalizeOn;
        states[s].envelopeOn  = &slots[s].envelopeOn;
        states[s].envelopeLoop = &slots[s].envelopeLoop;
        states[s].loopOn      = &slots[s].loopOn;
    }
    std::string err = presetMgr.save(path, states, xfade.x.base, darkMode);
    if (err.empty()) refreshPresetList();
    setStatus(err.empty() ? "Preset saved: " + path : "Save failed: " + err);
}

void ofApp::loadPreset()
{
    ofFileDialogResult res = ofSystemLoadDialog("Load Preset");
    if (!res.bSuccess) return;

    std::vector<ChaosOsc::PresetSlotState> states(2);
    for (int s = 0; s < 2; ++s) {
        states[s].params      = &slots[s].params;
        states[s].waveLib     = &slots[s].waveLib;
        states[s].sampleMode  = &slots[s].sampleMode;
        states[s].normalizeOn = &slots[s].normalizeOn;
        states[s].envelopeOn  = &slots[s].envelopeOn;
        states[s].envelopeLoop = &slots[s].envelopeLoop;
        states[s].loopOn      = &slots[s].loopOn;
    }
    // Save undo before overwriting params
    for (int s = 0; s < 2; ++s) slots[s].params.saveUndo();
    std::string err = presetMgr.load(res.getPath(), states, xfade.x.base, darkMode);
    if (err.empty()) {
        applyTheme();
        for (int s = 0; s < 2; ++s)
            slots[s].engine.setSampleMode(slots[s].sampleMode);
        setStatus("Preset loaded: " + res.getPath());
    } else {
        setStatus("Load failed: " + err);
    }
}

// ================================================================
//  Mouse
// ================================================================
void ofApp::mousePressed(int x, int y, int button)
{
    // Right-click on a slider = MIDI learn
    if (button == 2) {
        // Check all sliders for hit
        for (int s = 0; s < 2; ++s) {
            auto& ui = (paramTab == 0) ? sliderUI[s] : fxUI[s];
            ChaosOsc::Param* hit = ui.getParamAt(x, y);
            if (hit) { startMidiLearn(hit); return; }
        }
    }

    // Left-click: check for waveform point drag
    dragPointSlot = -1; dragPointIdx = -1;

    // Preset browser click
    if (!presetFiles.empty()) {
        float ox = ofGetWidth() - 200.f, oy = libRect.y;
        if (x > ox && x < ox + 198) {
            int row = (y - (int)(oy + 22)) / 14;
            if (row >= 0 && row < (int)presetFiles.size()) {
                presetBrowseIdx = row;
                // Load on click
                std::vector<ChaosOsc::PresetSlotState> states(2);
                for (int s = 0; s < 2; ++s) {
                    states[s].params      = &slots[s].params;
                    states[s].waveLib     = &slots[s].waveLib;
                    states[s].sampleMode  = &slots[s].sampleMode;
                    states[s].normalizeOn = &slots[s].normalizeOn;
                    states[s].envelopeOn  = &slots[s].envelopeOn;
        states[s].envelopeLoop = &slots[s].envelopeLoop;
                                states[s].loopOn      = &slots[s].loopOn;
                }
                for (int s = 0; s < 2; ++s) slots[s].params.saveUndo();
                std::string err = presetMgr.load(presetFiles[row], states, xfade.x.base, darkMode);
                if (err.empty()) {
                    applyTheme();
                    for (int s = 0; s < 2; ++s) slots[s].engine.setSampleMode(slots[s].sampleMode);
                    setStatus("Loaded: " + ofFilePath::getFileName(presetFiles[row]));
                } else {
                    setStatus("Load failed: " + err);
                }
                return;
            }
        }
    }
    if (button != 0) goto slider_input;  // only left-click starts point drag

    {
        float hw = ofGetWidth() * 0.5f;
        for (int s = 0; s < 2; ++s) {
            ofRectangle wr(s * hw, waveRect.y, hw, waveRect.height);
            if (!wr.inside((float)x, (float)y)) continue;
            float aX   = wr.x + 30.f;
            float aW   = wr.width - 60.f;
            float midY = wr.y + wr.height * 0.5f;
            float ampH = wr.height * 0.38f;
            auto& sl   = slots[s];
            // Find nearest dot within 28px
            int   bestI = -1;
            float bestD = 28.f * 28.f;
            for (int i = 0; i < NV; ++i) {
                float sx = aX + sl.ptX[i] * aW;
                float sy = ptToScreenY(sl.ptY[i], midY, ampH);
                float dx = (float)x - sx, dy = (float)y - sy;
                float d2 = dx*dx + dy*dy;
                if (d2 < bestD) { bestD = d2; bestI = i; }
            }
            if (bestI >= 0) {
                dragPointSlot = s;
                dragPointIdx  = bestI;
                editSlot      = s;
                return;
            }
        }
    }

    slider_input:

    xfadeUI.mousePressed(x, y, button);
    if (paramTab == 0) for (int s = 0; s < 2; ++s) sliderUI[s].mousePressed(x, y, button);
    else               for (int s = 0; s < 2; ++s) fxUI[s].mousePressed(x, y, button);
}
void ofApp::mouseDragged(int x, int y, int button)
{
    // Drag a waveform control point
    if (dragPointSlot >= 0 && dragPointIdx >= 0) {
        auto& sl   = slots[dragPointSlot];
        float hw   = ofGetWidth() * 0.5f;
        ofRectangle wr(dragPointSlot * hw, waveRect.y, hw, waveRect.height);
        float aX   = wr.x + 30.f;
        float aW   = wr.width - 60.f;
        float midY = wr.y + wr.height * 0.5f;
        float ampH = wr.height * 0.38f;
        float ny   = -((float)y - midY) / ampH;
        ny = std::max(-0.98f, std::min(0.98f, ny));

        // Write to waveLib custom shape so update() doesn't snap it back
        if (!sl.waveLib.hasCustomShape()) {
            // First drag -- snapshot current geom as custom shape
            sl.waveLib.setCustomShape(sl.geom);
        }
        // Get mutable custom samples via a temp copy, edit, re-set
        std::vector<float> cs = sl.geom;
        if (sl.waveLib.hasCustomShape()) {
            // Re-read from waveLib to get last custom state
            sl.waveLib.getMorphed(cs);
        }
        if (dragPointIdx < (int)cs.size()) cs[dragPointIdx] = ny;
        sl.waveLib.setCustomShape(cs);
        return;
    }

    xfadeUI.mouseDragged(x, y, button);
    if (paramTab == 0) for (int s = 0; s < 2; ++s) sliderUI[s].mouseDragged(x, y, button);
    else               for (int s = 0; s < 2; ++s) fxUI[s].mouseDragged(x, y, button);
}
void ofApp::mouseReleased(int x, int y, int button)
{
    dragPointSlot = dragPointIdx = -1;
    xfadeUI.mouseReleased(x, y, button);
    if (paramTab == 0) for (int s = 0; s < 2; ++s) sliderUI[s].mouseReleased(x, y, button);
    else               for (int s = 0; s < 2; ++s) fxUI[s].mouseReleased(x, y, button);
}
void ofApp::mouseMoved(int x, int y)
{
    auto& sl = slots[editSlot];
    if (sl.morphByMouse) sl.waveLib.setMorphT(x / float(ofGetWidth()));
}
void ofApp::dragEvent(ofDragInfo info)
{
    for (auto& path : info.files) loadSampleFile(path.string(), editSlot);
}

// ================================================================
//  loadSampleFile
// ================================================================
bool ofApp::loadSampleFile(const std::string& path, int s)
{
    // If already loading, skip
    if (slots[s].fileLoading.load()) return false;

    // Detach previous thread if finished
    if (slots[s].loadThread.joinable()) {
        slots[s].loadThread.detach();
    }

    slots[s].fileLoading.store(true);

    // Copy path and s for capture (avoid dangling ref)
    std::string pathCopy = path;
    slots[s].loadThread = std::thread([this, pathCopy, s]() {
        loadSampleFileBackground(pathCopy, s);
        slots[s].fileLoading.store(false);
    });

    return true;  // loading started (may not be done yet)
}

// Called on background thread -- does all I/O + decoding
void ofApp::loadSampleFileBackground(const std::string& path, int s)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return;
    char riff[4]; f.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) return;
    uint32_t cs; f.read((char*)&cs, 4);
    char wave[4]; f.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) return;

    uint16_t audioFmt=0, numCh=0, bps=0;
    uint32_t sr=0, dataSize=0;
    std::vector<uint8_t> raw;

    while (!f.eof()) {
        char id[4]; f.read(id, 4);
        uint32_t sz; f.read((char*)&sz, 4);
        if (f.fail()) break;
        if (std::strncmp(id, "fmt ", 4) == 0) {
            f.read((char*)&audioFmt, 2); f.read((char*)&numCh, 2);
            f.read((char*)&sr, 4);
            uint32_t br; f.read((char*)&br, 4);
            uint16_t ba; f.read((char*)&ba, 2);
            f.read((char*)&bps, 2);
            if (sz > 16) f.seekg(sz-16, std::ios::cur);
        } else if (std::strncmp(id, "data", 4) == 0) {
            dataSize = sz; raw.resize(sz);
            f.read((char*)raw.data(), sz); break;
        } else { f.seekg(sz, std::ios::cur); }
    }
    if (raw.empty() || bps == 0 || numCh == 0) return;

    // Load into STAGING buffer -- never touch sampleBuf while audio thread reads it
    int Bps=bps/8, total=dataSize/(Bps*numCh);
    auto& staging = slots[s].stagingBuf;
    staging.samples.resize(total);
    staging.sampleRate = sr;
    staging.loaded = false;  // mark invalid until fully written
    for (int i = 0; i < total; ++i) {
        int off = i * Bps * numCh; float v = 0.f;
        if (bps==16)      { int16_t x; std::memcpy(&x, raw.data()+off, 2); v=x/32768.f; }
        else if (bps==24) { int32_t x=0; std::memcpy((char*)&x+1, raw.data()+off, 3); x>>=8; v=x/8388608.f; }
        else if (bps==32) { std::memcpy(&v, raw.data()+off, 4); }
        staging.samples[i] = v;
    }
    auto pos = path.find_last_of("/\\");
    staging.name = (pos != std::string::npos) ? path.substr(pos+1) : path;
    auto dot = staging.name.find_last_of('.');
    if (dot != std::string::npos) staging.name = staging.name.substr(0, dot);
    // NOW mark valid and publish pointer atomically
    // Compute peak normalisation gain BEFORE publishing
    staging.computeNormGain(0.9f);
    staging.loaded = true;
    // Move into live buffer, then publish pointer atomically
    slots[s].sampleBuf = std::move(staging);
    // buildWaveDisplay reads sampleBuf -- safe here (background thread,
    // but sampleBuf is not yet published to audio thread)
    slots[s].buildWaveDisplay();
    // Publish to audio thread
    slots[s].engine.loadSampleBuffer(&slots[s].sampleBuf);
    slots[s].engine.setSampleLoop(true);
    slots[s].sampleMode = true;
    slots[s].engine.setSampleMode(true);
    ofLogNotice() << "Loaded: " << slots[s].sampleBuf.name
                  << " normGain=" << slots[s].sampleBuf.normGain;
}

// ================================================================
//  keyPressed
// ================================================================
void ofApp::keyPressed(int key)
{
    auto& sl = slots[editSlot];
    switch (key) {
        case OF_KEY_F11:    ofToggleFullscreen(); break;
        case OF_KEY_TAB:    editSlot = 1 - editSlot; break;
        case 'e': case 'E': paramTab = 1 - paramTab; break;  // toggle synth/fx panel

        case OF_KEY_UP:
            sl.params.pitch.base = std::min(sl.params.pitch.maxVal, sl.params.pitch.base + 1.f); break;
        case OF_KEY_DOWN:
            sl.params.pitch.base = std::max(sl.params.pitch.minVal, sl.params.pitch.base - 1.f); break;
        case OF_KEY_PAGE_UP:
            sl.params.pitch.base = std::min(sl.params.pitch.maxVal, sl.params.pitch.base + 10.f); break;
        case OF_KEY_PAGE_DOWN:
            sl.params.pitch.base = std::max(sl.params.pitch.minVal, sl.params.pitch.base - 10.f); break;

        case ' ':
            if (ofGetKeyPressed(OF_KEY_CONTROL)) {
                for (int s = 0; s < 2; ++s) { slots[s].params.saveUndo(); randomSeed++; slots[s].params.randomize(false, randomSeed); }
                setStatus("Randomized both slots");
            } else {
                sl.params.saveUndo(); randomSeed++; sl.params.randomize(false, randomSeed);
            }
            break;
        case 's':
            sl.params.saveUndo(); randomSeed++; sl.params.randomize(true, randomSeed);
            break;
        case 'S':  // Shift+S = subtle randomize BOTH slots
            for (int s = 0; s < 2; ++s) { slots[s].params.saveUndo(); randomSeed++; slots[s].params.randomize(true, randomSeed); }
            setStatus("Subtle randomized both slots");
            break;
        case 'x': case 'X':  // X = clear MIDI CC map
            midiCCMap.clear();
            saveMidiMap(ofToDataPath("midi_map.ini"));
            setStatus("MIDI map cleared");
            break;
        case 'f': case 'F': {  // F = save MIDI map, Ctrl+F = load MIDI map
            if (ofGetKeyPressed(OF_KEY_CONTROL)) {
                ofFileDialogResult res = ofSystemLoadDialog("Load MIDI Map (.ini)");
                if (res.bSuccess) loadMidiMap(res.getPath());
            } else {
                ofFileDialogResult res = ofSystemSaveDialog("midi_map.ini", "Save MIDI Map");
                if (res.bSuccess) {
                    std::string p = res.getPath();
                    if (p.find('.') == std::string::npos) p += ".ini";
                    saveMidiMap(p);
                    setStatus("MIDI map saved: " + p);
                }
            }
            break;
        }

        case 19:   // Ctrl+S = save preset only
            savePreset(); break;
        case 15:   // Ctrl+O = load preset only
            loadPreset(); break;

        case '[': {
            // Cycle MIDI channel down: ch1 -> omni -> ch16 -> ch15 ...
            sl.midiChannel = (sl.midiChannel < 0) ? 15 : sl.midiChannel - 1;
            if (sl.midiChannel == -1)
                setStatus("Slot " + std::string(editSlot==0?"A":"B") + " MIDI: omni (all channels)");
            else
                setStatus("Slot " + std::string(editSlot==0?"A":"B") + " MIDI: channel " + std::to_string(sl.midiChannel+1));
            break;
        }
        case ']': {
            // Cycle MIDI channel up: omni -> ch1 -> ch2 -> ... -> ch16 -> omni
            sl.midiChannel = (sl.midiChannel >= 15) ? -1 : sl.midiChannel + 1;
            if (sl.midiChannel == -1)
                setStatus("Slot " + std::string(editSlot==0?"A":"B") + " MIDI: omni (all channels)");
            else
                setStatus("Slot " + std::string(editSlot==0?"A":"B") + " MIDI: channel " + std::to_string(sl.midiChannel+1));
            break;
        }

        case 'r': case 'R':
            if (ofGetKeyPressed(OF_KEY_SHIFT)) { slots[0].resetAll(); slots[1].resetAll(); }
            else sl.resetAll();
            break;

        case OF_KEY_RIGHT: {
            auto& wl = sl.waveLib;
            int idx = (sl.editingWaveSlot == 0 ? wl.getSlotA() : wl.getSlotB());
            idx = (idx + 1) % wl.getShapeCount();
            if (sl.editingWaveSlot == 0) wl.setSlotA(idx); else wl.setSlotB(idx);
            break;
        }
        case OF_KEY_LEFT: {
            auto& wl = sl.waveLib;
            int idx = (sl.editingWaveSlot == 0 ? wl.getSlotA() : wl.getSlotB());
            idx = (idx - 1 + wl.getShapeCount()) % wl.getShapeCount();
            if (sl.editingWaveSlot == 0) wl.setSlotA(idx); else wl.setSlotB(idx);
            break;
        }
        case 'a': case 'A': sl.editingWaveSlot = 0; break;
        case 'b': case 'B': sl.editingWaveSlot = 1; break;
        case 'd': case 'D': sl.morphByMouse = !sl.morphByMouse; break;
        case 'm': case 'M':
            if (sl.sampleBuf.loaded) {
                sl.sampleMode = !sl.sampleMode;
                sl.engine.setSampleMode(sl.sampleMode);
            }
            break;
        case 't': case 'T':
            sl.engine.triggerSample();
            sl.engine.envOn();
            setStatus("Triggered");
            break;
        case 'v': case 'V':
            // V: toggle envelope on/off
            sl.envelopeOn = !sl.envelopeOn;
            if (sl.envelopeOn) {
                sl.envelopeLoop = false;
                sl.engine.envOn();
                setStatus("Envelope ON -- MIDI/T triggers, G: loop");
            } else {
                sl.envelopeLoop = false;
                setStatus("Envelope OFF -- oscillator runs freely");
            }
            break;
        case 'g': case 'G':
            // G: toggle envelope loop (auto-retrigger at Trig Hz)
            if (!sl.envelopeOn) sl.envelopeOn = true;
            sl.envelopeLoop = !sl.envelopeLoop;
            if (sl.envelopeLoop) {
                sl.engine.envOn();
                setStatus("Envelope LOOP ON -- auto-retriggers at Trig Hz");
            } else {
                setStatus("Envelope LOOP OFF");
            }
            break;
        case 'l': case 'L': {
            ofFileDialogResult res = ofSystemLoadDialog("Load WAV", false);
            if (res.bSuccess) loadSampleFile(res.getPath(), editSlot);
            break;
        }
        case 'n': case 'N':
            sl.normalizeOn = !sl.normalizeOn; break;
        case 'c':   // C = randomize chaos current slot
            slots[editSlot].params.saveUndo(); randomSeed++;
            slots[editSlot].params.randomizeChaos(false, randomSeed);
            setStatus("Chaos randomized");
            break;
        case 'C':   // Shift+C = randomize chaos both slots
            for (int s = 0; s < 2; ++s) { slots[s].params.saveUndo(); randomSeed++; slots[s].params.randomizeChaos(false, randomSeed); }
            setStatus("Chaos randomized -- both slots");
            break;
        case 'o': case 'O':
            sl.loopOn = !sl.loopOn;
            sl.engine.setSampleLoop(sl.loopOn);
            break;
        case 'p': case 'P':  // theme toggle
            darkMode = !darkMode;
            applyTheme();
            break;

        case 13:  // Enter -- do nothing (was incorrectly clearing MIDI map)
            break;

        // Ctrl+L = load WAV file (same as L key)
        case 12:  // Ctrl+L
        {
            ofFileDialogResult res = ofSystemLoadDialog("Load WAV", false);
            if (res.bSuccess) loadSampleFile(res.getPath(), editSlot);
            break;
        }

        // Ctrl+R = toggle recording
        case 18:  // Ctrl+R
            if (recorder.isRecording()) {
                recorder.stop();
                setStatus("Recording stopped.");
            } else {
                std::string recPath = ofToDataPath(
                    "recording_" + ofGetTimestampString("%Y%m%d_%H%M%S") + ".wav");
                if (recorder.start(recPath, 44100.0, 2))
                    setStatus("Recording to: " + recPath);
                else
                    setStatus("Failed to start recording.");
            }
            break;

        // Ctrl+Z = undo last randomize/load
        case 26:  // Ctrl+Z
            if (sl.params.restoreUndo())
                setStatus("Undo restored.");
            break;

        case '+': case '=': sl.params.gain.base = std::min(1.f, sl.params.gain.base + 0.05f); break;
        case '-':            sl.params.gain.base = std::max(0.f, sl.params.gain.base - 0.05f); break;

        case '?':
            showHelp = !showHelp;
            break;
    }
}

// ================================================================
//  draw
// ================================================================
void ofApp::draw()
{
    ofBackground(theme.bg);
    drawXfader();
    drawOscilloscope();
    for (int s = 0; s < 2; ++s) drawWaveAndPoints(s);
    if (paramTab == 0) for (int s = 0; s < 2; ++s) sliderUI[s].draw();
    else               for (int s = 0; s < 2; ++s) fxUI[s].draw();
    // Tab indicator bar -- first row of paramRect
    float tabY = paramRect.y + 10;
    ofSetColor(darkMode ? ofColor(14,18,26) : ofColor(200,205,220));
    ofDrawRectangle(paramRect.x, paramRect.y, paramRect.width, 18);
    ofSetColor(paramTab == 0 ? ofColor(100,200,140) : ofColor(60,120,80));
    ofDrawBitmapString("[ SYNTH ]", (int)(paramRect.x + 12), (int)tabY + 8);
    ofSetColor(paramTab == 1 ? ofColor(100,160,230) : ofColor(55,85,130));
    ofDrawBitmapString("[ EFFECTS ]", (int)(paramRect.x + 110), (int)tabY + 8);
    ofSetColor(theme.textDim);
    ofDrawBitmapString("E to switch", (int)(paramRect.x + 220), (int)tabY + 8);
    for (int s = 0; s < 2; ++s) drawWaveLibraryUI(s);
    drawDebugOverlay();

    // Status message (preset save/load feedback, recording state)
    if (!statusMsg.empty()) {
        ofSetColor(255, 220, 80, 220);
        ofDrawBitmapString(statusMsg, 16, (int)(scopeRect.y + 14));
    }

    // Recording indicator
    if (recorder.isRecording()) {
        float blink = std::sin(ofGetElapsedTimef() * 6.f) > 0 ? 1.f : 0.f;
        ofSetColor(220, 40, 40, (int)(180 + 60 * blink));
        ofDrawCircle(ofGetWidth() - 18, 12, 7);
        ofSetColor(220, 80, 80);
        ofDrawBitmapString("REC " + ofToString(recorder.getDurationSec(), 1) + "s",
                           ofGetWidth() - 70, 16);
    }
}

// ================================================================
//  Draw helpers
// ================================================================
float ofApp::ptToScreenX(float nx, const ofRectangle& r) const { return r.x + nx * r.width; }
float ofApp::ptToScreenY(float y, float midY, float ampH)  const { return midY - y * ampH; }

void ofApp::drawXfader()
{
    float w = ofGetWidth();
    ofSetColor(theme.bg); ofDrawRectangle(xfadeRect);
    float x = xfadeRect.x + 6, y = xfadeRect.y + 3;
    float bw = xfadeRect.width - 12, bh = xfadeRect.height - 6;
    ofSetColor(theme.panel); ofDrawRectangle(x, y, bw, bh);
    float xv = xfade.x.smoothed;
    ofSetColor(60, 180, 100); ofDrawRectangle(x, y, bw*(1.f-xv), bh*0.5f);
    ofSetColor(180, 100, 60); ofDrawRectangle(x+bw*(1.f-xv), y+bh*0.5f, bw*xv, bh*0.5f);
    ofSetColor(240, 240, 255); ofDrawCircle(x+bw*xv, y+bh*0.5f, bh*0.55f);
    ofSetColor(editSlot==0 ? ofColor(100,240,140) : ofColor(50,120,70));
    ofDrawBitmapString("A", (int)(x+4), (int)(y+bh-2));
    ofSetColor(editSlot==1 ? ofColor(240,160,100) : ofColor(120,70,40));
    ofDrawBitmapString("B", (int)(x+bw-12), (int)(y+bh-2));
    ofSetColor(70,70,95);
    ofDrawBitmapString("TAB:slot  E:synth/fx  F:fullscreen  UP/DOWN:pitch  L:load  M:sample  T:trig  SPACE:rand  R:reset  Shift+R:both",
                       (int)(x + bw*0.18f), (int)(y+bh-2));
}

void ofApp::drawOscilloscope()
{
    float midY  = scopeRect.y + scopeRect.height * 0.5f;
    float ampSc = scopeRect.height * 0.44f;
    ofSetColor(theme.panel); ofDrawRectangle(scopeRect);
    ofSetColor(theme.textDim);
    ofDrawLine(scopeRect.x, midY, scopeRect.x + scopeRect.width, midY);
    ofSetColor(theme.scopeLine); ofSetLineWidth(1.5f);
    ofPolyline line; line.resize(SCOPE_SIZE);
    for (int i = 0; i < SCOPE_SIZE; ++i)
        line[i] = { scopeRect.x + (i / float(SCOPE_SIZE-1)) * scopeRect.width,
                    midY - scopeDisplay[i] * ampSc, 0.f };
    line.draw();
    // Phase tick marks
    for (int s = 0; s < 2; ++s) {
        ofSetColor(s==0 ? ofColor(80,200,120,60) : ofColor(200,120,80,60));
        float px = scopeRect.x + slots[s].displayPhase * scopeRect.width;
        ofDrawLine(px, scopeRect.y, px, scopeRect.y + scopeRect.height);
    }
}

void ofApp::drawWaveAndPoints(int s)
{
    float hw   = ofGetWidth() * 0.5f;
    ofRectangle wr(s * hw, waveRect.y, hw, waveRect.height);
    float aX   = wr.x + 30.f, aW = wr.width - 60.f;
    float midY = wr.y + wr.height * 0.5f;
    float ampH = wr.height * 0.38f;
    auto& sl   = slots[s];

    ofSetColor(s == editSlot ? theme.panel : theme.bg);
    ofDrawRectangle(wr);
    ofNoFill(); ofSetLineWidth(s == editSlot ? 2.f : 1.f);
    ofSetColor(s == editSlot
        ? (s == 0 ? ofColor(40,150,70,100) : ofColor(150,90,40,100))
        : ofColor(20,20,30,50));
    ofDrawRectangle(wr); ofFill();

    ofSetColor(22, 22, 36);
    ofDrawLine(aX, midY, aX+aW, midY);

    // Sample file waveform (drawn behind dots)
    if (!sl.waveDisplay.empty() && sl.sampleMode) {
        ofSetColor(s==0 ? ofColor(30,80,50,80) : ofColor(80,50,30,80));
        ofPolyline wl;
        for (int i = 0; i < (int)sl.waveDisplay.size(); ++i) {
            float nx = i / float(sl.waveDisplay.size()-1);
            float sy = midY - sl.waveDisplay[i] * ampH;
            wl.addVertex(aX + nx * aW, sy);
        }
        wl.draw();
        // Mirrored negative
        ofPolyline wl2;
        for (int i = 0; i < (int)sl.waveDisplay.size(); ++i) {
            float nx = i / float(sl.waveDisplay.size()-1);
            float sy = midY + sl.waveDisplay[i] * ampH;
            wl2.addVertex(aX + nx * aW, sy);
        }
        wl2.draw();
        // Loop region highlight
        float lsX = aX + sl.params.loopStart.smoothed * aW;
        float leX = aX + sl.params.loopEnd.smoothed   * aW;
        if (sl.loopOn) {
            ofSetColor(s==0 ? ofColor(80,200,120,25) : ofColor(200,120,80,25));
            ofDrawRectangle(lsX, wr.y, leX-lsX, wr.height);
            ofSetColor(s==0 ? ofColor(80,200,120,100) : ofColor(200,120,80,100));
            ofSetLineWidth(1.f);
            ofDrawLine(lsX, wr.y, lsX, wr.y+wr.height);
            ofDrawLine(leX, wr.y, leX, wr.y+wr.height);
        }
        // Playhead: position within loop region mapped to loop region screen bounds
        {
            float normPos = (float)sl.engine.getSamplePos();  // 0-1 within loop region
            float px = lsX + normPos * (leX - lsX);
            ofSetColor(s==0 ? ofColor(80,220,120,200) : ofColor(220,140,80,200));
            ofSetLineWidth(1.5f);
            ofDrawLine(px, wr.y+2, px, wr.y+wr.height-2);
        }
    }

    // ADSR curve overlay (when envelope is on)
    if (sl.envelopeOn) {
        auto& ep = sl.params;
        float totalTime = ep.envAttack.base + ep.envDecay.base + 0.3f + ep.envRelease.base;
        auto tToX = [&](float t) { return aX + (t / totalTime) * aW * 0.6f; };
        float top = wr.y + 6, bot = wr.y + wr.height - 10;
        float sus = bot + (top - bot) * ep.envSustain.base;
        float tA = ep.envAttack.base, tD = ep.envDecay.base, tR = ep.envRelease.base;
        ofSetColor(s==0 ? ofColor(80,220,140,180) : ofColor(220,140,80,180));
        ofSetLineWidth(1.5f);
        ofPolyline env;
        env.addVertex(tToX(0),         bot);
        env.addVertex(tToX(tA),        top);
        env.addVertex(tToX(tA+tD),     sus);
        env.addVertex(tToX(tA+tD+0.3f),sus);
        env.addVertex(tToX(tA+tD+0.3f+tR), bot);
        env.draw();
        // Labels
        ofSetColor(s==0 ? ofColor(60,180,100,140) : ofColor(180,100,60,140));
        ofDrawBitmapString("A", (int)tToX(tA*0.5f), (int)bot-2);
        ofDrawBitmapString("D", (int)tToX(tA+tD*0.5f), (int)sus-2);
        ofDrawBitmapString("S", (int)tToX(tA+tD+0.15f), (int)sus-2);
        ofDrawBitmapString("R", (int)tToX(tA+tD+0.3f+tR*0.5f), (int)bot-2);
    }

    // Reference shape A
    const auto& sA = sl.waveLib.getShape(sl.waveLib.getSlotA());
    ofSetColor(25,60,38,60); ofPolyline ref;
    for (int i = 0; i < (int)sA.samples.size(); ++i)
        ref.addVertex(aX + (i/float(sA.samples.size()-1))*aW,
                      ptToScreenY(sA.samples[i], midY, ampH));
    ref.draw();

    // Live chaos curve
    ofColor wc = (s==0 ? theme.waveA : theme.waveB); wc.a = 170;
    ofSetColor(wc);
    ofSetLineWidth(1.5f);
    ofPolyline ml;
    for (int i = 0; i < NV; ++i)
        ml.addVertex(aX + sl.ptX[i]*aW, ptToScreenY(sl.ptY[i], midY, ampH));
    ml.draw();

    // Dots
    float ca = sl.params.chaosGlobal.smoothed;
    ofColor dotCol; dotCol.setHsb(ofMap(ca,0,1,172,12), 190, 230);
    for (int i = 0; i < NV; ++i) {
        float sx = aX + sl.ptX[i]*aW, sy = ptToScreenY(sl.ptY[i], midY, ampH);
        ofSetColor(dotCol.r,dotCol.g,dotCol.b,30); ofDrawCircle(sx,sy,7.f);
        ofSetColor(dotCol); ofDrawCircle(sx,sy,3.f);
    }

    // Label
    // Loading indicator
    if (sl.fileLoading.load()) {
        ofSetColor(255, 200, 80, 180);
        ofDrawBitmapString("LOADING...", (int)(wr.x + wr.width*0.4f), (int)midY);
    }

    ofSetColor(theme.textDim);
    std::string fxStr = "";
    if (sl.params.filterCutoff.base < 19500.f) fxStr += " FLT";
    if (sl.params.chorusWet.base > 0.01f)      fxStr += " CHR";
    if (sl.params.reverbWet.base > 0.01f)      fxStr += " REV";
    if (sl.params.crushRate.base > 0.01f || sl.params.crushBits.base < 15.9f) fxStr += " CRUSH";
    std::string slotStr =
        std::string(s==0?"[A] ":"[B] ") +
        ofToString(sl.params.pitch.smoothed,1)+"Hz" +
        (sl.sampleMode ? "  SMPL x"+ofToString(sl.params.sampleSpeed.smoothed,2) : "  WAVE") +
        (sl.waveLib.hasCustomShape() ? "  DRAW" : "") +
        "  pan="+ofToString(sl.params.pan.base,2) +
        "  chaos="+ofToString(ca,2) +
        (sl.midiChannel >= 0 ? "  ch"+std::to_string(sl.midiChannel+1) : "  omni") +
        fxStr;
    ofDrawBitmapString(slotStr, (int)aX, (int)(wr.y+wr.height-4));

    // Pan indicator -- small L/R bar
    {
        float panV = sl.params.pan.smoothed;   // -1..1
        float px   = aX + aW * 0.88f, py = wr.y + 6.f, pw = aW * 0.1f, ph = 5.f;
        ofSetColor(30, 35, 50);
        ofDrawRectangle(px, py, pw, ph);
        float ctr = px + pw * 0.5f;
        float barW = pw * 0.5f * std::abs(panV);
        if (panV < 0.f) {
            ofSetColor(s==0 ? ofColor(60,180,100) : ofColor(180,100,60));
            ofDrawRectangle(ctr - barW, py, barW, ph);
        } else {
            ofSetColor(s==0 ? ofColor(60,180,100) : ofColor(180,100,60));
            ofDrawRectangle(ctr, py, barW, ph);
        }
        ofSetColor(200,200,220,120);
        ofDrawLine(ctr, py, ctr, py + ph);
        ofSetColor(theme.textDim);
        ofDrawBitmapString(panV < -0.05f ? "L" : panV > 0.05f ? "R" : "C",
                           (int)(px + pw + 2), (int)(py + ph));
    }
}

void ofApp::drawWaveLibraryUI(int s)
{
    float hw = ofGetWidth() * 0.5f;
    ofRectangle lr(s * hw, libRect.y, hw, libRect.height);
    auto& sl = slots[s];
    ofSetColor(theme.panel); ofDrawRectangle(lr);

    float mt = sl.waveLib.getMorphT();
    const auto& sA = sl.waveLib.getShape(sl.waveLib.getSlotA());
    const auto& sB = sl.waveLib.getShape(sl.waveLib.getSlotB());

    ofSetColor(sl.editingWaveSlot==0 ? ofColor(100,220,140) : ofColor(55,130,80));
    ofDrawBitmapString("[A"+std::string(sl.editingWaveSlot==0?"*":" ")+"] "+sA.name+" ["+sA.category+"]",
                       (int)lr.x+10, (int)lr.y+18);
    ofSetColor(sl.editingWaveSlot==1 ? ofColor(220,140,80) : ofColor(130,80,45));
    ofDrawBitmapString("[B"+std::string(sl.editingWaveSlot==1?"*":" ")+"] "+sB.name+" ["+sB.category+"]",
                       (int)lr.x+10, (int)lr.y+34);

    float bX=lr.x+10, bY=lr.y+48, bW=lr.width-20;
    ofSetColor(18,22,28); ofDrawRectangle(bX,bY,bW,7);
    ofSetColor(55,130,80); ofDrawRectangle(bX,bY,bW*(1-mt),3.5f);
    ofSetColor(130,80,45); ofDrawRectangle(bX+bW*(1-mt),bY+3.5f,bW*mt,3.5f);
    ofSetColor(220,220,240,160); ofDrawCircle(bX+bW*mt,bY+3.5f,4);

    if (sl.sampleBuf.loaded) {
        float dur = sl.sampleBuf.samples.size() / (float)sl.sampleBuf.sampleRate;
        ofSetColor(s==0 ? ofColor(80,200,120,180) : ofColor(200,120,80,180));
        ofDrawBitmapString("SMPL: "+sl.sampleBuf.name+" ("+ofToString(dur,1)+"s)  M:toggle",
                           (int)lr.x+10, (int)lr.y+70);
    }
    // Help text and MIDI indicator drawn once (slot 0 call), centred across full width
    if (s == 0) {
        ofSetColor(theme.textDim);
        ofDrawBitmapString(
            "A/B:shape  <>:nav  D:morph  L:wav  N:norm  V:env  G:env-loop  O:loop  P:theme  X:clr-midi  []:ch"
            "   S:subtle  Shift+S:both  C:chaos  Space:rand  Ctrl+S:preset  Ctrl+O:load  F:midi-save  F11:fullscreen",
            (int)(ofGetWidth() * 0.5f - 390), (int)lr.y + 92);
        ofSetColor(midiOsc.midiOk ? ofColor(60,180,80) : (midiOsc.midiDevCount > 0 ? ofColor(220,160,0) : ofColor(160,60,60)));
        ofDrawBitmapString(midiOsc.midiOk ? "MIDI:OK" : (midiOsc.midiDevCount > 0 ? "MIDI:connecting" : "noMIDI"),
                           (int)(ofGetWidth() - 110), 18);
        ofSetColor(midiOsc.oscMsgCount > 0 ? ofColor(60,200,255) : ofColor(80,50,50));
        ofDrawBitmapString("OSC:" + ofToString(midiOsc.oscMsgCount), (int)(ofGetWidth() - 80), 32);
        if (!midiCCMap.empty()) {
            ofSetColor(ofColor(220,180,60));
            ofDrawBitmapString("CC:" + ofToString((int)midiCCMap.size()), (int)(ofGetWidth() - 80), 46);
        }
    }
}

void ofApp::drawDebugOverlay()
{
    ofSetColor(theme.textDim);
    ofDrawBitmapString(
        "slot="+ std::string(editSlot==0?"A":"B") +
        "  panel="+std::string(paramTab==0?"SYNTH":"FX") +
        (slots[editSlot].normalizeOn ? "  NORM" : "") +
        (slots[editSlot].envelopeLoop ? "  ENV-LOOP" :
         slots[editSlot].envelopeOn  ? "  ENV" : "") +
        (slots[editSlot].loopOn      ? "  LOOP" : "  one-shot") +
        (midiLearnTarget ? "  [MIDI LEARN -- move CC]" : "") +
        "  blk="+ofToString(slots[0].engine.getDebugState().blockCount.load()),
        16, (int)(scopeRect.y + scopeRect.height - 6));

    if (showHelp) drawHelpOverlay();
    drawPresetBrowser();
}

void ofApp::drawHelpOverlay()
{
    float w = ofGetWidth() * 0.5f, h = ofGetHeight() * 0.7f;
    float ox = ofGetWidth() * 0.25f, oy = ofGetHeight() * 0.1f;
    ofSetColor(0, 0, 0, 200);
    ofDrawRectangle(ox, oy, w, h);
    ofSetColor(60, 200, 120);
    ofDrawBitmapString("KEYBOARD SHORTCUTS  (? to close)", (int)(ox+20), (int)(oy+20));
    ofSetColor(180, 190, 210);
    const char* lines[] = {
        "TAB          Switch editing slot A / B",
        "E            Toggle SYNTH / EFFECTS panel",
        "F            Toggle fullscreen",
        "SPACE        Randomize current slot",
        "Ctrl+Space   Randomize BOTH slots",
        "S            Subtle randomize current slot",
        "Shift+S      Subtle randomize BOTH slots",
        "C            Randomize chaos amounts (current slot)",
        "Shift+C      Randomize chaos amounts (both slots)",
        "R            Reset current slot",
        "Shift+R      Reset both slots",
        "Ctrl+Z       Undo last randomize / load",
        "UP / DOWN    Pitch +/- 1 Hz",
        "PgUp / PgDn  Pitch +/- 10 Hz",
        "+ / -        Gain +/- 0.05",
        "A / B        Select wave morph slot A or B",
        "< / >        Navigate waveshapes",
        "D            Toggle mouse-drag morph",
        "M            Toggle sample mode",
        "T            Trigger envelope + sample (one-shot)",
        "V            Toggle envelope on/off",
        "             OFF: oscillator/sample runs freely at full volume",
        "             ON:  MIDI note-on triggers ADSR, note-off releases",
        "G            Toggle envelope loop (auto-retrigger at Trig Hz)",
        "O            Toggle sample loop on/off",
        "L            Load WAV file (dialog)",
        "N            Toggle normalize",
        "P            Toggle dark/light theme",
        "[ / ]        Cycle MIDI channel for slot  (omni, ch1-16)",
        "X            Clear all MIDI CC mappings",
        "",
        "PRESETS & MIDI MAP  (saved independently)",
        "Ctrl+S       Save preset (.cpreset)",
        "Ctrl+O       Load preset (.cpreset)",
        "F            Save MIDI map (dialog)  /  Ctrl+F: load MIDI map",
        "F11          Toggle fullscreen",
        "             midi_map.ini auto-loads on startup",
        "Ctrl+R       Start / stop WAV recording",
        "?            Show / hide this help",
        "",
        "MOUSE",
        "Right-click slider   MIDI learn (channel+CC aware)",
        "Dbl-click slider     Reset to default",
        "Drag dot             Sculpt waveshape",
        "Drag WAV file        Load into current slot",
        "",
        "ENV mode: 8-voice polyphony, oldest-note stealing",
        "          Velocity scales per-voice amp, Gain slider unaffected",
    };
    int ny = (int)(oy + 36);
    for (auto* l : lines) {
        ofDrawBitmapString(l, (int)(ox + 20), ny);
        ny += 14;
    }
}

void ofApp::refreshPresetList()
{
    presetFiles.clear();
    ofDirectory dir(ofToDataPath(""));
    dir.allowExt("cpreset");
    dir.listDir();
    for (int i = 0; i < (int)dir.size(); ++i)
        presetFiles.push_back(dir.getPath(i));
}

void ofApp::drawPresetBrowser()
{
    if (presetFiles.empty()) return;
    float ox = ofGetWidth() - 200.f;
    float oy = libRect.y;
    ofSetColor(darkMode ? ofColor(8,10,16,230) : ofColor(210,215,228,230));
    ofDrawRectangle(ox, oy, 198, (float)presetFiles.size() * 14 + 22);
    ofSetColor(theme.accent);
    ofDrawBitmapString("PRESETS", (int)(ox+6), (int)(oy+12));
    for (int i = 0; i < (int)presetFiles.size(); ++i) {
        bool sel = (i == presetBrowseIdx);
        ofSetColor(sel ? theme.accent : theme.text);
        std::string fname = ofFilePath::getFileName(presetFiles[i]);
        // strip extension
        auto dot = fname.rfind('.');
        if (dot != std::string::npos) fname = fname.substr(0, dot);
        ofDrawBitmapString((sel ? "> " : "  ") + fname, (int)(ox+6), (int)(oy + 22 + i * 14));
    }
}

void ofApp::startMidiLearn(ChaosOsc::Param* p)
{
    midiLearnTarget = p;
    setStatus("MIDI learn: move a CC for '" + p->name + "'");
}

void ofApp::saveMidiMap(const std::string& path)
{
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "# ChaosOsc MIDI map  format: cc.CHANNEL.CC=SLOT:ParamName\n";
    for (auto& kv : midiCCMap) {
        int ch = ChaosOsc::ccKeyChannel(kv.first);
        int cc = ChaosOsc::ccKeyCC(kv.first);
        f << "cc." << ch << "." << cc << "="
          << kv.second.slot << ":" << kv.second.param->name << "\n";
    }
}

void ofApp::loadMidiMap(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return;
    midiCCMap.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key.substr(0, 3) != "cc.") continue;
        // key = "cc.CHANNEL.CC"
        std::string rest = key.substr(3);
        int ch = 0, cc = 0;
        auto dot = rest.find('.');
        if (dot != std::string::npos) {
            ch = std::stoi(rest.substr(0, dot));
            cc = std::stoi(rest.substr(dot + 1));
        } else {
            // legacy format: cc.N  (channel 0)
            cc = std::stoi(rest);
        }
        auto colon = val.find(':');
        if (colon == std::string::npos) continue;
        int slot = std::stoi(val.substr(0, colon));
        std::string pname = val.substr(colon + 1);
        if (slot < 0 || slot > 1) continue;
        for (auto* p : slots[slot].params.all()) {
            if (p->name == pname) {
                int mapKey = ChaosOsc::midiCCKey(ch, cc);
                midiCCMap[mapKey] = {slot, p, ch, cc};
                break;
            }
        }
    }
    if (!midiCCMap.empty())
        setStatus("MIDI map loaded: " + std::to_string(midiCCMap.size()) + " mappings");
}
