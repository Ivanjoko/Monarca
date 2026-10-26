#pragma once
// ============================================================================
//  Monarca v2 - primitivas DSP (sin dependencias de JUCE, probables por separado)
//  Todo el estado interno es de doble precision: a 56 Hz con fs = 192 kHz un
//  filtro en float pierde bits utiles y ensucia el grave.
// ============================================================================
#include <cmath>
#include <complex>
#include <algorithm>

namespace monarca
{
constexpr double kPi = 3.14159265358979323846;

// false = formula literal de la especificacion: tauA = 32 / (1 + 2.5 * Crest)
//         (con transitorios fuertes el ataque se vuelve MAS rapido).
// true  = intencion descrita en el texto: con transitorios el ataque se
//         ralentiza para dejar pasar el golpe inicial (mas "punch").
inline constexpr bool kSlowAttackOnTransients = false;

inline float clampf (float x, float lo, float hi) noexcept { return std::fmin (std::fmax (x, lo), hi); }
inline double clampd (double x, double lo, double hi) noexcept { return std::fmin (std::fmax (x, lo), hi); }

// ---------------------------------------------------------------------------
// Andrew Simper (Cytomic) SVF - Topology Preserving Transform
// ---------------------------------------------------------------------------
struct SVF
{
    double a1 = 0, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0, ic1 = 0, ic2 = 0;

    void reset() noexcept { ic1 = ic2 = 0; }
    void copyCoefs (const SVF& o) noexcept { a1 = o.a1; a2 = o.a2; a3 = o.a3; m0 = o.m0; m1 = o.m1; m2 = o.m2; }

    void set (double g, double k, double c0, double c1, double c2) noexcept
    {
        const double d = 1.0 / (1.0 + g * (g + k));
        a1 = d; a2 = g * d; a3 = g * g * d; m0 = c0; m1 = c1; m2 = c2;
    }
    void setLowShelf (double fs, double fc, double gDb, double q) noexcept
    {
        const double A = std::pow (10.0, gDb / 40.0), g = std::tan (kPi * fc / fs) / std::sqrt (A), k = 1.0 / q;
        set (g, k, 1.0, k * (A - 1.0), A * A - 1.0);
    }
    void setHighShelf (double fs, double fc, double gDb, double q) noexcept
    {
        const double A = std::pow (10.0, gDb / 40.0), g = std::tan (kPi * fc / fs) * std::sqrt (A), k = 1.0 / q;
        set (g, k, A * A, k * (1.0 - A) * A, 1.0 - A * A);
    }
    void setBell (double fs, double fc, double gDb, double q) noexcept
    {
        const double A = std::pow (10.0, gDb / 40.0), g = std::tan (kPi * fc / fs), k = 1.0 / (q * A);
        set (g, k, 1.0, k * (A * A - 1.0), 0.0);
    }
    void setHighpass (double fs, double fc, double q) noexcept
    {
        const double g = std::tan (kPi * fc / fs), k = 1.0 / q;
        set (g, k, 1.0, -k, -1.0);
    }
    void setLowpass (double fs, double fc, double q) noexcept
    {
        const double g = std::tan (kPi * fc / fs), k = 1.0 / q;
        set (g, k, 0.0, 0.0, 1.0);
    }

    inline double process (double v0) noexcept
    {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return m0 * v0 + m1 * v1 + m2 * v2;
    }
};

// ---------------------------------------------------------------------------
// Biquad TDF-II en doble precision
// ---------------------------------------------------------------------------
struct BiquadD
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() noexcept { z1 = z2 = 0; }
    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// ITU-R BS.1770-4: etapa 1 (shelf de cabeza) + etapa 2 (RLB paso alto)
struct KWeighting
{
    BiquadD s1, s2;
    void prepare (double fs) noexcept
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / fs), Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416), a0 = 1.0 + K / Q + K * K;
            s1.b0 = (Vh + Vb * K / Q + K * K) / a0;
            s1.b1 = 2.0 * (K * K - Vh) / a0;
            s1.b2 = (Vh - Vb * K / Q + K * K) / a0;
            s1.a1 = 2.0 * (K * K - 1.0) / a0;
            s1.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPi * f0 / fs), a0 = 1.0 + K / Q + K * K;
            s2.b0 = 1.0; s2.b1 = -2.0; s2.b2 = 1.0;
            s2.a1 = 2.0 * (K * K - 1.0) / a0;
            s2.a2 = (1.0 - K / Q + K * K) / a0;
        }
        reset();
    }
    void reset() noexcept { s1.reset(); s2.reset(); }
    inline double process (double x) noexcept { return s2.process (s1.process (x)); }
};

