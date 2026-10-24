#pragma once
#include <cmath>
#include <algorithm>

namespace monarca
{
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Andrew Simper (Cytomic) SVF, Topology-Preserving Transform.
// ---------------------------------------------------------------------------
struct SVF
{
    float a1 = 0.f, a2 = 0.f, a3 = 0.f, m0 = 1.f, m1 = 0.f, m2 = 0.f;
    float ic1 = 0.f, ic2 = 0.f;

    void reset() noexcept { ic1 = ic2 = 0.f; }

    void copyCoefs (const SVF& o) noexcept
    {
        a1 = o.a1; a2 = o.a2; a3 = o.a3; m0 = o.m0; m1 = o.m1; m2 = o.m2;
    }

    void set (double g, double k, double c0, double c1, double c2) noexcept
    {
        const double d = 1.0 / (1.0 + g * (g + k));
        a1 = (float) d;
        a2 = (float) (g * d);
        a3 = (float) (g * g * d);
        m0 = (float) c0; m1 = (float) c1; m2 = (float) c2;
    }

    void setLowShelf (double fs, double fc, double gDb, double q) noexcept
    {
        const double A = std::pow (10.0, gDb / 40.0);
        const double g = std::tan (kPi * fc / fs) / std::sqrt (A);
        const double k = 1.0 / q;
        set (g, k, 1.0, k * (A - 1.0), A * A - 1.0);
    }

    void setHighShelf (double fs, double fc, double gDb, double q) noexcept
    {
        const double A = std::pow (10.0, gDb / 40.0);
        const double g = std::tan (kPi * fc / fs) * std::sqrt (A);
        const double k = 1.0 / q;
        set (g, k, A * A, k * (1.0 - A) * A, 1.0 - A * A);
    }

    void setBell (double fs, double fc, double gDb, double q) noexcept
    {
        const double A = std::pow (10.0, gDb / 40.0);
        const double g = std::tan (kPi * fc / fs);
        const double k = 1.0 / (q * A);
        set (g, k, 1.0, k * (A * A - 1.0), 0.0);
    }

    void setHighpass (double fs, double fc, double q) noexcept
    {
        const double g = std::tan (kPi * fc / fs);
        const double k = 1.0 / q;
        set (g, k, 1.0, -k, -1.0);
    }

    inline float process (float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        return m0 * v0 + m1 * v1 + m2 * v2;
    }
};

// ---------------------------------------------------------------------------
// Biquad (transposed direct form II)
// ---------------------------------------------------------------------------
struct Biquad
{
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;

    void reset() noexcept { z1 = z2 = 0.f; }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// ITU-R BS.1770-4 K-weighting (stage 1: head-related shelf, stage 2: RLB high-pass)
struct KWeighting
{
    Biquad s1, s2;

    void prepare (double fs) noexcept
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1.b0 = (float) ((Vh + Vb * K / Q + K * K) / a0);
            s1.b1 = (float) (2.0 * (K * K - Vh) / a0);
            s1.b2 = (float) ((Vh - Vb * K / Q + K * K) / a0);
            s1.a1 = (float) (2.0 * (K * K - 1.0) / a0);
            s1.a2 = (float) ((1.0 - K / Q + K * K) / a0);
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            s2.b0 = 1.f; s2.b1 = -2.f; s2.b2 = 1.f;
            s2.a1 = (float) (2.0 * (K * K - 1.0) / a0);
            s2.a2 = (float) ((1.0 - K / Q + K * K) / a0);
        }
        reset();
    }

    void reset() noexcept { s1.reset(); s2.reset(); }
    inline float process (float x) noexcept { return s2.process (s1.process (x)); }
};

// ---------------------------------------------------------------------------
// First-order allpass
// ---------------------------------------------------------------------------
struct AllPass1
{
    float a = 0.f, z = 0.f;

    void setFc (double fs, double fc) noexcept
    {
        const double t = std::tan (kPi * fc / fs);
        a = (float) ((t - 1.0) / (t + 1.0));
    }
    void reset() noexcept { z = 0.f; }

    inline float process (float x) noexcept
    {
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
};

// ---------------------------------------------------------------------------
// Triode-style saturation (closed form, no iteration needed)
// f(x) = (1-l) tanh(g x) + l (g x)/sqrt(1+(g x)^2)
// ---------------------------------------------------------------------------
inline float triode (float x, float gamma, float lambda) noexcept
{
    const float u = gamma * x;
    return (1.f - lambda) * std::tanh (u) + lambda * u / std::sqrt (1.f + u * u);
}

inline float clampf (float x, float lo, float hi) noexcept
{
    return std::fmin (std::fmax (x, lo), hi);
}
} // namespace monarca
