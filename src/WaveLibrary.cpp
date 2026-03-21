#define _USE_MATH_DEFINES
#include "WaveLibrary.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <cstring>

namespace ChaosOsc {

// ================================================================
//  init / buildLibrary
// ================================================================

void WaveLibrary::init(int N)
{
    n = std::max(4, std::min(64, N));
    shapes.clear();
    slotAIdx = 0;
    slotBIdx = 1;
    morphT   = 0.f;
    buildLibrary();
}

void WaveLibrary::buildLibrary()
{
    // ---- Classic ----
    add("Sine",          "Classic", makeSine());
    add("Triangle",      "Classic", makeTriangle());
    add("Saw Up",        "Classic", makeSawUp());
    add("Saw Down",      "Classic", makeSawDown());
    add("Square",        "Classic", makeSquare(0.5f));
    add("Pulse 25%",     "Classic", makePulse(0.25f));
    add("Pulse 10%",     "Classic", makePulse(0.10f));
    add("Silence",       "Classic", makeSilence());

    // ---- Soft ----
    add("Soft Sine",     "Soft",    makeSoftSine());
    add("Half Rect",     "Soft",    makeHalfRect());
    add("Full Rect",     "Soft",    makeFullRect());
    add("Sine Squared",  "Soft",    makeSineSquared());
    add("Cubic",         "Soft",    makeCubic());
    add("Tanh x1",       "Soft",    makeTanhClipped(1.0f));
    add("Tanh x2",       "Soft",    makeTanhClipped(2.0f));
    add("Tanh x4",       "Soft",    makeTanhClipped(4.0f));
    add("Tanh x8",       "Soft",    makeTanhClipped(8.0f));

    // ---- Chebyshev (pure harmonics) ----
    add("Cheby T2",      "Chebyshev", makeChebyshev(2));
    add("Cheby T3",      "Chebyshev", makeChebyshev(3));
    add("Cheby T4",      "Chebyshev", makeChebyshev(4));
    add("Cheby T5",      "Chebyshev", makeChebyshev(5));
    add("Cheby T6",      "Chebyshev", makeChebyshev(6));
    add("Cheby T7",      "Chebyshev", makeChebyshev(7));
    add("Cheby T8",      "Chebyshev", makeChebyshev(8));

    // ---- Additive ----
    add("Odd H 1+3",     "Additive", makeAdditive({{1,1.f},{3,0.33f}}));
    add("Odd H 1+3+5",   "Additive", makeAdditive({{1,1.f},{3,0.33f},{5,0.2f}}));
    add("Odd H 1-5",     "Additive", makeAdditive({{1,1.f},{3,0.33f},{5,0.2f},{7,0.14f},{9,0.11f}}));
    add("Even H 1+2",    "Additive", makeAdditive({{1,1.f},{2,0.5f}}));
    add("Even H 1+2+4",  "Additive", makeAdditive({{1,1.f},{2,0.5f},{4,0.25f}}));
    add("Bright Saw",    "Additive", makeAdditive({{1,1.f},{2,0.5f},{3,0.33f},{4,0.25f},{5,0.2f},{6,0.16f},{7,0.14f},{8,0.12f}}));
    add("Hollow",        "Additive", makeAdditive({{1,1.f},{3,0.5f},{5,0.33f},{7,0.25f}}));
    add("Formant A",     "Additive", makeAdditive({{1,1.f},{2,0.7f},{3,0.3f},{4,0.1f}}));
    add("Formant E",     "Additive", makeAdditive({{1,0.5f},{2,1.f},{3,0.7f},{4,0.2f}}));
    add("Formant O",     "Additive", makeAdditive({{1,1.f},{2,0.3f},{3,0.1f}}));

    // ---- Exotic ----
    add("Shark Tooth",   "Exotic",  makeSharkTooth());
    add("Staircase 4",   "Exotic",  makeStaircase(4));
    add("Staircase 8",   "Exotic",  makeStaircase(8));
    add("Folded x1",     "Exotic",  makeFoldedSine(1));
    add("Folded x2",     "Exotic",  makeFoldedSine(2));
    add("Folded x3",     "Exotic",  makeFoldedSine(3));
    add("Folded x4",     "Exotic",  makeFoldedSine(4));
    add("Abs Sine",      "Exotic",  makeFullRect());

    // ---- Noise-adjacent ----
    // Random but seeded — deterministic so always the same shape
    {
        std::vector<float> s(n);
        float v = 0.3f;
        for (int i = 0; i < n; ++i) {
            v = v * 1.7f + 0.13f;
            v = v - std::floor(v);   // cheap deterministic pseudo-random
            s[i] = v * 2.f - 1.f;
        }
        normalize(s);
        add("Random Walk", "Noise", s);
    }
    {
        std::vector<float> s(n);
        for (int i = 0; i < n; ++i) {
            float t = i / float(n);
            s[i] = std::sin(2.f * (float)M_PI * t)
                 + 0.3f * std::sin(7.f * (float)M_PI * t + 0.5f)
                 + 0.15f * std::sin(13.f * (float)M_PI * t + 1.1f);
        }
        normalize(s);
        add("Noisy Sine",  "Noise", s);
    }
}

// ================================================================
//  Slot control
// ================================================================

void WaveLibrary::setSlotA(int index)
{
    if (index >= 0 && index < (int)shapes.size()) {
        slotAIdx = index;
        hasCustom = false;  // returning to library mode
    }
}

void WaveLibrary::setSlotB(int index)
{
    if (index >= 0 && index < (int)shapes.size()) {
        slotBIdx = index;
        hasCustom = false;
    }
}

void WaveLibrary::setMorphT(float t)
{
    morphT = std::max(0.f, std::min(1.f, t));
}

void WaveLibrary::commit(int newBIndex)
{
    slotAIdx = slotBIdx;
    if (newBIndex >= 0 && newBIndex < (int)shapes.size())
        slotBIdx = newBIndex;
    morphT = 0.f;
}

void WaveLibrary::getMorphed(std::vector<float>& out) const
{
    out.resize(n);
    if (hasCustom) {
        // Custom free-draw shape — bypass library morph entirely
        for (int i = 0; i < n; ++i)
            out[i] = (i < (int)customSamples.size()) ? customSamples[i] : 0.f;
        return;
    }
    const auto& a = shapes[slotAIdx].samples;
    const auto& b = shapes[slotBIdx].samples;
    float t = morphT;
    for (int i = 0; i < n; ++i)
        out[i] = a[i] * (1.f - t) + b[i] * t;
}

const WaveShape& WaveLibrary::getShape(int index) const
{
    return shapes[std::max(0, std::min((int)shapes.size()-1, index))];
}

std::vector<std::string> WaveLibrary::getCategories() const
{
    std::vector<std::string> cats;
    for (auto& s : shapes) {
        bool found = false;
        for (auto& c : cats) if (c == s.category) { found = true; break; }
        if (!found) cats.push_back(s.category);
    }
    return cats;
}

std::vector<int> WaveLibrary::getIndicesForCategory(const std::string& cat) const
{
    std::vector<int> result;
    for (int i = 0; i < (int)shapes.size(); ++i)
        if (shapes[i].category == cat)
            result.push_back(i);
    return result;
}

// ================================================================
//  File loading  (minimal WAV parser — no external dependencies)
// ================================================================

int WaveLibrary::loadFromFile(const std::string& path, const std::string& name)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return -1;