// ---------------------------------------------------------------------------
// Allpass de primer orden
// ---------------------------------------------------------------------------
struct AllPass1
{
    double a = 0, z = 0;
    void setFc (double fs, double fc) noexcept { const double t = std::tan (kPi * fc / fs); a = (t - 1.0) / (t + 1.0); }
    void reset() noexcept { z = 0; }
    inline double process (double x) noexcept { const double y = a * x + z; z = x - a * y; return y; }
};

// ---------------------------------------------------------------------------
// Pasa-altos eliptico de 5o orden (Cauer).
// Prototipo analogico: rizado 0.1 dB, rechazo 70 dB (calculado con scipy.ellip),
// transformacion LP->HP (s -> W/s) y bilineal con pre-distorsion.
// Con fc = 84.3 Hz: -72 dB a 30 Hz, rizado < 0.1 dB por encima de fc.
// ---------------------------------------------------------------------------
struct EllipticHP5
{
    BiquadD sec[3];   // sec[2] es de primer orden

    void prepare (double fs, double fc) noexcept
    {
        using C = std::complex<double>;
        const C zp[2] = { C (0.0, 2.630966446653879), C (0.0, 4.157217797171757) };
        const C pp[2] = { C (-0.432402268244569, 0.699974217904542), C (-0.149496561187373, 1.076348335376947) };
        const double pr = -0.569617675140237;
        const double W = 2.0 * fs * std::tan (kPi * fc / fs);
        const double F2 = 2.0 * fs;

        for (int i = 0; i < 2; ++i)
        {
            const C zh = W / zp[i], ph = W / pp[i];
            const C zd = (F2 + zh) / (F2 - zh), pd = (F2 + ph) / (F2 - ph);
            BiquadD& b = sec[i];
            b.b0 = 1.0; b.b1 = -2.0 * zd.real(); b.b2 = std::norm (zd);
            b.a1 = -2.0 * pd.real(); b.a2 = std::norm (pd);
            const double g = (1.0 - b.a1 + b.a2) / (b.b0 - b.b1 + b.b2);   // ganancia 1 en Nyquist
            b.b0 *= g; b.b1 *= g; b.b2 *= g;
        }
        {
            const double ph = W / pr, pd = (F2 + ph) / (F2 - ph);
            BiquadD& b = sec[2];
            b.b0 = (1.0 + pd) * 0.5; b.b1 = -b.b0; b.b2 = 0.0;
            b.a1 = -pd; b.a2 = 0.0;
        }
        reset();
    }
    void reset() noexcept { for (auto& s : sec) s.reset(); }
    inline double process (double x) noexcept { return sec[2].process (sec[1].process (sec[0].process (x))); }
};

// ---------------------------------------------------------------------------
// Bloqueador de continua
// ---------------------------------------------------------------------------
struct DCBlock
{
    double R = 0.999, x1 = 0, y1 = 0;
    void prepare (double fs, double fc) noexcept { R = std::exp (-2.0 * kPi * fc / fs); x1 = y1 = 0; }
    inline double process (double x) noexcept { const double y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
};

// ---------------------------------------------------------------------------
// Triodo clase A con anti-aliasing por antiderivada (ADAA, orden 1)
//   f(x) = (1-l) tanh(g x) + l g x / sqrt(1 + (g x)^2)
//   F(x) = [ (1-l) ln cosh(g x) + l sqrt(1 + (g x)^2) ] / g          (F' = f)
//   y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
// El sesgo 'bias' rompe la simetria y genera 2o armonico (valvula real);
// se resta f(bias) para que el reposo quede en cero.
// Sin ramas: cuando dx ~ 0 se mezcla con f(punto medio) mediante una mascara.
// ---------------------------------------------------------------------------
struct TriodeADAA
{
    double gamma = 1.142, lambda = 0.165, bias = 0.0, f0 = 0.0, x1 = 0.0, F1 = 0.0;

    static inline double lncosh (double u) noexcept
    {
        const double a = std::fabs (u);
        return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
    }
    inline double f (double x) const noexcept
    {
        const double u = gamma * x;
        return (1.0 - lambda) * std::tanh (u) + lambda * u / std::sqrt (1.0 + u * u);
    }
    inline double F (double x) const noexcept
    {
        const double u = gamma * x;
        return ((1.0 - lambda) * lncosh (u) + lambda * std::sqrt (1.0 + u * u)) / gamma;
    }
    void setParams (double g, double l, double b) noexcept { gamma = g; lambda = l; bias = b; f0 = f (b); }
    void reset() noexcept { x1 = bias; F1 = F (bias); }

