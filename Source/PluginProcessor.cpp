#include "PluginProcessor.h"
#include "PluginEditor.h"

#if JUCE_MAC
 #include <Accelerate/Accelerate.h>
#endif

using namespace monarca;

namespace
{
inline void applyGain (float* x, const float* g, int n) noexcept
{
   #if JUCE_MAC
    vDSP_vmul (x, 1, g, 1, x, 1, (vDSP_Length) n);
   #else
    for (int i = 0; i < n; ++i) x[i] *= g[i];
   #endif
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout MonarcaProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<P> (juce::ParameterID { "chrysalis", 1 }, "Chrysalis", R (0.f, 1.f, 0.001f), 0.25f));
    l.add (std::make_unique<P> (juce::ParameterID { "migration", 1 }, "Migration", R (0.f, 1.f, 0.001f), 0.30f));
    l.add (std::make_unique<P> (juce::ParameterID { "scale", 1 },     "Scale",     R (0.f, 1.f, 0.001f), 0.35f));
    l.add (std::make_unique<P> (juce::ParameterID { "wings", 1 },     "Monarch Wings", R (0.f, 1.f, 0.001f), 0.30f));
    l.add (std::make_unique<P> (juce::ParameterID { "ceiling", 1 },   "Ceiling",   R (-12.f, 0.f, 0.01f), 0.f,
                                juce::AudioParameterFloatAttributes().withLabel ("dB")));
    return l;
}

MonarcaProcessor::MonarcaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MONARCA", createLayout())
{
    pChrys = apvts.getRawParameterValue ("chrysalis");
    pMigr  = apvts.getRawParameterValue ("migration");
    pScale = apvts.getRawParameterValue ("scale");
    pWings = apvts.getRawParameterValue ("wings");
    pCeil  = apvts.getRawParameterValue ("ceiling");
}

bool MonarcaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MonarcaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;

    os.initProcessing ((size_t) samplesPerBlock);
    os.reset();
    setLatencySamples ((int) std::lround (os.getLatencyInSamples()));

    gainBuf.assign ((size_t) std::max (samplesPerBlock, 16384), 1.f);

    for (int c = 0; c < 2; ++c)
    {
        lowA[c].setLowpass (fs, 150.0, 0.70710678); lowB[c].setLowpass (fs, 150.0, 0.70710678);
        lowA[c].reset(); lowB[c].reset(); lowPrev[c] = 0.0;
        sat[c].setParams (1.142, 0.165, 0.0); sat[c].reset();
        dc[c].prepare (fs, 8.0);

        lowShelf[c].setLowShelf  (fs, 56.74,    1.38, 0.70710678);
        bell[c].setBell          (fs, 282.15,  -0.92, 0.5413);
        highShelf[c].setHighShelf(fs, 12150.0,  1.55, 0.6180);
        lowShelf[c].reset(); bell[c].reset(); highShelf[c].reset();

        kIn[c].prepare (fs); kOut[c].prepare (fs);
    }
    mudHp.setHighpass (fs, 150.0, 0.70710678);
    mudLp.setLowpass  (fs, 500.0, 0.70710678);
    mudHp.reset(); mudLp.reset();
    envTot = envMud = 0.0;

    comp.prepare (fs);

    sideHp.prepare (fs, 84.3);
    sideShelf.setHighShelf (fs, 6800.0, 1.15, 0.70710678);
    sideShelf.reset();
    apL.setFc (fs, 10200.0); apR.setFc (fs, 10500.0);
    apL.reset(); apR.reset();

    const double twoPi = 2.0 * kPi;
    cEnv  = 1.0 - std::exp (-1.0 / (0.150 * fs));
    cMs   = 1.0 - std::exp (-1.0 / (0.400 * fs));
    cGain = 1.0 - std::exp (-twoPi * 0.5 / fs);
    cP    = 1.0 - std::exp (-1.0 / (0.020 * fs));

    chrys = *pChrys; migr = *pMigr; scl = *pScale; wings = *pWings;
    msIn = msOut = 1e-6; gSmooth = 1.0;
}

void MonarcaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n > (int) gainBuf.size() || n <= 0)
        return;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    float* d[2] = { L, R };

    const float tChrys = pChrys->load(), tMigr = pMigr->load(),
                tScale = pScale->load(), tWings = pWings->load();
    const double trim = std::pow (10.0, (double) pCeil->load() * 0.05);

    // ---- 0) Sonoridad de entrada (K-weighting, BS.1770-4) ---------------------
    for (int i = 0; i < n; ++i)
    {
        const double a = kIn[0].process (L[i]);
        const double b = kIn[1].process (R[i]);
        msIn += cMs * ((a * a + b * b) - msIn);
    }

    // ---- 1) Drive + transformador + EQ + de-mudder ----------------------------
    for (int i = 0; i < n; ++i)
    {
        chrys += (float) cP * (tChrys - chrys);
        migr  += (float) cP * (tMigr  - migr);
        scl   += (float) cP * (tScale - scl);
        wings += (float) cP * (tWings - wings);

        const double pre = 1.0 + 1.5 * chrys + 1.0 * migr;
        const double h   = 0.085 * chrys;

        double mono = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            double x = (double) d[c][i] * pre;

            // Transformador: y = x - h * low[n-1]^3 (+ termino par para el 2o armonico),
            // solo sobre la banda < 150 Hz (LR4)
            const double low = lowB[c].process (lowA[c].process (x));
            const double lp = lowPrev[c];
            x += h * (0.8 * lp * lp - lp * lp * lp);
            lowPrev[c] = low;

            mono += x;
            x = lowShelf[c].process (x);
            x = bell[c].process (x);
            x = highShelf[c].process (x);
            d[c][i] = (float) x;
        }
        mono *= 0.5;
        const double band = mudLp.process (mudHp.process (mono));
        envTot += cEnv * (mono * mono - envTot);
        envMud += cEnv * (band * band - envMud);
    }

    // Bell dinamico: se profundiza segun la fraccion de energia en 150-500 Hz
    {
        const double ratio = envMud / (envTot + 1e-12);
        const double lvlDb = 10.0 * std::log10 (envTot + 1e-12);
        const double gate  = clampd ((lvlDb + 55.0) / 15.0, 0.0, 1.0);
        const double depth = clampd ((ratio - 0.20) / 0.40, 0.0, 1.0) * gate;
        const double gDb   = -0.92 - 1.58 * (double) scl * depth;
        bell[0].setBell (fs, 282.15, gDb, 0.5413);
        bell[1].copyCoefs (bell[0]);
    }

    // ---- 2) Triodo clase A @ 8x con ADAA --------------------------------------
    {
        const double lam  = 0.165 + 0.10 * (double) scl;
        const double bias = 0.020 * (0.35 + 0.65 * (double) chrys);   // 2o armonico ~ -48 dB a nivel nominal
        sat[0].setParams (1.142, lam, bias);
        sat[1].setParams (1.142, lam, bias);

        juce::dsp::AudioBlock<float> blk (buffer);
        auto sub = blk.getSubsetChannelBlock (0, 2).getSubBlock (0, (size_t) n);
        auto up = os.processSamplesUp (sub);
        const size_t nu = up.getNumSamples();
        for (size_t c = 0; c < 2; ++c)
        {
            float* p = up.getChannelPointer (c);
            for (size_t i = 0; i < nu; ++i)
                p[i] = (float) sat[c].process ((double) p[i]);
        }
        os.processSamplesDown (sub);
    }

    // ---- 3) DC + compresor + profundidad M/S + AGC ----------------------------
    const double driveLin = 1.5 * chrys + migr;
    const double thr = -18.0 - 20.0 * std::log10 (1.0 + driveLin) * 0.387;
    const double mixPar = 0.20 * (double) migr;
    const double agcOn = (double) (msIn > 1.17e-7);       // puerta absoluta (~ -70 LKFS)
    float grMax = 0.f, peakOut = 0.f;

    for (int i = 0; i < n; ++i)
    {
        const double xl = dc[0].process (L[i]);
        const double xr = dc[1].process (R[i]);

        const auto o = comp.process (xl, xr, thr);
        grMax = std::fmax (grMax, o.grDb);
        const double g = (double) o.gMain * (1.0 - mixPar) + (double) o.gPar * mixPar;
        const double cl = xl * g, cr = xr * g;

        // Mid/Side
        const double M = 0.5 * (cl + cr);
        double S = 0.5 * (cl - cr);
        S = sideHp.process (S);                                   // eliptico 5o orden, 84.3 Hz
        S *= 1.0 + 0.6 * (double) wings;
        S += (double) wings * (sideShelf.process (S) - S);        // Baxandall alto +1.15 dB
        const double aL = apL.process (S), aR = apR.process (S);
        const double w = (double) wings;
        const double sL = S + w * (aL - S), sR = S + w * (aR - S);
        const double oL = M + sL, oR = M - sR;

        // AGC LKFS (suavizado 0.5 Hz, con puerta)
        const double ka = kOut[0].process (oL), kb = kOut[1].process (oR);
        msOut += cMs * ((ka * ka + kb * kb) - msOut);
        const double tg = clampd (std::sqrt ((msIn + 1e-12) / (msOut + 1e-12)), 0.25, 4.0);
        gSmooth += cGain * agcOn * (tg - gSmooth);
        gainBuf[(size_t) i] = (float) (gSmooth * trim);

        L[i] = (float) oL; R[i] = (float) oR;
    }

    applyGain (L, gainBuf.data(), n);
    applyGain (R, gainBuf.data(), n);

    for (int i = 0; i < n; ++i)
        peakOut = std::fmax (peakOut, std::fmax (std::fabs (L[i]), std::fabs (R[i])));

    grMeter.store (grMax);
    level.store (peakOut);
    sweet.store ((float) ((grMax > 1.5f) & (grMax < 5.5f) & (migr > 0.15f)));
}

juce::AudioProcessorEditor* MonarcaProcessor::createEditor() { return new MonarcaEditor (*this); }

void MonarcaProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void MonarcaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MonarcaProcessor(); }
