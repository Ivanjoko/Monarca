#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include "Dsp.h"

class MonarcaProcessor : public juce::AudioProcessor
{
public:
    MonarcaProcessor();
    ~MonarcaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Monarca"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> grMeter { 0.f };   // dB de reduccion de ganancia (pico del bloque)
    std::atomic<float> sweet   { 0.f };   // 1 = "punto dulce"
    std::atomic<float> level   { 0.f };   // pico de salida del bloque (0..1) para animaciones
    std::atomic<float> levelL  { 0.f };   // pico de salida L (lineal)
    std::atomic<float> levelR  { 0.f };   // pico de salida R (lineal)
    std::atomic<float> limGr   { 0.f };   // dB de reduccion del limitador (pico del bloque)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    double fs = 44100.0;

    std::atomic<float>* pChrys = nullptr;
    std::atomic<float>* pMigr  = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pWings = nullptr;
    std::atomic<float>* pCeil  = nullptr;

    float chrys = 0.f, migr = 0.f, scl = 0.f, wings = 0.f;   // suavizados por muestra

    juce::dsp::Oversampling<float> os { 2, 3,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };

    // Transformador (banda < 150 Hz, LR4) y saturador ADAA
    monarca::SVF lowA[2], lowB[2];
    double lowPrev[2] {};
    monarca::TriodeADAA sat[2];
    monarca::DCBlock dc[2];

    // EQ + de-mudder
    monarca::SVF lowShelf[2], bell[2], highShelf[2];
    monarca::SVF mudHp, mudLp;
    double envTot = 0.0, envMud = 0.0;

    // Compresor
    monarca::Compressor comp;

    // Profundidad M/S
    monarca::EllipticHP5 sideHp;
    monarca::SVF sideShelf;
    monarca::AllPass1 apL, apR;

    // AGC LKFS
    monarca::TransientShaper ts;
    monarca::TruePeakLimiter lim;
    monarca::AllPass1 apL2, apR2;
    double sLR = 0.0, sLL = 1e-9, sRR = 1e-9, cCorr = 0.0;

    monarca::KWeighting kIn[2], kOut[2];
    double msIn = 1e-6, msOut = 1e-6, gSmooth = 1.0;

    double cEnv = 0.0, cMs = 0.0, cGain = 0.0, cP = 0.0;

    std::vector<float> gainBuf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarcaProcessor)
};