    inline double process (double xin) noexcept
    {
        const double x = xin + bias;
        const double Fx = F (x);
        const double dx = x - x1;
        const double use = (double) (std::fabs (dx) > 1e-6);
        const double safe = dx + (1.0 - use);                   // evita 0/0 sin ramificar
        const double adaa = (Fx - F1) / safe;
        const double mid = f (0.5 * (x + x1));
        x1 = x; F1 = Fx;
        return use * adaa + (1.0 - use) * mid - f0;
    }
};

// ---------------------------------------------------------------------------
// Compresor cohesivo: sidechain HP 4o orden (Butterworth), RMS logaritmico,
// codo suave de 6 dB, ataque indexado al factor de cresta, release de doble
// etapa (65 / 420 ms) mezclado segun el caracter transitorio, y rama paralela 4:1.
// ---------------------------------------------------------------------------
struct Compressor
{
    struct Out { float gMain, gPar, grDb; };

    SVF hpA[2], hpB[2];
    double fs = 44100.0;
    double cRms = 0.0, peakRel = 0.0;
    double ms = 0, rawMs = 0, peak = 0;
    double grMF = 0, grMS = 0, grPF = 0, grPS = 0;

    void prepare (double sr) noexcept
    {
        fs = sr;
        for (int c = 0; c < 2; ++c)
        {
            hpA[c].setHighpass (fs, 96.35, 0.5411961);    // Butterworth 4o orden:
            hpB[c].setHighpass (fs, 96.35, 1.3065630);    // Q = 0.5412 y 1.3066
            hpA[c].reset(); hpB[c].reset();
        }
        cRms = 1.0 - std::exp (-1.0 / (0.025 * fs));
        peakRel = std::exp (-1.0 / (0.100 * fs));
        ms = rawMs = peak = grMF = grMS = grPF = grPS = 0;
    }

    static inline double softKnee (double over, double W, double slope) noexcept
    {
        const double t = clampd (over + 0.5 * W, 0.0, W);
        const double knee = t * t / (2.0 * W);
        const double sel = (double) (over > 0.5 * W);
        return slope * (sel * over + (1.0 - sel) * knee);
    }

    inline Out process (double l, double r, double thrDb) noexcept
    {
        const double sl = hpB[0].process (hpA[0].process (l));
        const double sr = hpB[1].process (hpA[1].process (r));
        ms    += cRms * (0.5 * (sl * sl + sr * sr) - ms);
        rawMs += cRms * (0.5 * (l * l + r * r) - rawMs);

        const double vdb = 10.0 * std::log10 (ms + 1e-24);          // = 20 log10(rms + eps)
        peak = std::fmax (std::fmax (std::fabs (l), std::fabs (r)), peak * peakRel);
        const double crest = clampd (peak / (std::sqrt (rawMs) + 1e-9), 1.0, 8.0);

        double tauA;
        if constexpr (kSlowAttackOnTransients)
            tauA = 6.5 + 21.5 * clampd ((crest - 1.5) / 4.0, 0.0, 1.0);
        else
            tauA = clampd (32.0 / (1.0 + 2.5 * crest), 6.5, 28.0);

        const double trans = clampd ((crest - 2.0) * 0.25, 0.0, 1.0);
        const double cA  = 1.0 - std::exp (-1.0 / (0.001 * tauA * fs));
        const double cRF = 1.0 - std::exp (-1.0 / (0.065 * fs));
        const double cRS = 1.0 - std::exp (-1.0 / (0.420 * fs));

        const double tM = softKnee (vdb - thrDb, 6.0, 0.5);              // 2:1
        const double tP = softKnee (vdb - (thrDb - 4.0), 6.0, 0.75);     // 4:1

        auto follow = [cA] (double& s, double tgt, double cR) noexcept
        {
            const double up = (double) (tgt > s);
            s += (up * cA + (1.0 - up) * cR) * (tgt - s);
        };
        follow (grMF, tM, cRF); follow (grMS, tM, cRS);
        follow (grPF, tP, cRF); follow (grPS, tP, cRS);

        const double grM = grMS + trans * (grMF - grMS);
        const double grP = grPS + trans * (grPF - grPS);
        return { (float) std::exp (-grM * 0.11512925464970229),
                 (float) std::exp (-grP * 0.11512925464970229),
                 (float) grM };
    }
};

// ---------------------------------------------------------------------------
// Moldeador de transitorios (diferencial de envolventes rapida - lenta).
// Realza como maximo +3 dB el golpe inicial; independiente del nivel.
// ---------------------------------------------------------------------------
struct TransientShaper
{
    double fs = 44100.0, cFA = 0, cFR = 0, cSA = 0, cSR = 0, cG = 0;
    double envF = 0, envS = 0, gSm = 1.0;

