#include "PluginEditor.h"
#include "KintsugiGlyphs.h"
#include "BinaryData.h"

namespace
{
    // Smooth value noise for the wood grain
    float hashNoise (int x, int y, int seed)
    {
        unsigned int h = (unsigned int) (x * 374761393 + y * 668265263 + seed * 2147483647);
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float) ((h ^ (h >> 16)) & 0xffff) / 65535.0f;
    }

    float valueNoise (float x, float y, int seed)
    {
        const int xi = (int) std::floor (x), yi = (int) std::floor (y);
        const float xf = x - (float) xi, yf = y - (float) yi;
        const float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
        const float a = hashNoise (xi, yi, seed), b = hashNoise (xi + 1, yi, seed);
        const float c = hashNoise (xi, yi + 1, seed), d = hashNoise (xi + 1, yi + 1, seed);
        return juce::jmap (v, juce::jmap (u, a, b), juce::jmap (u, c, d));
    }

    float fbm (float x, float y, int seed)
    {
        return 0.55f * valueNoise (x, y, seed) + 0.3f * valueNoise (x * 2.1f, y * 2.1f, seed + 7)
             + 0.15f * valueNoise (x * 4.3f, y * 4.3f, seed + 13);
    }

}

namespace Palette
{
    const juce::Colour plate   { 0xff324739 };   // deep, dark Space Echo green
    const juce::Colour cream   { 0xffeae1c4 };
    const juce::Colour ink     { 0xff171614 };
    const juce::Colour metal   { 0xffb9bbb5 };
    const juce::Colour gold    { 0xffc99a3e };
}

namespace Layout
{
    constexpr int W = 1182, H = 478;   // equal margins left and right
    constexpr int faderThumb = 22;   // half the fader cap height; JUCE keeps this much travel clear at each end
    constexpr int outColW = 118;
    constexpr int sideW = 46, railH = 46;   // walnut housing, even frame width all round
    constexpr int topH = 122;          // pressed groove sits here
    constexpr int colW = 84;
    constexpr int rowH = 122;
    constexpr int knobSize = 76;
    constexpr int bracketY = 150;
    constexpr int row1Y = 170;
    constexpr int marginX = 56;   // extra room on the left, clear of the brass corner
    constexpr int sectionGap = 26;
}

//==============================================================================
juce::Font KintsugiFonts::serif (float height, bool bold)
{
    static auto reg  = juce::Typeface::createSystemTypefaceFor (BinaryData::OldStandardRegular_ttf, BinaryData::OldStandardRegular_ttfSize);
    static auto bld  = juce::Typeface::createSystemTypefaceFor (BinaryData::OldStandardBold_ttf, BinaryData::OldStandardBold_ttfSize);
    return juce::Font (juce::FontOptions (bold ? bld : reg).withHeight (height));
}

//==============================================================================
KintsugiLookAndFeel::KintsugiLookAndFeel()
{
    setColour (juce::Label::textColourId, Palette::cream);
}

juce::Label* KintsugiLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return l;
}

void KintsugiLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider& s)
{
    using namespace juce;
    if (s.getProperties()["pointer"])
    {
        drawPointerKnob (g, x, y, w, h, startAngle + pos * (endAngle - startAngle), s);
        return;
    }
    if (s.getProperties()["kintsugi"])
    {
        drawKintsugiKnob (g, x, y, w, h, startAngle + pos * (endAngle - startAngle), s);
        return;
    }
    if (s.getProperties()["brass"])
    {
        drawBrassKnob (g, x, y, w, h, startAngle + pos * (endAngle - startAngle), s);
        return;
    }
    if (useCreamKnobs)
    {
        drawCreamKnob (g, x, y, w, h, startAngle + pos * (endAngle - startAngle), s);
        return;
    }
    // ---- silver spun-aluminium knob (kept as an alternative look) ----
    const auto c = Rectangle<int> (x, y, w, h).toFloat().getCentre();
    const float R = jmin (w, h) * 0.5f * 0.80f;       // skirt radius
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float lightAngle = -MathConstants<float>::pi * 0.25f;   // light from the upper left
    auto circle = [c] (float r, Point<float> off = {}) { return Rectangle<float> (r * 2, r * 2).withCentre (c + off); };
    auto polar  = [c] (float r, float a) { return c.getPointOnCircumference (r, a); };
    Random rng (s.getName().hashCode());               // each knob has its own wear, always the same

    // Spun aluminium: the reflection forms a "bow tie" that stays put while the knob turns
    auto spunMetal = [&] (float r, Colour darkC, Colour lightC)
    {
        constexpr int seg = 72;
        for (int i = 0; i < seg; ++i)
        {
            const float a0 = (float) i * MathConstants<float>::twoPi / seg;
            const float a1 = a0 + MathConstants<float>::twoPi / seg * 1.8f;   // overlap hides seams
            const float b = std::pow (0.5f + 0.5f * std::cos (2.0f * (a0 - lightAngle)), 1.6f);
            Path wedge;
            wedge.addPieSegment (circle (r), a0, a1, 0.0f);
            g.setColour (darkC.interpolatedWith (lightC, b));
            g.fillPath (wedge);
        }
        for (int k = 2; k <= 18; ++k)   // fine lathe rings
        {
            g.setColour ((k % 2 ? Colours::white : Colours::black).withAlpha (0.035f));
            g.drawEllipse (circle (r * (float) k / 18.0f), 0.6f);
        }
    };

    if (! s.isEnabled())
        g.beginTransparencyLayer (0.45f);

    // Soft drop shadow
    for (int i = 0; i < 7; ++i)
    {
        g.setColour (Colours::black.withAlpha (0.065f));
        g.fillEllipse (circle (R + (float) i * 0.9f, { 1.8f, 3.0f }));
    }

    // Silver skirt: spun metal, tarnished towards the rim
    spunMetal (R, Colour (0xff6e6f6a), Colour (0xffe9e8e2));
    g.setGradientFill (ColourGradient (Colours::transparentBlack, c.x, c.y, Colour (0x553a3420), c.x + R, c.y, true));
    g.fillEllipse (circle (R));
    for (int i = 0; i < 120; ++i)   // fine knurl on the skirt edge
    {
        const float a = angle + (float) i * MathConstants<float>::twoPi / 120.0f;
        g.setColour (Colours::black.withAlpha (0.10f));
        g.drawLine ({ polar (R * 0.93f, a), polar (R * 0.995f, a) }, 0.6f);
    }
    g.setColour (Colour (0xff2b2a26).withAlpha (0.75f));
    g.drawEllipse (circle (R), 1.0f);

    // Engraved indicator on the skirt (dark groove with a light lower edge)
    g.setColour (Colours::white.withAlpha (0.35f));
    g.drawLine ({ polar (R * 0.78f, angle).translated (0.6f, 0.7f), polar (R * 0.97f, angle).translated (0.6f, 0.7f) }, 1.0f);
    g.setColour (Colour (0xff1a1815));
    g.drawLine ({ polar (R * 0.78f, angle), polar (R * 0.97f, angle) }, 2.0f);

    // Black knurled body: slightly glossy bakelite, teeth worn lighter where fingers grip
    const float Rb = R * 0.74f;
    g.setGradientFill (ColourGradient (Colour (0xff2e2a26), c.x - Rb, c.y - Rb, Colour (0xff0a0908), c.x + Rb, c.y + Rb, false));
    g.fillEllipse (circle (Rb));
    constexpr int teeth = 44;
    for (int i = 0; i < teeth; ++i)
    {
        const float a = angle + (float) i * MathConstants<float>::twoPi / (float) teeth;
        const float lit = std::pow (0.5f + 0.5f * std::cos (a - lightAngle), 2.0f);
        const float wornTooth = 0.85f + 0.3f * rng.nextFloat();
        g.setColour (Colour (0xff57514a).withAlpha (jlimit (0.0f, 1.0f, (0.18f + 0.65f * lit) * wornTooth)));
        g.drawLine ({ polar (Rb * 0.84f, a), polar (Rb * 0.995f, a) }, 1.3f);
        g.setColour (Colours::black.withAlpha (0.6f));
        g.drawLine ({ polar (Rb * 0.84f, a + 0.07f), polar (Rb * 0.995f, a + 0.07f) }, 0.9f);
    }
    {   // gloss on the upper-left shoulder
        Path arc;
        arc.addCentredArc (c.x, c.y, Rb * 0.80f, Rb * 0.80f, 0.0f, lightAngle - 0.9f, lightAngle + 0.9f, true);
        g.setColour (Colours::white.withAlpha (0.13f));
        g.strokePath (arc, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.setColour (Colours::black);
    g.drawEllipse (circle (Rb), 1.0f);

    // Aluminium cap
    const float Rc = R * 0.47f;
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (circle (Rc + 1.4f, { 0.0f, 0.9f }));
    spunMetal (Rc, Colour (0xff75766f), Colour (0xfff3f2ec));
    // age: grime settling at the rim, a faint warm patina, a few scratches
    g.setGradientFill (ColourGradient (Colours::transparentBlack, c.x, c.y, Colour (0x66352f22), c.x + Rc, c.y, true));
    g.fillEllipse (circle (Rc));
    g.setColour (Colour (0xffc9b48a).withAlpha (0.08f));
    g.fillEllipse (circle (Rc));
    for (int i = 0; i < 5; ++i)
    {
        const float a = angle + rng.nextFloat() * MathConstants<float>::twoPi;
        const float r0 = rng.nextFloat() * Rc * 0.8f;
        const auto p0 = polar (r0, a);
        const auto p1 = p0.getPointOnCircumference (2.0f + rng.nextFloat() * Rc * 0.5f, rng.nextFloat() * MathConstants<float>::twoPi);
        g.setColour ((i % 2 ? Colours::white : Colours::black).withAlpha (0.18f + 0.15f * rng.nextFloat()));
        g.drawLine ({ p0, p1 }, 0.5f);
    }
    g.setColour (Colour (0xff3a3934));
    g.drawEllipse (circle (Rc), 0.9f);

    // Engraved pointer line on the cap
    g.setColour (Colours::white.withAlpha (0.4f));
    g.drawLine ({ polar (Rc * 0.20f, angle).translated (0.6f, 0.7f), polar (Rc * 0.90f, angle).translated (0.6f, 0.7f) }, 1.0f);
    Path p;
    p.startNewSubPath (polar (Rc * 0.20f, angle));
    p.lineTo (polar (Rc * 0.90f, angle));
    g.setColour (Colour (0xff1c1a17));
    g.strokePath (p, PathStrokeType (2.4f, PathStrokeType::curved, PathStrokeType::rounded));

    if (! s.isEnabled())
        g.endTransparencyLayer();
}

void KintsugiLookAndFeel::drawPointerKnob (juce::Graphics& g, int x, int y, int w, int h, float angle, juce::Slider& s)
{
    // Fairchild-style pointer knob in glossy oxblood, seen from above: a flange that runs out into a
    // pointer with a white triangle inlay, a fluted hub with rings on top, and a long grip block behind
    using namespace juce;
    const auto c = Rectangle<int> (x, y, w, h).toFloat().getCentre();
    const float R = jmin (w, h) * 0.5f * 0.88f;
    const Colour body (0xff5e1612), bodyDark (0xff2a0705), bodyLight (0xff9b3328);
    const auto rot = AffineTransform::rotation (angle, c.x, c.y);   // knob space points straight up
    auto litFill = [&] (Rectangle<float> r, float lift)            // lit from the upper left whatever the rotation
    {
        g.setGradientFill (ColourGradient (bodyLight.brighter (lift), r.getX(), r.getY(), bodyDark, r.getRight(), r.getBottom(), false));
    };

    if (! s.isEnabled()) g.beginTransparencyLayer (0.45f);

    // ---- outlines in knob space ----
    const float Rf = R * 0.62f;                       // flange radius
    Path flange;
    flange.addCentredArc (c.x, c.y, Rf, Rf, 0.0f, 0.42f, MathConstants<float>::twoPi - 0.42f, true);
    flange.lineTo (c.x, c.y - R);                     // pointer tip
    flange.closeSubPath();

    const float tailHalf = R * 0.30f, tailEnd = c.y + R * 0.98f;
    Path tail;
    tail.addRoundedRectangle (c.x - tailHalf, c.y, tailHalf * 2.0f, tailEnd - c.y, 3.0f);

    const float Rh = R * 0.46f;                        // fluted hub
    Path hub;
    constexpr int flutes = 9;
    for (int i = 0; i <= flutes * 8; ++i)
    {
        const float a = (float) i / (flutes * 8) * MathConstants<float>::twoPi;
        const float r = Rh * (1.0f - 0.07f * std::pow (std::abs (std::sin (a * flutes * 0.5f)), 3.0f));
        const auto p = c.getPointOnCircumference (r, a);
        i == 0 ? hub.startNewSubPath (p) : hub.lineTo (p);
    }
    hub.closeSubPath();

    // ---- shadow ----
    Path all (flange);
    all.addPath (tail);
    for (int i = 1; i <= 6; ++i)
    {
        g.setColour (Colours::black.withAlpha (0.07f));
        g.fillPath (all, rot.translated (1.0f + i * 0.6f, 1.8f + i * 0.8f));
    }

    // ---- flange + pointer (lowest level) ----
    litFill (Rectangle<float> (R * 2, R * 2).withCentre (c), 0.0f);
    g.fillPath (flange, rot);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.strokePath (flange, PathStrokeType (0.8f), rot);

    // white triangle inlay on the pointer
    Path tri;
    tri.startNewSubPath (c.x, c.y - R * 0.88f);
    tri.lineTo (c.x + R * 0.15f, c.y - R * 0.54f);
    tri.lineTo (c.x - R * 0.15f, c.y - R * 0.54f);
    tri.closeSubPath();
    g.setColour (Palette::cream);
    g.strokePath (tri, PathStrokeType (1.5f, PathStrokeType::mitered), rot);

    // ---- grip block behind (tallest part) ----
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillPath (tail, rot.translated (1.0f, 1.8f));
    litFill (Rectangle<float> (R * 2, R * 2).withCentre (c), 0.12f);
    g.fillPath (tail, rot);
    {
        // glossy top face: a soft streak along the block
        Path streak;
        streak.addRoundedRectangle (c.x - tailHalf * 0.55f, c.y + R * 0.10f, tailHalf * 0.35f, tailEnd - c.y - R * 0.18f, 2.0f);
        g.setColour (Colours::white.withAlpha (0.16f));
        g.fillPath (streak, rot);
    }
    g.setColour (Colours::black.withAlpha (0.5f));
    g.strokePath (tail, PathStrokeType (0.8f), rot);

    // ---- fluted hub ----
    g.setColour (Colours::black.withAlpha (0.4f));
    g.fillPath (hub, rot.translated (0.8f, 1.4f));
    litFill (Rectangle<float> (Rh * 2, Rh * 2).withCentre (c), 0.08f);
    g.fillPath (hub, rot);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.strokePath (hub, PathStrokeType (0.8f), rot);

    // rings on the top: an engraved ring and a raised centre boss
    auto ring = [&] (float r, float alphaDark, float alphaLight)
    {
        g.setColour (Colours::black.withAlpha (alphaDark));
        g.drawEllipse (Rectangle<float> (r * 2, r * 2).withCentre (c), 1.0f);
        g.setColour (Colours::white.withAlpha (alphaLight));
        g.drawEllipse (Rectangle<float> (r * 2, r * 2).withCentre (c.translated (0.5f, 0.6f)), 0.6f);
    };
    ring (Rh * 0.74f, 0.45f, 0.18f);
    const float Rc = Rh * 0.38f;
    g.setGradientFill (ColourGradient (bodyLight.brighter (0.2f), c.x - Rc, c.y - Rc, bodyDark, c.x + Rc, c.y + Rc, false));
    g.fillEllipse (Rectangle<float> (Rc * 2, Rc * 2).withCentre (c));
    ring (Rc, 0.5f, 0.22f);

    // gloss highlight across the hub
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.38f), c.x - Rh * 0.45f, c.y - Rh * 0.5f,
                                       Colours::white.withAlpha (0.0f), c.x - Rh * 0.45f + Rh * 0.6f, c.y - Rh * 0.5f, true));
    g.fillPath (hub, rot.followedBy (AffineTransform()).translated (0, 0));

    if (! s.isEnabled()) g.endTransparencyLayer();
}

int KintsugiLookAndFeel::getSliderThumbRadius (juce::Slider& s)
{
    return s.getProperties()["fader"] ? Layout::faderThumb : LookAndFeel_V4::getSliderThumbRadius (s);
}

void KintsugiLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                           float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    using namespace juce;
    if (! s.getProperties()["fader"])
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
        return;
    }

    // Console fader: a long slot cut into the plate and a ridged cap riding in it
    const float cx = (float) x + (float) w * 0.5f;
    const float top = (float) y - (float) Layout::faderThumb + 4.0f;
    const float bottom = (float) (y + h + Layout::faderThumb) - 4.0f;
    const auto slot = Rectangle<float> (cx - 3.0f, top, 6.0f, bottom - top);
    g.setColour (Colours::white.withAlpha (0.14f));               // lit lower/right lip
    g.fillRoundedRectangle (slot.translated (0.8f, 0.8f), 3.0f);
    g.setColour (Colour (0xff050504));
    g.fillRoundedRectangle (slot, 3.0f);
    g.setColour (Colour (0xff2c2a26));                              // the stem visible in the slot
    g.fillRect (slot.reduced (2.2f, 3.0f));

    // Cap: a classic British console fader knob in the same oxblood red as the switches. A solid block
    // with chamfered sides and a concave, dished top that your finger sits in; a cream line across the middle.
    const auto cap = Rectangle<float> (36.0f, 2.0f * Layout::faderThumb).withCentre ({ cx, sliderPos });
    const Colour body (0xff5e1612), bodyDark (0xff2a0705), bodyLight (0xff9b3328);
    for (int i = 1; i <= 6; ++i)
    {
        g.setColour (Colours::black.withAlpha (0.08f));
        g.fillRoundedRectangle (cap.translated (1.2f + i * 0.7f, 2.2f + i * 1.0f).expanded (i * 0.5f), 4.0f);
    }
    // the block, with chamfered sides: left and top faces catch the light, right and bottom fall away
    g.setGradientFill (ColourGradient (bodyLight, cap.getX(), cap.getY(), bodyDark, cap.getRight(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 4.0f);
    const auto dishR = cap.reduced (4.0f, 4.5f);
    {
        Path l, r, t, b;
        l.addQuadrilateral (cap.getX() + 1, cap.getY() + 2, dishR.getX(), dishR.getY(), dishR.getX(), dishR.getBottom(), cap.getX() + 1, cap.getBottom() - 2);
        r.addQuadrilateral (cap.getRight() - 1, cap.getY() + 2, dishR.getRight(), dishR.getY(), dishR.getRight(), dishR.getBottom(), cap.getRight() - 1, cap.getBottom() - 2);
        t.addQuadrilateral (cap.getX() + 2, cap.getY() + 1, cap.getRight() - 2, cap.getY() + 1, dishR.getRight(), dishR.getY(), dishR.getX(), dishR.getY());
        b.addQuadrilateral (cap.getX() + 2, cap.getBottom() - 1, cap.getRight() - 2, cap.getBottom() - 1, dishR.getRight(), dishR.getBottom(), dishR.getX(), dishR.getBottom());
        g.setColour (bodyLight.brighter (0.15f)); g.fillPath (l);
        g.setColour (bodyLight.brighter (0.30f)); g.fillPath (t);
        g.setColour (bodyDark.darker (0.15f));    g.fillPath (r);
        g.setColour (bodyDark.darker (0.35f));    g.fillPath (b);
    }
    // the dished top: its upper half faces away from the light, its lower half towards it
    {
        ColourGradient dish (bodyDark, dishR.getCentreX(), dishR.getY(), bodyLight.brighter (0.25f), dishR.getCentreX(), dishR.getBottom(), false);
        dish.addColour (0.45, body.darker (0.15f));
        g.setGradientFill (dish);
        g.fillRoundedRectangle (dishR, 2.0f);
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.25f), dishR.getX(), dishR.getCentreY(),
                                           Colours::black.withAlpha (0.0f), dishR.getX() + dishR.getWidth() * 0.3f, dishR.getCentreY(), false));
        g.fillRoundedRectangle (dishR, 2.0f);
        // the glossy plastic catches a soft reflection low in the dish
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.28f), dishR.getCentreX() - 4.0f, dishR.getBottom() - 6.0f,
                                           Colours::white.withAlpha (0.0f), dishR.getCentreX() + 8.0f, dishR.getBottom() - 2.0f, true));
        g.fillRoundedRectangle (dishR, 2.0f);
    }
    // the cream indicator line, inlaid across the middle
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRect (cap.getX() + 1.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 2.0f, 2.8f);
    g.setColour (Palette::cream);
    g.fillRect (cap.getX() + 1.0f, cap.getCentreY() - 1.2f, cap.getWidth() - 2.0f, 2.2f);
    // a little wear on the line where fingers have rubbed it
    g.setColour (bodyLight.withAlpha (0.35f));
    g.fillRect (cap.getCentreX() - 3.0f, cap.getCentreY() - 1.2f, 5.0f, 2.2f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (cap, 4.0f, 0.9f);
}


