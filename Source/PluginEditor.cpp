#include "PluginEditor.h"

// ===========================================================================
// LookAndFeel
// ===========================================================================
juce::Path MonarcaLnF::butterfly()
{
    // Semi-ala derecha en coordenadas unitarias (y negativa = arriba)
    juce::Path right;
    right.startNewSubPath (0.04f, -0.05f);                                     // ala anterior
    right.cubicTo (0.15f, -0.55f, 0.60f, -0.95f, 0.95f, -0.70f);
    right.cubicTo (0.85f, -0.35f, 0.55f, -0.10f, 0.04f, 0.05f);
    right.closeSubPath();
    right.startNewSubPath (0.04f, 0.04f);                                      // ala posterior
    right.cubicTo (0.45f, 0.00f, 0.75f, 0.25f, 0.62f, 0.62f);
    right.cubicTo (0.40f, 0.78f, 0.15f, 0.55f, 0.04f, 0.30f);
    right.closeSubPath();

    juce::Path left (right);
    left.applyTransform (juce::AffineTransform::scale (-1.f, 1.f));

    juce::Path all;
    all.addPath (right);
    all.addPath (left);
    all.startNewSubPath (0.f, -0.30f);                                         // cuerpo
    all.lineTo (0.f, 0.50f);
    return all;
}

void MonarcaLnF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                   float pos, float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.f);
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto c = r.getCentre();
    const bool isMig = (bool) s.getProperties()["migration"];
    const bool hot = isMig && sweet != nullptr && sweet->load() > 0.5f;
    const auto arcCol = hot ? col::target : col::amber;

    juce::Path track;
    track.addCentredArc (c.x, c.y, rad, rad, 0.f, a0, a1, true);
    g.setColour (col::muted.withAlpha (0.45f));
    g.strokePath (track, juce::PathStrokeType (1.0f));

    const float ang = a0 + pos * (a1 - a0);
    juce::Path val;
    val.addCentredArc (c.x, c.y, rad, rad, 0.f, a0, ang, true);
    g.setColour (arcCol);
    g.strokePath (val, juce::PathStrokeType (isMig ? 2.5f : 1.8f));

    const float ri = rad * 0.72f;
    g.setColour (col::comp);
    g.fillEllipse (c.x - ri, c.y - ri, ri * 2.f, ri * 2.f);
    g.setColour (col::muted.withAlpha (0.6f));
    g.drawEllipse (c.x - ri, c.y - ri, ri * 2.f, ri * 2.f, 1.0f);

    if (isMig)
    {
        auto bf = butterfly();
        bf.applyTransform (juce::AffineTransform::scale (ri * 0.78f).translated (c.x, c.y));
        g.setColour (arcCol.withAlpha (hot ? 0.28f : 0.14f));
        g.fillPath (bf);
        g.setColour (arcCol.withAlpha (0.9f));
        g.strokePath (bf, juce::PathStrokeType (1.0f));
    }
    else
    {
        juce::Path p;
        p.startNewSubPath (0.f, -ri * 0.45f);
        p.lineTo (0.f, -ri * 0.95f);
        g.setColour (col::text);
        g.strokePath (p, juce::PathStrokeType (1.2f),
                      juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    }
}

// ===========================================================================
// Firma interactiva
// ===========================================================================
SignatureLink::SignatureLink()
{
    setMouseCursor (juce::MouseCursor::StandardCursorType::PointingHandCursor);
}

void SignatureLink::paint (juce::Graphics& g)
{
    g.setColour (col::muted.interpolatedWith (col::amber, amount));
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 8.5f, juce::Font::plain).withStyle ("Light")));
    g.drawText ("@IVANJOKO_", getLocalBounds(), juce::Justification::centredRight, false);
}

void SignatureLink::mouseEnter (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::StandardCursorType::PointingHandCursor);
    goal = 1.f;
    startTimerHz (60);
}

void SignatureLink::mouseExit (const juce::MouseEvent&)
{
    goal = 0.f;
    startTimerHz (60);
}

void SignatureLink::mouseDown (const juce::MouseEvent&)
{
    juce::URL ("https://www.instagram.com/ivanjoko_/").launchInDefaultBrowser();
}

void SignatureLink::timerCallback()
{
    amount += 0.2f * (goal - amount);
    if (std::abs (goal - amount) < 0.01f)
    {
        amount = goal;
        stopTimer();
    }
    repaint();
}