    void prepare (double sr) noexcept
    {
        fs = sr;
        auto c = [sr] (double ms) { return 1.0 - std::exp (-1.0 / (0.001 * ms * sr)); };
        cFA = c (0.8);  cFR = c (12.0);
        cSA = c (25.0); cSR = c (140.0);
        cG  = c (0.25);
        envF = envS = 0; gSm = 1.0;
    }

    inline double gain (double l, double r, double amount) noexcept
    {
        const double a = std::fmax (std::fabs (l), std::fabs (r));
        const double uf = (double) (a > envF), us = (double) (a > envS);
        envF += (uf * cFA + (1.0 - uf) * cFR) * (a - envF);
        envS += (us * cSA + (1.0 - us) * cSR) * (a - envS);
        const double diff = clampd (((envF - envS) / (envS + 1e-5) - 0.10) * 1.1111, 0.0, 1.0);   // zona muerta 10 %
        const double target = 1.0 + 0.41 * amount * diff;               // 0.41 => +3 dB
        gSm += cG * (target - gSm);
        return gSm;
    }
};

// ---------------------------------------------------------------------------
// Limitador de pico verdadero con lookahead (~1.5 ms), enlazado en estereo.
//  - Pico real estimado con interpolacion cubica (Catmull-Rom) entre muestras.
//  - Ganancia = minimo en ventana de lookahead, suavizada por media movil
//    (garantiza ganancia <= requerida en el pico) + release exponencial.
//  - Sin ramas: buffers circulares de tamano potencia de 2 con mascara.
// Latencia = lookahead - 1 muestras.
// ---------------------------------------------------------------------------
struct TruePeakLimiter
{
    static constexpr int kMax = 1024;          // potencia de 2
    static constexpr int kMask = kMax - 1;

    double dl[kMax] {}, dr[kMax] {};           // retardo del audio
    double gReq[kMax] {};                      // ganancias requeridas
    double mHist[kMax] {};                     // minimos (para la media movil)
    double hl[4] {}, hr[4] {};                 // ultimas 4 muestras (deteccion)
    int ls = 72, pos = 0;
    double sumM = 0, gState = 1.0, cRel = 0, ceilLin = 1.0, lastGainDb = 0;

    int latency() const noexcept { return ls - 1; }

    void prepare (double fs, double releaseMs = 70.0) noexcept
    {
        ls = (int) clampd (std::round (0.0015 * fs), 8.0, 480.0);
        cRel = 1.0 - std::exp (-1.0 / (0.001 * releaseMs * fs));
        for (int i = 0; i < kMax; ++i) dl[i] = dr[i] = mHist[i] = 0, gReq[i] = 1.0;
        for (int i = 0; i < 4; ++i) hl[i] = hr[i] = 0;
        pos = 0; sumM = (double) ls; gState = 1.0; lastGainDb = 0;
        for (int i = 0; i < kMax; ++i) mHist[i] = 1.0;
    }

    static inline double mid (const double* h) noexcept          // punto medio entre h[1] y h[2]
    {
        return (-h[0] + 9.0 * h[1] + 9.0 * h[2] - h[3]) * 0.0625;
    }

    // Procesa una muestra estereo; devuelve la salida con retardo (ls-1)
    inline void process (double l, double r, double ceilingLin, double& outL, double& outR) noexcept
    {
        ceilLin = ceilingLin;

        hl[0] = hl[1]; hl[1] = hl[2]; hl[2] = hl[3]; hl[3] = l;
        hr[0] = hr[1]; hr[1] = hr[2]; hr[2] = hr[3]; hr[3] = r;
        double pk = std::fmax (std::fabs (l), std::fabs (r));
        pk = std::fmax (pk, std::fmax (std::fabs (mid (hl)), std::fabs (mid (hr))));
        const double req = std::fmin (1.0, ceilLin / (pk + 1e-12));

        dl[pos & kMask] = l; dr[pos & kMask] = r; gReq[pos & kMask] = req;

        // minimo de las ultimas ls peticiones
        double m = 1.0;
        for (int k = 0; k < ls; ++k) m = std::fmin (m, gReq[(pos - k) & kMask]);

        // media movil de ls minimos
        sumM += m - mHist[(pos - ls) & kMask];
        mHist[pos & kMask] = m;
        const double avg = sumM / (double) ls;

        // release exponencial; nunca por encima de la ganancia suavizada de ataque
        gState = std::fmin (avg, gState + cRel * (1.0 - gState));

        const int d = ls - 1;
        const double yl = dl[(pos - d) & kMask] * gState;
        const double yr = dr[(pos - d) & kMask] * gState;
        outL = clampd (yl, -ceilLin, ceilLin);        // red de seguridad (muestra)
        outR = clampd (yr, -ceilLin, ceilLin);
        ++pos;
    }
};
} // namespace monarca