//==============================================================================
// A replacement knob from the repairer's drawer: solid turned brass, knurled skirt, spun top,
// an engraved and black-filled pointer line. Not the original, but chosen to belong.
//==============================================================================
// The Kintsugi knob: the repairer's showpiece. An ivory cap that broke into pieces and was mended
// with gold, seams meeting in Y-junctions, one chipped piece of the rim rebuilt in solid gold, set in
// a knurled brass skirt. The whole repair turns with the knob.
void KintsugiLookAndFeel::drawKintsugiKnob (juce::Graphics& g, int x, int y, int w, int h, float angle, juce::Slider& s)
{
    using namespace juce;
    const auto c = Rectangle<int> (x, y, w, h).toFloat().getCentre();
    const float R = jmin (w, h) * 0.5f * 0.76f;
    const float Rc = R * 0.82f;
    const float lightAngle = -MathConstants<float>::pi * 0.25f;
    auto circle = [c] (float r, Point<float> off = {}) { return Rectangle<float> (r * 2, r * 2).withCentre (c + off); };
    auto polar  = [c] (float r, float a) { return c.getPointOnCircumference (r, a); };
    const Colour bDark (0xff5e4518), bMid (0xffb8913f), bLite (0xfff4dc96);
    const Colour gold (0xffd1a94e), goldLite (0xfff1d98f), goldDark (0xff8a6526);

    if (! s.isEnabled())
        g.beginTransparencyLayer (0.45f);

    for (int i = 0; i < 8; ++i)
    {
        g.setColour (Colours::black.withAlpha (0.07f));
        g.fillEllipse (circle (R + (float) i * 1.0f, { 2.2f, 3.6f }));
    }

    // knurled brass skirt
    g.setGradientFill (ColourGradient (bMid, c.x - R, c.y - R, bDark, c.x + R, c.y + R, false));
    g.fillEllipse (circle (R));
    constexpr int teeth = 84;
    for (int i = 0; i < teeth; ++i)
    {
        const float a = angle + (float) i * MathConstants<float>::twoPi / (float) teeth;
        const float lit = std::pow (0.5f + 0.5f * std::cos (a - lightAngle), 2.0f);
        g.setColour (bLite.withAlpha (jlimit (0.0f, 1.0f, 0.15f + 0.7f * lit)));
        g.drawLine ({ polar (R * 0.87f, a), polar (R * 0.995f, a) }, 1.0f);
        g.setColour (bDark.darker (0.6f).withAlpha (0.55f));
        g.drawLine ({ polar (R * 0.87f, a + 0.037f), polar (R * 0.995f, a + 0.037f) }, 0.7f);
    }
    g.setColour (bDark.darker (0.8f));
    g.drawEllipse (circle (R), 0.9f);
    // polished brass bezel around the cap
    g.setGradientFill (ColourGradient (bLite, c.x - R, c.y - R, bDark, c.x + R, c.y + R, false));
    g.fillEllipse (circle (Rc + 2.2f));
    g.setColour (bDark.darker (0.6f).withAlpha (0.8f));
    g.drawEllipse (circle (Rc + 0.3f), 0.8f);

    // the ivory cap, gently domed and glazed
    g.setGradientFill (ColourGradient (Colour (0xfff7eedb), c.x - Rc * 0.4f, c.y - Rc * 0.45f, Colour (0xffc9b68c), c.x + Rc, c.y + Rc, true));
    g.fillEllipse (circle (Rc));

    {
        Graphics::ScopedSaveState ss (g);
        Path cap;
        cap.addEllipse (circle (Rc));
        g.reduceClipRegion (cap);
        const auto rot = AffineTransform::rotation (angle, c.x, c.y);
        auto P = [c, Rc] (float px, float py) { return Point<float> (c.x + px * Rc, c.y + py * Rc); };

        // the piece that chipped out of the rim, rebuilt in solid gold
        {
            Path chip;
            chip.startNewSubPath (P (-1.15f, 0.20f));
            chip.lineTo (P (-0.78f, 0.27f));
            chip.lineTo (P (-0.66f, 0.47f));
            chip.lineTo (P (-0.74f, 0.70f));
            chip.lineTo (P (-1.10f, 0.78f));
            chip.closeSubPath();
            g.setColour (Colour (0xff4a3712).withAlpha (0.5f));
            g.fillPath (chip, rot.translated (0.3f, 0.4f));
            g.setGradientFill (ColourGradient (goldLite, c.x - Rc, c.y - Rc * 0.2f, goldDark, c.x - Rc * 0.5f, c.y + Rc * 0.8f, false));
            g.fillPath (chip, rot);
            Random rng (11);
            const auto bb = chip.getBounds().transformedBy (rot);
            Graphics::ScopedSaveState s2 (g);
            g.reduceClipRegion (chip, rot);
            for (int i = 0; i < 260; ++i)
            {
                const Point<float> q (bb.getX() + rng.nextFloat() * bb.getWidth(), bb.getY() + rng.nextFloat() * bb.getHeight());
                g.setColour ((rng.nextBool() ? goldLite : goldDark).withAlpha (0.18f + 0.2f * rng.nextFloat()));
                g.fillRect (Rectangle<float> (0.5f, 0.5f).withCentre (q));
            }
        }

        // the fractures, mended with gold: nearly straight lines, two Y-junctions, out to the rim
        const auto J1 = P (-0.10f, -0.02f), J2 = P (0.16f, 0.32f);
        const std::vector<std::vector<Point<float>>> lines {
            { P (-1.15f, -0.18f), P (-0.62f, -0.10f), P (-0.32f, -0.02f), J1 },
            { J1, P (0.14f, -0.38f), P (0.36f, -0.72f), P (0.50f, -1.15f) },
            { J1, P (0.03f, 0.14f), J2 },
            { J2, P (0.55f, 0.36f), P (0.85f, 0.46f), P (1.15f, 0.52f) },
            { J2, P (0.08f, 0.64f), P (-0.10f, 0.85f), P (-0.18f, 1.15f) },
            { P (-0.78f, 0.27f), P (-0.55f, 0.12f), P (-0.32f, -0.02f) },   // from the gold chip back to the main crack
        };
        Random rng (77);
        for (auto& pts : lines)
        {
            std::vector<Point<float>> L, Rr;
            for (size_t i = 0; i + 1 < pts.size(); ++i)
                for (int k = 0; k < 8; ++k)
                {
                    const float t = (float) k / 8.0f;
                    const auto p = pts[i] + (pts[i + 1] - pts[i]) * t;
                    const auto d = pts[i + 1] - pts[i];
                    const float dl = jmax (0.001f, d.getDistanceFromOrigin());
                    const Point<float> n (-d.y / dl, d.x / dl);
                    const float wd = 0.85f + 0.45f * rng.nextFloat();
                    L.push_back (p + n * wd);
                    Rr.push_back (p - n * wd);
                }
            L.push_back (pts.back()); Rr.push_back (pts.back());
            Path seam;
            seam.startNewSubPath (L.front());
            for (auto& p : L) seam.lineTo (p);
            for (auto it = Rr.rbegin(); it != Rr.rend(); ++it) seam.lineTo (*it);
            seam.closeSubPath();
            seam = seam.createPathWithRoundedCorners (0.7f);
            g.setColour (Colour (0xff4a3712).withAlpha (0.45f));
            g.fillPath (seam, rot.translated (0.3f, 0.4f));
            g.setGradientFill (ColourGradient (goldLite, c.x - Rc, c.y - Rc, goldDark, c.x + Rc, c.y + Rc, false));
            g.fillPath (seam, rot);
            g.setColour (Colour (0xfffbecc0).withAlpha (0.55f));
            g.strokePath (seam, PathStrokeType (0.35f), rot.translated (-0.25f, -0.3f));
        }
        // the lacquer pools a little where cracks meet
        for (auto j : { J1, J2 })
        {
            const auto jp = j.transformedBy (rot);
            g.setColour (gold);
            g.fillEllipse (Rectangle<float> (3.4f, 3.4f).withCentre (jp));
        }

        // pointer: an inlaid black line ending in a small gold pip
        g.setColour (Colour (0xff1a1714));
        g.drawLine ({ polar (Rc * 0.28f, angle), polar (Rc * 0.80f, angle) }, 2.6f);
        g.setGradientFill (ColourGradient (goldLite, polar (Rc * 0.9f, angle).x - 1.5f, polar (Rc * 0.9f, angle).y - 1.5f,
                                           goldDark, polar (Rc * 0.9f, angle).x + 2.0f, polar (Rc * 0.9f, angle).y + 2.0f, false));
        g.fillEllipse (Rectangle<float> (4.4f, 4.4f).withCentre (polar (Rc * 0.90f, angle)));

        // glaze: a soft highlight that stays put while the knob turns
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.45f), c.x - Rc * 0.38f, c.y - Rc * 0.42f,
                                           Colours::white.withAlpha (0.0f), c.x - Rc * 0.38f + Rc * 0.55f, c.y - Rc * 0.42f, true));
        g.fillEllipse (circle (Rc));
    }

    if (! s.isEnabled())
        g.endTransparencyLayer();
}

void KintsugiLookAndFeel::drawBrassKnob (juce::Graphics& g, int x, int y, int w, int h, float angle, juce::Slider& s)
{
    using namespace juce;
    const auto c = Rectangle<int> (x, y, w, h).toFloat().getCentre();
    const float R = jmin (w, h) * 0.5f * 0.74f;
    const float lightAngle = -MathConstants<float>::pi * 0.25f;
    auto circle = [c] (float r, Point<float> off = {}) { return Rectangle<float> (r * 2, r * 2).withCentre (c + off); };
    auto polar  = [c] (float r, float a) { return c.getPointOnCircumference (r, a); };
    Random rng (s.getName().hashCode());
    const Colour dark (0xff5e4518), mid (0xffb8913f), lite (0xfff4dc96);

    if (! s.isEnabled())
        g.beginTransparencyLayer (0.45f);

    for (int i = 0; i < 7; ++i)
    {
        g.setColour (Colours::black.withAlpha (0.07f));
        g.fillEllipse (circle (R + (float) i * 0.9f, { 1.8f, 3.0f }));
    }

    // knurled skirt: fine straight knurl, teeth catching the light on the upper-left
    g.setGradientFill (ColourGradient (mid, c.x - R, c.y - R, dark, c.x + R, c.y + R, false));
    g.fillEllipse (circle (R));
    constexpr int teeth = 64;
    for (int i = 0; i < teeth; ++i)
    {
        const float a = angle + (float) i * MathConstants<float>::twoPi / (float) teeth;
        const float lit = std::pow (0.5f + 0.5f * std::cos (a - lightAngle), 2.0f);
        g.setColour (lite.withAlpha (jlimit (0.0f, 1.0f, 0.15f + 0.7f * lit)));
        g.drawLine ({ polar (R * 0.86f, a), polar (R * 0.995f, a) }, 1.0f);
        g.setColour (dark.darker (0.6f).withAlpha (0.55f));
        g.drawLine ({ polar (R * 0.86f, a + 0.045f), polar (R * 0.995f, a + 0.045f) }, 0.7f);
    }
    g.setColour (dark.darker (0.8f));
    g.drawEllipse (circle (R), 0.9f);

    // polished chamfer between skirt and top
    const float Rt = R * 0.84f;
    g.setGradientFill (ColourGradient (lite, c.x - Rt, c.y - Rt, dark, c.x + Rt, c.y + Rt, false));
    g.fillEllipse (circle (Rt));

    // spun top: the lathe reflection forms a bow tie that stays put while the knob turns
    const float Rs = R * 0.76f;
    constexpr int seg = 72;
    for (int i = 0; i < seg; ++i)
    {
        const float a0 = (float) i * MathConstants<float>::twoPi / seg;
        const float a1 = a0 + MathConstants<float>::twoPi / seg * 1.8f;
        const float b = std::pow (0.5f + 0.5f * std::cos (2.0f * (a0 - lightAngle)), 1.5f);
        Path wedge;
        wedge.addPieSegment (circle (Rs), a0, a1, 0.0f);
        g.setColour (Colour (0xff8c6c2c).interpolatedWith (Colour (0xfff7e3a8), b));
        g.fillPath (wedge);
    }
    for (int k = 2; k <= 16; ++k)
    {
        g.setColour ((k % 2 ? Colours::white : Colours::black).withAlpha (0.04f));
        g.drawEllipse (circle (Rs * (float) k / 16.0f), 0.6f);
    }
    // a soft, even patina toward the rim; polished brighter where fingers touch it
    g.setGradientFill (ColourGradient (Colours::transparentBlack, c.x, c.y, Colour (0x55402c0c), c.x + Rs, c.y, true));
    g.fillEllipse (circle (Rs));
    g.setColour (dark.withAlpha (0.8f));
    g.drawEllipse (circle (Rs), 0.7f);
    for (int i = 0; i < 4; ++i)
    {
        const auto p0 = polar (rng.nextFloat() * Rs * 0.8f, rng.nextFloat() * MathConstants<float>::twoPi);
        const auto p1 = p0.getPointOnCircumference (2.0f + rng.nextFloat() * Rs * 0.4f, rng.nextFloat() * MathConstants<float>::twoPi);
        g.setColour ((i % 2 ? Colours::white : dark).withAlpha (0.15f));
        g.drawLine ({ p0, p1 }, 0.45f);
    }

    // engraved pointer line, filled black, with a bright lower lip
    g.setColour (lite.withAlpha (0.7f));
    g.drawLine ({ polar (Rs * 0.25f, angle).translated (0.5f, 0.6f), polar (R * 0.97f, angle).translated (0.5f, 0.6f) }, 1.0f);
    g.setColour (Colour (0xff15110a));
    g.drawLine ({ polar (Rs * 0.25f, angle), polar (R * 0.97f, angle) }, 1.8f);

    if (! s.isEnabled())
        g.endTransparencyLayer();
}

void KintsugiLookAndFeel::drawCreamKnob (juce::Graphics& g, int x, int y, int w, int h, float angle, juce::Slider& s)
{
    // Aged cream plastic knob, seen from above: round skirt with a black dot, seven-sided faceted body,
    // flat top with a black indicator line (the classic "jazz bass" shape)
    using namespace juce;
    const auto c = Rectangle<int> (x, y, w, h).toFloat().getCentre();
    const float R = jmin (w, h) * 0.5f * 0.80f;
    const float lightAngle = -MathConstants<float>::pi * 0.25f;   // light from the upper left
    const Colour creamLight (0xfffaf2dc), creamMid (0xffeadcb6), creamDark (0xffbfa97c);
    auto circle = [c] (float r, Point<float> off = {}) { return Rectangle<float> (r * 2, r * 2).withCentre (c + off); };
    constexpr int sides = 7;
    const float faceStep = MathConstants<float>::twoPi / sides;

    // radius of the softly rounded heptagon at direction a (polygon blended with a circle)
    auto heptRadius = [&] (float r, float a)
    {
        float rel = std::fmod (a - angle, faceStep);
        if (rel < 0) rel += faceStep;
        const float poly = std::cos (faceStep * 0.5f) / std::cos (rel - faceStep * 0.5f);
        return r * (0.72f * poly + 0.28f);
    };
    auto heptPath = [&] (float r)
    {
        Path p;
        constexpr int n = sides * 16;
        for (int i = 0; i <= n; ++i)
        {
            const float a = (float) i / n * MathConstants<float>::twoPi;
            const auto pt = c.getPointOnCircumference (heptRadius (r, a), a);
            i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
        }
        p.closeSubPath();
        return p;
    };

    if (! s.isEnabled()) g.beginTransparencyLayer (0.45f);

    // Shadow
    for (int i = 0; i < 7; ++i)
    {
        g.setColour (Colours::black.withAlpha (0.065f));
        g.fillEllipse (circle (R + (float) i * 0.9f, { 1.8f, 3.0f }));
    }

    // Skirt
    g.setGradientFill (ColourGradient (creamLight, c.x - R, c.y - R, creamDark, c.x + R, c.y + R, false));
    g.fillEllipse (circle (R));
    g.setGradientFill (ColourGradient (Colours::transparentBlack, c.x, c.y, Colour (0x30503c18), c.x + R, c.y, true));
    g.fillEllipse (circle (R));   // a little age towards the rim
    g.setColour (Colour (0xff5a4a2c).withAlpha (0.7f));
    g.drawEllipse (circle (R), 0.9f);
    {
        // glossy plastic: a bright rim where the skirt catches the light, and a soft sheen across it
        Path rim;
        rim.addCentredArc (c.x, c.y, R - 1.2f, R - 1.2f, 0.0f, lightAngle - 1.1f, lightAngle + 1.1f, true);
        g.setColour (Colours::white.withAlpha (0.70f));
        g.strokePath (rim, PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.35f), c.x - R * 0.62f, c.y - R * 0.62f,
                                           Colours::white.withAlpha (0.0f), c.x - R * 0.62f + R * 0.45f, c.y - R * 0.62f, true));
        g.fillEllipse (circle (R));
    }

    // Black position dot on the skirt
    g.setColour (Colour (0xff1a1714));
    g.fillEllipse (Rectangle<float> (3.4f, 3.4f).withCentre (c.getPointOnCircumference (R * 0.905f, angle)));

    // Faceted sides: each of the seven faces is lit by how it faces the light
    const float Ro = R * 0.80f, Ri = R * 0.64f;
    g.setColour (Colours::black.withAlpha (0.30f));
    g.fillPath (heptPath (Ro), AffineTransform::translation (0.8f, 1.6f));
    {
        constexpr int n = sides * 16;
        for (int i = 0; i < n; ++i)
        {
            const float a0 = (float) i / n * MathConstants<float>::twoPi;
            const float a1 = (float) (i + 1) / n * MathConstants<float>::twoPi + 0.02f;   // overlap hides seams
            const float am = 0.5f * (a0 + a1);
            float rel = std::fmod (am - angle, faceStep);
            if (rel < 0) rel += faceStep;
            const float faceNormal = am - rel + faceStep * 0.5f;
            const float lit = 0.5f + 0.5f * (0.75f * std::cos (faceNormal - lightAngle - MathConstants<float>::halfPi)
                                             + 0.25f * std::cos (am - lightAngle - MathConstants<float>::halfPi));
            Path seg;
            seg.startNewSubPath (c.getPointOnCircumference (heptRadius (Ro, a0), a0));
            seg.lineTo (c.getPointOnCircumference (heptRadius (Ro, a1), a1));
            seg.lineTo (c.getPointOnCircumference (heptRadius (Ri, a1), a1));
            seg.lineTo (c.getPointOnCircumference (heptRadius (Ri, a0), a0));
            seg.closeSubPath();
            g.setColour (creamDark.interpolatedWith (creamLight, jlimit (0.0f, 1.0f, lit)));
            g.fillPath (seg);
            const float spec = std::pow (jlimit (0.0f, 1.0f, lit), 8.0f);   // glossy reflection
            if (spec > 0.02f)
            {
                g.setColour (Colours::white.withAlpha (0.75f * spec));
                g.fillPath (seg);
            }
        }
    }
    g.setColour (Colour (0xff5a4a2c).withAlpha (0.6f));
    g.strokePath (heptPath (Ro), PathStrokeType (0.8f));

    // Flat top
    const auto top = heptPath (Ri);
    g.setGradientFill (ColourGradient (creamLight, c.x - Ri, c.y - Ri, creamMid, c.x + Ri, c.y + Ri, false));
    g.fillPath (top);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.22f), c.x - Ri * 0.4f, c.y - Ri * 0.45f,
                                       Colours::white.withAlpha (0.0f), c.x + Ri * 0.2f, c.y + Ri * 0.2f, true));
    g.fillPath (top);
    {
        // crisp window-light reflection on the glossy top
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (top);
        // a soft glow plus a small, sharp hot spot: that contrast is what reads as gloss
        const auto glow = Rectangle<float> (Ri * 1.05f, Ri * 0.70f).withCentre (c.translated (-Ri * 0.28f, -Ri * 0.36f));
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.45f), glow.getCentreX(), glow.getCentreY(),
                                           Colours::white.withAlpha (0.0f), glow.getRight(), glow.getCentreY(), true));
        g.fillEllipse (glow);
        const auto hot = Rectangle<float> (Ri * 0.42f, Ri * 0.22f).withCentre (c.translated (-Ri * 0.36f, -Ri * 0.46f));
        g.setColour (Colours::white.withAlpha (0.92f));
        {
            Path hotSpot;
            hotSpot.addEllipse (hot);
            g.fillPath (hotSpot, AffineTransform::rotation (-0.6f, hot.getCentreX(), hot.getCentreY()));
        }
        g.fillEllipse (Rectangle<float> (Ri * 0.10f, Ri * 0.10f).withCentre (c.translated (-Ri * 0.05f, -Ri * 0.62f)));
        // faint reflected light along the lower right of the top
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.0f), c.x, c.y,
                                           Colours::white.withAlpha (0.18f), c.x + Ri * 0.7f, c.y + Ri * 0.7f, false));
        g.fillPath (top);
    }
    g.setColour (Colour (0xff6a5834).withAlpha (0.45f));
    g.strokePath (top, PathStrokeType (0.6f));
    {
        // bright edge where the top meets the lit faces
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (Rectangle<float> (c.x - Ri * 1.2f, c.y - Ri * 1.2f, Ri * 1.5f, Ri * 1.5f).toNearestInt());
        g.setColour (Colours::white.withAlpha (0.65f));
        g.strokePath (top, PathStrokeType (0.9f));
    }

    // Decades of use: each original knob has yellowed by its own amount, picked up stains, grime in
    // the crevices and a few chips and scratches. All of it turns with the knob.
    {
        Random rng (s.getName().hashCode() * 31 + 7);
        const Colour nicotine (0xff9a7434), grime (0xff3d3018);
        Path whole;
        whole.addEllipse (circle (R));
        g.setColour (nicotine.withAlpha (0.07f + 0.11f * rng.nextFloat()));
        g.fillPath (whole);
        // grime packed into the crevice between skirt and faceted body, and around the top
        g.setColour (grime.withAlpha (0.38f));
        g.strokePath (heptPath (Ro + 0.6f), PathStrokeType (1.5f));
        g.setColour (grime.withAlpha (0.22f));
        g.strokePath (top, PathStrokeType (1.1f));
        // stains: soft brownish blotches
        const int stains = 1 + rng.nextInt (3);
        for (int i = 0; i < stains; ++i)
        {
            const auto o = c.getPointOnCircumference (rng.nextFloat() * R * 0.85f, angle + rng.nextFloat() * MathConstants<float>::twoPi);
            const float r = R * (0.15f + 0.25f * rng.nextFloat());
            g.setGradientFill (ColourGradient (nicotine.withAlpha (0.16f + 0.12f * rng.nextFloat()), o.x, o.y,
                                               nicotine.withAlpha (0.0f), o.x + r, o.y, true));
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (whole);
            g.fillEllipse (Rectangle<float> (r * 2, r * 1.5f).withCentre (o));
        }
        // a chip or two out of the skirt edge
        const int chips = rng.nextInt (3);
        for (int i = 0; i < chips; ++i)
        {
            const float a = angle + rng.nextFloat() * MathConstants<float>::twoPi;
            const float sz = 1.4f + rng.nextFloat() * 1.8f;
            Path chip;
            chip.startNewSubPath (c.getPointOnCircumference (R + 0.6f, a - sz / R));
            chip.lineTo (c.getPointOnCircumference (R - sz * (0.8f + 0.4f * rng.nextFloat()), a + 0.2f * sz / R));
            chip.lineTo (c.getPointOnCircumference (R + 0.6f, a + sz / R));
            chip.closeSubPath();
            g.setColour (Colour (0xffd9c99f));
            g.fillPath (chip);
            g.setColour (grime.withAlpha (0.5f));
            g.strokePath (chip, PathStrokeType (0.4f));
        }
        // scratches across the top and skirt
        for (int i = 0; i < 4 + rng.nextInt (4); ++i)
        {
            const auto p0 = c.getPointOnCircumference (rng.nextFloat() * R * 0.9f, angle + rng.nextFloat() * MathConstants<float>::twoPi);
            const auto p1 = p0.getPointOnCircumference (2.0f + rng.nextFloat() * R * 0.5f, rng.nextFloat() * MathConstants<float>::twoPi);
            g.setColour ((rng.nextBool() ? grime.withAlpha (0.22f) : Colours::white.withAlpha (0.3f)));
            g.drawLine ({ p0, p1 }, 0.4f);
        }
    }

    // Black indicator line on the top, its paint worn thin in places
    Path line;
    line.startNewSubPath (c.getPointOnCircumference (Ri * 0.30f, angle));
    line.lineTo (c.getPointOnCircumference (Ri * 0.88f, angle));
    g.setColour (Colour (0xff1a1714));
    g.strokePath (line, PathStrokeType (2.6f, PathStrokeType::curved, PathStrokeType::rounded));
    {
        Random rng (s.getName().hashCode() + 3);
        for (int i = 0; i < 5; ++i)
        {
            const auto pt = c.getPointOnCircumference (Ri * (0.32f + 0.55f * rng.nextFloat()), angle);
            g.setColour (creamMid.withAlpha (0.35f + 0.3f * rng.nextFloat()));
            g.fillEllipse (Rectangle<float> (1.2f, 1.0f).withCentre (pt.translated (rng.nextFloat() - 0.5f, rng.nextFloat() - 0.5f)));
        }
    }

    if (! s.isEnabled()) g.endTransparencyLayer();
}


