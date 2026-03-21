#pragma once
#include <vector>
#include <string>
#include <array>
#include <functional>
#include <cmath>

namespace ChaosOsc {

// ============================================================
//  WaveLibrary
//  Stores a collection of waveshapes as N-point float arrays.
//  All shapes are resampled to N points at init time.
//
//  Two-slot morph system:
//    slotA = current shape
//    slotB = target shape
//    morphT [0,1] crossfades between them
//    commit() snaps B into A, ready for next morph
//
//  Usage:
//    1. Call init(N) to set resolution
//    2. Call buildLibrary() to populate all built-in shapes
//    3. Call loadFromFile(path) to add audio buffer shapes
//    4. Set slotA/B by index, adjust morphT
//    5. Call getMorphed(result) to get current N-point blend
// ============================================================

// One waveshape — N samples representing one period, normalized [-1,1]
struct WaveShape {
    std::string        name;
    std::string        category;
    std::vector<float> samples;   // length = N
};

class WaveLibrary {
public:
    WaveLibrary() = default;

    // Must call before anything else.
    // N = number of control points (4-64)
    void init(int N);

    // Build all built-in shapes at current N.
    // Called automatically by init().
    void buildLibrary();

    // Load a WAV/AIFF file, extract one period, resample to N.
    // Returns index of new shape, or -1 on failure.
    // name = display name shown in UI
    int loadFromFile(const std::string& path, const std::string& name = "");

    // Custom shape — set to override slot A with hand-drawn data
    // Call setCustomShape(samples) to enter free-draw mode.
    // Call clearCustomShape() to return to library-driven mode.
    void setCustomShape(const std::vector<float>& samples) {
        customSamples = samples;
        if ((int)customSamples.size() != n) customSamples.resize(n, 0.f);
        hasCustom = true;
    }
    void clearCustomShape() { hasCustom = false; }
    bool hasCustomShape()   const { return hasCustom; }

    // ---- Slot control ----
    void  setSlotA(int index);
    void  setSlotB(int index);
    int   getSlotA() const { return slotAIdx; }
    int   getSlotB() const { return slotBIdx; }

    // morphT: 0 = pure A, 1 = pure B
    void  setMorphT(float t);
    float getMorphT() const { return morphT; }

    // Snap B into A, optionally load new shape into B
    void commit(int newBIndex = -1);

    // Get current morphed result into out (must be size N)
    void getMorphed(std::vector<float>& out) const;

    // Get a single shape by index
    const WaveShape& getShape(int index) const;
    int              getShapeCount() const { return (int)shapes.size(); }
    int              getN()          const { return n; }

    // Category list for UI
    std::vector<std::string> getCategories() const;
    std::vector<int>         getIndicesForCategory(const std::string& cat) const;

private:
    // ---- Shape generators ----
    // All return N samples in [-1,1]
    std::vector<float> makeSine();
    std::vector<float> makeTriangle();
    std::vector<float> makeSawUp();
    std::vector<float> makeSawDown();
    std::vector<float> makeSquare(float duty = 0.5f);
    std::vector<float> makePulse(float duty);
    std::vector<float> makeSoftSine();
    std::vector<float> makeHalfRect();
    std::vector<float> makeFullRect();
    std::vector<float> makeSineSquared();
    std::vector<float> makeCubic();
    std::vector<float> makeSharkTooth();
    std::vector<float> makeStaircase(int steps);
    std::vector<float> makeTanhClipped(float drive);
    std::vector<float> makeFoldedSine(int folds);
    std::vector<float> makeSilence();

    // Chebyshev T_n — pure nth harmonic
    std::vector<float> makeChebyshev(int order);

    // Additive combinations
    std::vector<float> makeAdditive(const std::vector<std::pair<int,float>>& harmonics);

    // Resample arbitrary float buffer to N points (linear interp)
    std::vector<float> resample(const std::vector<float>& src, int targetN) const;

    // Normalize buffer to [-1,1] peak
    void normalize(std::vector<float>& buf) const;

    // Add a shape to library
    void add(const std::string& name, const std::string& category,
             std::vector<float> samples);

    int n = 16;   // current resolution

    std::vector<WaveShape> shapes;

    int   slotAIdx = 0;
    int   slotBIdx = 1;
    float morphT   = 0.f;

    // Free-draw custom shape
    bool              hasCustom = false;
    std::vector<float> customSamples;
};

} // namespace ChaosOsc
