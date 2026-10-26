#include "PluginEditor.h"

using juce::Colour;
using juce::Rectangle;
namespace
{
constexpr float kPiF = juce::MathConstants<float>::pi;

struct Row { const char* name; const char* desc; };
const Row kRows[] = {
    { "CHRYSALIS",     "Calidez analógica. Sube la ganancia de entrada y añade armónicos densos en los graves con un modelo de transformador (por debajo de 150 Hz)." },
    { "MIGRATION",     "Control maestro de pegada. Sube la ganancia, baja el umbral del compresor y mezcla compresión paralela para dar cuerpo e impacto al transitorio." },
    { "SCALE",         "Claridad. Aumenta la densidad armónica de la saturación y profundiza un recorte dinámico en 282 Hz cuando se acumula \"barro\" en medios-graves." },
    { "MONARCH WINGS", "Profundidad y anchura estéreo. Abre el canal Side con desfase suave y aire en agudos. Por debajo de unos 84 Hz el estéreo se mantiene en mono." },
    { "CEILING",       "Techo de salida (-12 a 0 dB). Un limitador de pico verdadero con anticipación de 1.5 ms protege tu conversor; el control de sonoridad iguala el volumen con la entrada." },
    { "BARRAS",        "Las tres barras de la derecha: reducción del compresor (grande, hasta 12 dB) y nivel de salida izquierdo y derecho. El punto LIMIT se enciende cuando actúa el limitador." },
    { "PUNTO DULCE",   "Con entre 1.5 y 5.5 dB de compresión y Migration activo, el arco de Migration se enciende en naranja eléctrico." },
};
} // namespace

// ===========================================================================
// KnobSlider
// ===========================================================================
KnobSlider::KnobSlider (bool isMigration, juce::String title, juce::String unitText, juce::String hint,
                        const std::atomic<float>* sweetFlag, const std::atomic<float>* levelMeter)
    : hintText (std::move (hint)), migration (isMigration), knobTitle (std::move (title)),
      unit (std::move (unitText)), sweet (sweetFlag), level (levelMeter)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (kPiF * 1.25f, kPiF * 2.75f, true);
    setMouseDragSensitivity (220);
    startTimerHz (30);
}

juce::Path KnobSlider::butterfly()
{
    juce::Path right;
    right.startNewSubPath (0.04f, -0.05f);                                  // ala anterior
    right.cubicTo (0.15f, -0.55f, 0.60f, -0.95f, 0.95f, -0.70f);
    right.cubicTo (0.85f, -0.35f, 0.55f, -0.10f, 0.04f, 0.05f);
    right.closeSubPath();
    right.startNewSubPath (0.04f, 0.04f);                                   // ala posterior
    right.cubicTo (0.45f, 0.00f, 0.75f, 0.25f, 0.62f, 0.62f);
    right.cubicTo (0.40f, 0.78f, 0.15f, 0.55f, 0.04f, 0.30f);
    right.closeSubPath();
    right.startNewSubPath (0.02f, -0.30f);                                  // antena
    right.quadraticTo (0.06f, -0.52f, 0.18f, -0.60f);

    juce::Path left (right);
    left.applyTransform (juce::AffineTransform::scale (-1.f, 1.f));

    juce::Path all;
    all.addPath (right);
    all.addPath (left);
    all.startNewSubPath (0.f, -0.30f);                                      // cuerpo
    all.lineTo (0.f, 0.50f);
    return all;
}

void KnobSlider::mouseEnter (const juce::MouseEvent& e) { juce::Slider::mouseEnter (e); hovering = true; }
void KnobSlider::mouseExit  (const juce::MouseEvent& e) { juce::Slider::mouseExit (e);  hovering = false; }

void KnobSlider::timerCallback()
{
    const float targetPos = (float) valueToProportionOfLength (getValue());
    const float hot = (sweet != nullptr && sweet->load() > 0.5f && migration) ? 1.f : 0.f;
    const float lv  = (level != nullptr && migration) ? level->load() : 0.f;

    const float oPos = displayPos, oHov = hoverAmt, oGlow = glow, oLvl = lvl;
    displayPos += 0.35f * (targetPos - displayPos);
    hoverAmt   += 0.25f * ((hovering ? 1.f : 0.f) - hoverAmt);
    glow       += 0.12f * (hot - glow);
    lvl        += 0.25f * (lv - lvl);
    phase      += 0.09f;

    const float eps = 0.0008f;
    const bool changed = std::abs (displayPos - oPos) > eps || std::abs (hoverAmt - oHov) > eps
                      || std::abs (glow - oGlow) > eps || std::abs (lvl - oLvl) > 0.002f;
    if (std::abs (displayPos - targetPos) < eps) displayPos = targetPos;
    if (changed || migration)          // la mariposa "respira" siempre
        repaint();
}