//==============================================================================
//==============================================================================
namespace
{
    // Draws everything that is "printed" on the plate into an image, so it can be worn down
    void drawPrint (juce::Graphics& g, const std::vector<std::pair<juce::Rectangle<int>, juce::Slider*>>& knobRects,
                    const std::vector<juce::String>& knobNames, const std::vector<juce::StringArray>& stepLabels,
                    const std::vector<int>& steps)
    {
        using namespace juce;
        g.setColour (Palette::cream);

        for (size_t i = 0; i < knobRects.size(); ++i)
        {
            auto [r, s] = knobRects[i];
            const auto c = r.toFloat().getCentre();
            const float R = (float) r.getWidth() * 0.5f * 0.80f;
            const auto rp = s->getRotaryParameters();
            const int n = steps[i] > 1 ? steps[i] : 11;

            for (int t = 0; t < n; ++t)
            {
                const float a = rp.startAngleRadians + (float) t / (float) (n - 1) * (rp.endAngleRadians - rp.startAngleRadians);
                const bool major = (t == 0 || t == n - 1 || (steps[i] <= 1 && t == n / 2));
                g.drawLine ({ c.getPointOnCircumference (R + 4.0f, a), c.getPointOnCircumference (R + (major ? 10.5f : 8.0f), a) },
                            major ? 1.9f : 1.3f);
            }

            if (const int nl = stepLabels[i].size(); nl >= 2)
            {
                g.setFont (KintsugiFonts::serif (9.5f).withExtraKerningFactor (0.06f));
                for (int e = 0; e < nl; ++e)
                {
                    const float a = rp.startAngleRadians + (float) e / (float) (nl - 1) * (rp.endAngleRadians - rp.startAngleRadians);
                    const auto pt = c.getPointOnCircumference (R + 19.0f, a);
                    g.drawText (stepLabels[i][e], Rectangle<float> (50, 12).withCentre (pt), Justification::centred);
                }
            }

            g.setFont (KintsugiFonts::serif (11.5f).withExtraKerningFactor (0.08f));
            g.drawText (knobNames[i].toUpperCase(), Rectangle<int> (r.getCentreX() - 70, r.getBottom() + 1, 140, 16),
                        Justification::centred);
        }
    }

    void wearPrint (juce::Image& img, int seed, float strength)
    {
        juce::Random rng (seed);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);

        for (int y = 0; y < bd.height; ++y)
            for (int x = 0; x < bd.width; ++x)
            {
                auto px = bd.getPixelColour (x, y);
                if (px.getAlpha() == 0) continue;
                float f = 1.0f - strength * 0.35f * rng.nextFloat();
                // soft, irregular worn-off patches (value noise, so no blocky squares)
                const float b = 0.65f * valueNoise ((float) x * 0.11f, (float) y * 0.11f, seed)
                              + 0.35f * valueNoise ((float) x * 0.37f, (float) y * 0.37f, seed + 1);
                if (b < 0.16f * strength) f *= 0.25f;
                else if (b < 0.24f * strength) f *= 0.65f;
                bd.setPixelColour (x, y, px.withMultipliedAlpha (f));
            }
    }
}


void KintsugiAudioProcessorEditor::PlateArea::paint (juce::Graphics& g)
{
    if (image != nullptr)
        g.drawImage (*image, getLocalBounds().toFloat());
}

void KintsugiAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    // The wooden frame casts a soft shadow onto the plate's top and left edges
    using namespace juce;
    const auto p = plateArea.getBounds().toFloat();
    for (int i = 0; i < 7; ++i)
    {
        const float a = 0.22f * (1.0f - (float) i / 7.0f);
        g.setColour (Colours::black.withAlpha (a));
        g.fillRect (p.getX(), p.getY() + (float) i, p.getWidth(), 1.0f);
        g.fillRect (p.getX() + (float) i, p.getY(), 1.0f, p.getHeight());
    }
    g.setColour (Colours::black.withAlpha (0.12f));
    g.fillRect (p.getX(), p.getBottom() - 1.0f, p.getWidth(), 1.0f);
    g.fillRect (p.getRight() - 1.0f, p.getY(), 1.0f, p.getHeight());
}

namespace
{
    // Satin-finished cherry at a point; 'across' runs across the grain, 'along' runs with it.
    // Fine, straight, dense grain with long streaks and only a gentle colour drift.
    juce::Colour cherry (float across, float along, int seed)
    {
        using namespace juce;
        const float warp   = 6.0f * fbm (across * 0.004f, along * 0.0008f, seed);           // grain is nearly straight
        const float coarse = fbm ((across + warp) * 0.045f, along * 0.0009f, seed + 1);     // wide light/dark streaks
        const float lines  = fbm ((across + warp) * 0.42f, along * 0.0045f, seed + 2);      // dense fine grain
        const float drift  = fbm (across * 0.006f, along * 0.0005f, seed + 3);              // slow colour change along the board
        const float pores  = valueNoise (across * 2.2f, along * 0.13f, seed + 4);
        float t = 0.52f + 0.30f * (coarse - 0.5f) + 0.30f * (lines - 0.5f) + 0.18f * (drift - 0.5f);
        if (pores > 0.9f) t -= 0.10f * (pores - 0.9f) / 0.1f;
        t = jlimit (0.0f, 1.0f, t);
        const Colour dark (0xff2a170d), mid (0xff482a1a), light (0xff66412b);
        return t < 0.5f ? dark.interpolatedWith (mid, t * 2.0f) : mid.interpolatedWith (light, (t - 0.5f) * 2.0f);
    }
}

void KintsugiAudioProcessorEditor::renderHousingCherry()
{
    using namespace juce;
    using namespace Layout;
    const float scale = 2.0f;
    const int tw = W + 2 * sideW, th = H + 2 * railH;
    const int iw = (int) (tw * scale), ih = (int) (th * scale);
    housing = Image (Image::RGB, iw, ih, true);

    const float sx = sideW * scale, ry = railH * scale;
    constexpr float chamferStart = 0.80f;   // last 20 % of each piece is the inner chamfer down to the plate

    {
        Image::BitmapData bd (housing, Image::BitmapData::writeOnly);
        for (int y = 0; y < ih; ++y)
            for (int x = 0; x < iw; ++x)
            {
                // distance into each of the four frame pieces (0 = outer edge, 1 = plate edge)
                const float dl = (float) x / sx, dr = (float) (iw - 1 - x) / sx;
                const float dt = (float) y / ry, db = (float) (ih - 1 - y) / ry;
                const float d = jmin (jmin (dl, dr), jmin (dt, db));
                if (d >= 1.0f) continue;

                // the smallest distance decides the piece -> clean 45-degree mitres
                int piece; // 0 left, 1 right, 2 top, 3 bottom
                if (d == dl) piece = 0; else if (d == dr) piece = 1; else if (d == dt) piece = 2; else piece = 3;

                const bool vertical = piece < 2;
                Colour c = vertical ? cherry ((float) x + piece * 1500.0f, (float) y, 31 + piece)
                                    : cherry ((float) y + piece * 1500.0f, (float) x, 31 + piece);

                // flat satin face, lit evenly from the upper left
                const float faceLight[]    = { 1.00f, 0.95f, 1.03f, 0.93f };
                // the inner chamfer faces the plate: bottom and right chamfers face the light, top and left face away
                const float chamferLight[] = { 0.80f, 1.12f, 0.76f, 1.16f };
                float shade = d < chamferStart ? faceLight[piece] : chamferLight[piece];

                // softly eased outer edge
                shade *= 0.86f + 0.14f * jlimit (0.0f, 1.0f, d / 0.06f);
                // faint satin sheen across the face
                if (d < chamferStart)
                    shade *= 1.0f + 0.035f * std::sin (d / chamferStart * MathConstants<float>::pi);

                bd.setPixelColour (x, y, Colour::fromFloatRGBA (jmin (1.0f, c.getFloatRed() * shade),
                                                                jmin (1.0f, c.getFloatGreen() * shade),
                                                                jmin (1.0f, c.getFloatBlue() * shade), 1.0f));
            }
    }

    Graphics g (housing);
    g.addTransform (AffineTransform::scale (scale));

    const Rectangle<float> pr ((float) sideW, (float) railH, (float) W, (float) H);
    const Rectangle<float> outer (0, 0, (float) tw, (float) th);

    // Crease where the flat face turns into the chamfer
    const auto creaseRect = outer.getProportion (Rectangle<float> (0, 0, 1, 1))
                                 .withTrimmedLeft  ((float) sideW * chamferStart).withTrimmedRight  ((float) sideW * chamferStart)
                                 .withTrimmedTop   ((float) railH * chamferStart).withTrimmedBottom ((float) railH * chamferStart);
    g.setColour (Colour (0xff3a1608).withAlpha (0.35f));
    g.drawRect (creaseRect, 0.8f);

    // Mitre joints: fine lines from each outer corner to the plate corner
    for (auto [o, i] : { std::pair { outer.getTopLeft(), pr.getTopLeft() }, std::pair { outer.getTopRight(), pr.getTopRight() },
                         std::pair { outer.getBottomLeft(), pr.getBottomLeft() }, std::pair { outer.getBottomRight(), pr.getBottomRight() } })
    {
        g.setColour (Colour (0xff3a1608).withAlpha (0.45f));
        g.drawLine ({ o, i }, 0.7f);
    }

    // Outer edge and the dark seam where the wood meets the plate
    g.setColour (Colour (0xff2a1006).withAlpha (0.7f));
    g.drawRect (outer, 1.2f);
    g.setColour (Colour (0xff0d0603));
    g.drawRect (pr.expanded (1.2f), 1.8f);
}

namespace
{
    // Cellular noise: distance to the nearest of a scatter of points (pebble grain, hammered dimples)
    float worley (float x, float y, int seed)
    {
        const int xi = (int) std::floor (x), yi = (int) std::floor (y);
        float best = 9.0f;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int cx = xi + dx, cy = yi + dy;
                uint32_t h = (uint32_t) (cx * 374761393 + cy * 668265263 + seed * 1442695041);
                h = (h ^ (h >> 13)) * 1274126177u;
                h ^= h >> 16;
                const float px = (float) cx + (float) (h & 0xffff) / 65536.0f;
                const float py = (float) cy + (float) ((h >> 16) & 0xffff) / 65536.0f;
                best = std::min (best, (px - x) * (px - x) + (py - y) * (py - y));
            }
        return std::sqrt (best);
    }

    juce::Colour woodAt (float across, float along, int seed, juce::Colour dark, juce::Colour mid, juce::Colour light, float figure)
    {
        using namespace juce;
        const float warp   = (6.0f + 30.0f * figure) * fbm (across * 0.004f, along * 0.0012f, seed);
        const float coarse = fbm ((across + warp) * 0.04f, along * 0.0011f, seed + 1);
        const float lines  = fbm ((across + warp) * 0.40f, along * 0.005f, seed + 2);
        const float drift  = fbm (across * 0.006f, along * 0.0006f, seed + 3);
        const float pores  = valueNoise (across * 2.4f, along * 0.12f, seed + 4);
        float t = 0.50f + 0.34f * (coarse - 0.5f) + 0.28f * (lines - 0.5f) + 0.2f * (drift - 0.5f);
        if (pores > 0.88f) t -= 0.14f * (pores - 0.88f) / 0.12f;
        t = jlimit (0.0f, 1.0f, t);
        return t < 0.5f ? dark.interpolatedWith (mid, t * 2.0f) : mid.interpolatedWith (light, (t - 0.5f) * 2.0f);
    }

    // Height of a frame's cross-section at a (0 = outer edge, 1 = where it meets the plate)
    float caseProfile (int style, bool cheek, float a)
    {
        auto roundOver = [] (float x, float r) { const float u = 1.0f - x / r; return r * std::sqrt (std::max (0.0f, 1.0f - u * u)); };
        auto smooth = [] (float e0, float e1, float x) { const float t = juce::jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0)); return t * t * (3.0f - 2.0f * t); };
        switch (style)
        {
            case 1:   // walnut: rounded outer edge, flat, a fine bead, then a cove down to the plate
            {
                float h = a < 0.22f ? roundOver (a, 0.22f) : 0.22f;
                h += 0.025f * std::exp (-std::pow ((a - 0.72f) / 0.025f, 2.0f));
                h -= 0.15f * smooth (0.78f, 1.0f, a);
                return h;
            }
            case 2:
                if (cheek)   // teak cheek: generously rounded both sides
                    return 0.30f * std::pow (std::max (0.0f, 1.0f - std::pow (2.0f * a - 1.0f, 2.0f)), 0.35f);
                return a < 0.10f ? roundOver (a, 0.10f) : 0.10f - 0.08f * smooth (0.93f, 1.0f, a);   // steel rail
            case 3:   // tolex: big soft roundover, flat, then the piping cord
            {
                float h = a < 0.34f ? roundOver (a, 0.34f) : 0.34f;
                const float pc = 0.90f, pr = 0.055f;
                if (std::abs (a - pc) < pr)
                    h += 0.06f * std::sqrt (1.0f - std::pow ((a - pc) / pr, 2.0f));
                if (a > pc + pr) h -= 0.05f;
                return h;
            }
            default:  // hammertone steel: folded sheet with a tight outer radius and a sharp fold down to the plate
                return (a < 0.12f ? roundOver (a, 0.12f) : 0.12f) - 0.10f * smooth (0.90f, 0.97f, a);
        }
    }
}

void KintsugiAudioProcessorEditor::renderHousing()
{
    if (caseStyle == 0) renderHousingCherry();
    else if (caseStyle == 5) renderHousingCeramic();
    else renderHousingStyled (caseStyle);
}