    // Read RIFF header
    char riff[4]; f.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) return -1;

    uint32_t chunkSize; f.read((char*)&chunkSize, 4);
    char wave[4]; f.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) return -1;

    // Find fmt and data chunks
    uint16_t audioFormat = 0, numChannels = 0;
    uint32_t sampleRate = 0, byteRate = 0;
    uint16_t blockAlign = 0, bitsPerSample = 0;
    uint32_t dataSize = 0;
    std::vector<uint8_t> rawData;

    while (!f.eof()) {
        char id[4]; f.read(id, 4);
        uint32_t size; f.read((char*)&size, 4);
        if (f.fail()) break;

        if (std::strncmp(id, "fmt ", 4) == 0) {
            f.read((char*)&audioFormat,  2);
            f.read((char*)&numChannels,  2);
            f.read((char*)&sampleRate,   4);
            f.read((char*)&byteRate,     4);
            f.read((char*)&blockAlign,   2);
            f.read((char*)&bitsPerSample,2);
            if (size > 16) f.seekg(size - 16, std::ios::cur);
        } else if (std::strncmp(id, "data", 4) == 0) {
            dataSize = size;
            rawData.resize(size);
            f.read((char*)rawData.data(), size);
            break;
        } else {
            f.seekg(size, std::ios::cur);
        }
    }

    if (rawData.empty() || bitsPerSample == 0) return -1;

    // Convert to float mono (take first channel only)
    int bytesPerSample = bitsPerSample / 8;
    int totalSamples   = dataSize / (bytesPerSample * numChannels);

    std::vector<float> mono(totalSamples);
    for (int i = 0; i < totalSamples; ++i) {
        int byteOffset = i * bytesPerSample * numChannels;  // first channel
        float val = 0.f;
        if (bitsPerSample == 16) {
            int16_t s;
            std::memcpy(&s, rawData.data() + byteOffset, 2);
            val = s / 32768.f;
        } else if (bitsPerSample == 24) {
            int32_t s = 0;
            std::memcpy((char*)&s + 1, rawData.data() + byteOffset, 3);
            s >>= 8;
            val = s / 8388608.f;
        } else if (bitsPerSample == 32) {
            float s;
            std::memcpy(&s, rawData.data() + byteOffset, 4);
            val = s;
        }
        mono[i] = val;
    }

    // Resample to N points
    auto resampled = resample(mono, n);
    normalize(resampled);

    // Use filename as name if none given
    std::string displayName = name;
    if (displayName.empty()) {
        auto pos = path.find_last_of("/\\");
        displayName = (pos != std::string::npos) ? path.substr(pos+1) : path;
        auto dot = displayName.find_last_of('.');
        if (dot != std::string::npos) displayName = displayName.substr(0, dot);
    }

    add(displayName, "File", resampled);
    return (int)shapes.size() - 1;
}

