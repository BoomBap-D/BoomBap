#pragma once
#include <cmath>
#include <algorithm>

namespace boombap
{
constexpr double kPi = 3.14159265358979323846;

// RBJ-cookbook biquad (transposed direct form II). Coefficients are computed
// in double, audio runs in float. No JUCE dependency.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0.0f; }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2)
    {
        b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0);
        a1 = (float) (na1 / a0); a2 = (float) (na2 / a0);
    }

    void makeLowpass (double sr, double f, double q)
    {
        const double w = 2.0 * kPi * clampF (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }

    void makeHighpass (double sr, double f, double q)
    {
        const double w = 2.0 * kPi * clampF (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }

    // constant 0 dB peak-gain band-pass
    void makeBandpass (double sr, double f, double q)
    {
        const double w = 2.0 * kPi * clampF (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set (al, 0.0, -al, 1 + al, -2 * c, 1 - al);
    }

    void makePeaking (double sr, double f, double gainDb, double q)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * clampF (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }

    void makeLowShelf (double sr, double f, double gainDb, double q)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * clampF (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double t = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - t),
             (A + 1) + (A - 1) * c + t, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
    }

    void makeHighShelf (double sr, double f, double gainDb, double q)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * clampF (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double t = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + t), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - t),
             (A + 1) - (A - 1) * c + t, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t);
    }

private:
    static double clampF (double f, double sr) { return std::clamp (f, 10.0, 0.45 * sr); }
};
} // namespace boombap