void KintsugiAudioProcessorEditor::renderHousingStyled (int style)
{
    using namespace juce;
    using namespace Layout;
    const float scale = 2.0f;
    const int tw = W + 2 * sideW, th = H + 2 * railH;
    const int iw = (int) (tw * scale), ih = (int) (th * scale);
    housing = Image (Image::RGB, iw, ih, true);
    const float sx = sideW * scale, ry = railH * scale;

    // light from the upper left and a little in front, as for every part of the panel
    const float Lx = -0.48f, Ly = -0.56f, Lz = 0.68f;
    const float hx = Lx, hy = Ly, hz = Lz + 1.0f, hl = std::sqrt (hx * hx + hy * hy + hz * hz);

    {
        Image::BitmapData bd (housing, Image::BitmapData::writeOnly);
        for (int y = 0; y < ih; ++y)
            for (int x = 0; x < iw; ++x)
            {
                const float dl = (float) x / sx, dr = (float) (iw - 1 - x) / sx;
                const float dt = (float) y / ry, db = (float) (ih - 1 - y) / ry;
                const float d = jmin (jmin (dl, dr), jmin (dt, db));
                if (d >= 1.0f) continue;

                int piece;   // 0 left, 1 right, 2 top, 3 bottom
                if (style == 2 && (dl < 1.0f || dr < 1.0f)) piece = dl < dr ? 0 : 1;   // the cheeks run full height
                else if (d == dl) piece = 0; else if (d == dr) piece = 1; else if (d == dt) piece = 2; else piece = 3;
                const float a = piece == 0 ? dl : piece == 1 ? dr : piece == 2 ? dt : db;
                const bool cheek = style == 2 && piece < 2;
                const float dirX = piece == 0 ? 1.0f : piece == 1 ? -1.0f : 0.0f;
                const float dirY = piece == 2 ? 1.0f : piece == 3 ? -1.0f : 0.0f;
                const float width = piece < 2 ? sx : ry;

                // surface normal from the profile's slope (in pixels of height per pixel across)
                const float da = 0.004f;
                const float slope = (caseProfile (style, cheek, jmin (1.0f, a + da)) - caseProfile (style, cheek, jmax (0.0f, a - da))) / (2.0f * da);
                float nx = -slope * dirX, ny = -slope * dirY;

                // material colour, texture bumps, sheen
                Colour base;
                float specStrength = 0.06f, shininess = 8.0f;
                const float fx = (float) x, fy = (float) y;
                const bool vertical = piece < 2;
                if (style == 1)
                {
                    base = vertical ? woodAt (fx + piece * 1500.0f, fy, 41 + piece, Colour (0xff1c110a), Colour (0xff3b2415), Colour (0xff6b4a2e), 0.6f)
                                    : woodAt (fy + piece * 1500.0f, fx, 41 + piece, Colour (0xff1c110a), Colour (0xff3b2415), Colour (0xff6b4a2e), 0.6f);
                    specStrength = 0.10f; shininess = 10.0f;
                }
                else if (style == 2)
                {
                    if (cheek)
                    {
                        base = woodAt (fx + piece * 1500.0f, fy, 51 + piece, Colour (0xff4a2d14), Colour (0xff7d4f26), Colour (0xffae7a42), 0.25f);
                        specStrength = 0.07f; shininess = 9.0f;
                    }
                    else
                    {
                        const float grain = valueNoise (fx * 0.9f, fy * 0.9f, 61) - 0.5f;
                        base = Colour (0xff17181a).interpolatedWith (Colour (0xff222325), grain + 0.5f);
                        nx += 0.06f * grain; ny += 0.06f * (valueNoise (fx * 0.9f + 7.0f, fy * 0.9f, 62) - 0.5f);
                        specStrength = 0.10f; shininess = 14.0f;
                    }
                }
                else if (style == 3)
                {
                    const float pc = 0.90f, pr = 0.055f;
                    if (std::abs (a - pc) < pr)
                    {
                        base = Colour (0xffe6d7b0);   // cream piping, like the knobs
                        specStrength = 0.18f; shininess = 16.0f;
                    }
                    else if (a > pc + pr) base = Colour (0xff0b0a09);
                    else
                    {
                        // pebble grain: small domes, each catching the light on its upper left
                        const float f = 0.38f;
                        const float hC = worley (fx * f, fy * f, 71), hX = worley ((fx + 1.0f) * f, fy * f, 71), hY = worley (fx * f, (fy + 1.0f) * f, 71);
                        nx += jlimit (-0.6f, 0.6f, (hX - hC) * 1.0f); ny += jlimit (-0.6f, 0.6f, (hY - hC) * 1.0f);
                        base = Colour (0xff141210).interpolatedWith (Colour (0xff2c2824), jlimit (0.0f, 1.0f, 1.0f - hC));
                        specStrength = 0.16f; shininess = 11.0f;
                    }
                }
                else
                {
                    // hammertone: shallow hammered dimples under a metallic grey-green paint
                    const float f = 0.075f;
                    const float hC = worley (fx * f, fy * f, 81), hX = worley ((fx + 1.0f) * f, fy * f, 81), hY = worley (fx * f, (fy + 1.0f) * f, 81);
                    nx -= jlimit (-0.5f, 0.5f, (hX - hC) * 1.2f); ny -= jlimit (-0.5f, 0.5f, (hY - hC) * 1.2f);
                    const float flake = valueNoise (fx * 1.7f, fy * 1.7f, 82);
                    base = Colour (0xff4c5754).interpolatedWith (Colour (0xff626e6a), flake);
                    specStrength = 0.24f + 0.12f * (flake > 0.85f ? 1.0f : 0.0f); shininess = 22.0f;
                    if (a < 0.05f) base = base.interpolatedWith (Colour (0xff9aa09c), 0.35f * (1.0f - a / 0.05f));   // edges worn to bright steel
                }

                const float nl = std::sqrt (nx * nx + ny * ny + 1.0f);
                const float ndl = jmax (0.0f, (nx * Lx + ny * Ly + Lz) / nl);
                const float shade = 0.42f + 0.58f * ndl / Lz;
                const float ndh = jmax (0.0f, (nx * hx + ny * hy + hz) / (nl * hl));
                const float spec = specStrength * std::pow (ndh, shininess);

                // a little edge wear on the outer corners of the frame
                const float r = jlimit (0.0f, 1.0f, base.getFloatRed() * shade + spec);
                const float gg = jlimit (0.0f, 1.0f, base.getFloatGreen() * shade + spec * 0.97f);
                const float b = jlimit (0.0f, 1.0f, base.getFloatBlue() * shade + spec * 0.9f);
                bd.setPixelColour (x, y, Colour::fromFloatRGBA (r, gg, b, 1.0f));
            }
    }

    Graphics g (housing);
    g.addTransform (AffineTransform::scale (scale));
    const Rectangle<float> pr ((float) sideW, (float) railH, (float) W, (float) H);
    const Rectangle<float> outer (0, 0, (float) tw, (float) th);

    if (style == 1)
    {
        // fine mitre joints
        for (auto [o, i] : { std::pair { outer.getTopLeft(), pr.getTopLeft() }, std::pair { outer.getTopRight(), pr.getTopRight() },
                             std::pair { outer.getBottomLeft(), pr.getBottomLeft() }, std::pair { outer.getBottomRight(), pr.getBottomRight() } })
        {
            g.setColour (Colour (0xff120a05).withAlpha (0.55f));
            g.drawLine ({ o, i }, 0.6f);
        }
    }
    else if (style == 2)
    {
        // joints between rails and cheeks, and the screws holding the rails
        for (float xj : { (float) sideW, (float) (tw - sideW) })
        {
            g.setColour (Colour (0xff050505).withAlpha (0.8f));
            g.drawLine (xj, 0.0f, xj, (float) railH, 0.9f);
            g.drawLine (xj, (float) (th - railH), xj, (float) th, 0.9f);
        }
        for (float yc : { (float) railH * 0.5f, (float) th - (float) railH * 0.5f })
            for (float xc : { (float) sideW + 30.0f, (float) tw * 0.5f, (float) (tw - sideW) - 30.0f })
            {
                const Point<float> c (xc, yc);
                g.setColour (Colours::black.withAlpha (0.5f));
                g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (c.translated (0.4f, 0.7f)));
                g.setGradientFill (ColourGradient (Colour (0xffd9dad6), c.x - 3, c.y - 3, Colour (0xff4b4d4a), c.x + 4, c.y + 4, false));
                g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre (c));
                g.setColour (Colour (0xff222222));
                g.drawLine ({ c.translated (-2.6f, 0.0f), c.translated (2.6f, 0.0f) }, 1.0f);
                g.drawLine ({ c.translated (0.0f, -2.6f), c.translated (0.0f, 2.6f) }, 1.0f);
            }
    }
    else if (style == 3)
    {
        // chrome corner protectors, riveted
        for (int k = 0; k < 4; ++k)
        {
            const bool right = k == 1 || k == 3, bottom = k >= 2;
            const float s = 38.0f;
            Path cap;
            cap.startNewSubPath (0, 0);
            cap.lineTo (s, 0);
            cap.quadraticTo (s * 0.92f, s * 0.42f, s * 0.42f, s * 0.42f);
            cap.quadraticTo (s * 0.42f, s * 0.92f, 0, s);
            cap.closeSubPath();
            const auto xf = AffineTransform::scale (right ? -1.0f : 1.0f, bottom ? -1.0f : 1.0f)
                                .translated (right ? (float) tw : 0.0f, bottom ? (float) th : 0.0f);
            g.setColour (Colours::black.withAlpha (0.45f));
            g.fillPath (cap, xf.translated (1.2f, 2.0f));
            const auto cb = cap.getBounds().transformedBy (xf);
            g.setGradientFill (ColourGradient (Colour (0xfff4f5f2), cb.getX(), cb.getY(), Colour (0xff3d3f3c), cb.getRight(), cb.getBottom(), false));
            g.fillPath (cap, xf);
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.55f), cb.getCentreX() - 6, cb.getCentreY() - 8,
                                               Colours::white.withAlpha (0.0f), cb.getCentreX() + 4, cb.getCentreY() + 2, false));
            g.fillPath (cap, xf);
            g.setColour (Colour (0xff1e1f1d).withAlpha (0.8f));
            g.strokePath (cap, PathStrokeType (0.7f), xf);
            for (auto rp : { Point<float> (s * 0.22f, s * 0.22f), Point<float> (s * 0.66f, s * 0.13f), Point<float> (s * 0.13f, s * 0.66f) })
            {
                const auto c = rp.transformedBy (xf);
                g.setGradientFill (ColourGradient (Colours::white, c.x - 1.5f, c.y - 1.5f, Colour (0xff505250), c.x + 2.0f, c.y + 2.0f, false));
                g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (c));
            }
        }
    }
    else
    {
        // hammertone: four screws in the corners of the folded frame
        for (auto c : { Point<float> (sideW * 0.45f, railH * 0.45f), Point<float> ((float) tw - sideW * 0.45f, railH * 0.45f),
                        Point<float> (sideW * 0.45f, (float) th - railH * 0.45f), Point<float> ((float) tw - sideW * 0.45f, (float) th - railH * 0.45f) })
        {
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre (c.translated (0.5f, 0.8f)));
            g.setGradientFill (ColourGradient (Colour (0xffe2e3df), c.x - 4, c.y - 4, Colour (0xff4c4e4a), c.x + 4, c.y + 4, false));
            g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (c));
            g.setColour (Colour (0xff222222));
            g.drawLine ({ c.translated (-3.0f, 0.0f), c.translated (3.0f, 0.0f) }, 1.1f);
            g.drawLine ({ c.translated (0.0f, -3.0f), c.translated (0.0f, 3.0f) }, 1.1f);
        }
    }

    // the dark seam where the case meets the plate
    g.setColour (Colour (0xff070605));
    g.drawRect (pr.expanded (1.2f), 1.8f);
}

void KintsugiAudioProcessorEditor::renderHousingCeramic()
{
    // A glazed stoneware case, cast in one piece with soft corners. It broke, and was mended
    // the kintsugi way: the fractures filled with gold, one lost corner chip rebuilt in solid gold.
    using namespace juce;
    using namespace Layout;
    const float scale = 2.0f;
    const int tw = W + 2 * sideW, th = H + 2 * railH;
    const int iw = (int) (tw * scale), ih = (int) (th * scale);
    housing = Image (Image::RGB, iw, ih, true);
    const float fw = (float) sideW * scale;              // frame width in pixels
    const float rc = 22.0f * scale;                      // outer corner radius
    const float cx = iw * 0.5f, cy = ih * 0.5f, hx = iw * 0.5f, hy = ih * 0.5f;
    const float Lx = -0.48f, Ly = -0.56f, Lz = 0.68f;
    const float Hx = Lx, Hy = Ly, Hz = Lz + 1.0f, Hl = std::sqrt (Hx * Hx + Hy * Hy + Hz * Hz);

    auto profile = [] (float a)
    {
        // thick, softly rounded body; the glaze dips into a shallow cove where it meets the plate
        const float r = 0.42f;
        const float u = 1.0f - jmin (a, r) / r;
        float h = r * std::sqrt (jmax (0.0f, 1.0f - u * u));
        const float t = jlimit (0.0f, 1.0f, (a - 0.82f) / 0.18f);
        return h - 0.12f * t * t * (3.0f - 2.0f * t);
    };

    {
        Image::BitmapData bd (housing, Image::BitmapData::writeOnly);
        for (int y = 0; y < ih; ++y)
            for (int x = 0; x < iw; ++x)
            {
                // distance in from the outer rounded rectangle, and the inward direction
                const float px = (float) x + 0.5f - cx, py = (float) y + 0.5f - cy;
                const float qx = std::abs (px) - (hx - rc), qy = std::abs (py) - (hy - rc);
                float dist, dx, dy;
                if (qx > 0.0f && qy > 0.0f)
                {
                    const float l = std::sqrt (qx * qx + qy * qy);
                    dist = rc - l;
                    dx = -(qx / jmax (0.001f, l)) * (px < 0 ? -1.0f : 1.0f);
                    dy = -(qy / jmax (0.001f, l)) * (py < 0 ? -1.0f : 1.0f);
                }
                else if (qx > qy) { dist = rc - qx; dx = px < 0 ? 1.0f : -1.0f; dy = 0.0f; }
                else              { dist = rc - qy; dx = 0.0f; dy = py < 0 ? 1.0f : -1.0f; }

                if (dist < 0.0f) { bd.setPixelColour (x, y, Colour (0xff0e0f10)); continue; }   // outside the rounded corner
                // inside the plate opening?
                if (std::abs (px) < hx - fw && std::abs (py) < hy - fw) continue;
                const float a = jlimit (0.0f, 1.0f, dist / fw);

                const float da = 0.004f;
                const float slope = (profile (jmin (1.0f, a + da)) - profile (jmax (0.0f, a - da))) / (2.0f * da);
                float nx = -slope * dx, ny = -slope * dy;
                // the glaze is never perfectly flat: a gentle, slow ripple
                nx += 0.05f * (fbm ((float) x * 0.02f, (float) y * 0.02f, 91) - 0.5f);
                ny += 0.05f * (fbm ((float) x * 0.02f + 9.0f, (float) y * 0.02f, 92) - 0.5f);

                // speckled blue-grey stoneware glaze; it pools darker in the cove and breaks pale over the edges
                // deep green-blue stoneware: layered mottling like stone, fine mineral grain, speckles
                const float fxp = (float) x, fyp = (float) y;
                const float broad = fbm (fxp * 0.006f, fyp * 0.006f, 93);
                const float mottle = fbm (fxp * 0.035f, fyp * 0.035f, 95);
                const float grain = valueNoise (fxp * 0.9f, fyp * 0.9f, 96);
                const float vein = std::abs (fbm (fxp * 0.012f + 30.0f, fyp * 0.02f, 97) - 0.5f);
                float tone = 0.45f * broad + 0.35f * mottle + 0.12f * grain;
                Colour base = Colour (0xff061214).interpolatedWith (Colour (0xff17323a), jlimit (0.0f, 1.0f, tone));
                base = base.interpolatedWith (Colour (0xff22393d), 0.25f * jlimit (0.0f, 1.0f, 1.0f - vein * 8.0f));   // soft cloudy depth, no hard lines
                const float pool = jlimit (0.0f, 1.0f, (a - 0.80f) / 0.2f);
                base = base.interpolatedWith (Colour (0xff030a0b), 0.55f * pool);
                const float brk = jlimit (0.0f, 1.0f, (std::abs (slope) - 0.6f) * 0.5f) * (a < 0.5f ? 1.0f : 0.0f);
                base = base.interpolatedWith (Colour (0xff6b6a5c), 0.35f * brk);   // the clay body shows faintly through thin glaze
                const float sp = valueNoise (fxp * 0.55f, fyp * 0.55f, 94);
                if (sp > 0.965f) base = base.interpolatedWith (Colour (0xff060c0c), 0.45f);         // sparse, soft iron speckles
                else if (sp < 0.02f) base = base.interpolatedWith (Colour (0xff5f7b78), 0.3f);     // the odd pale fleck
                // rough, stony surface: fine grit plus a coarser pitted texture
                const float pit = fbm (fxp * 0.18f, fyp * 0.18f, 99);
                nx += 0.22f * (grain - 0.5f) + 0.35f * (fbm (fxp * 0.18f + 0.6f, fyp * 0.18f, 99) - pit);
                ny += 0.22f * (valueNoise (fxp * 0.9f + 3.0f, fyp * 0.9f, 98) - 0.5f) + 0.35f * (fbm (fxp * 0.18f, fyp * 0.18f + 0.6f, 99) - pit);
                if (valueNoise (fxp * 0.3f, fyp * 0.3f, 100) > 0.94f) base = base.darker (0.5f);   // tiny pinholes in the glaze

                const float nl = std::sqrt (nx * nx + ny * ny + 1.0f);
                const float ndl = jmax (0.0f, (nx * Lx + ny * Ly + Lz) / nl);
                const float shade = 0.45f + 0.55f * ndl / Lz;
                const float ndh = jmax (0.0f, (nx * Hx + ny * Hy + Hz) / (nl * Hl));
                const float spec = 0.18f * std::pow (ndh, 30.0f) + 0.05f * std::pow (ndh, 4.0f);   // a rougher, more matte glaze
                bd.setPixelColour (x, y, Colour::fromFloatRGBA (jlimit (0.0f, 1.0f, base.getFloatRed() * shade + spec),
                                                                jlimit (0.0f, 1.0f, base.getFloatGreen() * shade + spec),
                                                                jlimit (0.0f, 1.0f, base.getFloatBlue() * shade + spec * 0.95f), 1.0f));
            }
    }

    Graphics g (housing);
    g.addTransform (AffineTransform::scale (scale));
    const float W_ = (float) tw, H_ = (float) th, f = (float) sideW;
    const Rectangle<float> pr (f, (float) railH, (float) W, (float) H);

    // clip all gold work to the frame itself
    Path frame;
    frame.addRoundedRectangle (0, 0, W_, H_, 22.0f);
    Path hole;
    hole.addRectangle (pr);
    frame.addPath (hole);
    frame.setUsingNonZeroWinding (false);
    {
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (frame);

    const Colour goldLite (0xfff1d98f), gold (0xffd1a94e), goldDark (0xff8a6526);
    // A real ceramic repair: the gold is never even. It thickens where small chips came away, thins to
    // almost nothing in places, and its edges are ragged, each side on its own. Hairlines taper out.
    auto seam = [&] (std::vector<Point<float>> pts, float width, int seed, bool taperEnd = false)
    {
        std::vector<Point<float>> c;
        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            const float l = pts[i].getDistanceFrom (pts[i + 1]);
            const int n = jmax (2, (int) (l * 2.0f));
            for (int k = 0; k < n; ++k) c.push_back (pts[i] + (pts[i + 1] - pts[i]) * ((float) k / (float) n));
        }
        c.push_back (pts.back());
        std::vector<Point<float>> L (c.size()), R (c.size());
        float dist = 0.0f, total = 0.0f;
        for (size_t i = 1; i < c.size(); ++i) total += c[i].getDistanceFrom (c[i - 1]);
        for (size_t i = 0; i < c.size(); ++i)
        {
            if (i > 0) dist += c[i].getDistanceFrom (c[i - 1]);
            const auto d = c[jmin (i + 2, c.size() - 1)] - c[i >= 2 ? i - 2 : 0];
            const float dl = jmax (0.001f, d.getDistanceFromOrigin());
            const Point<float> n (-d.y / dl, d.x / dl);
            float w = width * (0.35f + 1.1f * fbm (dist * 0.05f, 0.5f, seed));       // thick and thin
            if (taperEnd) w *= jlimit (0.15f, 1.0f, (total - dist) / 30.0f);
            const float chipL = jmax (0.0f, valueNoise (dist * 0.09f, 1.5f, seed + 3) - 0.7f) * width * 3.5f;   // filled chips,
            const float chipR = jmax (0.0f, valueNoise (dist * 0.09f, 2.5f, seed + 4) - 0.7f) * width * 3.5f;   // one side at a time
            const float rl = (valueNoise (dist * 0.8f, 3.5f, seed + 5) - 0.5f) * 0.9f;                         // ragged edges
            const float rr = (valueNoise (dist * 0.8f, 4.5f, seed + 6) - 0.5f) * 0.9f;
            L[i] = c[i] + n * jmax (0.15f, w * 0.5f + chipL + rl);
            R[i] = c[i] - n * jmax (0.15f, w * 0.5f + chipR + rr);
        }
        Path p;
        p.startNewSubPath (L.front());
        for (auto& q : L) p.lineTo (q);
        for (auto it = R.rbegin(); it != R.rend(); ++it) p.lineTo (*it);
        p.closeSubPath();
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillPath (p, AffineTransform::translation (0.45f, 0.75f));
        const auto b = p.getBounds();
        g.setGradientFill (ColourGradient (goldLite, b.getX(), b.getY(), goldDark, b.getRight(), b.getBottom(), false));
        g.fillPath (p);
        {
            // satin gold powder: uneven, a little grainy
            Graphics::ScopedSaveState s2 (g);
            g.reduceClipRegion (p);
            Random rng (seed + 50);
            for (int i = 0; i < (int) (total * width * 1.5f); ++i)
            {
                const Point<float> q (b.getX() + rng.nextFloat() * b.getWidth(), b.getY() + rng.nextFloat() * b.getHeight());
                g.setColour ((rng.nextBool() ? goldLite : goldDark).withAlpha (0.15f + 0.25f * rng.nextFloat()));
                g.fillRect (Rectangle<float> (0.5f, 0.5f).withCentre (q));
            }
        }
        g.setColour (Colour (0xfffbecc0).withAlpha (0.5f));
        g.strokePath (p, PathStrokeType (0.4f), AffineTransform::translation (-0.3f, -0.35f));
        g.setColour (goldDark.darker (0.5f).withAlpha (0.6f));
        g.strokePath (p, PathStrokeType (0.35f));
    };

    // Each break is its own: a Y on the top, a long run along the left, a short jagged break with a
    // dying hairline on the right, and a split on the bottom.
    const Point<float> j1 { W_ * 0.305f, 21.0f };
    seam ({ { W_ * 0.29f, -2.0f }, { W_ * 0.30f, 11.0f }, j1 }, 3.6f, 1);
    seam ({ j1, { W_ * 0.296f, 34.0f }, { W_ * 0.302f, f + 1.5f } }, 3.0f, 2);
    seam ({ j1, { W_ * 0.335f, 30.0f }, { W_ * 0.372f, 38.0f }, { W_ * 0.395f, f + 1.5f } }, 2.6f, 3);

    const Point<float> jl { 20.0f, H_ * 0.545f };
    seam ({ { -2.0f, H_ * 0.40f }, { 7.0f, H_ * 0.415f }, { 11.0f, H_ * 0.45f }, { 16.0f, H_ * 0.47f }, { 15.0f, H_ * 0.51f },
            jl, { 27.0f, H_ * 0.585f }, { 25.0f, H_ * 0.62f }, { 33.0f, H_ * 0.66f }, { f + 1.5f, H_ * 0.705f } }, 4.4f, 4);
    seam ({ jl, { 9.0f, H_ * 0.575f }, { 6.0f, H_ * 0.61f }, { -2.0f, H_ * 0.635f } }, 2.6f, 10);                // an extra strand out to the edge
    seam ({ { 27.0f, H_ * 0.585f }, { 34.0f, H_ * 0.575f }, { 39.0f, H_ * 0.585f } }, 1.4f, 11, true);          // and a short hairline
    g.setColour (gold);
    g.fillEllipse (Rectangle<float> (4.4f, 3.6f).withCentre (jl));

    const Point<float> jr { W_ - 20.0f, H_ * 0.315f };
    seam ({ { W_ + 2.0f, H_ * 0.29f }, { W_ - 9.0f, H_ * 0.30f }, jr, { W_ - 27.0f, H_ * 0.285f }, { W_ - 35.0f, H_ * 0.30f },
            { W_ - f - 1.5f, H_ * 0.292f } }, 3.2f, 5);
    seam ({ jr, { W_ - 24.0f, H_ * 0.35f }, { W_ - 27.0f, H_ * 0.38f } }, 1.8f, 7, true);   // hairline running out

    const Point<float> jb { W_ * 0.588f, H_ - 22.0f };
    seam ({ { W_ * 0.60f, H_ + 2.0f }, { W_ * 0.594f, H_ - 11.0f }, jb }, 4.0f, 6);
    seam ({ jb, { W_ * 0.582f, H_ - 34.0f }, { W_ * 0.577f, H_ - f - 1.5f } }, 3.0f, 8);
    seam ({ jb, { W_ * 0.605f, H_ - 31.0f }, { W_ * 0.618f, H_ - 40.0f }, { W_ * 0.624f, H_ - f - 1.5f } }, 2.4f, 9);

    for (auto j : { j1, jr, jb })   // lacquer pooled where cracks meet
    {
        g.setColour (gold);
        g.fillEllipse (Rectangle<float> (4.6f, 3.8f).withCentre (j));
    }

    // The bottom-right corner: a shard that broke off and was glued back with gold, a hairline branching
    // off it, and two small chips filled with gold, one on the outer rim, one along the crack.
    {
        seam ({ { W_ - 58.0f, H_ + 3.0f }, { W_ - 52.0f, H_ - 12.0f }, { W_ - 41.0f, H_ - 21.0f }, { W_ - 33.0f, H_ - 34.0f },
                { W_ - 19.0f, H_ - 40.0f }, { W_ - 10.0f, H_ - 52.0f }, { W_ + 3.0f, H_ - 57.0f } }, 3.4f, 31);
        seam ({ { W_ - 41.0f, H_ - 21.0f }, { W_ - 30.0f, H_ - 15.0f }, { W_ - 21.0f, H_ - 13.0f }, { W_ - 14.0f, H_ - 6.0f } }, 1.6f, 32, true);
        g.setColour (gold);
        g.fillEllipse (Rectangle<float> (4.0f, 3.4f).withCentre ({ W_ - 41.0f, H_ - 21.0f }));

        // small chips, filled with gold: irregular, angular, a little proud of the glaze
        auto chipAt = [&] (Point<float> c, float r, int seed)
        {
            Random rng (seed);
            Path p;
            const int n = 7;
            for (int k = 0; k < n; ++k)
            {
                const float a = (float) k / (float) n * MathConstants<float>::twoPi + (rng.nextFloat() - 0.5f) * 0.5f;
                const auto q = c.getPointOnCircumference (r * (0.55f + 0.6f * rng.nextFloat()), a);
                k == 0 ? p.startNewSubPath (q) : p.lineTo (q);
            }
            p.closeSubPath();
            g.setColour (Colours::black.withAlpha (0.45f));
            g.fillPath (p, AffineTransform::translation (0.4f, 0.7f));
            const auto b = p.getBounds();
            g.setGradientFill (ColourGradient (goldLite, b.getX(), b.getY(), goldDark, b.getRight(), b.getBottom(), false));
            g.fillPath (p);
            g.setColour (goldLite.withAlpha (0.6f));
            g.strokePath (p, PathStrokeType (0.5f), AffineTransform::translation (0.35f, 0.45f));
            g.setColour (goldDark.darker (0.5f).withAlpha (0.6f));
            g.strokePath (p, PathStrokeType (0.35f));
        };
        chipAt ({ W_ - 3.0f, H_ - 26.0f }, 5.5f, 41);    // a bite out of the outer rim
        chipAt ({ W_ - 33.0f, H_ - 33.0f }, 3.6f, 42);   // a flake lost along the crack
    }
    }   // end of clipped gold work

    // the dark seam where the case meets the plate
    g.setColour (Colour (0xff070605));
    g.drawRect (pr.expanded (1.2f), 1.8f);
}

void KintsugiAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImage (housing, getLocalBounds().toFloat());
}

//==============================================================================
PilotLamp::PilotLamp()
{
    setInterceptsMouseClicks (false, false);   // an indicator only: the toggle switch beside it does the switching
}

void PilotLamp::setGlow (float g)
{
    g = juce::jlimit (0.0f, 1.0f, g);
    if (std::abs (g - glow) > 0.004f) { glow = g; repaint(); }
}

//==============================================================================
ToggleSwitch::ToggleSwitch()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ToggleSwitch::setOn (bool b)
{
    if (b != on) { on = b; repaint(); }
}

void ToggleSwitch::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()) && onClick)
        onClick();
}

void ToggleSwitch::paint (juce::Graphics& g)
{
    // Seen from the front: a knurled chrome bushing with a hex nut, and a tapered bat lever
    // tilted up (in) or down (out), throwing its shadow down-right onto the plate
    using namespace juce;
    const auto c = getLocalBounds().toFloat().getCentre();
    const float rb = 6.6f;

    // shadow of the bushing
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (rb * 2 + 2, rb * 2 + 2).withCentre (c.translated (0.9f, 1.5f)));
    // hex nut
    Path hex;
    hex.addPolygon (c, 6, rb, 0.0f);
    g.setGradientFill (ColourGradient (Colour (0xfff0f0ea), c.x - rb, c.y - rb, Colour (0xff53554f), c.x + rb, c.y + rb, false));
    g.fillPath (hex);
    g.setColour (Colour (0xff2a2a27).withAlpha (0.8f));
    g.strokePath (hex, PathStrokeType (0.6f));
    // threaded bushing
    const float rt = rb * 0.62f;
    g.setGradientFill (ColourGradient (Colour (0xffd9dad4), c.x - rt, c.y - rt, Colour (0xff3c3d39), c.x + rt, c.y + rt, false));
    g.fillEllipse (Rectangle<float> (rt * 2, rt * 2).withCentre (c));
    for (int k = 1; k <= 3; ++k)
    {
        g.setColour (Colours::black.withAlpha (0.18f));
        g.drawEllipse (Rectangle<float> (rt * 2 * (float) k / 3.6f, rt * 2 * (float) k / 3.6f).withCentre (c), 0.4f);
    }

    // the bat lever, foreshortened: base at the bushing, tip toward the top (in) or bottom (out)
    const float dir = on ? -1.0f : 1.0f;
    const float len = 12.0f;
    const auto tip = c.translated (0.0f, dir * len);
    Path bat;
    bat.startNewSubPath (c.translated (-1.6f, 0.0f));
    bat.lineTo (tip.translated (-2.6f, 0.0f));
    bat.quadraticTo (tip.translated (0.0f, dir * 2.4f), tip.translated (2.6f, 0.0f));
    bat.lineTo (c.translated (1.6f, 0.0f));
    bat.closeSubPath();
    g.setColour (Colours::black.withAlpha (0.32f));
    g.fillPath (bat, AffineTransform::translation (1.8f, on ? 2.6f : 3.4f));
    g.setGradientFill (ColourGradient (Colour (0xfff7f7f2), c.x - 2.6f, c.y, Colour (0xff4c4e49), c.x + 2.8f, c.y, false));
    g.fillPath (bat);
    g.setColour (Colours::white.withAlpha (0.7f));
    g.drawLine ({ c.translated (-0.9f, dir * 2.0f), tip.translated (-1.4f, -dir * 1.0f) }, 0.6f);
    g.setColour (Colour (0xff2a2a27).withAlpha (0.7f));
    g.strokePath (bat, PathStrokeType (0.5f));
    // the rounded end of the bat catches the light
    g.setGradientFill (ColourGradient (Colours::white, tip.x - 1.2f, tip.y - 1.0f, Colour (0xff6b6d68), tip.x + 2.5f, tip.y + 2.0f, true));
    g.fillEllipse (Rectangle<float> (5.4f, 4.0f).withCentre (tip.translated (0.0f, dir * 0.6f)));
}

void PilotLamp::paint (juce::Graphics& g)
{
    // Vintage jewel pilot light: knurled chrome bezel around a faceted amber lens
    using namespace juce;
    const auto c = getLocalBounds().toFloat().getCentre();
    const float rb = 9.5f, rl = 6.4f;
    const float lit = glow;

    if (lit > 0.01f)
    {
        const Colour glowC (0xffffa040);
        g.setGradientFill (ColourGradient (glowC.withAlpha (0.42f * lit), c.x, c.y, glowC.withAlpha (0.0f), c.x + rb * 2.2f, c.y, true));
        g.fillEllipse (Rectangle<float> (rb * 4.4f, rb * 4.4f).withCentre (c));
    }

    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (rb * 2 + 2, rb * 2 + 2).withCentre (c.translated (0.8f, 1.4f)));
    g.setGradientFill (ColourGradient (Colour (0xfff2f2ee), c.x - rb, c.y - rb, Colour (0xff4a4c49), c.x + rb, c.y + rb, false));
    g.fillEllipse (Rectangle<float> (rb * 2, rb * 2).withCentre (c));
    for (int i = 0; i < 40; ++i)
    {
        const float a = (float) i * MathConstants<float>::twoPi / 40.0f;
        g.setColour (Colours::black.withAlpha (0.2f));
        g.drawLine ({ c.getPointOnCircumference (rb * 0.82f, a), c.getPointOnCircumference (rb, a) }, 0.6f);
    }
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawEllipse (Rectangle<float> (rl * 2 + 1.2f, rl * 2 + 1.2f).withCentre (c), 1.0f);

    const Colour lensDark  = Colour (0xff3a160a).interpolatedWith (Colour (0xffd2560f), lit);
    const Colour lensLight = Colour (0xff7a3418).interpolatedWith (Colour (0xffffd27a), lit);
    g.setGradientFill (ColourGradient (lensLight, c.x - 1.5f, c.y - 1.5f, lensDark, c.x + rl, c.y + rl, true));
    g.fillEllipse (Rectangle<float> (rl * 2, rl * 2).withCentre (c));
    for (int i = 0; i < 8; ++i)
    {
        const float a0 = (float) i * MathConstants<float>::twoPi / 8.0f, a1 = a0 + MathConstants<float>::twoPi / 8.0f;
        Path f;
        f.startNewSubPath (c.getPointOnCircumference (rl * 0.45f, (a0 + a1) * 0.5f));
        f.lineTo (c.getPointOnCircumference (rl, a0));
        f.lineTo (c.getPointOnCircumference (rl, a1));
        f.closeSubPath();
        g.setColour ((i % 2 ? Colours::white : Colours::black).withAlpha (0.14f - 0.04f * lit));
        g.fillPath (f);
    }
    g.setColour (Colours::white.withAlpha (0.4f + 0.35f * lit));
    g.fillEllipse (Rectangle<float> (2.6f, 2.0f).withCentre (c.translated (-2.2f, -2.4f)));
}

//==============================================================================
namespace
{
    // VU scale: needle position is proportional to voltage, so 0 VU sits at about 70 % of the arc
    float vuProportion (float vu)
    {
        return (std::pow (10.0f, vu / 20.0f) - 0.1f) / (std::pow (10.0f, 3.0f / 20.0f) - 0.1f);
    }
    constexpr float vuHalfArc = 0.82f;   // radians either side of straight up

    struct VUGeom
    {
        juce::Rectangle<float> bezel, face;
        juce::Point<float> pivot;
        float radius;
    };

    VUGeom vuGeometry (juce::Rectangle<float> b)
    {
        VUGeom g;
        g.bezel = b.reduced (1.0f);
        g.face = g.bezel.reduced (9.0f);   // a deeper bezel: the face sits down in a recess
        g.radius = g.face.getHeight() * 1.02f;
        g.pivot = { g.face.getCentreX(), g.face.getBottom() + g.face.getHeight() * 0.22f };
        return g;
    }

    float vuAngle (float p) { return -vuHalfArc + 2.0f * vuHalfArc * p; }
}

VUMeter::VUMeter (juce::String l, int v) : label (std::move (l)), variant (v)
{
    setInterceptsMouseClicks (false, false);
}

void VUMeter::resized() { renderFace(); }

void VUMeter::tick (float dt)
{
    // A real VU movement: a spring-mass needle that takes about 300 ms to settle, with a hint of overshoot
    const float vu = 10.0f * std::log10 (target + 1e-12f) + 18.0f;   // -18 dBFS RMS = 0 VU
    const float goal = juce::jlimit (-0.035f, 1.06f, vuProportion (vu));
    constexpr int sub = 4;
    const float h = dt / sub;
    for (int i = 0; i < sub; ++i)
    {
        // the replacement movement is a touch less damped: it swings past the mark a little more
        const float acc = (variant == 0 ? 115.0f : 128.0f) * (goal - pos) - (variant == 0 ? 18.5f : 14.0f) * vel;
        vel += acc * h;
        pos += vel * h;
    }
    pos = juce::jlimit (-0.04f, 1.08f, pos);   // the end stops
    repaint();
}

void VUMeter::renderFace()
{
    using namespace juce;
    const float scale = 2.0f;
    face = Image (Image::ARGB, jmax (1, (int) (getWidth() * scale)), jmax (1, (int) (getHeight() * scale)), true);
    Graphics g (face);
    g.addTransform (AffineTransform::scale (scale));
    const auto geo = vuGeometry (getLocalBounds().toFloat());

    // The meter body stands proud of the plate: a soft, wide contact shadow
    for (int i = 1; i <= 9; ++i)
    {
        g.setColour (Colours::black.withAlpha (0.055f));
        g.fillRoundedRectangle (geo.bezel.translated (1.2f + i * 0.45f, 2.0f + i * 0.7f).expanded (i * 0.35f), 8.0f);
    }
    if (variant == 0)
    {
        // Moulded cream bakelite: a rounded outer edge, a flat top, then a chamfered step down into the
        // window. Lit from the upper left: each slope is bright or dark depending on which way it faces.
        const auto b = geo.bezel;
        const auto flat = b.reduced (2.6f);
        const auto wallTop = geo.face.expanded (4.6f);
        const Colour cLite (0xfffbf3dc), cMid (0xffe2d3ac), cDark (0xff9d8656), cDeep (0xff6e5a34);

        // rounded outer edge
        g.setGradientFill (ColourGradient (cLite, b.getX(), b.getY(), cDeep, b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 8.0f);
        // flat top
        g.setGradientFill (ColourGradient (cMid.brighter (0.06f), flat.getX(), flat.getY(), cMid.darker (0.12f), flat.getRight(), flat.getBottom(), false));
        g.fillRoundedRectangle (flat, 6.0f);
        // aged: yellowed toward the edges, a little grime
        g.setGradientFill (ColourGradient (Colour (0x009a7434), b.getCentreX(), b.getCentreY(), Colour (0x389a7434), b.getRight(), b.getBottom(), true));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (Colours::white.withAlpha (0.55f));
        g.drawRoundedRectangle (b.reduced (0.9f).translated (-0.2f, -0.2f), 7.5f, 0.7f);   // highlight on the rounded edge
        g.setColour (cDeep.withAlpha (0.7f));
        g.drawRoundedRectangle (b, 8.0f, 0.8f);

        // the chamfered wall down into the window: the upper and left slopes face away from the light (dark),
        // the lower and right slopes face it (bright)
        {
            const auto o = wallTop, in = geo.face.expanded (1.2f);
            Path t, l, r, btm;
            t.addQuadrilateral (o.getX(), o.getY(), o.getRight(), o.getY(), in.getRight(), in.getY(), in.getX(), in.getY());
            l.addQuadrilateral (o.getX(), o.getY(), in.getX(), in.getY(), in.getX(), in.getBottom(), o.getX(), o.getBottom());
            r.addQuadrilateral (o.getRight(), o.getY(), o.getRight(), o.getBottom(), in.getRight(), in.getBottom(), in.getRight(), in.getY());
            btm.addQuadrilateral (o.getX(), o.getBottom(), in.getX(), in.getBottom(), in.getRight(), in.getBottom(), o.getRight(), o.getBottom());
            g.setColour (cDark.darker (0.25f)); g.fillPath (t);
            g.setColour (cDark);                g.fillPath (l);
            g.setColour (cMid.brighter (0.1f)); g.fillPath (r);
            g.setColour (cLite);                g.fillPath (btm);
            g.setColour (Colour (0xff3d3018).withAlpha (0.35f));   // grime packed into the corners of the step
            g.drawRoundedRectangle (o, 3.5f, 0.9f);
        }
        g.setColour (Colour (0xff0e0c0a));
        g.fillRoundedRectangle (geo.face.expanded (1.2f), 2.5f);   // black gasket around the glass

        Random rng (label.hashCode());
        for (int i = 0; i < 7; ++i)
        {
            const Point<float> p0 (b.getX() + rng.nextFloat() * b.getWidth(), b.getY() + rng.nextFloat() * 4.0f + (rng.nextBool() ? 0.0f : b.getHeight() - 5.0f));
            g.setColour ((rng.nextBool() ? Colour (0xff3d3018).withAlpha (0.2f) : Colours::white.withAlpha (0.35f)));
            g.drawLine ({ p0, p0.translated ((rng.nextFloat() - 0.5f) * 14.0f, (rng.nextFloat() - 0.5f) * 3.0f) }, 0.4f);
        }
    }
    else
    {
        // the replacement: a chrome bezel, a little tarnished, held by two small screws
        g.setGradientFill (ColourGradient (Colour (0xffe4e4de), geo.bezel.getX(), geo.bezel.getY(),
                                           Colour (0xff5d5e59), geo.bezel.getRight(), geo.bezel.getBottom(), false));
        g.fillRoundedRectangle (geo.bezel, 4.0f);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.0f), geo.bezel.getX(), geo.bezel.getCentreY(),
                                           Colour (0x30604a20), geo.bezel.getRight(), geo.bezel.getCentreY(), false));
        g.fillRoundedRectangle (geo.bezel, 4.0f);
        g.setColour (Colour (0xff2a2a27));
        g.drawRoundedRectangle (geo.bezel, 4.0f, 0.9f);
        g.setColour (Colour (0xff141412));
        g.fillRoundedRectangle (geo.face.expanded (1.6f), 3.0f);   // black gasket
        for (auto pt : { Point<float> (geo.bezel.getX() + 3.6f, geo.bezel.getY() + 3.6f),
                         Point<float> (geo.bezel.getRight() - 3.6f, geo.bezel.getBottom() - 3.6f) })
        {
            g.setGradientFill (ColourGradient (Colour (0xfff0f0ea), pt.x - 1.6f, pt.y - 1.6f, Colour (0xff55564f), pt.x + 1.6f, pt.y + 1.6f, false));
            g.fillEllipse (Rectangle<float> (3.4f, 3.4f).withCentre (pt));
            g.setColour (Colour (0xff2a2a27));
            g.drawLine ({ pt.translated (-1.2f, 0.9f), pt.translated (1.2f, -0.9f) }, 0.6f);
        }
    }

    // Warm, lamp-lit face: brightest low in the middle where the bulbs sit
    const auto f = geo.face;
    {
        // the original's bulbs are warm and its face has yellowed; the replacement is paler, its lamp a little cooler
        ColourGradient lamp (variant == 0 ? Colour (0xfff5e0a6) : Colour (0xfffbf3d8), f.getCentreX(), f.getBottom() - f.getHeight() * 0.2f,
                             variant == 0 ? Colour (0xffc99d55) : Colour (0xffd7c9a0), f.getX(), f.getY(), true);
        g.setGradientFill (lamp);
        g.fillRoundedRectangle (f, 3.0f);
    }
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (f.toNearestInt());
    {
        // the face is printed card: a fine, slightly uneven paper texture
        Random rng (label.hashCode() + 5);
        for (int i = 0; i < (int) (f.getWidth() * f.getHeight() * 0.6f); ++i)
        {
            const Point<float> q (f.getX() + rng.nextFloat() * f.getWidth(), f.getY() + rng.nextFloat() * f.getHeight());
            g.setColour ((rng.nextBool() ? Colours::white : Colour (0xff6b5228)).withAlpha (0.05f + 0.05f * rng.nextFloat()));
            g.fillRect (Rectangle<float> (0.5f, 0.5f).withCentre (q));
        }
        // the lamps sit low behind the face: a warm pool of light, falling off toward the top corners
        g.setGradientFill (ColourGradient (Colour (0x30ffd27a), f.getCentreX(), f.getBottom(), Colour (0x00ffd27a), f.getCentreX(), f.getY(), false));
        g.fillRect (f);
        g.setGradientFill (ColourGradient (Colour (0x00000000), f.getCentreX(), f.getCentreY() + 6.0f,
                                           Colour (0x3a2a1a08), f.getX() - 4.0f, f.getY() - 4.0f, true));
        g.fillRect (f);
    }

    const auto c = geo.pivot;
    const float r = geo.radius;
    const Colour ink (0xff1d1a16), red (0xffb3261b);

    // Scale arc: black up to 0 VU, red above
    auto arc = [&] (float from, float to, float rad, float thick, Colour col)
    {
        Path p;
        p.addCentredArc (c.x, c.y, rad, rad, 0.0f, vuAngle (vuProportion (from)), vuAngle (vuProportion (to)), true);
        g.setColour (col);
        g.strokePath (p, PathStrokeType (thick));
    };
    auto txt = [this] (float size, bool bold)
    {
        return variant == 0 ? KintsugiFonts::serif (size, bold)
                            : Font (FontOptions().withHeight (size * 1.05f).withStyle (bold ? "Bold" : "Regular"));
    };
    arc (-20.0f, 0.0f, r * 0.80f, variant == 0 ? 1.0f : 0.8f, ink);
    arc (0.0f, 3.0f, r * 0.80f, variant == 0 ? 1.0f : 0.8f, red);
    if (variant == 0)
        arc (0.0f, 3.0f, r * 0.825f, 3.2f, red);
    else
        arc (0.0f, 3.0f, r * 0.845f, 5.0f, red.withAlpha (0.85f));   // a broader red band

    for (float vu : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
    {
        const float a = vuAngle (vuProportion (vu));
        const bool major = vu == -20.0f || vu == -10.0f || vu == -7.0f || vu == -5.0f || vu == -3.0f || vu >= -1.0f;
        g.setColour (vu > 0.0f ? red : ink);
        g.drawLine ({ c.getPointOnCircumference (r * 0.80f, a), c.getPointOnCircumference (r * (major ? 0.88f : 0.85f), a) }, major ? 1.2f : 0.9f);
        if (vu == -20.0f || vu == -10.0f || vu == -7.0f || vu == -5.0f || vu == -3.0f || vu == 0.0f || vu == 3.0f || vu == -1.0f || vu == 1.0f)
        {
            const String t = vu > 0 ? "+" + String ((int) vu) : String ((int) vu);
            g.setFont (txt (vu == 0.0f ? 8.5f : 7.5f, vu == 0.0f));
            g.drawText (t, Rectangle<float> (22.0f, 10.0f).withCentre (c.getPointOnCircumference (r * 0.95f, a)), Justification::centred);
        }
    }
    // the small percentage marks under the arc: dots on the original, short ticks on the replacement
    for (int i = 0; i <= 10; ++i)
    {
        const float a = vuAngle ((float) i / 10.0f);
        g.setColour (ink.withAlpha (0.7f));
        if (variant == 0)
            g.fillEllipse (Rectangle<float> (1.4f, 1.4f).withCentre (c.getPointOnCircumference (r * 0.74f, a)));
        else
            g.drawLine ({ c.getPointOnCircumference (r * 0.72f, a), c.getPointOnCircumference (r * 0.76f, a) }, 0.6f);
    }

    g.setColour (ink);
    if (variant == 0)
    {
        g.setFont (KintsugiFonts::serif (13.0f).withExtraKerningFactor (0.1f));
        g.drawText ("VU", Rectangle<float> (40.0f, 16.0f).withCentre ({ c.x, f.getY() + f.getHeight() * 0.60f }), Justification::centred);
    }
    else
    {
        g.setFont (txt (12.5f, true).withExtraKerningFactor (0.05f));
        g.drawText ("VU", Rectangle<float> (40.0f, 16.0f).withCentre ({ c.x, f.getY() + f.getHeight() * 0.58f }), Justification::centred);
    }
    g.setFont (txt (8.0f, true).withExtraKerningFactor (0.1f));
    g.drawText (label, Rectangle<float> (f.getRight() - 18.0f, f.getBottom() - 15.0f, 12.0f, 10.0f), Justification::centred);
    if (variant == 0)
    {
        g.setColour (red.withAlpha (0.85f));
        g.drawText ("PEAK", Rectangle<float> (f.getX() + 6.0f, f.getBottom() - 15.0f, 30.0f, 10.0f), Justification::centredLeft);
    }
    else
    {
        g.setColour (ink.withAlpha (0.75f));
        g.setFont (txt (5.6f, false).withExtraKerningFactor (0.12f));
        g.drawText ("TYPE 41-B", Rectangle<float> (f.getX() + 5.0f, f.getBottom() - 14.0f, 40.0f, 9.0f), Justification::centredLeft);
    }

    // age on the original: the face has browned a little near the bulbs' heat, at the top
    if (variant == 0)
    {
        g.setGradientFill (ColourGradient (Colour (0x26603c10), f.getCentreX(), f.getY(), Colour (0x00603c10), f.getCentreX(), f.getY() + f.getHeight() * 0.5f, false));
        g.fillRect (f);
    }
}

