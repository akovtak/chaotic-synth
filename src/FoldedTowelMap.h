#pragma once
#include <cmath>

namespace ChaosOsc {

// ============================================================
//  FoldedTowelMap
//  Discrete hyperchaotic map with two positive Lyapunov
//  exponents. Each instance holds independent state so every
//  control point gets its own chaotic trajectory.
//
//  Formula:
//    x' = 3.8x(1-x) - 0.05(y+0.35)(1-2z)
//    y' = 0.1[(y+0.35)(1-2z) - 1](1-1.9x)
//    z' = 3.78z(1-z) + 0.2y
//
//  Output: three normalized signals in [-1, 1], one per axis.
//  Each axis is normalized independently using observed range
//  tracking that converges after the burn-in period.
//
//  OF visualisation:
//    getRawX/Y/Z() return the un-normalized state.
//    Feed these directly into an ofVboMesh to draw the
//    attractor trajectory in 3D.
// ============================================================

class FoldedTowelMap {
public:
    FoldedTowelMap() = default;

    // Call before any audio use.
    // seed offsets the initial conditions slightly so multiple
    // instances don't move in lockstep. Pass 0,1,2,... for
    // each control point.
    void init(int seed = 0);

    // Advance the map by one step.
    void step();

    // Advance by n steps (burn-in and chaos speed control).
    void step(int n);

    // Normalized outputs in [-1, 1] — use these for deformation.
    float getX() const { return normX; }
    float getY() const { return normY; }
    float getZ() const { return normZ; }

    // Raw state — for OF 3D attractor visualisation.
    // x ~[0,1],  y ~[-0.05, 0.05],  z ~[0,1]
    double getRawX() const { return x; }
    double getRawY() const { return y; }
    double getRawZ() const { return z; }

    // How many steps have been taken (useful for debug overlay).
    int getStepCount() const { return stepCount; }

private:
    void   updateNormalized();
    double normalizeAxis(double v, double& mn, double& mx) const;
    static double tanhNorm(double v, double scale);

    // Map state
    double x = 0.5;
    double y = 0.1;
    double z = 0.5;

    // Normalized outputs
    float normX = 0.f;
    float normY = 0.f;
    float normZ = 0.f;

    // Per-axis running range trackers
    double xMin =  1e9,  xMax = -1e9;
    double yMin =  1e9,  yMax = -1e9;
    double zMin =  1e9,  zMax = -1e9;

    int stepCount = 0;

    // Steps before range estimates are trusted.
    // During warm-up, tanh fallback is used instead.
    static constexpr int WARMUP_STEPS = 500;

    // Burn-in length applied in init().
    static constexpr int BURNIN_STEPS = 1000;

    // Small perturbation per seed to decorrelate instances.
    static constexpr double SEED_DELTA = 0.0013;
};

} // namespace ChaosOsc
