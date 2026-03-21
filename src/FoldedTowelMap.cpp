#include "FoldedTowelMap.h"
#include <algorithm>
#include <cmath>

// ============================================================
//  FoldedTowelMap.cpp
// ============================================================

namespace ChaosOsc {

// ----------------------------------------------------------------
//  init
//  Seed offsets initial conditions so N instances diverge quickly.
//  Then burn in BURNIN_STEPS iterations before connecting to audio
//  so the trajectory is already on the attractor.
// ----------------------------------------------------------------
void FoldedTowelMap::init(int seed)
{
    // Base initial conditions known to stay on the attractor
    x = 0.5  + seed * SEED_DELTA;
    y = 0.1  + seed * SEED_DELTA * 0.3;
    z = 0.5  + seed * SEED_DELTA * 0.7;

    // Reset range trackers and step counter
    xMin =  1e9;  xMax = -1e9;
    yMin =  1e9;  yMax = -1e9;
    zMin =  1e9;  zMax = -1e9;
    stepCount = 0;

    normX = normY = normZ = 0.f;

    // Burn in — moves trajectory onto attractor and primes
    // the range trackers so normalization is accurate from
    // the first audio sample.
    step(BURNIN_STEPS);
}

// ----------------------------------------------------------------
//  step  (single iteration)
//  Applies the folded-towel map formula exactly as specified,
//  then updates normalized outputs.
// ----------------------------------------------------------------
void FoldedTowelMap::step()
{
    // Pre-compute shared subexpressions
    const double yShift   = y + 0.35;
    const double twoZ     = 1.0 - 2.0 * z;
    const double coupling = yShift * twoZ;

    // Apply map
    const double xn = 3.8  * x * (1.0 - x) - 0.05 * coupling;
    const double yn = 0.1  * (coupling - 1.0) * (1.0 - 1.9 * x);
    const double zn = 3.78 * z * (1.0 - z)   + 0.2  * y;

    x = xn;
    y = yn;
    z = zn;

    ++stepCount;

    // Update range trackers
    xMin = std::min(xMin, x);  xMax = std::max(xMax, x);
    yMin = std::min(yMin, y);  yMax = std::max(yMax, y);
    zMin = std::min(zMin, z);  zMax = std::max(zMax, z);

    updateNormalized();
}

// ----------------------------------------------------------------
//  step(n)  — multiple iterations
// ----------------------------------------------------------------
void FoldedTowelMap::step(int n)
{
    for (int i = 0; i < n; ++i)
        step();
}

// ----------------------------------------------------------------
//  updateNormalized
//  During warm-up (before WARMUP_STEPS) uses tanh as a bounded
//  fallback since range estimates are not yet reliable.
//  After warm-up, maps observed [min,max] linearly to [-1,1].
// ----------------------------------------------------------------
void FoldedTowelMap::updateNormalized()
{
    if (stepCount < WARMUP_STEPS) {
        // tanh fallback — always bounded, slightly compressed
        // y axis has a much smaller natural range so use a
        // tighter scale factor for it
        normX = static_cast<float>(tanhNorm(x - 0.5, 2.0));
        normY = static_cast<float>(tanhNorm(y,       20.0));
        normZ = static_cast<float>(tanhNorm(z - 0.5, 2.0));
    } else {
        normX = static_cast<float>(normalizeAxis(x, xMin, xMax));
        normY = static_cast<float>(normalizeAxis(y, yMin, yMax));
        normZ = static_cast<float>(normalizeAxis(z, zMin, zMax));
    }
}

// ----------------------------------------------------------------
//  normalizeAxis
//  Linear map from [min,max] to [-1,1].
//  Falls back to tanh if range is degenerate (shouldn't happen
//  after burn-in, but guards against edge cases).
// ----------------------------------------------------------------
double FoldedTowelMap::normalizeAxis(double v, double& mn, double& mx) const
{
    const double range = mx - mn;
    if (range < 1e-10)
        return std::tanh(v);                    // degenerate fallback

    // Map [mn,mx] -> [0,1] -> [-1,1]
    return 2.0 * (v - mn) / range - 1.0;
}

// ----------------------------------------------------------------
//  tanhNorm
//  Scales v by 'scale' then applies tanh so output is in (-1,1).
//  Used during warm-up before range estimates are reliable.
// ----------------------------------------------------------------
double FoldedTowelMap::tanhNorm(double v, double scale)
{
    return std::tanh(v * scale);
}

} // namespace ChaosOsc
