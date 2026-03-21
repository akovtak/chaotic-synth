#pragma once
#include <cmath>

namespace ChaosOsc {

// ============================================================
//  ThomasSystem
//  Cyclically symmetric chaotic system:
//
//    dx/dt = -bx + sin(y)
//    dy/dt = -by + sin(z)
//    dz/dt = -bz + sin(x)
//
//  Single parameter b controls the full transition:
//    b > 0.32  : stable fixed point (deterministic, no motion)
//    b ~ 0.23  : periodic orbits (regular, predictable)
//    b ~ 0.20  : period doubling begins
//    b ~ 0.18  : onset of chaos
//    b < 0.18  : fully chaotic labyrinthine trajectories
//
//  All three axes have equal dynamic range (cyclic symmetry)
//  so any axis can drive any deformation direction freely.
//
//  Interpolation architecture:
//    chaosAmount 0.0 -> Lissajous orbit (pure deterministic)
//    chaosAmount 0.5 -> Thomas near bifurcation (periodic)
//    chaosAmount 1.0 -> Thomas fully chaotic
// ============================================================

class ThomasSystem {
public:
    ThomasSystem() = default;

    // seed    : 0,1,2,... per control point to decorrelate
    // dtScale : speed multiplier, slightly different per point
    void init(int seed = 0, double dtScale = 1.0);

    // Advance by one RK4 step
    void step();
    void step(int n);

    // Set chaos amount [0,1] — maps to b parameter + Lissajous blend
    // Call this from the UI thread freely (atomic)
    void setChaosAmount(float amount);

    void setSpeed(double speed);  // 0.1 = slow, 1.0 = normal, 4.0 = fast

    // Normalized outputs in [-1, 1] — all axes equal range
    float getX() const { return normX; }
    float getY() const { return normY; }
    float getZ() const { return normZ; }

    // Raw state for visualisation
    double getRawX() const { return x; }
    double getRawY() const { return y; }
    double getRawZ() const { return z; }

    // Current b value (useful for debug overlay)
    double getB() const { return b; }

    int getStepCount() const { return stepCount; }

private:
    struct State { double x, y, z; };

    State   deriv(const State& s) const;
    State   integrate(const State& s) const;
    State   lissajous(double t) const;
    void    updateNormalized();
    double  normalizeAxis(double v, double& mn, double& mx) const;

    // Thomas state
    double x = 0.1, y = 0.0, z = -0.4;

    // Lissajous state (deterministic reference orbit)
    double lissajousT = 0.0;

    // b parameter — controls chaos vs order in Thomas system
    // interpolated smoothly toward targetB each step
    double b       = 0.32;   // start fully ordered
    double targetB = 0.32;

    // Integration step
    double dt = 0.05;

    // Chaos amount [0,1] — drives b mapping + blend weight
    float chaosAmount = 0.f;

    // Normalized outputs
    float normX = 0.f, normY = 0.f, normZ = 0.f;

    // Range trackers
    double xMin =  1e9, xMax = -1e9;
    double yMin =  1e9, yMax = -1e9;
    double zMin =  1e9, zMax = -1e9;

    // Phase offset per seed so Lissajous orbits are staggered
    double phaseOffset = 0.0;

    int stepCount = 0;

    // b range for chaos control mapping
    static constexpr double B_ORDERED = 0.32;   // fixed point
    static constexpr double B_CHAOTIC = 0.168;  // full chaos

    static constexpr int    WARMUP_STEPS = 500;
    static constexpr int    BURNIN_STEPS = 8000;
    static constexpr double SEED_DELTA = 0.13;
};

} // namespace ChaosOsc
