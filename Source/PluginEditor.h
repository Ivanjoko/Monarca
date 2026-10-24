#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace col
{
inline const juce::Colour bg     { 0xff080809 };   // Background_Dark
inline const juce::Colour comp   { 0xff141416 };   // Component_Gray
inline const juce::Colour muted  { 0xff55555c };   // Text_Muted
inline const juce::Colour text   { 0xffeaeaea };   // Text_Active
inline const juce::Colour amber  { 0xffd47a2a };   // Monarch_Amber
inline const juce::Colour target { 0xffff9f1c };   // Signal_Target
}

// ---------------------------------------------------------------------------
struct MonarcaLnF : public juce::LookAndFeel_V4
{
    const std::atomic<float>* sweet = nullptr;

    static juce::Path butterfly();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float a0, float a1, juce::Slider&) override;
};

// ---------------------------------------------------------------------------
class SignatureLink : public juce::Component, private juce::Timer
{
public:
    SignatureLink();
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    float amount = 0.f, goal = 0.f;
};

// ---------------------------------------------------------------------------
class MonarcaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MonarcaEditor (MonarcaProcessor&);
    ~MonarcaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintWingGrid (juce::Graphics&) const;

    MonarcaProcessor& proc;
    MonarcaLnF lnf;

    juce::Slider chrysalis, scale, migration, wings, ceiling;
    using Att = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Att> aChrys, aScale, aMigr, aWings, aCeil;

    SignatureLink signature;

    juce::Rectangle<float> barArea;
    float grSmooth = 0.f;
    bool lastSweet = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarcaEditor)
};
