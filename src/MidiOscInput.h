#pragma once
// ============================================================
//  MidiOscInput -- zero external dependencies.
//  MIDI via Windows WinMM.  OSC via Winsock2 UDP + OSC 1.0.
//
//  CC mapping: NONE by default.
//  All CC->param routing is done externally in ofApp via midiCCMap.
//  Channel is preserved: key = (channel << 8) | cc_number.
//
//  OSC paths (port 8000 UDP):
//    /slot/<0|1>/pitch  /gain  /pan  /chaos  /chaos_speed
//    /slot/<0|1>/y_amp  /fm_depth  /fm_ratio  /warp  /warp_ratio
//    /slot/<0|1>/inharm  /fb_gain  /fb_chaos  /fb_delay
//    /slot/<0|1>/speed  /loop_start  /loop_end
//    /slot/<0|1>/env_attack  /env_decay  /env_sustain  /env_release  /env_rate
//    /slot/<0|1>/filter  /filter_res  /filter_mode
//    /slot/<0|1>/chorus  /chorus_rate  /chorus_depth
//    /slot/<0|1>/reverb  /reverb_size  /reverb_damp
//    /slot/<0|1>/crush_bits  /crush_rate
//    /slot/<0|1>/trigger  /sample_mode
//    /xfade   f  0-1
// ============================================================

#include "ParamSystem.h"
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <mutex>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <mmsystem.h>
  #include <winsock2.h>
  #pragma comment(lib, "ws2_32.lib")
  #pragma comment(lib, "winmm.lib")
#endif

namespace ChaosOsc {

struct MidiNoteEvent {
    int   channel  = 0;
    int   note     = 60;
    int   velocity = 100;
    bool  isOn     = true;
};

// Encodes channel+CC into a single int key: (channel << 8) | cc
inline int midiCCKey(int channel, int cc) { return ((channel & 0xFF) << 8) | (cc & 0xFF); }
inline int ccKeyChannel(int key)           { return (key >> 8) & 0xFF; }
inline int ccKeyCC(int key)                { return key & 0xFF; }

// ── OSC 1.0 parser ──────────────────────────────────────────
struct OscMsg {
    std::string address;
    struct Arg { char tag; float f; int32_t i; };
    Arg args[8]; int numArgs=0;
    float getFloat(int n) const { return n<numArgs?args[n].f:0.f; }
    int   getInt  (int n) const { return n<numArgs?args[n].i:0;   }
};

static bool parseOsc(const char* d, int len, OscMsg& out) {
    if (len<4||d[0]!='/') return false;
    int pos=0; out.address.clear();
    while (pos<len&&d[pos]) out.address+=d[pos++];
    pos=(pos+4)&~3;
    if (pos>=len||d[pos]!=',') return true;
    pos++; std::string tags;
    while (pos<len&&d[pos]) tags+=d[pos++];
    pos=(pos+4)&~3; out.numArgs=0;
    for (char tag:tags) {
        if (out.numArgs>=8||pos+4>len) break;
        OscMsg::Arg& a=out.args[out.numArgs++]; a.tag=tag;
        uint32_t raw; memcpy(&raw,d+pos,4);
        raw=((raw>>24)&0xFF)|((raw>>8)&0xFF00)|((raw<<8)&0xFF0000)|((raw<<24)&0xFF000000u);
        if (tag=='f'){memcpy(&a.f,&raw,4);a.i=(int32_t)a.f;}
        else         {memcpy(&a.i,&raw,4);a.f=(float)a.i;}
        pos+=4;
    }
    return true;
}

// ── MidiOscInput ────────────────────────────────────────────
class MidiOscInput {
public:
    // Note callback: provides full MidiNoteEvent (channel, note, velocity, isOn)
    std::function<void(MidiNoteEvent)>    onNote;
    // OSC-specific callbacks
    std::function<void(float)>            onXfade;
    std::function<void(int,bool)>         onSampleMode;
    std::function<void(int)>              onTrigger;

    bool  midiOk       = false;
    int   midiDevCount = 0;
    bool  oscOk        = false;
    int   oscMsgCount  = 0;
    float pitchBendOffset = 0.f;

    bool init(int oscPort = 8000) {
#ifdef _WIN32
        tryOpenMidi();
        WSADATA wsa; WSAStartup(MAKEWORD(2,2),&wsa);
        udpSock = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if (udpSock != INVALID_SOCKET) {
            u_long nb=1; ioctlsocket(udpSock,FIONBIO,&nb);
            sockaddr_in a{}; a.sin_family=AF_INET;
            a.sin_addr.s_addr=INADDR_ANY;
            a.sin_port=htons((u_short)oscPort);
            if (bind(udpSock,(sockaddr*)&a,sizeof(a))==0) oscOk=true;
            else { closesocket(udpSock); udpSock=INVALID_SOCKET; }
        }
#endif
        return midiOk || oscOk;
    }

