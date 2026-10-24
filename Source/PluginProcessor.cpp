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
    const double twoPi = 2.0 * kPi;

    os.initProcessing ((size_t) samplesPerBlock);
    os.reset();
    setLatencySamples ((int) std::lround (os.getLatencyInSamples()));

    gainBuf.assign ((size_t) std::max (samplesPerBlock, 16384), 1.f);

    for (int c = 0; c < 2; ++c)
    {
        lowShelf[c].setLowShelf  (fs, 56.74,    1.38, 0.70710678);
        bell[c].setBell          (fs, 282.15,  -0.92, 0.5413);
        highShelf[c].setHighShelf(fs, 12150.0,  1.55, 0.6180);
        scHp[c].setHighpass      (fs, 96.35, 0.70710678);
        lowShelf[c].reset(); bell[c].reset(); highShelf[c].reset(); scHp[c].reset();
        kIn[c].prepare (fs); kOut[c].prepare (fs);
        lp[c] = lpPrev[c] = 0.f;
    }

    // Side: Butterworth 5º orden a 84.3 Hz (2 biquads + 1 polo)
    sideHpA.setHighpass (fs, 84.3, 0.6180340);
    sideHpB.setHighpass (fs, 84.3, 1.6180340);
    sideHpA.reset(); sideHpB.reset(); sideLp = 0.f;
    sideShelf.setHighShelf (fs, 6800.0, 1.15, 0.70710678);
    sideShelf.reset();
    apL.setFc (fs, 10200.0); apR.setFc (fs, 10500.0);
    apL.reset(); apR.reset();

    cLow    = (float) (1.0 - std::exp (-twoPi * 150.0 / fs));
    cSideLp = (float) (1.0 - std::exp (-twoPi * 84.3 / fs));
    cEnv    = (float) (1.0 - std::exp (-1.0 / (0.300 * fs)));
    cRms    = (float) (1.0 - std::exp (-1.0 / (0.020 * fs)));
    cMs     = (float) (1.0 - std::exp (-1.0 / (0.400 * fs)));
    cGain   = (float) (1.0 - std::exp (-twoPi * 0.5 / fs));
    cP      = (float) (1.0 - std::exp (-1.0 / (0.020 * fs)));
    peakRel = (float) std::exp (-1.0 / (0.100 * fs));

    chrys = *pChrys; migr = *pMigr; scl = *pScale; wings = *pWings;
    ms = rawMs = peak = grMain = grPar = envE = 0.f;
    msIn = msOut = 1e-6f; gSmooth = 1.f;
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
    const float trim = std::pow (10.f, pCeil->load() * 0.05f);
    const float fsF = (float) fs;

    // ---- 0) Sonoridad de entrada (K-weighting) --------------------------------
    for (int i = 0; i < n; ++i)
    {
        const float a = kIn[0].process (L[i]);
        const float b = kIn[1].process (R[i]);
        msIn += cMs * ((a * a + b * b) - msIn);
    }

    // ---- 1) Drive + transformador + EQ (frecuencia de muestreo nativa) -------
    for (int i = 0; i < n; ++i)
    {
        chrys += cP * (tChrys - chrys);
        migr  += cP * (tMigr  - migr);
        scl   += cP * (tScale - scl);
        wings += cP * (tWings - wings);

        const float pre = 1.f + 1.5f * chrys + 1.0f * migr;
        const float h   = 0.085f * chrys;

        for (int c = 0; c < 2; ++c)
        {
            float x = d[c][i] * pre;
            lp[c] += cLow * (x - lp[c]);
            const float p3 = lpPrev[c] * lpPrev[c] * lpPrev[c];
            x -= h * p3;                       // y = x - h * x[n-1]^3, solo banda < 150 Hz
            lpPrev[c] = lp[c];
            x = lowShelf[c].process (x);
            x = bell[c].process (x);
            x = highShelf[c].process (x);
            d[c][i] = x;
        }
        envE += cEnv * (0.5f * (L[i] * L[i] + R[i] * R[i]) - envE);
    }

    // Bell dinámico: más profundo cuanto más energía hay (por bloque)
    {
        const float lvlDb = 10.f * std::log10 (envE + 1e-12f);
        const float depth = clampf ((lvlDb + 30.f) * 0.05f, 0.f, 1.f);
        const float gDb   = -0.92f - 1.58f * scl * depth;
        bell[0].setBell (fs, 282.15, (double) gDb, 0.5413);
        bell[1].copyCoefs (bell[0]);
    }

    // ---- 2) Saturación triodo @ 8x ------------------------------------------------
    {
        juce::dsp::AudioBlock<float> blk (buffer);
        auto sub = blk.getSubsetChannelBlock (0, 2).getSubBlock (0, (size_t) n);
        auto up = os.processSamplesUp (sub);
        const float gam = 1.142f, lam = 0.165f + 0.10f * scl;
        const size_t nu = up.getNumSamples();
        for (size_t c = 0; c < 2; ++c)
        {
            float* p = up.getChannelPointer (c);
            for (size_t i = 0; i < nu; ++i)
                p[i] = triode (p[i], gam, lam);
        }
        os.processSamplesDown (sub);
    }

    // ---- 3) Compresor + Profundidad M/S + AGC LKFS ------------------------------
    const float driveLin = 1.5f * chrys + migr;
    const float thr = -18.f - 20.f * std::log10 (1.f + driveLin) * 0.387f;
    const float mixPar = 0.20f * migr;
    float grMax = 0.f;

    for (int i = 0; i < n; ++i)
    {
        // sidechain
        const float sl = scHp[0].process (L[i]);
        const float sr = scHp[1].process (R[i]);
        ms    += cRms * (0.5f * (sl * sl + sr * sr) - ms);
        rawMs += cRms * (0.5f * (L[i] * L[i] + R[i] * R[i]) - rawMs);
        const float rms = std::sqrt (ms);
        const float vdb = 20.f * std::log10 (rms + 1e-12f);

        peak = std::fmax (std::fmax (std::fabs (L[i]), std::fabs (R[i])), peak * peakRel);
        const float crest = clampf (peak / (std::sqrt (rawMs) + 1e-6f), 1.f, 8.f);

        const float tauA = clampf (32.f / (1.f + 2.5f * crest), 6.5f, 28.f);
        const float trans = clampf ((crest - 2.f) * 0.25f, 0.f, 1.f);
        const float tauR = 420.f + (65.f - 420.f) * trans;
        const float cA = std::exp (-1.f / (0.001f * tauA * fsF));
        const float cR = std::exp (-1.f / (0.001f * tauR * fsF));

        const float over = std::fmax (vdb - thr, 0.f);
        const float tgtM = over * 0.5f;     // 2:1
        const float tgtP = over * 0.75f;    // 4:1 paralelo

        const float upM = (float) (tgtM > grMain);
        const float upP = (float) (tgtP > grPar);
        grMain += (upM * (1.f - cA) + (1.f - upM) * (1.f - cR)) * (tgtM - grMain);
        grPar  += (upP * (1.f - cA) + (1.f - upP) * (1.f - cR)) * (tgtP - grPar);
        grMax = std::fmax (grMax, grMain);

        const float gM = std::exp (-grMain * 0.11512925f);
        const float gP = std::exp (-grPar  * 0.11512925f);
        const float g  = gM * (1.f - mixPar) + gP * mixPar;
        const float cl = L[i] * g, cr = R[i] * g;

        // Mid/Side
        const float M = 0.5f * (cl + cr);
        float S = 0.5f * (cl - cr);
        S = sideHpB.process (sideHpA.process (S));
        sideLp += cSideLp * (S - sideLp);
        S -= sideLp;                                   // 5º orden
        S *= 1.f + 0.6f * wings;
        S += wings * (sideShelf.process (S) - S);
        const float aL = apL.process (S), aR = apR.process (S);
        const float sL = S + wings * (aL - S);
        const float sR = S + wings * (aR - S);
        const float oL = M + sL, oR = M - sR;

        // AGC (sonoridad de salida vs entrada, suavizado a 0.5 Hz)
        const float ka = kOut[0].process (oL), kb = kOut[1].process (oR);
        msOut += cMs * ((ka * ka + kb * kb) - msOut);
        const float tg = clampf (std::sqrt ((msIn + 1e-9f) / (msOut + 1e-9f)), 0.25f, 4.f);
        gSmooth += cGain * (tg - gSmooth);
        gainBuf[(size_t) i] = gSmooth * trim;

        L[i] = oL; R[i] = oR;
    }

    applyGain (L, gainBuf.data(), n);
    applyGain (R, gainBuf.data(), n);

    grMeter.store (grMax);
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