// ===========================================================================
// Editor
// ===========================================================================
MonarcaEditor::MonarcaEditor (MonarcaProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    lnf.sweet = &proc.sweet;
    setLookAndFeel (&lnf);

    const float pi = juce::MathConstants<float>::pi;
    for (auto* s : { &chrysalis, &scale, &migration, &wings, &ceiling })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters (pi * 1.25f, pi * 2.75f, true);
        s->setPopupDisplayEnabled (true, true, this);
        addAndMakeVisible (*s);
    }
    migration.getProperties().set ("migration", true);

    aChrys = std::make_unique<Att> (proc.apvts, "chrysalis", chrysalis);
    aScale = std::make_unique<Att> (proc.apvts, "scale",     scale);
    aMigr  = std::make_unique<Att> (proc.apvts, "migration", migration);
    aWings = std::make_unique<Att> (proc.apvts, "wings",     wings);
    aCeil  = std::make_unique<Att> (proc.apvts, "ceiling",   ceiling);

    addAndMakeVisible (signature);
    setSize (560, 340);
    startTimerHz (30);
}

MonarcaEditor::~MonarcaEditor() { setLookAndFeel (nullptr); }

void MonarcaEditor::resized()
{
    chrysalis.setBounds (37, 48, 96, 96);
    scale.setBounds     (37, 178, 96, 96);
    migration.setBounds (190, 72, 180, 180);
    wings.setBounds     (427, 48, 96, 96);
    ceiling.setBounds   (427, 178, 96, 96);
    barArea = { 546.f, 48.f, 1.5f, 226.f };
    signature.setBounds (getWidth() - 15 - 80, getHeight() - 15 - 12, 80, 12);
}

void MonarcaEditor::paintWingGrid (juce::Graphics& g) const
{
    const float cx = 280.f, cy = 170.f;
    juce::Path p;
    for (float sgn : { -1.f, 1.f })
    {
        for (int k = 1; k <= 7; ++k)                       // nervaduras radiales
        {
            const float t = (float) k / 7.f;
            p.startNewSubPath (cx, cy);
            p.cubicTo (cx + sgn * 60.f, cy - 40.f * t,
                       cx + sgn * 150.f, cy - 190.f * t + 40.f,
                       cx + sgn * (240.f + 20.f * t), cy - 150.f + 300.f * t);
        }
        for (int k = 1; k <= 5; ++k)                       // celdas concéntricas
        {
            const float r = 30.f * (float) k;
            p.startNewSubPath (cx, cy - r);
            p.cubicTo (cx + sgn * r * 1.1f, cy - r * 1.3f,
                       cx + sgn * r * 1.8f, cy - r * 0.2f,
                       cx + sgn * r * 1.2f, cy + r * 0.9f);
        }
    }
    g.setColour (col::comp.withAlpha (0.05f));
    g.strokePath (p, juce::PathStrokeType (0.5f));
}

void MonarcaEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    paintWingGrid (g);

    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 9.f, juce::Font::plain).withStyle ("Light")));
    auto label = [&g] (const juce::String& t, juce::Rectangle<int> b)
    {
        g.drawText (t.toUpperCase(), b.getX() - 20, b.getBottom() + 2, b.getWidth() + 40, 12,
                    juce::Justification::centred, false);
    };
    label ("Chrysalis", chrysalis.getBounds());
    label ("Scale", scale.getBounds());
    label ("Migration", migration.getBounds());
    label ("Monarch Wings", wings.getBounds());
    label ("Ceiling", ceiling.getBounds());

    // Barra de Gain Reduction (1.5 px)
    g.setColour (col::muted.withAlpha (0.3f));
    g.fillRect (barArea);
    const float fill = juce::jlimit (0.f, 1.f, grSmooth / 12.f) * barArea.getHeight();
    g.setColour (lastSweet ? col::target : col::amber);
    g.fillRect (barArea.withHeight (fill));
}

void MonarcaEditor::timerCallback()
{
    grSmooth += 0.3f * (proc.grMeter.load() - grSmooth);
    const bool sw = proc.sweet.load() > 0.5f;
    if (sw != lastSweet)
    {
        lastSweet = sw;
        migration.repaint();
    }
    repaint (barArea.expanded (2.f).toNearestInt());
}
