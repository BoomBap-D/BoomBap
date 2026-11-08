#pragma once
#include <algorithm>
#include <cmath>

namespace boombap
{
// MPC-style timing works in ticks: 96 pulses per quarter note.
constexpr int kPPQN = 96;
constexpr int kTicksPer16th = kPPQN / 4; // 24
constexpr int kTicksPer8th  = kPPQN / 2; // 48

// MPC swing: notes on the grid are paired, and the second note of each pair
// is delayed so the pair splits swing% : (100 - swing)%.
//   50 = straight, 66 = triplet feel, 75 = maximum (very lazy)
// gridTicks = 24 swings 16ths, 48 swings 8ths.
// Only notes exactly on the grid are moved, like the hardware.
inline int swingOffsetTicks (int tick, int gridTicks, float swingPercent)
{
    swingPercent = std::clamp (swingPercent, 50.0f, 75.0f);
    const int pair = gridTicks * 2;
    if (tick % pair != gridTicks)
        return 0;
    return (int) std::lround (swingPercent / 100.0f * (float) pair) - gridTicks;
}
} // namespace boombap