void VUMeter::paint (juce::Graphics& g)
{
    using namespace juce;
    g.drawImage (face, getLocalBounds().toFloat());
    const auto geo = vuGeometry (getLocalBounds().toFloat());
    const auto f = geo.face;
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (f.toNearestInt());
        const float a = vuAngle (pos);
        const auto tip  = geo.pivot.getPointOnCircumference (geo.radius * 0.91f, a);
        const auto mid  = geo.pivot.getPointOnCircumference (geo.radius * 0.62f, a);
        const auto base = geo.pivot.getPointOnCircumference (geo.radius * 0.30f, a);

        // the needle floats a few millimetres above the face: a soft shadow, offset down-right
        for (auto [off, al, w] : { std::tuple { 2.6f, 0.08f, 2.6f }, std::tuple { 2.0f, 0.12f, 1.6f } })
        {
            g.setColour (Colours::black.withAlpha (al));
            g.drawLine ({ base.translated (off * 0.7f, off), tip.translated (off * 0.7f, off) }, w);
        }
        // a tapered needle: a stiffer black shaft that thins to a fine tip
        {
            const Point<float> dir ((tip.x - base.x), (tip.y - base.y));
            const float len = dir.getDistanceFromOrigin();
            const Point<float> n (-dir.y / len, dir.x / len);
            Path needle;
            needle.startNewSubPath (base + n * 0.9f);
            needle.lineTo (mid + n * 0.55f);
            needle.lineTo (tip + n * 0.18f);
            needle.lineTo (tip - n * 0.18f);
            needle.lineTo (mid - n * 0.55f);
            needle.lineTo (base - n * 0.9f);
            needle.closeSubPath();
            g.setColour (Colour (0xff141210));
            g.fillPath (needle);
            g.setColour (Colours::white.withAlpha (0.25f));   // the lit edge of the shaft
            g.drawLine ({ base + n * 0.7f, mid + n * 0.4f }, 0.35f);
        }
        if (variant == 1)
        {
            g.setColour (Colour (0xffc0281a));
            g.drawLine ({ geo.pivot.getPointOnCircumference (geo.radius * 0.74f, a), tip }, 1.4f);
        }

        // the moulded pivot housing: domed, with a lit top edge and a little zero-adjust screw
        Path cover;
        const float rc = geo.radius * 0.40f;
        cover.addCentredArc (geo.pivot.x, geo.pivot.y, rc, rc, 0.0f, -1.6f, 1.6f, true);
        cover.closeSubPath();
        g.setColour (Colours::black.withAlpha (0.3f));
        g.fillPath (cover, AffineTransform::translation (0.8f, 1.6f).scaled (1.0f, 1.0f));
        g.setGradientFill (ColourGradient (Colour (0xff3a342e), geo.pivot.x - rc * 0.4f, geo.pivot.y - rc,
                                           Colour (0xff0b0a09), geo.pivot.x + rc * 0.5f, geo.pivot.y - rc * 0.2f, false));
        g.fillPath (cover);
        {
            Path rim;
            rim.addCentredArc (geo.pivot.x, geo.pivot.y, rc - 0.6f, rc - 0.6f, 0.0f, -1.45f, 0.35f, true);
            g.setColour (Colours::white.withAlpha (0.28f));
            g.strokePath (rim, PathStrokeType (0.7f));
        }
        {
            const Point<float> sc (geo.pivot.x, f.getBottom() - 4.2f);
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillEllipse (Rectangle<float> (5.4f, 5.4f).withCentre (sc.translated (0.3f, 0.5f)));
            g.setGradientFill (ColourGradient (Colour (0xffcfd0cb), sc.x - 1.8f, sc.y - 1.8f, Colour (0xff3f403c), sc.x + 2.2f, sc.y + 2.2f, false));
            g.fillEllipse (Rectangle<float> (4.8f, 4.8f).withCentre (sc));
            g.setColour (Colour (0xff1a1a18));
            g.drawLine ({ sc.translated (-1.6f, 0.6f), sc.translated (1.6f, -0.6f) }, 0.8f);
        }

        // depth: the bezel casts a shadow down onto the face along its top and left edges
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.42f), f.getX(), f.getY(),
                                           Colours::black.withAlpha (0.0f), f.getX(), f.getY() + 10.0f, false));
        g.fillRect (f);
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.32f), f.getX(), f.getY(),
                                           Colours::black.withAlpha (0.0f), f.getX() + 8.0f, f.getY(), false));
        g.fillRect (f);

        // the glass: slightly domed. A broad soft reflection from the upper left, a sharper window-light
        // streak, a bright rim where the glass meets the gasket, and the odd speck of dust
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.20f), f.getX() + f.getWidth() * 0.18f, f.getY() + f.getHeight() * 0.12f,
                                           Colours::white.withAlpha (0.0f), f.getX() + f.getWidth() * 0.62f, f.getY() + f.getHeight() * 0.75f, true));
        g.fillRect (f);
        {
            Path streak;
            streak.startNewSubPath (f.getX() + f.getWidth() * 0.06f, f.getBottom());
            streak.lineTo (f.getX() + f.getWidth() * 0.30f, f.getY());
            streak.lineTo (f.getX() + f.getWidth() * 0.40f, f.getY());
            streak.lineTo (f.getX() + f.getWidth() * 0.16f, f.getBottom());
            streak.closeSubPath();
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.13f), f.getX(), f.getY(),
                                               Colours::white.withAlpha (0.02f), f.getX(), f.getBottom(), false));
            g.fillPath (streak);
            Path thin;
            thin.startNewSubPath (f.getX() + f.getWidth() * 0.46f, f.getY());
            thin.lineTo (f.getX() + f.getWidth() * 0.50f, f.getY());
            thin.lineTo (f.getX() + f.getWidth() * 0.30f, f.getBottom());
            thin.lineTo (f.getX() + f.getWidth() * 0.27f, f.getBottom());
            thin.closeSubPath();
            g.setColour (Colours::white.withAlpha (0.07f));
            g.fillPath (thin);
        }
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawLine (f.getX() + 3.0f, f.getBottom() - 0.6f, f.getRight() - 2.0f, f.getBottom() - 0.6f, 0.6f);   // rim catching light
        g.drawLine (f.getRight() - 0.6f, f.getY() + 3.0f, f.getRight() - 0.6f, f.getBottom() - 2.0f, 0.5f);
        Random dust (label.hashCode() + 9);
        for (int i = 0; i < 5; ++i)
        {
            g.setColour (Colours::white.withAlpha (0.18f + 0.15f * dust.nextFloat()));
            g.fillEllipse (Rectangle<float> (0.7f, 0.7f).withCentre ({ f.getX() + dust.nextFloat() * f.getWidth(), f.getY() + dust.nextFloat() * f.getHeight() }));
        }
    }
}

//==============================================================================
DrumWindow::DrumWindow (juce::StringArray i) : items (std::move (i))
{
    setInterceptsMouseClicks (false, false);
}

void DrumWindow::tick()
{
    if (std::abs (target - pos) < 0.002f) { if (pos != target) { pos = target; repaint(); } return; }
    pos += (target - pos) * 0.22f;   // the drum turns quickly, then settles into its detent
    repaint();
}

void DrumWindow::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    const auto ap = b.reduced (2.0f);          // the opening in the plate
    const float itemH = ap.getHeight() * 0.95f;

    // the cut edge of the plate: a bevel, dark on the upper-left lip, catching light on the lower-right
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (ap.expanded (1.4f), 2.4f);
    g.setColour (Colours::white.withAlpha (0.22f));
    g.drawRoundedRectangle (ap.expanded (1.6f).translated (0.5f, 0.6f), 2.4f, 0.6f);

    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (ap.toNearestInt());
    // the drum: aged ivory, printed in black serif
    g.setColour (Colour (0xffe9dcb8));
    g.fillRect (ap);
    const int centre = (int) std::floor (pos + 0.5f);
    for (int k = centre - 1; k <= centre + 1; ++k)
    {
        if (k < 0 || k >= items.size()) continue;
        const float y = ap.getCentreY() + ((float) k - pos) * itemH;
        g.setColour (Colour (0xff1c1812));
        g.setFont (KintsugiFonts::serif (ap.getHeight() * 0.78f, true));
        g.drawText (items[k], Rectangle<float> (ap.getX(), y - itemH * 0.5f, ap.getWidth(), itemH), Justification::centred);
    }
    // curvature of the drum: it turns away into shadow at the top and bottom of the window
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.55f), ap.getX(), ap.getY(),
                                       Colours::black.withAlpha (0.0f), ap.getX(), ap.getY() + ap.getHeight() * 0.42f, false));
    g.fillRect (ap);
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.0f), ap.getX(), ap.getBottom() - ap.getHeight() * 0.42f,
                                       Colours::black.withAlpha (0.5f), ap.getX(), ap.getBottom(), false));
    g.fillRect (ap);
    // shadow cast by the plate onto the drum, behind the upper lip
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.45f), ap.getX(), ap.getY(),
                                       Colours::black.withAlpha (0.0f), ap.getX() + 3.0f, ap.getY() + 3.0f, false));
    g.fillRect (ap);
}

//==============================================================================
KintsugiAudioProcessorEditor::KintsugiAudioProcessorEditor (KintsugiAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    if (caseStyleOverride >= 0)
        caseStyle = caseStyleOverride;
    using MC = juce::MathConstants<float>;
    setLookAndFeel (&lnf);
    plateArea.setInterceptsMouseClicks (false, true);
    addAndMakeVisible (plateArea);

    // Console
    cDrive  = &addKnob ("cdrive", "Drive");
    cDrive->slider.getProperties().set ("brass", true);   // the most-used knobs wore out first: brass replacements
    iron    = &addSwitch ("xfmr", "Transformer", { "NICKEL", "STEEL", "IRON" }, 0.30f);
    iron->stepLabels.clear();   // the drum window shows the core type; the switch keeps its three ticks
    hum     = &addKnob ("hum", "Hum");
    channel = &addKnob ("channel", "Channel", 24);
    channel->stepLabels = { "1", "24" };
    glue    = &addKnob ("glue", "Glue");
    drift   = &addKnob ("drift", "");   // its name is on an engraved brass plaque, not printed
    drift->slider.getProperties().set ("kintsugi", true);   // this knob's cap was cracked once, and mended with gold

    // Valve
    vDrive = &addKnob ("vdrive", "Drive");
    vDrive->slider.getProperties().set ("brass", true);
    bias   = &addKnob ("bias", "Bias");
    bias->marks = { { 0.0f, "COLD" }, { 1.0f, "HOT" } };
    sag    = &addKnob ("sag", "Sag");
    vType  = &addSwitch ("vtype", "Valve", { "ECC82", "ECC83", "EL84" }, 0.30f);
    tone   = &addKnob ("tone", "Tone");
    tone->marks = { { 0.0f, "DARK" }, { 1.0f, "OPEN" } };
    vMix   = &addKnob ("vmix", "Mix");

    // Tape
    speed   = &addSwitch ("speed", "Speed", { juce::String::fromUTF8 ("7\xc2\xbd"), "15", "30" }, 0.30f);
    speed->stepLabels.clear();   // the drum window shows the speed; the switch keeps just its three ticks
    tSat    = &addKnob ("tsat", "Saturation");
    tSat->slider.getProperties().set ("brass", true);
    wow     = &addKnob ("wow", "Wow");
    flutter = &addKnob ("flutter", "Flutter");
    wear    = &addKnob ("wear", "Age");

    // Output
    mix    = &addKnob ("mix", "Mix");
    mix->marks = { { 0.0f, "DRY" }, { 1.0f, "WET" } };
    output = &addKnob ("output", "Output");
    output->slider.setSliderStyle (juce::Slider::LinearVertical);
    output->slider.getProperties().set ("fader", true);
    output->slider.setDoubleClickReturnValue (true, 0.0);
    output->slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    output->printed = false;
    juce::ignoreUnused (MC::pi);

    // Section lamps (indicators) and the toggle switches that put each section in or out
    for (auto [lamp, sw, id, tip] : { std::tuple { &consoleLamp, &consoleSwitch, "consoleon", "Console in / out" },
                                      std::tuple { &valveLamp, &valveSwitch, "valveon", "Valve in / out" },
                                      std::tuple { &tapeLamp, &tapeSwitch, "tapeon", "Tape in / out" } })
    {
        plateArea.addAndMakeVisible (*lamp);
        plateArea.addAndMakeVisible (*sw);
        sw->setTooltip (tip);
        const juce::String paramId (id);
        sw->onClick = [this, paramId] { toggleParam (paramId); };
    }

    // channel and speed numbers live on drums behind small windows, not as text
    channel->readout.setVisible (false);
    iron->readout.setVisible (false);
    plateArea.addAndMakeVisible (xfmrDrum);
    speed->readout.setVisible (false);
    plateArea.addAndMakeVisible (channelDrum);
    plateArea.addAndMakeVisible (speedDrum);

    // The VU meters are kept in the code but no longer on the panel: 金継ぎ is printed in their place
    constexpr bool showMeters = false;
    if (showMeters)
    {
        plateArea.addAndMakeVisible (vuL);
        plateArea.addAndMakeVisible (vuR);
    }

    setSize (Layout::W + 2 * Layout::sideW, Layout::H + 2 * Layout::railH);
    timerCallback();
    channelDrum.snap();
    xfmrDrum.snap();   // open on the current numbers, without rolling
    speedDrum.snap();
    startTimerHz (60);
}

KintsugiAudioProcessorEditor::~KintsugiAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KintsugiAudioProcessorEditor::toggleParam (const juce::String& id)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

KintsugiAudioProcessorEditor::Knob& KintsugiAudioProcessorEditor::addKnob (const juce::String& paramId, const juce::String& name, int steps)
{
    auto k = std::make_unique<Knob>();
    k->name = name;
    k->steps = steps;
    k->slider.setName (paramId);
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->slider.setMouseDragSensitivity (steps > 1 ? (steps > 4 ? 260 : 120) : 220);
    plateArea.addAndMakeVisible (k->slider);

    k->readout.setJustificationType (juce::Justification::centred);
    k->readout.setFont (KintsugiFonts::serif (11.5f, false));
    k->readout.setColour (juce::Label::textColourId, Palette::cream.withAlpha (0.92f));
    k->readout.setInterceptsMouseClicks (false, false);
    plateArea.addAndMakeVisible (k->readout);

    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k->slider);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

KintsugiAudioProcessorEditor::Knob& KintsugiAudioProcessorEditor::addSwitch (const juce::String& paramId, const juce::String& name,
                                                                         juce::StringArray labels, float spread)
{
    // Fairchild-style pointer knob for stepped choices, labels printed at each position
    auto& k = addKnob (paramId, name, labels.size());
    k.slider.getProperties().set ("pointer", true);
    k.stepLabels = labels;
    const float pi = juce::MathConstants<float>::pi;
    k.slider.setRotaryParameters (pi * (2.0f - spread), pi * (2.0f + spread), true);
    return k;
}

void KintsugiAudioProcessorEditor::timerCallback()
{
    const bool cOn = proc.apvts.getRawParameterValue ("consoleon")->load() > 0.5f;
    const bool vOn = proc.apvts.getRawParameterValue ("valveon")->load() > 0.5f;
    const bool tOn = proc.apvts.getRawParameterValue ("tapeon")->load() > 0.5f;

    for (auto* k : { cDrive, iron, hum, channel, glue, drift }) k->slider.setEnabled (cOn);
    for (auto* k : { vDrive, bias, sag, vType, tone, vMix }) k->slider.setEnabled (vOn);
    for (auto* k : { speed, tSat, wow, flutter, wear }) k->slider.setEnabled (tOn);

    consoleSwitch.setOn (cOn);
    valveSwitch.setOn (vOn);
    tapeSwitch.setOn (tOn);
    consoleLamp.setGlow (cOn ? 1.0f : 0.0f);
    tapeLamp.setGlow (tOn ? 1.0f : 0.0f);
    // The valve lamp breathes with how hard the tube is working, and flickers a touch
    const float heatTarget = vOn ? 0.55f + 0.45f * proc.getValveHeat() : 0.0f;
    valveGlow += (heatTarget - valveGlow) * 0.12f;
    valveLamp.setGlow (valveGlow * (vOn ? 0.97f + 0.03f * flicker.nextFloat() : 1.0f));

    channelDrum.setIndex (juce::roundToInt (channel->slider.getValue()) - 1);
    speedDrum.setIndex (juce::roundToInt (speed->slider.getValue()));
    xfmrDrum.setIndex (juce::roundToInt (iron->slider.getValue()));
    xfmrDrum.tick();
    channelDrum.tick();
    speedDrum.tick();

    vuL.setMeanSquare (proc.getMeterLevel (0));
    vuR.setMeanSquare (proc.getMeterLevel (1));
    vuL.tick (1.0f / 60.0f);
    vuR.tick (1.0f / 60.0f);

    for (auto& k : knobs)
    {
        const bool show = k->alwaysShowValue || k->slider.isMouseOverOrDragging();
        const auto text = show ? k->slider.getTextFromValue (k->slider.getValue()) : juce::String();
        if (k->alwaysShowValue)
            k->readout.setAlpha (k->slider.isEnabled() ? 1.0f : 0.45f);
        if (k->readout.getText() != text)
            k->readout.setText (text, juce::dontSendNotification);
    }
}

//==============================================================================
void KintsugiAudioProcessorEditor::placeKnob (Knob& k, float col, int row, juce::Point<int> origin, int cw)
{
    const int cx = origin.x + juce::roundToInt (col * (float) cw) + cw / 2;
    const int top = origin.y + row * Layout::rowH;
    k.slider.setBounds (cx - Layout::knobSize / 2, top, Layout::knobSize, Layout::knobSize);
    k.readout.setBounds (cx - Layout::colW / 2, top + Layout::knobSize + 18, Layout::colW, 15);
}