void KnobSlider::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (3.f);
    const float rad = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const auto rp = getRotaryParameters();
    const float a0 = rp.startAngleRadians, a1 = rp.endAngleRadians;
    const float ang = a0 + displayPos * (a1 - a0);
    const Colour arcCol = col::amber.interpolatedWith (col::target, glow);

    // 1) pozo hundido
    juce::Path well;
    well.addEllipse (c.x - rad, c.y - rad, rad * 2.f, rad * 2.f);
    g.setGradientFill (juce::ColourGradient (Colour (0xff030304), c.x - rad * 0.6f, c.y - rad,
                                             Colour (0xff1a1a1f), c.x + rad * 0.6f, c.y + rad, false));
    g.fillPath (well);
    g.setColour (juce::Colours::white.withAlpha (0.05f + 0.04f * hoverAmt));
    g.drawEllipse (c.x - rad, c.y - rad, rad * 2.f, rad * 2.f, 0.8f);

    // marcas de escala y anillo vivo (Migration)
    {
        const float t0 = migration ? 0.930f : 0.945f, t1 = migration ? 0.962f : 0.988f;
        for (int k = 0; k <= 10; ++k)
        {
            const float f = (float) k / 10.f, a = a0 + f * (a1 - a0);
            const float sx = std::sin (a), sy = -std::cos (a);
            g.setColour (f <= displayPos + 0.001f ? arcCol.withAlpha (0.75f) : col::muted.withAlpha (0.45f));
            g.drawLine (c.x + sx * rad * t0, c.y + sy * rad * t0,
                        c.x + sx * rad * t1, c.y + sy * rad * t1, (k % 5 == 0) ? 1.2f : 0.7f);
        }
        if (migration)
            for (int k = 0; k < 48; ++k)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) k / 48.f;
                const float pulse = 0.5f + 0.5f * std::sin (phase * 1.3f - (float) k * 0.4f);
                g.setColour (arcCol.withAlpha (0.05f + 0.30f * lvl * pulse + 0.20f * glow * pulse));
                g.fillEllipse (c.x + std::sin (a) * rad * 0.985f - 0.7f,
                               c.y - std::cos (a) * rad * 0.985f - 0.7f, 1.4f, 1.4f);
            }
    }

    // 2) pista y arco de valor con resplandor
    const float arcR = rad * 0.90f;
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.f, a0, a1, true);
    g.setColour (col::muted.withAlpha (0.35f));
    g.strokePath (track, juce::PathStrokeType (1.0f));

    juce::Path val;
    val.addCentredArc (c.x, c.y, arcR, arcR, 0.f, a0, juce::jmax (a0 + 0.001f, ang), true);
    g.setColour (arcCol.withAlpha (0.16f + 0.24f * glow + 0.10f * hoverAmt));
    g.strokePath (val, juce::PathStrokeType (migration ? 7.f : 5.f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.setColour (arcCol);
    g.strokePath (val, juce::PathStrokeType (migration ? 2.4f : 1.8f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // 3) cuerpo con relieve
    const float ri = rad * 0.72f;
    juce::Path body;
    body.addEllipse (c.x - ri, c.y - ri, ri * 2.f, ri * 2.f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 7, { 0, 3 }).drawForPath (g, body);
    g.setGradientFill (juce::ColourGradient (Colour (0xff2c2c32), c.x - ri * 0.7f, c.y - ri * 0.9f,
                                             Colour (0xff0b0b0d), c.x + ri * 0.7f, c.y + ri * 0.9f, false));
    g.fillPath (body);

    juce::Path hl;
    hl.addCentredArc (c.x, c.y, ri - 0.6f, ri - 0.6f, 0.f, -kPiF * 0.80f, kPiF * 0.10f, true);
    g.setColour (juce::Colours::white.withAlpha (0.16f + 0.10f * hoverAmt));
    g.strokePath (hl, juce::PathStrokeType (1.0f));
    juce::Path sh;
    sh.addCentredArc (c.x, c.y, ri - 0.6f, ri - 0.6f, 0.f, kPiF * 0.25f, kPiF * 0.95f, true);
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.strokePath (sh, juce::PathStrokeType (1.2f));

    // 4) tapa concava
    const float rc = ri * 0.78f;
    g.setGradientFill (juce::ColourGradient (Colour (0xff09090b), c.x - rc * 0.6f, c.y - rc * 0.9f,
                                             Colour (0xff1d1d22), c.x + rc * 0.6f, c.y + rc * 0.9f, false));
    g.fillEllipse (c.x - rc, c.y - rc, rc * 2.f, rc * 2.f);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawEllipse (c.x - rc, c.y - rc, rc * 2.f, rc * 2.f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawEllipse (c.x - rc + 1.f, c.y - rc + 1.f, rc * 2.f - 2.f, rc * 2.f - 2.f, 0.6f);

    // 5) indicador
    juce::Path p;
    p.startNewSubPath (0.f, -rc * 1.10f);
    p.lineTo (0.f, -ri * 0.93f);
    g.setColour (arcCol);
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::rotation (ang).translated (c.x, c.y));

    // 6) contenido de la tapa: valor numerico fijo
    const Colour valCol = col::text.interpolatedWith (arcCol, 0.30f * hoverAmt + (isMouseButtonDown() ? 0.30f : 0.f));
    const juce::String txt = getTextFromValue (getValue());

    if (migration)
    {
        auto bf = butterfly();
        const float sx = rc * 0.62f * (1.f + 0.035f * std::sin (phase) + 0.10f * lvl);
        const float sy = rc * 0.62f * (1.f + 0.020f * std::sin (phase * 0.7f));
        bf.applyTransform (juce::AffineTransform::scale (sx, sy).translated (c.x, c.y - rc * 0.16f));
        if (glow > 0.02f)
            juce::DropShadow (arcCol.withAlpha (0.55f * glow), 10, { 0, 0 }).drawForPath (g, bf);
        g.setColour (arcCol.withAlpha (0.12f + 0.20f * glow));
        g.fillPath (bf);
        g.setColour (arcCol.withAlpha (0.90f));
        g.strokePath (bf, juce::PathStrokeType (1.0f));

        g.setColour (valCol);
        g.setFont (ui::mono (12.f));
        g.drawText (txt + " " + unit, Rectangle<float> (c.x - rc, c.y + rc * 0.58f, rc * 2.f, 14.f),
                    juce::Justification::centred, false);
    }
    else
    {
        g.setColour (valCol);
        g.setFont (ui::mono (11.f));
        g.drawText (txt, Rectangle<float> (c.x - rc, c.y - rc * 0.40f, rc * 2.f, 14.f),
                    juce::Justification::centred, false);
        g.setColour (col::muted);
        g.setFont (ui::sans (7.5f, "Regular"));
        g.drawText (unit, Rectangle<float> (c.x - rc, c.y + rc * 0.14f, rc * 2.f, 10.f),
                    juce::Justification::centred, false);
    }
}

// ===========================================================================
// InfoButton
// ===========================================================================
InfoButton::InfoButton()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (30);
}

void InfoButton::mouseEnter (const juce::MouseEvent&) { hovering = true; }
void InfoButton::mouseExit  (const juce::MouseEvent&) { hovering = false; }
void InfoButton::mouseDown  (const juce::MouseEvent&) { if (onClick) onClick(); }

void InfoButton::timerCallback()
{
    const float o = hover;
    hover += 0.25f * ((hovering ? 1.f : 0.f) - hover);
    if (std::abs (hover - o) > 0.001f) repaint();
}

void InfoButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.f);
    const auto c = r.getCentre();
    const Colour k = col::muted.interpolatedWith (col::amber, juce::jmax (hover, active ? 1.f : 0.f));
    if (active)
    {
        g.setColour (col::amber.withAlpha (0.15f));
        g.fillEllipse (r);
    }
    g.setColour (k);
    g.drawEllipse (r, 1.0f);
    g.fillEllipse (c.x - 1.1f, c.y - 4.6f, 2.2f, 2.2f);                        // punto de la "i"
    g.fillRoundedRectangle (c.x - 0.9f, c.y - 1.4f, 1.8f, 5.6f, 0.9f);         // asta
}