    void close() {
#ifdef _WIN32
        if (midiOk && midiHandle) {
            midiInStop(midiHandle);
            midiInClose(midiHandle);
            midiHandle = nullptr;
        }
        if (udpSock!=INVALID_SOCKET) { closesocket(udpSock); udpSock=INVALID_SOCKET; }
        WSACleanup();
#endif
    }

    // Call every frame.  params[] used for OSC dispatch only.
    void poll(std::array<ParamSystem*,2> params) {
        // Retry MIDI every ~2 sec
        if (++retryCounter > 120) { retryCounter=0; tryOpenMidi(); }
        {
            std::lock_guard<std::mutex> lock(midiMutex);
            for (auto& rm : midiQueue) processMidiMsg(rm);
            midiQueue.clear();
        }
#ifdef _WIN32
        if (udpSock!=INVALID_SOCKET) {
            char buf[4096]; int bytes;
            while ((bytes=recv(udpSock,buf,sizeof(buf),0))>0) {
                OscMsg msg;
                if (parseOsc(buf,bytes,msg)) { dispatchOsc(msg,params); oscMsgCount++; }
            }
        }
#endif
    }

    // Returns combined channel+cc key for the last received CC, or -1
    // Key = midiCCKey(channel, cc).  Use ccKeyChannel()/ccKeyCC() to unpack.
    int   getLastCCKey()             { int v=lastCCKey; lastCCKey=-1; return v; }
    // Returns normalised [0,1] value for key, erases entry, or -1 if not present
    float getCCValue(int ccKey) {
        auto it=ccValues.find(ccKey);
        if (it==ccValues.end()) return -1.f;
        float v=it->second; ccValues.erase(it); return v;
    }

private:
#ifdef _WIN32
    HMIDIIN midiHandle = nullptr;
    SOCKET  udpSock    = INVALID_SOCKET;
#endif
    int   lastCCKey = -1;
    std::map<int,float> ccValues;   // key=midiCCKey -> normalised value
    std::mutex midiMutex;
    int   retryCounter = 0;
    struct RawMidi { BYTE status, b1, b2; };
    std::vector<RawMidi> midiQueue;

    void tryOpenMidi() {
#ifdef _WIN32
        midiDevCount = (int)midiInGetNumDevs();
        if (!midiOk && midiDevCount > 0) {
            MMRESULT res = midiInOpen(&midiHandle, 0,
                (DWORD_PTR)midiCallback, (DWORD_PTR)this, CALLBACK_FUNCTION);
            if (res == MMSYSERR_NOERROR) { midiInStart(midiHandle); midiOk=true; }
        }
        if (midiOk && midiDevCount==0) {
            midiInStop(midiHandle); midiInClose(midiHandle);
            midiHandle=nullptr; midiOk=false;
        }
#endif
    }

#ifdef _WIN32
    static void CALLBACK midiCallback(HMIDIIN, UINT msg,
        DWORD_PTR instance, DWORD_PTR p1, DWORD_PTR)
    {
        if (msg!=MIM_DATA) return;
        auto* self=reinterpret_cast<MidiOscInput*>(instance);
        RawMidi rm;
        rm.status=(BYTE)(p1&0xFF);
        rm.b1=(BYTE)((p1>>8)&0xFF);
        rm.b2=(BYTE)((p1>>16)&0xFF);
        std::lock_guard<std::mutex> lock(self->midiMutex);
        self->midiQueue.push_back(rm);
    }
#endif

    void processMidiMsg(const RawMidi& rm) {
        BYTE type    = rm.status & 0xF0;
        int  channel = rm.status & 0x0F;
        if (type==0x90 && rm.b2>0) {
            MidiNoteEvent e; e.channel=channel; e.note=rm.b1;
            e.velocity=rm.b2; e.isOn=true;
            if (onNote) onNote(e);
        } else if (type==0x80 || (type==0x90 && rm.b2==0)) {
            MidiNoteEvent e; e.channel=channel; e.note=rm.b1;
            e.velocity=0; e.isOn=false;
            if (onNote) onNote(e);
        } else if (type==0xB0) {
            int key = midiCCKey(channel, rm.b1);
            float v = rm.b2 / 127.f;
            lastCCKey = key;
            ccValues[key] = v;
        } else if (type==0xE0) {
            int bend = (rm.b2<<7)|rm.b1;
            pitchBendOffset = ((bend-8192)/8192.f)*2.f;
        }
    }