void KintsugiAudioProcessorEditor::resized()
{
    using namespace Layout;
    plateArea.setBounds (sideW, railH, W, H);
    sections.clear();
    int x = marginX;
    auto addSection = [&] (const juce::String& title, int cols, int cw, PilotLamp* lamp, ToggleSwitch* sw = nullptr)
    {
        sections.push_back ({ title, { x, bracketY, cols * cw, H - bracketY - 20 }, lamp, sw });
        juce::Point<int> origin { x, row1Y };
        x += cols * cw + sectionGap;
        return origin;
    };

    const auto con = addSection ("Console", 3, colW, &consoleLamp, &consoleSwitch);
    placeKnob (*cDrive, 0, 0, con);
    placeKnob (*iron, 1, 0, con);
    placeKnob (*hum, 2, 0, con);
    placeKnob (*channel, 0.5f, 1, con);   // two under three
    placeKnob (*glue, 1.5f, 1, con);

    const auto val = addSection ("Valve", 3, colW, &valveLamp, &valveSwitch);
    placeKnob (*vDrive, 0, 0, val);
    placeKnob (*bias, 1, 0, val);
    placeKnob (*sag, 2, 0, val);
    placeKnob (*vType, 0, 1, val);
    placeKnob (*tone, 1, 1, val);
    placeKnob (*vMix, 2, 1, val);

    const auto tap = addSection ("Tape", 3, colW, &tapeLamp, &tapeSwitch);
    placeKnob (*speed, 0.5f, 0, tap);   // two on top, centred over the three below
    placeKnob (*tSat, 1.5f, 0, tap);
    placeKnob (*wow, 0, 1, tap);
    placeKnob (*flutter, 1, 1, tap);
    placeKnob (*wear, 2, 1, tap);

    const auto out = addSection ("Output", 2, outColW, nullptr);
    placeKnob (*mix, 0, 0, out, outColW);
    {
        // the Kintsugi knob: bigger, under Mix, beside the fader
        const int cx = out.x + outColW / 2, cy = row1Y + rowH + knobSize / 2 + 6;
        drift->slider.setBounds (cx - 52, cy - 52, 104, 104);
        drift->readout.setBounds (cx - colW / 2, cy + 52 + 22, colW, 15);
    }
    {
        const int cx = out.x + 2 * outColW - 38;
        const int top = row1Y - 6, bottom = row1Y + 2 * rowH + 22;
        output->slider.setBounds (cx - 28, top, 56, bottom - top);
        output->readout.setBounds (cx - colW / 2, bottom + 17, colW, 15);
    }

    // Section lamps sit in the bracket, just right of the title
    for (auto& s : sections)
    {
        if (s.lamp == nullptr) continue;
        const auto font = KintsugiFonts::serif (13.0f).withExtraKerningFactor (0.22f);
        const float tw = juce::GlyphArrangement::getStringWidth (font, s.title.toUpperCase()) + 14.0f;
        const float block = tw + 54.0f;   // title, lamp, switch
        const float right = (float) s.bounds.getCentreX() + block * 0.5f;
        const int lx = juce::roundToInt (right - 39.0f);
        s.lamp->setBounds (lx - 24, s.bounds.getY() - 24, 48, 48);
        if (s.toggle != nullptr)
            s.toggle->setBounds (juce::roundToInt (right - 12.0f) - 11, s.bounds.getY() - 17, 22, 34);
    }

    // drum windows just under the Channel and Speed names
    {
        const auto cb = channel->slider.getBounds();
        channelDrum.setBounds (cb.getCentreX() - 15, cb.getBottom() + 19, 30, 16);
        const auto xb = iron->slider.getBounds();
        xfmrDrum.setBounds (xb.getCentreX() - 26, xb.getBottom() + 19, 52, 16);
        const auto sb = speed->slider.getBounds();
        speedDrum.setBounds (sb.getCentreX() - 19, sb.getBottom() + 19, 30, 16);
    }

    // Two VU meters in the header, between the name and the logo
    // the meters sit above the Output section, right edge kept clear of the brass corner
    {
        // the meters sit centred over the Output section: they show what comes out
        const int vuW = 120, vuH = 76, gap = 8;
        const int cx = sections.back().bounds.getCentreX();
        const int vuY = topH - vuH - 8;   // low in the header, just above the groove and the Output section
        vuL.setBounds (cx - gap / 2 - vuW, vuY, vuW, vuH);
        vuR.setBounds (cx + gap / 2, vuY, vuW, vuH);
    }

    renderPlate();
    plateArea.image = &plate;
    renderHousing();
}

//==============================================================================
namespace
{
    // ======================= Kintsugi, in metal =======================
    // Repaired by someone who loves old equipment: the broken-off corners were cut back to a clean curve
    // and rebuilt in brass, fitted flush and riveted; dents were left alone; worn-out knobs were replaced
    // with good brass ones from the drawer. Nothing hidden, everything done with care.
    namespace Repair
    {
        using juce::Colour;
        const Colour brass     { 0xffc39b4e };
        const Colour brassLite { 0xfff0d998 };
        const Colour brassDark { 0xff6f5422 };
        const Colour brassDeep { 0xff3e2f14 };

        // A small countersunk rivet, filed nearly flush and polished
        void rivet (juce::Graphics& g, juce::Point<float> c, float r = 2.0f)
        {
            using namespace juce;
            g.setColour (brassDeep.withAlpha (0.55f));
            g.fillEllipse (Rectangle<float> (r * 2 + 1.0f, r * 2 + 1.0f).withCentre (c.translated (0.25f, 0.35f)));
            g.setGradientFill (ColourGradient (Colour (0xfffbe8b4), c.x - r * 0.6f, c.y - r * 0.6f, brassDark, c.x + r, c.y + r, true));
            g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (c));
            g.setColour (Colours::white.withAlpha (0.7f));
            g.fillEllipse (Rectangle<float> (r * 0.6f, r * 0.45f).withCentre (c.translated (-r * 0.3f, -r * 0.35f)));
        }

        // An engraved brass name plaque, riveted on: the repairer's finest work
        void plaque (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, int seed)
        {
            using namespace juce;
            g.setColour (Colours::black.withAlpha (0.18f));
            g.fillRoundedRectangle (r.translated (1.2f, 1.9f), 2.0f);
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (r.translated (0.5f, 0.8f), 2.0f);
            g.setGradientFill (ColourGradient (brassLite.interpolatedWith (brass, 0.35f), r.getX(), r.getY(),
                                               brassDark.interpolatedWith (brass, 0.3f), r.getRight(), r.getBottom(), false));
            g.fillRoundedRectangle (r, 2.0f);
            {
                Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (r.toNearestInt());
                Random rng (seed);
                for (int i = 0; i < (int) (r.getWidth() * r.getHeight() * 0.35f); ++i)
                {
                    const Point<float> o (r.getX() + rng.nextFloat() * r.getWidth(), r.getY() + rng.nextFloat() * r.getHeight());
                    const float l = 4.0f + rng.nextFloat() * 14.0f;
                    g.setColour ((rng.nextBool() ? brassLite : brassDeep).withAlpha (0.05f + 0.07f * rng.nextFloat()));
                    g.drawLine ({ o.translated (-l * 0.5f, 0), o.translated (l * 0.5f, 0) }, 0.4f);
                }
            }
            // bevelled edge
            g.setColour (Colour (0xfffff3cf).withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (0.6f).translated (0.4f, 0.5f), 1.6f, 0.7f);
            g.setColour (brassDeep.withAlpha (0.8f));
            g.drawRoundedRectangle (r, 2.0f, 0.7f);
            // a fine engraved border, then the engraved name
            g.setColour (brassDeep.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (2.6f), 1.0f, 0.45f);
            // one line per row of text, each centred
            const auto lines = StringArray::fromLines (text);
            const float lineH = (r.getHeight() - 5.0f) / (float) jmax (1, lines.size());
            for (int i = 0; i < lines.size(); ++i)
            {
                const auto tr = Rectangle<float> (r.getX() + 8.0f, r.getY() + 2.5f + lineH * (float) i, r.getWidth() - 16.0f, lineH);
                g.setFont (KintsugiFonts::serif (lineH * (i == 0 ? 0.80f : 0.68f)).withExtraKerningFactor (lines.size() == 1 ? 0.16f : (i == 0 ? 0.22f : 0.34f)));
                g.setColour (brassLite.withAlpha (0.85f));
                g.drawText (lines[i], tr.translated (0.4f, 0.5f), Justification::centred);
                g.setColour (brassDeep.withAlpha (0.95f));
                g.drawText (lines[i], tr, Justification::centred);
            }
            // two tiny rivets
            for (auto pt : { Point<float> (r.getX() + 5.0f, r.getCentreY()), Point<float> (r.getRight() - 5.0f, r.getCentreY()) })
                rivet (g, pt, 1.6f);
        }


        void screw (juce::Graphics& g, juce::Point<float> pt, float slotAngle)
        {
            using namespace juce;
            const float r = 6.0f;
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillEllipse (Rectangle<float> (r * 2 + 1.5f, r * 2 + 1.5f).withCentre (pt.translated (0.6f, 1.0f)));
            g.setGradientFill (ColourGradient (Colour (0xffd8d9d4), pt.x - r, pt.y - r, Colour (0xff6c6e6a), pt.x + r, pt.y + r, false));
            g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (pt));
            g.setColour (Colour (0xff2a2a28));
            g.drawLine ({ pt.getPointOnCircumference (r * 0.8f, slotAngle), pt.getPointOnCircumference (r * 0.8f, slotAngle + MathConstants<float>::pi) }, 1.6f);
        }

        // A corner rebuilt in brass: the broken plate was cut back to a true arc (centre, radius,
        // between two angles), and a brass piece fitted flush against it, with an engraved border line
        // following the joint and evenly spaced rivets.
        void cornerInlay (juce::Graphics& g, juce::Point<float> centre, float radius, float a0, float a1, int rivets, int seed)
        {
            using namespace juce;
            const auto box = [centre] (float r) { return Rectangle<float> (r * 2, r * 2).withCentre (centre); };
            Path piece;
            piece.addPieSegment (box (radius), a0, a1, 0.0f);
            Path joint;
            joint.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, a0, a1, true);

            // brass, brushed in one direction, warmer where the light falls
            const auto b = piece.getBounds();
            g.setGradientFill (ColourGradient (brassLite.interpolatedWith (brass, 0.45f), b.getX(), b.getY(),
                                               brassDark.interpolatedWith (brass, 0.4f), b.getRight(), b.getBottom(), false));
            g.fillPath (piece);
            {
                Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (piece);
                Random rng (seed);
                for (int i = 0; i < (int) (b.getWidth() * b.getHeight() * 0.25f); ++i)
                {
                    const Point<float> o (b.getX() + rng.nextFloat() * b.getWidth(), b.getY() + rng.nextFloat() * b.getHeight());
                    const float l = 5.0f + rng.nextFloat() * 18.0f;
                    g.setColour ((rng.nextBool() ? brassLite : brassDeep).withAlpha (0.04f + 0.07f * rng.nextFloat()));
                    g.drawLine ({ o.translated (-l * 0.5f, 0), o.translated (l * 0.5f, 0) }, 0.4f);
                }
                // a gentle, even patina, darker toward the frame where hands never polished it
                g.setGradientFill (ColourGradient (Colours::transparentBlack, centre.x, centre.y,
                                                   brassDeep.withAlpha (0.0f), centre.x + radius, centre.y, true));
                g.setGradientFill (ColourGradient (brassDeep.withAlpha (0.22f), centre.x, centre.y,
                                                   brassDeep.withAlpha (0.0f), centre.x + radius * 0.8f, centre.y, true));
                g.fillPath (piece);

                // engraved border line, parallel to the joint: a fine groove, lit on its lower edge
                const float re = radius - 5.0f;
                Path eng;
                eng.addCentredArc (centre.x, centre.y, re, re, 0.0f, a0, a1, true);
                g.setColour (brassLite.withAlpha (0.8f));
                g.strokePath (eng, PathStrokeType (0.6f), AffineTransform::translation (0.35f, 0.45f));
                g.setColour (brassDeep.withAlpha (0.85f));
                g.strokePath (eng, PathStrokeType (0.65f));

                // rivets, evenly spaced along the joint
                const float rr = radius - 11.0f;
                for (int i = 0; i < rivets; ++i)
                {
                    const float t = ((float) i + 0.5f) / (float) rivets;
                    rivet (g, centre.getPointOnCircumference (rr, a0 + (a1 - a0) * t), 1.9f);
                }
            }
            // the joint: a hairline where brass meets paint, the plate's cut edge just catching the light
            g.setColour (Colour (0xff15130c).withAlpha (0.8f));
            g.strokePath (joint, PathStrokeType (0.75f));
            Path lip;
            lip.addCentredArc (centre.x, centre.y, radius + 0.9f, radius + 0.9f, 0.0f, a0, a1, true);
            g.setColour (Colours::white.withAlpha (0.22f));
            g.strokePath (lip, PathStrokeType (0.5f));
        }

        // A dent, left as it is. Real dents are never neat ovals: the outline is irregular and lopsided,
        // drawn out toward the side the knock came from, and the deepest point sits off-centre.
        // The metal is bent, not just shaded: a soft hollow (the far wall catches the light, the near
        // wall falls into shade), a ridge where the plate bends back, faint creases, and paint knocked
        // off at the point of impact.
        juce::Path dentShape (juce::Point<float> c, float rx, float ry, float rot, float towards, float pull, int seed, float scale,
                              juce::Point<float> anchor)
        {
            using namespace juce;
            Random rng (seed);
            const float p1 = rng.nextFloat() * 6.28f, p2 = rng.nextFloat() * 6.28f, p3 = rng.nextFloat() * 6.28f;
            const float a1 = 0.10f + 0.12f * rng.nextFloat(), a2 = 0.06f + 0.08f * rng.nextFloat(), a3 = 0.03f + 0.04f * rng.nextFloat();
            Path p;
            constexpr int n = 96;
            for (int i = 0; i <= n; ++i)
            {
                const float t = (float) i / n * MathConstants<float>::twoPi;
                float m = 1.0f + a1 * std::sin (2.0f * t + p1) + a2 * std::sin (3.0f * t + p2) + a3 * std::sin (5.0f * t + p3);
                const float d = std::cos (t - towards);
                m += pull * std::pow (jmax (0.0f, d), 3.0f) - 0.12f * pull * std::pow (jmax (0.0f, -d), 2.0f);   // teardrop
                const Point<float> local (std::cos (t) * rx * m, std::sin (t) * ry * m);
                const auto pt = local.rotatedAboutOrigin (rot) + c;
                const auto scaled = anchor + (pt - anchor) * scale;   // inner layers shrink toward the deepest point
                i == 0 ? p.startNewSubPath (scaled) : p.lineTo (scaled);
            }
            p.closeSubPath();
            return p;
        }

        void dent (juce::Graphics& g, juce::Point<float> c, float rx, float ry, float rot, float towards, float pull, float depth, int seed)
        {
            using namespace juce;
            Random rng (seed + 50);
            // the deepest point: off-centre, toward the side the blow came from
            const auto deep = c + Point<float> (std::cos (towards) * rx, std::sin (towards) * ry).rotatedAboutOrigin (rot) * 0.35f;
            const auto outline = dentShape (c, rx, ry, rot, towards, pull, seed, 1.0f, deep);
            const auto b = outline.getBounds();

            for (int k = 0; k < 12; ++k)
            {
                const float f = 1.0f - (float) k / 13.0f;
                const auto layer = dentShape (c, rx, ry, rot, towards, pull, seed, f, deep);
                const auto lb = layer.getBounds();
                g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.06f * depth), lb.getX(), lb.getY(),
                                                   Colour (0xffd8e0d0).withAlpha (0.028f * depth), lb.getRight(), lb.getBottom(), false));
                g.fillPath (layer);
            }
            // the rim where the plate bends: dark on the near (upper-left) side, a soft light on the far side
            g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.10f * depth), b.getX(), b.getY(),
                                               Colours::white.withAlpha (0.09f * depth), b.getRight(), b.getBottom(), false));
            g.strokePath (outline, PathStrokeType (1.4f));
            // a couple of faint creases running out from the deepest point
            for (int i = 0; i < 2; ++i)
            {
                const float a = towards + MathConstants<float>::pi + (rng.nextFloat() - 0.5f) * 1.6f;
                const float len = jmax (rx, ry) * (0.5f + 0.4f * rng.nextFloat());
                Path cr;
                cr.startNewSubPath (deep);
                const auto end = deep + Point<float> (std::cos (a), std::sin (a)).rotatedAboutOrigin (rot) * len;
                cr.quadraticTo ((deep + end) * 0.5f + Point<float> (rng.nextFloat() - 0.5f, rng.nextFloat() - 0.5f) * 3.0f, end);
                g.setColour (Colours::black.withAlpha (0.10f * depth));
                g.strokePath (cr, PathStrokeType (0.7f));
                g.setColour (Colours::white.withAlpha (0.06f * depth));
                g.strokePath (cr, PathStrokeType (0.45f), AffineTransform::translation (0.5f, 0.6f));
            }
            // paint crazed and knocked off where it struck
            for (int i = 0; i < 5; ++i)
            {
                const float a = rng.nextFloat() * MathConstants<float>::twoPi;
                g.setColour (Colour (0xff10140f).withAlpha (0.32f));
                g.drawLine ({ deep, deep.getPointOnCircumference (1.0f + rng.nextFloat() * 2.4f * depth, a) }, 0.35f);
            }
            for (int i = 0; i < 3; ++i)
            {
                const auto q = deep.translated ((rng.nextFloat() - 0.5f) * 3.0f, (rng.nextFloat() - 0.5f) * 3.0f);
                Path flake;
                const float r = 0.5f + rng.nextFloat() * 1.1f * depth;
                for (int k = 0; k < 5; ++k)
                {
                    const auto pt = q.getPointOnCircumference (r * (0.6f + rng.nextFloat() * 0.6f), (float) k / 5.0f * MathConstants<float>::twoPi);
                    k == 0 ? flake.startNewSubPath (pt) : flake.lineTo (pt);
                }
                flake.closeSubPath();
                g.setColour (Colour (0xffb3b5ae));
                g.fillPath (flake);
            }
        }

        // Where the larger meters used to sit: the paint under their bezels never faded,
        // so a faint, slightly deeper rectangle remains, with two old mounting holes filled and painted over
        void ghostFootprint (juce::Graphics& g, juce::Rectangle<float> r)
        {
            using namespace juce;
            g.setColour (Colour (0xff0e2418).withAlpha (0.055f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (Colours::black.withAlpha (0.07f));
            g.drawRoundedRectangle (r, 6.0f, 0.7f);
            g.setColour (Colours::white.withAlpha (0.04f));
            g.drawRoundedRectangle (r.translated (0.6f, 0.7f), 6.0f, 0.6f);
            for (auto pt : { Point<float> (r.getX() + 4.5f, r.getCentreY()), Point<float> (r.getRight() - 4.5f, r.getCentreY()) })
            {
                g.setColour (Colours::black.withAlpha (0.08f));
                g.drawEllipse (Rectangle<float> (4.2f, 4.2f).withCentre (pt), 0.6f);
                g.setColour (Colours::white.withAlpha (0.04f));
                g.drawEllipse (Rectangle<float> (4.2f, 4.2f).withCentre (pt.translated (0.5f, 0.6f)), 0.5f);
            }
        }

        // The repairer's mark, struck with individual letter punches: each letter sits a little
        // differently, as hand-punched letters always do
        void punchedText (juce::Graphics& g, const juce::String& text, juce::Point<float> centre, float size, float rot, int seed)
        {
            using namespace juce;
            GlyphArrangement ga;
            const auto font = KintsugiFonts::serif (size).withExtraKerningFactor (0.18f);
            ga.addLineOfText (font, text, 0.0f, 0.0f);
            const auto bb = ga.getBoundingBox (0, -1, true);
            Random rng (seed);
            for (int i = 0; i < ga.getNumGlyphs(); ++i)
            {
                auto& gl = ga.getGlyph (i);
                if (gl.isWhitespace()) continue;
                const auto gc = Point<float> (gl.getLeft() + gl.getRight(), gl.getBaselineY() * 2.0f - size * 0.7f) * 0.5f;
                const auto xf = AffineTransform::rotation ((rng.nextFloat() - 0.5f) * 0.12f, gc.x, gc.y)
                                    .translated (-bb.getCentreX(), -bb.getCentreY() + (rng.nextFloat() - 0.5f) * 0.6f)
                                    .rotated (rot).translated (centre);
                const float depth = 0.75f + 0.25f * rng.nextFloat();   // some punches struck harder
                g.setColour (brassLite.withAlpha (0.8f * depth));
                gl.draw (g, xf.translated (0.35f, 0.45f));
                g.setColour (brassDeep.withAlpha (0.9f * depth));
                gl.draw (g, xf);
            }
        }

        // The repairer's touchmark: a small struck circle with a star
        void touchmark (juce::Graphics& g, juce::Point<float> c, float r)
        {
            using namespace juce;
            Path star;
            star.addStar (c, 8, r * 0.28f, r * 0.72f, 0.0f);
            Path ring;
            ring.addEllipse (Rectangle<float> (r * 2, r * 2).withCentre (c));
            for (auto [col, off] : { std::pair { brassLite.withAlpha (0.8f), 0.4f }, std::pair { brassDeep.withAlpha (0.9f), 0.0f } })
            {
                g.setColour (col);
                g.strokePath (ring, PathStrokeType (0.6f), AffineTransform::translation (off, off * 1.2f));
                g.fillPath (star, AffineTransform::translation (off, off * 1.2f));
            }
        }

        // A slim brass strip let into a worn stretch of the edge, flush, with countersunk screws
        void edgeStrip (juce::Graphics& g, juce::Rectangle<float> r, int screws, int seed)
        {
            using namespace juce;
            Path strip;
            strip.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight() + 6.0f, 2.2f, 2.2f, true, true, false, false);
            g.setGradientFill (ColourGradient (brassLite.interpolatedWith (brass, 0.4f), r.getX(), r.getY(),
                                               brassDark.interpolatedWith (brass, 0.35f), r.getX(), r.getBottom(), false));
            g.fillPath (strip);
            {
                Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (strip);
                Random rng (seed);
                for (int i = 0; i < (int) (r.getWidth() * r.getHeight() * 0.3f); ++i)
                {
                    const Point<float> o (r.getX() + rng.nextFloat() * r.getWidth(), r.getY() + rng.nextFloat() * r.getHeight());
                    const float l = 5.0f + rng.nextFloat() * 20.0f;
                    g.setColour ((rng.nextBool() ? brassLite : brassDeep).withAlpha (0.05f + 0.07f * rng.nextFloat()));
                    g.drawLine ({ o.translated (-l * 0.5f, 0), o.translated (l * 0.5f, 0) }, 0.4f);
                }
                // engraved border, open at the plate edge
                Path eng;
                eng.startNewSubPath (r.getX() + 4.5f, r.getBottom() + 2.0f);
                eng.lineTo (r.getX() + 4.5f, r.getY() + 4.5f);
                eng.lineTo (r.getRight() - 4.5f, r.getY() + 4.5f);
                eng.lineTo (r.getRight() - 4.5f, r.getBottom() + 2.0f);
                g.setColour (brassLite.withAlpha (0.8f));
                g.strokePath (eng, PathStrokeType (0.55f), AffineTransform::translation (0.4f, 0.45f));
                g.setColour (brassDeep.withAlpha (0.85f));
                g.strokePath (eng, PathStrokeType (0.6f));
                // countersunk screws in the corners, slots lined up as a careful repairer does
                juce::ignoreUnused (screws);
                for (auto sc : { Point<float> (r.getX() + 11.0f, r.getY() + 11.0f), Point<float> (r.getRight() - 11.0f, r.getY() + 11.0f),
                                 Point<float> (r.getX() + 11.0f, r.getBottom() - 9.0f), Point<float> (r.getRight() - 11.0f, r.getBottom() - 9.0f) })
                {
                    g.setColour (brassDeep.withAlpha (0.6f));
                    g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (sc.translated (0.2f, 0.3f)));
                    g.setGradientFill (ColourGradient (Colour (0xfffbe8b4), sc.x - 1.6f, sc.y - 1.6f, brassDark, sc.x + 2.2f, sc.y + 2.2f, false));
                    g.fillEllipse (Rectangle<float> (4.6f, 4.6f).withCentre (sc));
                    g.setColour (brassDeep);
                    g.drawLine ({ sc.translated (-1.8f, 0.0f), sc.translated (1.8f, 0.0f) }, 0.8f);
                }
            }
            g.setColour (Colour (0xff15130c).withAlpha (0.8f));
            g.drawLine (r.getX(), r.getY() + 2.2f, r.getX(), r.getBottom() + 4, 0.75f);
            g.drawLine (r.getRight(), r.getY() + 2.2f, r.getRight(), r.getBottom() + 4, 0.75f);
            g.drawLine (r.getX() + 2.2f, r.getY(), r.getRight() - 2.2f, r.getY(), 0.75f);
            g.setColour (Colours::white.withAlpha (0.22f));
            g.drawLine (r.getX() + 2.2f, r.getY() - 0.8f, r.getRight() - 2.2f, r.getY() - 0.8f, 0.5f);
        }

        // Years of hands: paint rubbed through in arcs where fingers reach for the busiest knobs
        void fingerWear (juce::Graphics& g, juce::Point<float> c, float r, float from, float to, float amount)
        {
            using namespace juce;
            for (int k = 0; k < 4; ++k)
            {
                Path arc;
                const float rr = r + 2.0f + (float) k * 1.6f;
                arc.addCentredArc (c.x, c.y, rr, rr, 0.0f, from + 0.15f * (float) k, to - 0.1f * (float) k, true);
                g.setColour (Colour (0xffc2c6bc).withAlpha (0.05f * amount));
                g.strokePath (arc, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }

    }
}

