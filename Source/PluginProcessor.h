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

    std::atomic<float> grMeter { 0.f };   // dB de reducción de ganancia (pico del bloque)
    std::atomic<float> sweet { 0.f };     // 1 = "punto dulce"

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    double fs = 44100.0;

    // Parámetros
    std::atomic<float>* pChrys = nullptr;
    std::atomic<float>* pMigr  = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pWings = nullptr;
    std::atomic<float>* pCeil  = nullptr;

    // Suavizado de parámetros
    float chrys = 0.f, migr = 0.f, scl = 0.f, wings = 0.f;

    // Oversampling 8x (3 etapas half-band polifásicas IIR)
    juce::dsp::Oversampling<float> os { 2, 3,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };

    // Transformador (<150 Hz)
    float lp[2] {}, lpPrev[2] {};

    // EQ
    monarca::SVF lowShelf[2], bell[2], highShelf[2];

    // Compresor
    monarca::SVF scHp[2];
    float ms = 0.f, rawMs = 0.f, peak = 0.f, grMain = 0.f, grPar = 0.f, envE = 0.f;

    // Profundidad M/S
    monarca::SVF sideHpA, sideHpB, sideShelf;
    float sideLp = 0.f;
    monarca::AllPass1 apL, apR;

    // AGC LKFS
    monarca::KWeighting kIn[2], kOut[2];
    float msIn = 1e-6f, msOut = 1e-6f, gSmooth = 1.f;

    // Coeficientes
    float cLow = 0.f, cEnv = 0.f, cRms = 0.f, cMs = 0.f, cGain = 0.f, cP = 0.f,
          cSideLp = 0.f, peakRel = 0.f;

    std::vector<float> gainBuf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarcaProcessor)
};
