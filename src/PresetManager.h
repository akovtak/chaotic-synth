#pragma once
// ============================================================
//  PresetManager
//  Saves/loads all synthesis parameters, wave library state,
//  and slot flags to a simple key=value text file.
//
//  Format (one line per value):
//    slot.0.gain=0.65
//    slot.0.waveA=3
//    slot.0.morphT=0.25
//    ...
//    xfade=0.5
//    darkMode=1
//
//  Usage:
//    PresetManager pm;
//    pm.save("mypreset.cpreset", slots, xfade, darkMode);
//    pm.load("mypreset.cpreset", slots, xfade, darkMode);
// ============================================================

#include "ParamSystem.h"
#include "WaveLibrary.h"
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <functional>

namespace ChaosOsc {

// Forward-declared here, defined by ofApp's Slot struct
// We take explicit references to avoid circular includes.
struct PresetSlotState {
    ParamSystem* params     = nullptr;
    WaveLibrary* waveLib    = nullptr;
    bool* sampleMode        = nullptr;
    bool* normalizeOn       = nullptr;
    bool* envelopeOn        = nullptr;
    bool* envelopeLoop      = nullptr;
    bool* loopOn            = nullptr;
};

class PresetManager {
public:
    // Returns "" on success, error string on failure
    std::string save(const std::string& path,
                     const std::vector<PresetSlotState>& slots,
                     float xfade, bool darkMode)
    {
        std::ofstream f(path);
        if (!f.is_open()) return "Cannot open file for writing: " + path;

        f << "# ChaosOsc preset\n";
        f << "version=1\n";
        f << "xfade=" << xfade << "\n";
        f << "darkMode=" << (darkMode ? 1 : 0) << "\n";

        for (int s = 0; s < (int)slots.size(); ++s) {
            const auto& sl = slots[s];
            std::string pfx = "slot." + std::to_string(s) + ".";

            // All params
            if (sl.params) {
                for (auto* p : sl.params->all()) {
                    std::string key = sanitizeKey(p->name);
                    f << pfx << key << "=" << p->base << "\n";
                    f << pfx << key << ".chaos=" << p->chaosAmt << "\n";
                }
            }

            // Wave library
            if (sl.waveLib) {
                f << pfx << "waveA=" << sl.waveLib->getSlotA() << "\n";
                f << pfx << "waveB=" << sl.waveLib->getSlotB() << "\n";
                f << pfx << "morphT=" << sl.waveLib->getMorphT() << "\n";
            }

            // Flags
            if (sl.sampleMode)  f << pfx << "sampleMode="  << (*sl.sampleMode  ? 1 : 0) << "\n";
            if (sl.normalizeOn) f << pfx << "normalizeOn=" << (*sl.normalizeOn ? 1 : 0) << "\n";
            if (sl.envelopeOn)  f << pfx << "envelopeOn="  << (*sl.envelopeOn  ? 1 : 0) << "\n";
            if (sl.envelopeLoop) f << pfx << "envelopeLoop=" << (*sl.envelopeLoop ? 1 : 0) << "\n";
            if (sl.loopOn)      f << pfx << "loopOn="      << (*sl.loopOn      ? 1 : 0) << "\n";
        }

        return "";
    }

    std::string load(const std::string& path,
                     std::vector<PresetSlotState>& slots,
                     float& xfade, bool& darkMode)
    {
        std::ifstream f(path);
        if (!f.is_open()) return "Cannot open file: " + path;

        // Parse into key=value map
        std::map<std::string, std::string> kv;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            kv[line.substr(0, eq)] = line.substr(eq + 1);
        }

        auto getF = [&](const std::string& k, float def) -> float {
            auto it = kv.find(k);
            if (it == kv.end()) return def;
            try { return std::stof(it->second); } catch (...) { return def; }
        };
        auto getI = [&](const std::string& k, int def) -> int {
            auto it = kv.find(k);
            if (it == kv.end()) return def;
            try { return std::stoi(it->second); } catch (...) { return def; }
        };

        xfade    = getF("xfade",    0.5f);
        darkMode = getI("darkMode", 1) != 0;

        for (int s = 0; s < (int)slots.size(); ++s) {
            auto& sl = slots[s];
            std::string pfx = "slot." + std::to_string(s) + ".";

            if (sl.params) {
                for (auto* p : sl.params->all()) {
                    std::string key = sanitizeKey(p->name);
                    p->base     = getF(pfx + key,          p->defaultVal);
                    p->chaosAmt = getF(pfx + key + ".chaos", 0.f);
                    p->smoothed = p->base;
                }
            }

            if (sl.waveLib) {
                sl.waveLib->setSlotA(getI(pfx + "waveA", 0));
                sl.waveLib->setSlotB(getI(pfx + "waveB", 1));
                sl.waveLib->setMorphT(getF(pfx + "morphT", 0.f));
            }

            if (sl.sampleMode)  *sl.sampleMode  = getI(pfx + "sampleMode",  0) != 0;
            if (sl.normalizeOn) *sl.normalizeOn  = getI(pfx + "normalizeOn", 0) != 0;
            if (sl.envelopeOn)  *sl.envelopeOn   = getI(pfx + "envelopeOn",  0) != 0;
            if (sl.envelopeLoop) *sl.envelopeLoop  = getI(pfx + "envelopeLoop", 0) != 0;
            if (sl.loopOn)      *sl.loopOn        = getI(pfx + "loopOn",      1) != 0;
        }

        return "";
    }

private:
    // Replace spaces and special chars so key is file-safe
    static std::string sanitizeKey(const std::string& name) {
        std::string out;
        for (char c : name) {
            if (std::isalnum(c)) out += (char)std::tolower(c);
            else if (c == ' ' || c == '_') out += '_';
            // skip parens, slashes, etc
        }
        return out;
    }
};

} // namespace ChaosOsc