void KintsugiAudioProcessorEditor::renderPlate()
{
    using namespace juce;
    using namespace Layout;
    const float scale = 2.0f;
    const int pw = (int) (W * scale), ph = (int) (H * scale);
    Random rng (2027);

    // ---- 1. Base aluminium with green paint, brushed and noisy ----
    Image base (Image::RGB, pw, ph, false);
    {
        Image::BitmapData bd (base, Image::BitmapData::writeOnly);
        std::vector<float> rowTone ((size_t) ph);
        float drift = 0;
        for (auto& v : rowTone) { drift = drift * 0.9f + (rng.nextFloat() - 0.5f) * 2.4f; v = drift; }
        std::vector<float> brushRow ((size_t) ph);   // each brushed line has its own brightness
        for (auto& v : brushRow) v = rng.nextFloat();

        for (int y = 0; y < ph; ++y)
        {
            float streak = 0;
            for (int x = 0; x < pw; ++x)
            {
                if (rng.nextFloat() < 0.01f) streak = (rng.nextFloat() - 0.5f) * 7.0f;
                streak *= 0.995f;
                const float ex = jmin ((float) x, (float) (pw - x)) / (float) pw;
                const float ey = jmin ((float) y, (float) (ph - y)) / (float) ph;
                const float edge = jlimit (0.0f, 1.0f, jmin (ex * 9.0f, ey * 5.0f));
                // brushed aluminium under thin paint: long horizontal brush lines that come and go,
                // a broad soft sheen where the metal reflects the room, and a cooler metallic glint in the lines
                const float fx = (float) x / (float) pw, fy = (float) y / (float) ph;
                const float brushLine = (brushRow[(size_t) y] - 0.5f) * (0.6f + 0.8f * fbm (fx * 3.0f, fy * 40.0f, 7));
                const float sheen = std::exp (-std::pow ((fx - 0.34f + 0.12f * fy) / 0.30f, 2.0f));
                const float glint = jmax (0.0f, brushLine) * sheen;
                const float n = (rng.nextFloat() - 0.5f) * 7.0f + rowTone[(size_t) y] * 0.8f + streak * 0.7f
                                + brushLine * 9.0f + sheen * 9.0f - (1.0f - edge) * 18.0f;
                auto col = Palette::plate;
                bd.setPixelColour (x, y, Colour::fromRGB ((uint8) jlimit (0, 255, (int) (col.getRed() + n + glint * 6.0f)),
                                                          (uint8) jlimit (0, 255, (int) (col.getGreen() + n * 1.05f + glint * 7.0f)),
                                                          (uint8) jlimit (0, 255, (int) (col.getBlue() + n * 0.95f + glint * 8.0f))));
            }
        }
    }

    plate = Image (Image::ARGB, pw, ph, true);
    Graphics g (plate);
    g.drawImageAt (base, 0, 0);
    {
        Graphics::ScopedSaveState scaled (g);
        g.addTransform (AffineTransform::scale (scale));

        for (int i = 0; i < 9; ++i)
        {
            const float cx = rng.nextFloat() * W, cy = rng.nextFloat() * H, r = 40.0f + rng.nextFloat() * 120.0f;
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.05f), cx, cy, Colours::white.withAlpha (0.0f), cx + r, cy, true));
            g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
        }
        for (int i = 0; i < 18; ++i)
        {
            float cx = rng.nextFloat() * W, cy = rng.nextFloat() * H;
            if (rng.nextBool()) cx = rng.nextBool() ? rng.nextFloat() * 26.0f : W - rng.nextFloat() * 26.0f;
            else                cy = rng.nextBool() ? rng.nextFloat() * 18.0f : H - rng.nextFloat() * 18.0f;
            Path chip;
            const int pts = 7;
            const float rr = 1.2f + rng.nextFloat() * 3.2f;
            for (int k = 0; k < pts; ++k)
            {
                const float a = (float) k / pts * MathConstants<float>::twoPi;
                const auto pt = Point<float> (cx, cy).getPointOnCircumference (rr * (0.5f + rng.nextFloat()), a);
                k == 0 ? chip.startNewSubPath (pt) : chip.lineTo (pt);
            }
            chip.closeSubPath();
            g.setColour (Palette::metal.withAlpha (0.75f));
            g.fillPath (chip);
            g.setColour (Colours::black.withAlpha (0.25f));
            g.strokePath (chip, PathStrokeType (0.5f));
        }
        for (int i = 0; i < 70; ++i)
        {
            const Point<float> a (rng.nextFloat() * W, rng.nextFloat() * H);
            const float len = 6.0f + rng.nextFloat() * 40.0f, ang = rng.nextFloat() * MathConstants<float>::twoPi;
            g.setColour ((rng.nextFloat() < 0.75f ? Colours::white : Colours::black).withAlpha (0.05f + rng.nextFloat() * 0.12f));
            g.drawLine ({ a, a.getPointOnCircumference (len, ang) }, 0.5f);
        }

        // Pressed groove between top and lower section
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillRect (18.0f, (float) topH, (float) W - 36.0f, 1.6f);
        g.setColour (Colours::white.withAlpha (0.18f));
        g.fillRect (18.0f, (float) topH + 1.6f, (float) W - 36.0f, 1.0f);

        // Corner screws
        for (auto pt : { Point<float> (15, 15), Point<float> ((float) W - 15, 15), Point<float> (15, (float) H - 15), Point<float> ((float) W - 15, (float) H - 15) })
        {
            const float r = 6.0f;
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillEllipse (Rectangle<float> (r * 2 + 1.5f, r * 2 + 1.5f).withCentre (pt.translated (0.6f, 1.0f)));
            g.setGradientFill (ColourGradient (Colour (0xffd8d9d4), pt.x - r, pt.y - r, Colour (0xff6c6e6a), pt.x + r, pt.y + r, false));
            g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (pt));
            const float a = rng.nextFloat() * MathConstants<float>::pi;
            g.setColour (Colour (0xff2a2a28));
            g.drawLine ({ pt.getPointOnCircumference (r * 0.8f, a), pt.getPointOnCircumference (r * 0.8f, a + MathConstants<float>::pi) }, 1.6f);
        }
    }

    // ---- 2. Printed layer (labels, brackets, logo), then worn ----
    Image print (Image::ARGB, pw, ph, true);
    {
        Graphics pg (print);
        pg.addTransform (AffineTransform::scale (scale));
        pg.setColour (Palette::cream);

        // 金継ぎ, printed in the same cream ink as the rest, centred under the name
        {
            auto glyphs = Drawable::parseSVGPath (KintsugiGlyphs::path);
            const auto gb = glyphs.getBounds();
            const float targetH = 18.0f;
            const float sc = targetH / gb.getHeight();
            // centred under the KINTSUGI name (the name's letter-spacing leaves a trailing gap, so trim it)
            const float nameW = GlyphArrangement::getStringWidth (KintsugiFonts::serif (47.0f).withExtraKerningFactor (0.14f), "KINTSUGI");
            const float cxN = 62.0f + (nameW - 47.0f * 0.14f * 0.5f) * 0.5f;
            glyphs.applyTransform (AffineTransform::translation (-gb.getCentreX(), -gb.getCentreY()).scaled (sc).translated (cxN, 93.0f));
            pg.fillPath (glyphs);
        }

        // the maker's name, lower centre, below the knobs
        pg.setFont (KintsugiFonts::serif (8.5f).withExtraKerningFactor (0.42f));
        pg.drawText ("RELUCTANT", W / 2 - 110, H - 56, 220, 12, Justification::centred);
        pg.setFont (KintsugiFonts::serif (17.0f).withExtraKerningFactor (0.12f));
        pg.drawText ("ASTRONAUT", W / 2 - 130, H - 46, 260, 22, Justification::centred);

        pg.setFont (KintsugiFonts::serif (47.0f).withExtraKerningFactor (0.14f));
        pg.drawText ("KINTSUGI", 62, 28, 400, 54, Justification::centredLeft);
   // centred on the header, level with the logo

        // Section brackets:  |---- TITLE (lamp) ----|
        for (auto& s : sections)
        {
            const auto b = s.bounds;
            const float y = (float) b.getY();
            pg.setFont (KintsugiFonts::serif (13.0f).withExtraKerningFactor (0.22f));
            const auto title = s.title.toUpperCase();
            const float tw = GlyphArrangement::getStringWidth (pg.getCurrentFont(), title) + 14.0f;
            const float blockW = tw + (s.lamp != nullptr ? 54.0f : 0.0f);
            const float mid = (float) b.getCentreX();
            pg.drawText (title, Rectangle<float> (mid - blockW * 0.5f, y - 8.0f, tw, 16.0f), Justification::centred);
            const float side = mid - blockW * 0.5f - (float) b.getX() - 6;
            if (side > 6.0f)
            {
                pg.fillRect ((float) b.getX() + 6, y - 0.75f, side, 1.5f);
                pg.fillRect (mid + blockW * 0.5f, y - 0.75f, side, 1.5f);
                pg.fillRect ((float) b.getX() + 6, y - 0.75f, 1.5f, 8.0f);
                pg.fillRect ((float) b.getRight() - 7.5f, y - 0.75f, 1.5f, 8.0f);
            }
        }

        std::vector<std::pair<Rectangle<int>, Slider*>> rects;
        std::vector<String> names;
        std::vector<StringArray> labels;
        std::vector<int> steps;
        for (auto& k : knobs)
        {
            if (! k->printed) continue;
            rects.push_back ({ k->slider.getBounds(), &k->slider });
            names.push_back (k->name);
            labels.push_back (k->stepLabels);
            steps.push_back (k->steps);
        }
        drawPrint (pg, rects, names, labels, steps);

        // Printed words at the ends of some scales (COLD / HOT, DARK / OPEN, DRY / WET)
        for (auto& k : knobs)
        {
            if (k->marks.empty()) continue;
            const auto r = k->slider.getBounds();
            const auto c = r.toFloat().getCentre();
            const float R = (float) r.getWidth() * 0.5f * 0.80f;
            const auto rp = k->slider.getRotaryParameters();
            pg.setFont (KintsugiFonts::serif (8.5f, false).withExtraKerningFactor (0.04f));
            for (auto& [value, text] : k->marks)
            {
                const float prop = (float) k->slider.valueToProportionOfLength (value);
                const float a = rp.startAngleRadians + prop * (rp.endAngleRadians - rp.startAngleRadians);
                pg.drawText (text, Rectangle<float> (34.0f, 11.0f).withCentre (c.getPointOnCircumference (R + 21.0f, a)),
                             Justification::centred);
            }
        }

        // the unit beside the speed window
        {
            const auto wb = speedDrum.getBounds();
            pg.setFont (KintsugiFonts::serif (8.5f).withExtraKerningFactor (0.12f));
            pg.drawText ("IPS", Rectangle<int> (wb.getRight() + 3, wb.getY(), 26, wb.getHeight()), Justification::centredLeft);
        }

        // Fader scale
        {
            auto& s = output->slider;
            const auto b = s.getBounds();
            const float cx = (float) b.getCentreX();
            const float top = (float) b.getY() + faderThumb, len = (float) b.getHeight() - 2.0f * faderThumb;
            for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f })
            {
                const float yy = top + (1.0f - (float) s.valueToProportionOfLength (db)) * len;
                const bool zero = db == 0.0f;
                pg.fillRect (cx - (zero ? 26.0f : 23.0f), yy - 0.75f, zero ? 12.0f : 9.0f, 1.5f);
                pg.fillRect (cx + 14.0f, yy - 0.75f, zero ? 12.0f : 9.0f, 1.5f);
                pg.setFont (KintsugiFonts::serif (9.5f, zero));
                pg.drawText (db > 0 ? "+" + String ((int) db) : String ((int) db),
                             Rectangle<float> (cx - 62.0f, yy - 6.0f, 32.0f, 12.0f), Justification::centredRight);
            }
            for (float p = 0.0f; p <= 1.0001f; p += 1.0f / 24.0f)
                pg.fillRect (cx + 14.0f, top + p * len - 0.4f, 5.0f, 0.8f);
            pg.setFont (KintsugiFonts::serif (11.5f).withExtraKerningFactor (0.08f));
            pg.drawText ("OUTPUT", Rectangle<int> (b.getCentreX() - 50, b.getBottom() + 1, 100, 16), Justification::centred);
        }
    }
    wearPrint (print, 78, 1.0f);
    g.drawImageAt (print, 0, 0);

    // ---- 3. A life lived, and a careful repair ----
    {
        Graphics::ScopedSaveState scaled (g);
        g.addTransform (AffineTransform::scale (scale));
        const float w = (float) W, h = (float) H;
        const float pi = MathConstants<float>::pi;
        Random rng (8128);

        // history the repairer chose to leave: finger wear around the busiest knobs and the fader,
        // a cup ring, rubbed edges, scratches
        for (auto* k : { cDrive, vDrive, tSat, mix, iron })
        {
            const auto b = k->slider.getBounds().toFloat();
            Repair::fingerWear (g, b.getCentre(), b.getWidth() * 0.4f, 1.6f, 3.6f, k == tSat || k == cDrive ? 1.4f : 1.0f);
        }
        {
            const auto fb = output->slider.getBounds().toFloat();
            for (float side : { -1.0f, 1.0f })
            {
                g.setColour (Colour (0xffc2c6bc).withAlpha (0.07f));
                g.fillRoundedRectangle (Rectangle<float> (5.0f, fb.getHeight() * 0.45f).withCentre ({ fb.getCentreX() + side * 9.0f, fb.getCentreY() - 6.0f }), 2.5f);
            }
        }
        for (int i = 0; i < 260; ++i)   // edges rubbed by rack rails and hands
        {
            const bool horiz = rng.nextBool();
            const float t = rng.nextFloat();
            const Point<float> p = horiz ? Point<float> (t * w, rng.nextBool() ? rng.nextFloat() * 5.0f : h - rng.nextFloat() * 5.0f)
                                         : Point<float> (rng.nextBool() ? rng.nextFloat() * 5.0f : w - rng.nextFloat() * 5.0f, t * h);
            g.setColour (Colour (0xffb7bbb1).withAlpha (0.10f + 0.18f * rng.nextFloat()));
            g.fillEllipse (Rectangle<float> (1.0f + rng.nextFloat() * 3.0f, 0.8f + rng.nextFloat() * 1.6f).withCentre (p));
        }
        for (int i = 0; i < 26; ++i)   // longer scratches through the paint
        {
            const Point<float> a (rng.nextFloat() * w, rng.nextFloat() * h);
            const float len = 10.0f + rng.nextFloat() * 50.0f, ang = rng.nextFloat() * pi;
            Path sc;
            sc.startNewSubPath (a);
            sc.quadraticTo (a.getPointOnCircumference (len * 0.5f, ang + 0.05f), a.getPointOnCircumference (len, ang));
            g.setColour (Colour (0xffb9bcb4).withAlpha (0.12f + 0.2f * rng.nextFloat()));
            g.strokePath (sc, PathStrokeType (0.35f + 0.3f * rng.nextFloat()));
        }

        // dents, near the edges where a unit gets knocked; the one on the right edge is a real knock
        Repair::dent (g, { w * 0.5f - 191.0f, 14.0f }, 15.0f, 7.5f, 0.2f, -1.9f, 0.45f, 1.0f, 11);         // knocked from above
        Repair::dent (g, { w - 13.0f, 292.0f }, 10.0f, 27.0f, 0.08f, 0.15f, 0.55f, 1.6f, 12);  // a real knock on the edge
        Repair::dent (g, { 300.0f, h - 11.0f }, 14.0f, 7.0f, -0.25f, 1.2f, 0.7f, 0.9f, 13);     // a glancing blow

        // the footprints of the larger meters that were here before


        // the two corners that broke off, rebuilt in brass
        Repair::cornerInlay (g, { -6.0f, h + 6.0f }, 86.0f, 0.0f, pi * 0.5f, 6, 93);
        Repair::screw (g, { 15.0f, h - 15.0f }, 2.1f);
        Repair::cornerInlay (g, { w + 6.0f, -6.0f }, 62.0f, pi, pi * 1.5f, 4, 94);
        Repair::screw (g, { w - 15.0f, 15.0f }, 0.7f);

        // a worn stretch of the bottom edge, cut out square and let in with a slim brass strip
        // the brass strip let into the bottom edge: set to true to bring it back
        constexpr bool showEdgeStrip = false;
        if (showEdgeStrip)
            Repair::edgeStrip (g, { w * 0.5f + 90.0f, h - 46.0f, 196.0f, 46.0f }, 4, 95);

        // the Kintsugi knob's name, on an engraved brass plaque
        {
            const auto kb = drift->slider.getBounds().toFloat();
            Repair::plaque (g, Rectangle<float> (124.0f, 18.0f).withCentre ({ kb.getCentreX(), kb.getBottom() + 12.0f }), "INSTABILITY", 97);
        }

        // the repairer's mark, punched into the large brass corner
        // the repairer's mark: One Bright Pearl
        // the mark follows the curve of the brass, struck along the diagonal
        Repair::punchedText (g, "RESTORED BY", { 36.4f, h - 42.4f }, 4.3f, 0.785f, 21);
        Repair::punchedText (g, "OBP", { 30.1f, h - 36.1f }, 7.6f, 0.785f, 23);
        Repair::punchedText (g, String::fromUTF8 ("XI \xc2\xb7 MMXXVI"), { 24.4f, h - 30.4f }, 4.3f, 0.785f, 22);
        Repair::touchmark (g, { 46.0f, h - 13.0f }, 3.4f);
    }
}
