#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace col
{
inline const juce::Colour bg     { 0xff080809 };   // Background_Dark
inline const juce::Colour comp   { 0xff141416 };   // Component_Gray
inline const juce::Colour muted  { 0xff55555c };   // Text_Muted
inline const juce::Colour text   { 0xffeaeaea };   // Text_Active
inline const juce::Colour amber  { 0xffd47a2a };   // Monarch_Amber
inline const juce::Colour target { 0xffff9f1c };   // Signal_Target
inline const juce::Colour glowHi { 0xffffc27a };   // reflejo del titulo
}

namespace ui
{
inline juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
inline juce::Font sans (float h, const char* style = "Light")
{
    return juce::Font (juce::FontOptions ("Avenir Next", h, juce::Font::plain).withStyle (style));
}
inline juce::Font mono (float h)
{
    return juce::Font (juce::FontOptions ("Menlo", h, juce::Font::plain));
}
}

// ---------------------------------------------------------------------------
// Rotatorio vectorial con relieve, animacion y valor numerico fijo en el centro
// ---------------------------------------------------------------------------
class KnobSlider : public juce::Slider, private juce::Timer
{
public:
    KnobSlider (bool isMigration, juce::String title, juce::String unitText, juce::String hint,
                const std::atomic<float>* sweetFlag, const std::atomic<float>* levelMeter);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    juce::String hintText;

private:
    void timerCallback() override;
    static juce::Path butterfly();

    bool migration;
    juce::String knobTitle, unit;
    const std::atomic<float>* sweet;
    const std::atomic<float>* level;

    float displayPos = 0.f, hoverAmt = 0.f, glow = 0.f, lvl = 0.f, phase = 0.f;
    bool hovering = false;
};

// ---------------------------------------------------------------------------
class InfoButton : public juce::Component, private juce::Timer
{
public:
    InfoButton();
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setActive (bool a) { active = a; repaint(); }
    std::function<void()> onClick;

private:
    void timerCallback() override;
    float hover = 0.f;
    bool hovering = false, active = false;
};

// ---------------------------------------------------------------------------
class InfoOverlay : public juce::Component, private juce::Timer
{
public:
    InfoOverlay();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void show (bool s);
    std::function<void()> onClose;

private:
    void timerCallback() override;
    float amt = 0.f, goal = 0.f;
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
class MonarcaView : public juce::Component, private juce::Timer
{
public:
    explicit MonarcaView (MonarcaProcessor&);
    ~MonarcaView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void rebuildBackground();
    void paintStatic (juce::Graphics&) const;
    void paintWingGrid (juce::Graphics&) const;
    void setHint (const juce::String&);

    MonarcaProcessor& proc;

    KnobSlider chrysalis, scale, migration, wings, ceiling;
    using Att = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Att> aChrys, aScale, aMigr, aWings, aCeil;

    InfoButton info;
    InfoOverlay overlay;
    SignatureLink signature;

    juce::Image bgCache;
    juce::Rectangle<float> barArea, barL, barR;
    juce::Rectangle<int> meterRepaint { 526, 30, 34, 262 }, ledRepaint { 448, 288, 66, 14 };
    float mL = 0.f, mR = 0.f, hL = 0.f, hR = 0.f, limAmt = 0.f;
    juce::Rectangle<int> titleArea { 140, 8, 280, 44 }, hintArea { 20, 314, 430, 16 };
    juce::String hint;
    float grSmooth = 0.f, shimmer = -1.f;
    bool lastSweet = false, titleWasActive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarcaView)
};

// Contenedor redimensionable: el diseño (560x340) se escala con una transformacion
class MonarcaEditor : public juce::AudioProcessorEditor
{
public:
    explicit MonarcaEditor (MonarcaProcessor&);
    void resized() override;

private:
    MonarcaView view;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarcaEditor)
};
