#pragma once
#include "ofMain.h"
#include "ofSoundStream.h"
#include "PolyEngine.h"
#include "AudioEngine.h"
#include "ThomasSystem.h"
#include "FoldedTowelMap.h"
#include "WaveLibrary.h"
#include "ParamSystem.h"
#include "SliderUI.h"
#include "SamplePlayer.h"
#include "MidiOscInput.h"
#include "PresetManager.h"
#include "Recorder.h"
#include <fstream>
#include <cstring>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>

struct Slot {
    ChaosOsc::PolyEngine   engine;   // polyphonic -- 8 voices
    ChaosOsc::ParamSystem  params;
    ChaosOsc::WaveLibrary  waveLib;
    ChaosOsc::SampleBuffer sampleBuf;
    bool sampleMode    = false;
    bool normalizeOn   = false;
    bool envelopeOn    = false;
    bool envelopeLoop  = false;   // auto-retrigger at envTrigRate Hz
    bool loopOn        = true;
    int  midiChannel   = -1;      // -1 = omni

    ChaosOsc::SampleBuffer stagingBuf;
    std::atomic<bool> fileLoading {false};
    std::thread       loadThread;

    static constexpr int NUM_VIS = 32;
    ChaosOsc::ThomasSystem   thomas[32];
    ChaosOsc::FoldedTowelMap towel[32];

    std::vector<float> baseY, geom;
    float ptX[32] = {}, ptY[32] = {};

    int   editingWaveSlot = 1;
    std::vector<std::string> categories;
    int   currentCategoryIdx = 0;
    bool  morphByMouse = false;
    float displayPhase = 0.f;

    static constexpr int WAVE_DISP = 1024;
    std::vector<float> waveDisplay;

    void init(int slotIdx, double sampleRate, int numPoints);
    void update();
    void resetAll();
    void buildWaveDisplay();
};

class ofApp : public ofBaseApp {
public:
    void setup()   override;
    void update()  override;
    void draw()    override;
    void exit()    override;

    void audioOut(ofSoundBuffer& buffer) override;
    void keyPressed (int key)            override;
    void mouseMoved (int x, int y)       override;
    void mousePressed (int x, int y, int button) override;
    void mouseDragged (int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void dragEvent(ofDragInfo info)      override;

private:
    bool darkMode = true;
    struct Theme {
        ofColor bg, panel, text, textDim, accent, scopeLine, waveA, waveB;
    } theme;
    void applyTheme();

    Slot  slots[2];
    int   editSlot = 0;

    ChaosOsc::CrossfadeParam xfade;
    ChaosOsc::SliderUI       xfadeUI;
    ChaosOsc::SliderUI       sliderUI[2];
    ChaosOsc::SliderUI       fxUI[2];
    int  paramTab = 0;

    static constexpr int SCOPE_SIZE = 1024;
    float  scopeBuf[SCOPE_SIZE] = {};
    float  scopeDisplay[SCOPE_SIZE] = {};
    int    scopeWrite = 0;
    bool   scopeTriggered = false;
    float  scopePrev = 0.f;
    void   updateScope(float s);

    ofSoundStream soundStream;
    unsigned int  randomSeed = 42;

    ChaosOsc::MidiOscInput midiOsc;
    float pitchBendSemitones = 0.f;

    ChaosOsc::PresetManager presetMgr;
    ChaosOsc::Recorder      recorder;
    std::string             statusMsg;
    double                  statusExpiry = 0.0;
    void setStatus(const std::string& s) {
        statusMsg    = s;
        statusExpiry = ofGetElapsedTimeMillis() + 3000.0;
    }

    void savePreset();
    void loadPreset();
    void saveUndo();
    void restoreUndo();

    // MIDI learn -- key = midiCCKey(channel, cc)
    ChaosOsc::Param* midiLearnTarget = nullptr;
    struct MidiMapping { int slot; ChaosOsc::Param* param; int channel; int cc; };
    std::map<int, MidiMapping> midiCCMap;
    void startMidiLearn(ChaosOsc::Param* p);
    void saveMidiMap(const std::string& path);
    void loadMidiMap(const std::string& path);

    static float midiNoteToHz(int note) {
        return 440.f * std::pow(2.f, (note - 69) / 12.f);
    }

    bool showHelp = false;
    void drawHelpOverlay();

    std::vector<std::string> presetFiles;
    int   presetBrowseIdx = -1;
    void  refreshPresetList();
    void  drawPresetBrowser();

    int   dragPointSlot = -1;
    int   dragPointIdx  = -1;

    ofRectangle scopeRect, waveRect, paramRect, libRect, xfadeRect;
    void setupLayout();
    void setupSliders(int s);
    void setupFxSliders(int s);

    void updateSlotEngineParams(int s);
    bool loadSampleFile(const std::string& path, int s);
    void loadSampleFileBackground(const std::string& path, int s);

    void drawOscilloscope();
    void drawWaveAndPoints(int s);
    void drawWaveLibraryUI(int s);
    void drawDebugOverlay();
    void drawSlotStatus(int s);
    void drawXfader();

    float ptToScreenX(float nx, const ofRectangle& r) const;
    float ptToScreenY(float y, float midY, float ampH) const;
};