// ================================================================
//  Shape generators
// ================================================================

void WaveLibrary::add(const std::string& name, const std::string& category,
                      std::vector<float> samples)
{
    WaveShape ws;
    ws.name     = name;
    ws.category = category;
    ws.samples  = std::move(samples);
    shapes.push_back(std::move(ws));
}

std::vector<float> WaveLibrary::makeSine()
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i)
        s[i] = std::sin(2.f * (float)M_PI * i / n);
    return s;
}

std::vector<float> WaveLibrary::makeTriangle()
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) {
        float t = i / float(n);
        s[i] = (t < 0.25f) ? 4.f*t :
               (t < 0.75f) ? 2.f - 4.f*t :
                              4.f*t - 4.f;
    }
    return s;
}

std::vector<float> WaveLibrary::makeSawUp()
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i)
        s[i] = 2.f * (i / float(n)) - 1.f;
    return s;
}

std::vector<float> WaveLibrary::makeSawDown()
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i)
        s[i] = 1.f - 2.f * (i / float(n));
    return s;
}

std::vector<float> WaveLibrary::makeSquare(float duty)
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i)
        s[i] = (i / float(n) < duty) ? 1.f : -1.f;
    return s;
}

std::vector<float> WaveLibrary::makePulse(float duty)
{
    return makeSquare(duty);
}

