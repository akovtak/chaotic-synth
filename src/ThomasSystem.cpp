#define _USE_MATH_DEFINES
#include "ThomasSystem.h"
#include <algorithm>
#include <cmath>

namespace ChaosOsc {

// ----------------------------------------------------------------
//  init
// ----------------------------------------------------------------
void ThomasSystem::init(int seed, double dtScale)
{
    // Stagger initial conditions per seed
    x = 0.1  + seed * SEED_DELTA;
    y = 0.0  + seed * SEED_DELTA * 0.6;
    z = -0.4 + seed * SEED_DELTA * 0.4;

    // Phase offset for Lissajous staggering
    phaseOffset = seed * (2.0 * M_PI / 8.0);
    lissajousT  = phaseOffset;

    dt = 0.05 * dtScale;

    b       = B_ORDERED;
    targetB = B_ORDERED;
    chaosAmount = 0.f;

    xMin =  1e9; xMax = -1e9;
    yMin =  1e9; yMax = -1e9;
    zMin =  1e9; zMax = -1e9;

    stepCount = 0;
    normX = normY = normZ = 0.f;

    // Burn in with ordered state so range trackers prime correctly
    step(BURNIN_STEPS);
}

// ----------------------------------------------------------------
//  setChaosAmount
//  Maps [0,1] to b parameter and stores blend weight.
//  0.0  -> B_ORDERED (fixed point / Lissajous)
//  0.5  -> midpoint (periodic Thomas orbits)
//  1.0  -> B_CHAOTIC (full chaos)
// ----------------------------------------------------------------
void ThomasSystem::setChaosAmount(float amount)
{
    amount = std::max(0.f, std::min(1.f, amount));
    chaosAmount = amount;

    // Map amount to b: higher amount = lower b = more chaos
    targetB = B_ORDERED + (B_CHAOTIC - B_ORDERED) * (double)amount;
}

void ThomasSystem::setSpeed(double speed)
{
    dt = 0.05 * speed * (1.0 - (b - B_CHAOTIC) / (B_ORDERED - B_CHAOTIC) * 0.5);
}

// ----------------------------------------------------------------
//  deriv  —  Thomas equations
// ----------------------------------------------------------------
ThomasSystem::State ThomasSystem::deriv(const State& s) const
{
    State d;
    d.x = -b * s.x + std::sin(s.y);
    d.y = -b * s.y + std::sin(s.z);
    d.z = -b * s.z + std::sin(s.x);
    return d;
}

// ----------------------------------------------------------------
//  lissajous
//  Deterministic reference orbit — three sinusoids at different
//  frequencies and phases producing a 3D Lissajous figure.
//  All outputs in [-1,1] naturally, no normalization needed.
// ----------------------------------------------------------------
ThomasSystem::State ThomasSystem::lissajous(double t) const
{
    State s;
    s.x = std::sin(t);
    s.y = std::sin(2.0 * t + M_PI / 3.0);
    s.z = std::sin(3.0 * t + M_PI / 5.0);
    return s;
}

// ----------------------------------------------------------------
//  integrate  —  RK4
// ----------------------------------------------------------------
ThomasSystem::State ThomasSystem::integrate(const State& s) const
{
    State k1 = deriv(s);

    State s2 = { s.x + 0.5*dt*k1.x,
                 s.y + 0.5*dt*k1.y,
                 s.z + 0.5*dt*k1.z };
    State k2 = deriv(s2);

    State s3 = { s.x + 0.5*dt*k2.x,
                 s.y + 0.5*dt*k2.y,
                 s.z + 0.5*dt*k2.z };
    State k3 = deriv(s3);

    State s4 = { s.x + dt*k3.x,
                 s.y + dt*k3.y,
                 s.z + dt*k3.z };
    State k4 = deriv(s4);

    State next;
    next.x = s.x + (dt/6.0)*(k1.x + 2.0*k2.x + 2.0*k3.x + k4.x);
    next.y = s.y + (dt/6.0)*(k1.y + 2.0*k2.y + 2.0*k3.y + k4.y);
    next.z = s.z + (dt/6.0)*(k1.z + 2.0*k2.z + 2.0*k3.z + k4.z);
    return next;
}

// ----------------------------------------------------------------
//  step (single)
// ----------------------------------------------------------------
void ThomasSystem::step()
{
    // Smoothly interpolate b toward target (1% per step)
    b += (targetB - b) * 0.01;

    // Advance Thomas system
    State s = {x, y, z};
    State n = integrate(s);

    if (std::isfinite(n.x) && std::isfinite(n.y) && std::isfinite(n.z))
    {
        x = n.x; y = n.y; z = n.z;
    }
    else
    {
        // Safe reset — should never happen with Thomas system
        x = 0.1; y = 0.0; z = -0.4;
    }

    // Advance Lissajous phase
    lissajousT += dt * 0.3;   // slower than Thomas so orbits differ

    ++stepCount;

    // Update range trackers from Thomas state
    xMin = std::min(xMin, x); xMax = std::max(xMax, x);
    yMin = std::min(yMin, y); yMax = std::max(yMax, y);
    zMin = std::min(zMin, z); zMax = std::max(zMax, z);

    updateNormalized();
}

void ThomasSystem::step(int n)
{
    for (int i = 0; i < n; ++i)
        step();
}

// ----------------------------------------------------------------
//  updateNormalized
//  Blends between Lissajous (deterministic) and Thomas (chaotic)
//  based on chaosAmount.
//  At chaosAmount=0: pure Lissajous, always in [-1,1]
//  At chaosAmount=1: pure Thomas normalized output
//  In between: smooth crossfade
// ----------------------------------------------------------------
void ThomasSystem::updateNormalized()
{
    // Thomas normalized outputs
    float tx, ty, tz;
    if (stepCount < WARMUP_STEPS) {
        // tanh fallback during warm-up
        tx = static_cast<float>(std::tanh(x));
        ty = static_cast<float>(std::tanh(y));
        tz = static_cast<float>(std::tanh(z));
    } else {
        tx = static_cast<float>(normalizeAxis(x, xMin, xMax));
        ty = static_cast<float>(normalizeAxis(y, yMin, yMax));
        tz = static_cast<float>(normalizeAxis(z, zMin, zMax));
    }

    // Lissajous outputs — always clean [-1,1]
    State lis = lissajous(lissajousT);
    float lx  = static_cast<float>(lis.x);
    float ly  = static_cast<float>(lis.y);
    float lz  = static_cast<float>(lis.z);

    // Crossfade: 0=Lissajous, 1=Thomas
    float ca = chaosAmount;
    normX = lx * (1.f - ca) + tx * ca;
    normY = ly * (1.f - ca) + ty * ca;
    normZ = lz * (1.f - ca) + tz * ca;
}

// ----------------------------------------------------------------
//  normalizeAxis
// ----------------------------------------------------------------
double ThomasSystem::normalizeAxis(double v, double& mn, double& mx) const
{
    const double range = mx - mn;
    if (range < 1e-10) return std::tanh(v);
    return 2.0 * (v - mn) / range - 1.0;
}

} // namespace ChaosOsc