    void dispatchOsc(const OscMsg& msg, std::array<ParamSystem*,2>& ps) {
        const std::string& addr=msg.address;
        if (addr=="/xfade"){if(onXfade&&msg.numArgs>0)onXfade(msg.getFloat(0));return;}
        for (int s=0;s<2;++s) {
            std::string pre="/slot/"+std::to_string(s)+"/";
            if (addr.size()<=pre.size()||addr.substr(0,pre.size())!=pre) continue;
            std::string pm=addr.substr(pre.size());
            auto* p=ps[s]; if(!p) continue;
            if (pm=="trigger")    {if(onTrigger)onTrigger(s);return;}
            if (pm=="sample_mode"&&msg.numArgs>0){if(onSampleMode)onSampleMode(s,msg.getInt(0)!=0);return;}
            if (msg.numArgs<1) return;
            float v=msg.getFloat(0);
            if      (pm=="pitch")        p->pitch.base        =std::max(20.f,  std::min(8000.f,v));
            else if (pm=="gain")         p->gain.base         =std::max(0.f,   std::min(1.f,v));
            else if (pm=="pan")          p->pan.base          =std::max(-1.f,  std::min(1.f,v));
            else if (pm=="chaos")        p->chaosGlobal.base  =std::max(0.f,   std::min(1.f,v));
            else if (pm=="chaos_speed")  p->chaosSpeed.base   =std::max(0.1f,  std::min(8.f,v));
            else if (pm=="y_amp")        p->yAmplitude.base   =std::max(0.f,   std::min(1.f,v));
            else if (pm=="fm_depth")     p->fmDepth.base      =std::max(0.f,   std::min(1.f,v));
            else if (pm=="fm_ratio")     p->fmRatio.base      =std::max(0.5f,  std::min(8.f,v));
            else if (pm=="warp")         p->warpAmount.base   =std::max(0.f,   std::min(1.f,v));
            else if (pm=="warp_ratio")   p->warpRatio.base    =std::max(0.5f,  std::min(8.f,v));
            else if (pm=="inharm")       p->inharmonicity.base=std::max(0.f,   std::min(1.f,v));
            else if (pm=="fb_gain")      p->feedbackGain.base =std::max(0.f,   std::min(0.95f,v));
            else if (pm=="fb_chaos")     p->feedbackChaos.base=std::max(0.f,   std::min(1.f,v));
            else if (pm=="fb_delay")     p->fbDelayScale.base =std::max(0.05f, std::min(4.f,v));
            else if (pm=="speed")        p->sampleSpeed.base  =std::max(-4.f,  std::min(4.f,v));
            else if (pm=="loop_start")   p->loopStart.base    =std::max(0.f,   std::min(1.f,v));
            else if (pm=="loop_end")     p->loopEnd.base      =std::max(0.f,   std::min(1.f,v));
            else if (pm=="env_attack")   p->envAttack.base    =std::max(0.001f,std::min(4.f,v));
            else if (pm=="env_decay")    p->envDecay.base     =std::max(0.001f,std::min(4.f,v));
            else if (pm=="env_sustain")  p->envSustain.base   =std::max(0.f,   std::min(1.f,v));
            else if (pm=="env_release")  p->envRelease.base   =std::max(0.001f,std::min(8.f,v));
            else if (pm=="env_rate")     p->envTrigRate.base  =std::max(0.05f, std::min(30.f,v));
            else if (pm=="filter")       p->filterCutoff.base =std::max(20.f,  std::min(20000.f,v));
            else if (pm=="filter_res")   p->filterRes.base    =std::max(0.5f,  std::min(8.f,v));
            else if (pm=="filter_mode")  p->filterMode.base   =std::round(std::max(0.f,std::min(2.f,v)));
            else if (pm=="chorus")       p->chorusWet.base    =std::max(0.f,   std::min(1.f,v));
            else if (pm=="chorus_rate")  p->chorusRate.base   =std::max(0.05f, std::min(8.f,v));
            else if (pm=="chorus_depth") p->chorusDepth.base  =std::max(0.f,   std::min(20.f,v));
            else if (pm=="reverb")       p->reverbWet.base    =std::max(0.f,   std::min(1.f,v));
            else if (pm=="reverb_size")  p->reverbSize.base   =std::max(0.f,   std::min(1.f,v));
            else if (pm=="reverb_damp")  p->reverbDamp.base   =std::max(0.f,   std::min(1.f,v));
            else if (pm=="crush_bits")   p->crushBits.base    =std::max(1.f,   std::min(16.f,v));
            else if (pm=="crush_rate")   p->crushRate.base    =std::max(0.f,   std::min(1.f,v));
            return;
        }
    }
};

} // namespace ChaosOsc