// ===========================================================================
// InfoOverlay
// ===========================================================================
InfoOverlay::InfoOverlay()
{
    setVisible (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void InfoOverlay::show (bool s)
{
    goal = s ? 1.f : 0.f;
    if (s) setVisible (true);
    startTimerHz (60);
}

void InfoOverlay::mouseDown (const juce::MouseEvent&) { if (onClose) onClose(); }

void InfoOverlay::timerCallback()
{
    amt += 0.22f * (goal - amt);
    if (std::abs (goal - amt) < 0.01f)
    {
        amt = goal;
        stopTimer();
        if (goal <= 0.f) setVisible (false);
    }
    repaint();
}

void InfoOverlay::paint (juce::Graphics& g)
{
    if (amt <= 0.001f) return;
    auto A = [this] (Colour c) { return c.withMultipliedAlpha (amt); };

    g.fillAll (A (Colour (0xf2080809)));

    g.setColour (A (col::text));
    g.setFont (ui::sans (15.f, "Light").withExtraKerningFactor (0.35f));
    g.drawText (ui::u8 ("GUÍA RÁPIDA"), 0, 12, getWidth(), 22, juce::Justification::centred, false);
    g.setColour (A (col::amber.withAlpha (0.6f)));
    g.drawHorizontalLine (40, 200.f, 360.f);

    int y = 52;
    for (const auto& r : kRows)
    {
        g.setColour (A (col::amber));
        g.setFont (ui::sans (9.5f, "Medium").withExtraKerningFactor (0.12f));
        g.drawText (ui::u8 (r.name), 28, y, 118, 14, juce::Justification::topLeft, false);

        g.setColour (A (col::text.withAlpha (0.85f)));
        g.setFont (ui::sans (9.5f, "Light"));
        g.drawFittedText (ui::u8 (r.desc), Rectangle<int> (150, y - 1, 385, 30),
                          juce::Justification::topLeft, 2, 1.0f);
        y += 35;
    }

    g.setColour (A (col::muted));
    g.setFont (ui::sans (8.5f, "Light"));
    g.drawText (ui::u8 ("Doble clic en un mando = valor por defecto  ·  toca para cerrar"),
                0, getHeight() - 20, getWidth(), 12, juce::Justification::centred, false);
}

// ===========================================================================
// SignatureLink
// ===========================================================================
SignatureLink::SignatureLink()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SignatureLink::paint (juce::Graphics& g)
{
    g.setColour (col::muted.interpolatedWith (col::amber, amount));
    g.setFont (ui::sans (8.5f, "Light"));
    g.drawText ("@IVANJOKO_", getLocalBounds(), juce::Justification::centredRight, false);
}

void SignatureLink::mouseEnter (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
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
MonarcaView::MonarcaView (MonarcaProcessor& p)
    : proc (p),
      chrysalis (false, "Chrysalis", "%", ui::u8 ("Chrysalis · calidez analógica: ganancia de entrada y armónicos en graves."), &p.sweet, &p.level),
      scale     (false, "Scale", "%", ui::u8 ("Scale · claridad: densidad armónica y recorte dinámico del barro (282 Hz)."), &p.sweet, &p.level),
      migration (true,  "Migration", "%", ui::u8 ("Migration · pegada: ganancia, umbral y compresión paralela acoplados."), &p.sweet, &p.level),
      wings     (false, "Monarch Wings", "%", ui::u8 ("Monarch Wings · profundidad y anchura estéreo (graves siempre en mono)."), &p.sweet, &p.level),
      ceiling   (false, "Ceiling", "dB", ui::u8 ("Ceiling · techo de salida con limitador de pico verdadero y sonoridad igualada."), &p.sweet, &p.level)
{
    for (auto* s : { &chrysalis, &scale, &migration, &wings, &ceiling })
    {
        s->addMouseListener (this, false);
        addAndMakeVisible (*s);
    }

    auto pct = [] (double v) { return juce::String (v * 100.0, 1); };
    for (auto* s : { &chrysalis, &scale, &migration, &wings })
        s->textFromValueFunction = pct;
    ceiling.textFromValueFunction = [] (double v) { return juce::String (v, 1); };

    aChrys = std::make_unique<Att> (proc.apvts, "chrysalis", chrysalis);
    aScale = std::make_unique<Att> (proc.apvts, "scale",     scale);
    aMigr  = std::make_unique<Att> (proc.apvts, "migration", migration);
    aWings = std::make_unique<Att> (proc.apvts, "wings",     wings);
    aCeil  = std::make_unique<Att> (proc.apvts, "ceiling",   ceiling);

    auto setDefault = [this] (KnobSlider& s, const char* id)
    {
        if (auto* prm = proc.apvts.getParameter (id))
            s.setDoubleClickReturnValue (true, (double) prm->convertFrom0to1 (prm->getDefaultValue()));
    };
    setDefault (chrysalis, "chrysalis"); setDefault (scale, "scale"); setDefault (migration, "migration");
    setDefault (wings, "wings");         setDefault (ceiling, "ceiling");

    info.onClick = [this] { const bool open = ! overlay.isVisible();
                            overlay.show (open); info.setActive (open); };
    overlay.onClose = [this] { overlay.show (false); info.setActive (false); };

    addAndMakeVisible (info);
    addAndMakeVisible (signature);
    addChildComponent (overlay);     // oculto hasta pulsar (i)

    setSize (560, 340);
    startTimerHz (30);
}

MonarcaView::~MonarcaView() = default;

void MonarcaView::resized()
{
    chrysalis.setBounds (39, 50, 96, 96);
    scale.setBounds     (39, 180, 96, 96);
    migration.setBounds (190, 78, 180, 180);
    wings.setBounds     (425, 50, 96, 96);
    ceiling.setBounds   (425, 180, 96, 96);
    barArea = { 543.5f, 50.f, 1.5f, 226.f };
    barL = { 548.5f, 50.f, 1.5f, 226.f };
    barR = { 551.5f, 50.f, 1.5f, 226.f };

    info.setBounds (getWidth() - 36, 12, 20, 20);
    overlay.setBounds (getLocalBounds());
    signature.setBounds (getWidth() - 15 - 80, getHeight() - 15 - 12, 80, 12);
    rebuildBackground();
}

void MonarcaView::rebuildBackground()
{
    const float s = 3.f;      // cache vectorial a 3x (Retina y ventana ampliada)
    bgCache = juce::Image (juce::Image::ARGB, (int) (getWidth() * s), (int) (getHeight() * s), true);
    juce::Graphics g (bgCache);
    g.addTransform (juce::AffineTransform::scale (s));
    paintStatic (g);
}

void MonarcaView::paintWingGrid (juce::Graphics& g) const
{
    const float cx = 280.f, cy = 170.f;
    juce::Path veins, cells;
    for (float sgn : { -1.f, 1.f })
    {
        for (int k = 1; k <= 8; ++k)                          // nervaduras radiales
        {
            const float t = (float) k / 8.f;
            veins.startNewSubPath (cx, cy);
            veins.cubicTo (cx + sgn * 60.f, cy - 40.f * t,
                           cx + sgn * 150.f, cy - 190.f * t + 40.f,
                           cx + sgn * (250.f + 20.f * t), cy - 160.f + 320.f * t);
        }
        for (int k = 1; k <= 6; ++k)                          // celdas concentricas
        {
            const float r = 28.f * (float) k;
            cells.startNewSubPath (cx, cy - r);
            cells.cubicTo (cx + sgn * r * 1.1f, cy - r * 1.3f,
                           cx + sgn * r * 1.8f, cy - r * 0.2f,
                           cx + sgn * r * 1.2f, cy + r * 0.9f);
        }
    }
    g.setColour (col::muted.withAlpha (0.10f));
    g.strokePath (veins, juce::PathStrokeType (0.5f));
    g.setColour (col::amber.withAlpha (0.07f));
    g.strokePath (cells, juce::PathStrokeType (0.5f));
}

void MonarcaView::paintStatic (juce::Graphics& g) const
{
    // fondo con viñeta radial
    g.setGradientFill (juce::ColourGradient (Colour (0xff131316), 280.f, 150.f,
                                             col::bg, 280.f + 360.f, 150.f, true));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (col::amber.withAlpha (0.07f), 280.f, 170.f,
                                             col::amber.withAlpha (0.f), 280.f + 140.f, 170.f, true));
    g.fillEllipse (140.f, 30.f, 280.f, 280.f);
    paintWingGrid (g);

    // paneles en relieve
    const Rectangle<float> panels[3] = { { 20.f, 38.f, 134.f, 268.f },
                                         { 172.f, 64.f, 216.f, 242.f },
                                         { 406.f, 38.f, 134.f, 268.f } };
    for (const auto& r : panels)
    {
        juce::Path pp;
        pp.addRoundedRectangle (r, 10.f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 10, { 0, 4 }).drawForPath (g, pp);
        g.setGradientFill (juce::ColourGradient (Colour (0xff161619), r.getX(), r.getY(),
                                                 Colour (0xff0b0b0d), r.getX(), r.getBottom(), false));
        g.fillPath (pp);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (r.translated (0.5f, 0.8f).reduced (0.5f), 10.f, 1.0f);
    }

    // nombres bajo cada mando
    g.setColour (col::muted);
    g.setFont (ui::sans (8.5f, "Regular").withExtraKerningFactor (0.20f));
    auto label = [&g] (const char* t, Rectangle<int> b)
    {
        g.drawText (ui::u8 (t), b.getX() - 20, b.getBottom() + 1, b.getWidth() + 40, 12,
                    juce::Justification::centred, false);
    };
    label ("CHRYSALIS",     { 39, 50, 96, 96 });
    label ("SCALE",         { 39, 180, 96, 96 });
    label ("MIGRATION",     { 190, 78, 180, 180 });
    label ("MONARCH WINGS", { 425, 50, 96, 96 });
    label ("CEILING",       { 425, 180, 96, 96 });

    // adorno bajo el titulo
    {
        juce::ColourGradient gr (col::amber.withAlpha (0.f), 190.f, 56.f, col::amber.withAlpha (0.f), 370.f, 56.f, false);
        gr.addColour (0.5, col::amber.withAlpha (0.75f));
        g.setGradientFill (gr);
        g.fillRect (190.f, 55.5f, 180.f, 1.0f);
    }
    juce::Path dia;
    dia.addQuadrilateral (280.f, 52.5f, 283.f, 56.f, 280.f, 59.5f, 277.f, 56.f);
    g.setColour (col::amber);
    g.fillPath (dia);

    // escala de la barra de gain reduction
    g.setColour (col::muted.withAlpha (0.30f));
    g.fillRect (barArea); g.fillRect (barL); g.fillRect (barR);
    g.setColour (col::muted.withAlpha (0.6f));
    g.setFont (ui::sans (6.5f, "Regular"));
    g.drawText ("GR", 534, 36, 20, 10, juce::Justification::centred, false);
    g.drawText ("L R", 544, 36, 14, 10, juce::Justification::centred, false);
    g.setFont (ui::sans (6.5f, "Regular").withExtraKerningFactor (0.2f));
    g.drawText ("LIMIT", 464, 291, 40, 8, juce::Justification::centredLeft, false);
    for (int k = 0; k <= 4; ++k)
        g.fillRect (barArea.getX() - 4.f, barArea.getY() + barArea.getHeight() * (float) k / 4.f - 0.25f, 3.f, 0.5f);
}

void MonarcaView::setHint (const juce::String& h)
{
    if (h == hint) return;
    hint = h;
    repaint (hintArea);
}

void MonarcaView::mouseEnter (const juce::MouseEvent& e)
{
    if (auto* k = dynamic_cast<KnobSlider*> (e.eventComponent)) setHint (k->hintText);
}

void MonarcaView::mouseExit (const juce::MouseEvent& e)
{
    if (dynamic_cast<KnobSlider*> (e.eventComponent) != nullptr) setHint ({});
}

void MonarcaView::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (bgCache, juce::AffineTransform::scale (1.f / 3.f));

    // titulo con reflejo ambar que lo recorre cada pocos segundos
    {
        const auto f = ui::sans (27.f, "Ultra Light").withExtraKerningFactor (0.32f);
        const Colour base = col::text.darker (0.10f);
        juce::ColourGradient gr (base, 140.f, 0.f, base, 420.f, 0.f, false);
        if (shimmer >= 0.f)
        {
            const double p = (double) shimmer;
            gr.addColour (juce::jlimit (0.0, 1.0, p - 0.14), base);
            gr.addColour (juce::jlimit (0.0, 1.0, p),        col::glowHi);
            gr.addColour (juce::jlimit (0.0, 1.0, p + 0.14), base);
        }
        g.setGradientFill (gr);
        g.setFont (f);
        g.drawText ("MONARCA", titleArea, juce::Justification::centred, false);
    }

    // barra de Gain Reduction
    const float fill = juce::jlimit (0.f, 1.f, grSmooth / 12.f) * barArea.getHeight();
    const Colour bc = lastSweet ? col::target : col::amber;
    g.setColour (bc.withAlpha (0.25f));
    g.fillRect (barArea.withHeight (fill).expanded (1.5f, 0.f));
    g.setColour (bc);
    g.fillRect (barArea.withHeight (fill));

    // medidores de salida L/R con retencion de pico
    auto meter = [&g] (Rectangle<float> r, float v, float hold)
    {
        const float h = juce::jlimit (0.f, 1.f, v) * r.getHeight();
        g.setGradientFill (juce::ColourGradient (col::target, 0.f, r.getY(), col::amber.darker (0.3f), 0.f, r.getBottom(), false));
        g.fillRect (Rectangle<float> (r.getX(), r.getBottom() - h, r.getWidth(), h));
        g.setColour (col::text.withAlpha (0.85f));
        g.fillRect (r.getX(), r.getBottom() - juce::jlimit (0.f, 1.f, hold) * r.getHeight() - 0.5f, r.getWidth(), 1.0f);
    };
    meter (barL, mL, hL);
    meter (barR, mR, hR);

    // LED del limitador
    g.setColour (col::muted.interpolatedWith (col::target, limAmt));
    g.fillEllipse (456.f, 293.f, 4.f, 4.f);
    if (limAmt > 0.05f)
    {
        g.setColour (col::target.withAlpha (0.25f * limAmt));
        g.fillEllipse (454.f, 291.f, 8.f, 8.f);
    }

    // linea de ayuda
    g.setFont (ui::sans (9.f, "Light"));
    g.setColour (hint.isEmpty() ? col::muted.withAlpha (0.65f) : col::text.withAlpha (0.85f));
    g.drawText (hint.isEmpty() ? ui::u8 ("Pasa el ratón por un mando para ver qué hace  ·  (i) guía completa") : hint,
                hintArea, juce::Justification::centredLeft, false);
}

void MonarcaView::timerCallback()
{
    grSmooth += 0.3f * (proc.grMeter.load() - grSmooth);
    const bool sw = proc.sweet.load() > 0.5f;
    lastSweet = sw;
    auto dbPos = [] (float lin) { return juce::jlimit (0.f, 1.f, (20.f * std::log10 (lin + 1e-6f) + 60.f) / 60.f); };
    const float tl = dbPos (proc.levelL.load()), tr = dbPos (proc.levelR.load());
    mL = juce::jmax (tl, mL - 0.05f);  mR = juce::jmax (tr, mR - 0.05f);
    hL = juce::jmax (mL, hL - 0.006f); hR = juce::jmax (mR, hR - 0.006f);
    limAmt += 0.3f * (juce::jlimit (0.f, 1.f, proc.limGr.load() / 1.5f) - limAmt);
    repaint (meterRepaint);
    repaint (ledRepaint);

    // reflejo del titulo: 2.5 s de recorrido cada 10 s
    const double t = std::fmod (juce::Time::getMillisecondCounterHiRes() * 0.001, 10.0);
    const bool active = t < 2.5;
    shimmer = active ? (float) (t / 2.5 * 1.4 - 0.2) : -1.f;
    if (active || titleWasActive)
        repaint (titleArea);
    titleWasActive = active;
}

// ===========================================================================
// Contenedor redimensionable
// ===========================================================================
MonarcaEditor::MonarcaEditor (MonarcaProcessor& p)
    : AudioProcessorEditor (&p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (420, 255, 1120, 680);
    getConstrainer()->setFixedAspectRatio (560.0 / 340.0);
    setSize (560, 340);
}

void MonarcaEditor::resized()
{
    view.setBounds (0, 0, 560, 340);
    view.setTransform (juce::AffineTransform::scale ((float) getWidth() / 560.f));
}