std::vector<float> WaveLibrary::makeSoftSine()
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) {
        float v = std::sin(2.f * (float)M_PI * i / n);
        s[i] = std::tanh(v * 2.f) / std::tanh(2.f);
    }
    return s;
}

std::vector<float> WaveLibrary::makeHalfRect()
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) {
        float v = std::sin(2.f * (float)M_PI * i / n);
        s[i] = std::max(0.f, v);
    }
    normalize(s);
    return s;
}

std::vector<float> WaveLibrary::makeFullRect()
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i)
        s[i] = std::abs(std::sin(2.f * (float)M_PI * i / n));
    normalize(s);
    return s;
}

std::vector<float> WaveLibrary::makeSineSquared()
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) {
        float v = std::sin(2.f * (float)M_PI * i / n);
        s[i] = v * v * (v > 0 ? 1.f : -1.f);
    }
    return s;
}

std::vector<float> WaveLibrary::makeCubic()
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) {
        float t = 2.f * i / float(n) - 1.f;
        s[i] = t * t * t;
    }
    return s;
}

std::vector<float> WaveLibrary::makeSharkTooth()
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) {
        float t = i / float(n);
        if (t < 0.7f)       s[i] = t / 0.7f * 2.f - 1.f;
        else                 s[i] = 1.f - (t - 0.7f) / 0.3f * 2.f;
    }
    return s;
}

std::vector<float> WaveLibrary::makeStaircase(int steps)
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) {
        float t = i / float(n);
        int   st = (int)(t * steps);
        s[i] = 2.f * st / float(steps - 1) - 1.f;
    }
    return s;
}

std::vector<float> WaveLibrary::makeTanhClipped(float drive)
{
    std::vector<float> s(n);
    float norm = std::tanh(drive);
    for (int i = 0; i < n; ++i) {
        float v = std::sin(2.f * (float)M_PI * i / n);
        s[i] = std::tanh(v * drive) / norm;
    }
    return s;
}

std::vector<float> WaveLibrary::makeFoldedSine(int folds)
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) {
        float v = std::sin(2.f * (float)M_PI * i / n);
        for (int f = 0; f < folds; ++f) {
            if (v >  1.f) v =  2.f - v;
            if (v < -1.f) v = -2.f - v;
            v = std::sin((float)M_PI * v * 0.5f);
        }
        s[i] = v;
    }
    return s;
}

std::vector<float> WaveLibrary::makeSilence()
{
    return std::vector<float>(n, 0.f);
}

std::vector<float> WaveLibrary::makeChebyshev(int order)
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) {
        float theta = 2.f * (float)M_PI * i / n;
        // T_n(cos θ) = cos(nθ) — pure nth harmonic
        s[i] = std::cos(order * theta);
    }
    return s;
}

std::vector<float> WaveLibrary::makeAdditive(
    const std::vector<std::pair<int,float>>& harmonics)
{
    std::vector<float> s(n, 0.f);
    for (auto& [h, amp] : harmonics)
        for (int i = 0; i < n; ++i)
            s[i] += amp * std::sin(2.f * (float)M_PI * h * i / n);
    normalize(s);
    return s;
}

// ================================================================
//  Utilities
// ================================================================

std::vector<float> WaveLibrary::resample(const std::vector<float>& src, int targetN) const
{
    std::vector<float> out(targetN);
    int srcN = (int)src.size();
    if (srcN == 0) return out;
    for (int i = 0; i < targetN; ++i) {
        float  pos = i * (srcN / float(targetN));
        int    lo  = (int)pos % srcN;
        int    hi  = (lo + 1) % srcN;
        float  t   = pos - std::floor(pos);
        out[i] = src[lo] * (1.f - t) + src[hi] * t;
    }
    return out;
}

void WaveLibrary::normalize(std::vector<float>& buf) const
{
    float peak = 0.f;
    for (float v : buf) peak = std::max(peak, std::abs(v));
    if (peak < 1e-6f) return;
    for (float& v : buf) v /= peak;
}

} // namespace ChaosOsc
