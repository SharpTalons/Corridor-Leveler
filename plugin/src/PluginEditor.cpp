// Corridor Leveler - plugin window.
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Layout (design size 1120 x 630): header strip; input peak meter on the left edge, output on
// the right; between them a scrolling RMS level graph on one even 0 to -70 dB scale, with a gain
// lane on top. The controls sit at the bottom, blended over the lower part of the graph.
#include "PluginEditor.h"

#include "Updates.h"

#include <cmath>
#include <vector>

using namespace juce;

namespace {

// ---------------------------------------------------------------- theme
// Backgrounds stay true black / true white; the hardware parts (knob caps, buttons, wells)
// get their own materials so they read as physical objects.
struct Theme {
    Colour bg, surface, sunk, graph, ink, muted, line, accent, accentSoft, boost, cut, limit, warn, ok;
    Colour capTop, capBottom, wellTop, wellBottom, hilite, edge;
    float shadow;  // drop-shadow strength
    bool dark;
    // role colours: each control keeps one colour on its knob and on its line in the graph
    Colour thr, resp, rng, inTrace, meterLo;
};
const Theme kLight{Colour(0xffffffff), Colour(0xffffffff), Colour(0xffebebeb), Colour(0xffffffff), Colour(0xff000000),
                   Colour(0xff5f6368), Colour(0xffdcdcdc), Colour(0xff17756a), Colour(0xffcfe5e1), Colour(0xff2672b5),
                   Colour(0xffc24d33), Colour(0xffc7302b), Colour(0xffb7862a), Colour(0xff2b7d47),
                   Colour(0xffffffff), Colour(0xffdfe1e4), Colour(0xffd3d5d9), Colour(0xffeef0f2), Colour(0xffffffff),
                   Colour(0xffbfc2c7), 0.28f, false,
                   Colour(0xffa86f00), Colour(0xff2f6fd6), Colour(0xffb8407c), Colour(0xff3d6fb0), Colour(0xff2b9a4f)};
const Theme kDark{Colour(0xff000000), Colour(0xff000000), Colour(0xff1c1c1c), Colour(0xff000000), Colour(0xffffffff),
                  Colour(0xffa3a3a3), Colour(0xff2e2e2e), Colour(0xff4cc2ae), Colour(0xff1f3b37), Colour(0xff62a9e8),
                  Colour(0xffee7b5f), Colour(0xfff0605a), Colour(0xffe0b354), Colour(0xff6cc58b),
                  Colour(0xff34373c), Colour(0xff141518), Colour(0xff030303), Colour(0xff121315), Colour(0x26ffffff),
                  Colour(0xff050505), 0.7f, true,
                  Colour(0xfff2b84b), Colour(0xff6aa8ff), Colour(0xffec7cbd), Colour(0xff7fb0ea), Colour(0xff4fd27a)};

Font mono(float h, bool bold = false) {
    return Font(FontOptions(Font::getDefaultMonospacedFontName(), h, bold ? Font::bold : Font::plain));
}
Font sans(float h, bool bold = false) { return Font(FontOptions(h, bold ? Font::bold : Font::plain)); }

String fmtDb(float v, int dec = 1, bool sign = false) {
    String s = String(std::abs(v), dec);
    const bool zero = s == String(0.0, dec);
    if (v < 0 && !zero) return "-" + s;
    if (sign && v > 0 && !zero) return "+" + s;
    return s;
}

constexpr float kTop = 54.f, kDbRange = 70.f;  // graph and meters share this 0 dB line
constexpr int kHist = 500;  // 10 s at 20 ms per entry
constexpr float kPi = MathConstants<float>::pi;

// ---------------------------------------------------------------- icons (vector, drawn at any size)
void drawIcon(Graphics& g, const String& name, Rectangle<float> area, Colour col) {
    const float s = jmin(area.getWidth(), area.getHeight());
    const auto b = area.withSizeKeepingCentre(s, s);
    const float cx = b.getCentreX(), cy = b.getCentreY(), x = b.getX(), y = b.getY();
    const PathStrokeType st(jmax(1.4f, s * 0.11f), PathStrokeType::curved, PathStrokeType::rounded);
    g.setColour(col);
    Path p;
    if (name == "power") {
        p.addCentredArc(cx, cy + s * 0.04f, s * 0.36f, s * 0.36f, 0.f, 0.7f, 2.f * kPi - 0.7f, true);
        p.startNewSubPath(cx, y + s * 0.06f); p.lineTo(cx, cy - s * 0.02f);
        g.strokePath(p, st);
    } else if (name == "moon") {
        Graphics::ScopedSaveState ss(g);
        Path outer; outer.addEllipse(cx - s * 0.38f, cy - s * 0.38f, s * 0.76f, s * 0.76f);
        g.reduceClipRegion(outer);
        Path both; both.addEllipse(cx - s * 0.38f, cy - s * 0.38f, s * 0.76f, s * 0.76f);
        both.addEllipse(cx - s * 0.12f, cy - s * 0.58f, s * 0.72f, s * 0.72f);
        both.setUsingNonZeroWinding(false);
        g.fillPath(both);
    } else if (name == "sun") {
        g.fillEllipse(cx - s * 0.19f, cy - s * 0.19f, s * 0.38f, s * 0.38f);
        for (int k = 0; k < 8; ++k) {
            const float a = float(k) * kPi / 4.f;
            p.startNewSubPath(cx + std::sin(a) * s * 0.31f, cy - std::cos(a) * s * 0.31f);
            p.lineTo(cx + std::sin(a) * s * 0.46f, cy - std::cos(a) * s * 0.46f);
        }
        g.strokePath(p, PathStrokeType(jmax(1.2f, s * 0.09f), PathStrokeType::curved, PathStrokeType::rounded));
    } else if (name == "glide") {  // gain gliding down to a resting line
        p.startNewSubPath(x + s * 0.08f, y + s * 0.18f);
        p.cubicTo(x + s * 0.45f, y + s * 0.18f, x + s * 0.48f, y + s * 0.72f, x + s * 0.92f, y + s * 0.72f);
        g.strokePath(p, st);
        g.setColour(col.withMultipliedAlpha(0.5f));
        g.fillRect(x + s * 0.08f, y + s * 0.86f, s * 0.84f, jmax(1.f, s * 0.07f));
    } else if (name == "wave") {
        for (int k = 0; k <= 24; ++k) {
            const float t = float(k) / 24.f, px = x + s * (0.06f + 0.88f * t), py = cy - std::sin(t * 2.f * kPi) * s * 0.26f;
            if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
        }
        g.strokePath(p, st);
    } else if (name == "lock") {
        g.fillRoundedRectangle(x + s * 0.2f, y + s * 0.44f, s * 0.6f, s * 0.46f, s * 0.08f);
        p.startNewSubPath(x + s * 0.32f, y + s * 0.46f);
        p.lineTo(x + s * 0.32f, y + s * 0.32f);
        p.addCentredArc(cx, y + s * 0.32f, s * 0.18f, s * 0.18f, 0.f, -kPi * 0.5f, kPi * 0.5f, false);
        p.lineTo(x + s * 0.68f, y + s * 0.46f);
        g.strokePath(p, st);
    } else if (name == "target") {
        g.drawEllipse(cx - s * 0.34f, cy - s * 0.34f, s * 0.68f, s * 0.68f, jmax(1.3f, s * 0.1f));
        g.fillEllipse(cx - s * 0.1f, cy - s * 0.1f, s * 0.2f, s * 0.2f);
        for (int k = 0; k < 4; ++k) {
            const float a = float(k) * kPi * 0.5f;
            p.startNewSubPath(cx + std::sin(a) * s * 0.36f, cy - std::cos(a) * s * 0.36f);
            p.lineTo(cx + std::sin(a) * s * 0.5f, cy - std::cos(a) * s * 0.5f);
        }
        g.strokePath(p, st);
    } else if (name == "download") {
        p.startNewSubPath(cx, y + s * 0.08f); p.lineTo(cx, y + s * 0.62f);
        p.startNewSubPath(cx - s * 0.24f, y + s * 0.4f); p.lineTo(cx, y + s * 0.64f); p.lineTo(cx + s * 0.24f, y + s * 0.4f);
        p.startNewSubPath(x + s * 0.12f, y + s * 0.74f); p.lineTo(x + s * 0.12f, y + s * 0.9f); p.lineTo(x + s * 0.88f, y + s * 0.9f); p.lineTo(x + s * 0.88f, y + s * 0.74f);
        g.strokePath(p, st);
    } else if (name == "refresh") {
        p.addCentredArc(cx, cy, s * 0.34f, s * 0.34f, 0.f, 0.5f, 2.f * kPi - 0.3f, true);
        g.strokePath(p, st);
        Path a; a.addTriangle(cx + s * 0.06f, y + s * 0.04f, cx + s * 0.06f, y + s * 0.36f, cx + s * 0.3f, y + s * 0.2f);
        g.fillPath(a);
    } else if (name == "close") {
        p.startNewSubPath(x + s * 0.25f, y + s * 0.25f); p.lineTo(x + s * 0.75f, y + s * 0.75f);
        p.startNewSubPath(x + s * 0.75f, y + s * 0.25f); p.lineTo(x + s * 0.25f, y + s * 0.75f);
        g.strokePath(p, st);
    } else if (name == "logo") {  // a level held between two walls: the corridor
        g.fillRoundedRectangle(x, y + s * 0.1f, s, s * 0.1f, s * 0.05f);
        g.fillRoundedRectangle(x, y + s * 0.8f, s, s * 0.1f, s * 0.05f);
        for (int k = 0; k <= 30; ++k) {
            const float t = float(k) / 30.f, env = 1.f - t * 0.72f;
            const float px = x + s * t, py = cy - std::sin(t * 4.f * kPi) * s * 0.24f * env;
            if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
        }
        g.strokePath(p, PathStrokeType(jmax(1.4f, s * 0.08f), PathStrokeType::curved, PathStrokeType::rounded));
    }
}

// raised cap (a physical button) and recessed well (a pressed button / display)
void drawRaised(Graphics& g, const Theme& t, Rectangle<float> r, float rad, bool hover) {
    Path p; p.addRoundedRectangle(r, rad);
    DropShadow(Colours::black.withAlpha(t.shadow * 0.7f), 7, {0, 2}).drawForPath(g, p);
    g.setGradientFill(ColourGradient(t.capTop.brighter(hover ? 0.08f : 0.f), 0.f, r.getY(), t.capBottom, 0.f, r.getBottom(), false));
    g.fillPath(p);
    g.setColour(t.hilite);
    g.drawLine(r.getX() + rad, r.getY() + 1.f, r.getRight() - rad, r.getY() + 1.f, 1.f);
    g.setColour(t.edge); g.strokePath(p, PathStrokeType(1.f));
}
void drawWell(Graphics& g, const Theme& t, Rectangle<float> r, float rad, Colour glow = Colours::transparentBlack) {
    Path p; p.addRoundedRectangle(r, rad);
    if (!glow.isTransparent()) DropShadow(glow.withAlpha(0.35f), 10, {}).drawForPath(g, p);
    g.setGradientFill(ColourGradient(t.wellTop, 0.f, r.getY(), t.wellBottom, 0.f, r.getBottom(), false));
    g.fillPath(p);
    {
        Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(p);
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(t.dark ? 0.6f : 0.16f), 0.f, r.getY(),
                                         Colours::transparentBlack, 0.f, r.getY() + jmin(14.f, r.getHeight() * 0.5f), false));
        g.fillRect(r);
    }
    g.setColour(glow.isTransparent() ? t.edge : glow.withAlpha(0.7f));
    g.strokePath(p, PathStrokeType(1.f));
    g.setColour(t.hilite.withMultipliedAlpha(0.6f));
    g.drawLine(r.getX() + rad, r.getBottom() + 0.5f, r.getRight() - rad, r.getBottom() + 0.5f, 1.f);
}

// ---------------------------------------------------------------- look and feel
class Look : public LookAndFeel_V4 {
public:
    const Theme* t = &kDark;

    // A machined knob: printed scale, recessed track with a lit value arc, knurled cap with a
    // shaded face and an engraved pointer.
    void drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s) override {
        const Theme& c = *t;
        const float size = float(jmin(w, h)) - 4.f;
        const float cx = float(x) + float(w) * 0.5f, cy = float(y) + float(h) * 0.5f, R = size * 0.5f;
        const float tw = jmax(4.f, R * 0.12f), rr = R * 0.84f;
        const float ang = a0 + pos * (a1 - a0);

        // printed scale
        for (int k = 0; k <= 10; ++k) {
            const float a = a0 + float(k) / 10.f * (a1 - a0), r1 = R * (k % 5 == 0 ? 0.93f : 0.96f);
            g.setColour(c.muted.withAlpha(k % 5 == 0 ? 0.8f : 0.45f));
            g.drawLine(cx + r1 * std::sin(a), cy - r1 * std::cos(a), cx + R * std::sin(a), cy - R * std::cos(a), 1.1f);
        }
        // recessed track
        Path track; track.addCentredArc(cx, cy, rr, rr, 0.f, a0, a1, true);
        g.setColour(c.dark ? Colour(0xff0b0b0c) : c.wellTop);
        g.strokePath(track, PathStrokeType(tw + 2.f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour(c.sunk);
        g.strokePath(track, PathStrokeType(tw - 1.f, PathStrokeType::curved, PathStrokeType::rounded));
        // lit value arc with a soft glow
        if (pos > 0.002f) {
            const String role = s.getProperties().getWithDefault("role", "");
            const Colour rc = role == "thr" ? c.thr : role == "tgt" ? c.accent : role == "resp" ? c.resp
                            : role == "rng" ? c.rng : role == "ceil" ? c.limit : c.ink;
            Path val; val.addCentredArc(cx, cy, rr, rr, 0.f, a0, ang, true);
            g.setColour(rc.withAlpha(c.dark ? 0.25f : 0.18f));
            g.strokePath(val, PathStrokeType(tw * 2.4f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour(rc);
            g.strokePath(val, PathStrokeType(tw - 1.f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        // cap
        const float cr = R * 0.64f;
        Path skirt; skirt.addEllipse(cx - cr, cy - cr, cr * 2.f, cr * 2.f);
        DropShadow(Colours::black.withAlpha(c.shadow * 0.85f), int(R * 0.42f), {0, int(R * 0.16f)}).drawForPath(g, skirt);
        g.setGradientFill(ColourGradient(c.capTop.darker(0.1f), cx, cy - cr, c.capBottom.darker(c.dark ? 0.4f : 0.15f), cx, cy + cr, false));
        g.fillPath(skirt);
        for (int k = 0; k < 48; ++k) {  // knurling
            const float a = float(k) / 48.f * 2.f * kPi;
            g.setColour((k % 2 ? Colours::black : Colours::white).withAlpha(c.dark ? 0.16f : 0.22f));
            g.drawLine(cx + cr * 0.86f * std::sin(a), cy - cr * 0.86f * std::cos(a), cx + cr * 0.99f * std::sin(a), cy - cr * 0.99f * std::cos(a), 1.f);
        }
        const float fr = cr * 0.82f;
        Path face; face.addEllipse(cx - fr, cy - fr, fr * 2.f, fr * 2.f);
        g.setGradientFill(ColourGradient(c.capTop, cx - fr * 0.6f, cy - fr * 0.8f, c.capBottom, cx + fr * 0.6f, cy + fr * 0.9f, false));
        g.fillPath(face);
        ColourGradient spec(Colours::white.withAlpha(c.dark ? 0.12f : 0.75f), cx - fr * 0.35f, cy - fr * 0.45f,
                            Colours::white.withAlpha(0.f), cx - fr * 0.35f + fr * 0.75f, cy - fr * 0.45f, true);
        g.setGradientFill(spec); g.fillPath(face);
        g.setColour(c.hilite); g.strokePath(face, PathStrokeType(1.f));
        if (s.isMouseOverOrDragging()) { g.setColour(c.ink.withAlpha(0.45f)); g.strokePath(skirt, PathStrokeType(1.2f)); }
        // engraved pointer
        const float p0 = fr * 0.22f, p1 = fr * 0.86f, pw = jmax(2.f, R * 0.07f);
        const float sx = std::sin(ang), sy = -std::cos(ang);
        g.setColour(Colours::black.withAlpha(c.dark ? 0.7f : 0.25f));
        g.drawLine(cx + p0 * sx + 0.8f, cy + p0 * sy + 0.8f, cx + p1 * sx + 0.8f, cy + p1 * sy + 0.8f, pw);
        g.setColour(c.dark ? c.ink : Colour(0xff1a1a1a));
        g.drawLine(cx + p0 * sx, cy + p0 * sy, cx + p1 * sx, cy + p1 * sy, pw);

        // live level marker on the track (Target shows the output level, Threshold the input)
        const var live = s.getProperties()["live"];
        if (!live.isVoid()) {
            const double lv = double(live);
            if (lv > s.getMinimum() - 15.0) {
                const float lp = float(jlimit(0.0, 1.0, (lv - s.getMinimum()) / (s.getMaximum() - s.getMinimum())));
                const float la = a0 + lp * (a1 - a0), lr = tw * 0.55f;
                const float lx = cx + rr * std::sin(la), ly = cy - rr * std::cos(la);
                g.setColour(c.ink.withAlpha(0.25f)); g.fillEllipse(lx - lr * 1.8f, ly - lr * 1.8f, lr * 3.6f, lr * 3.6f);
                g.setColour(c.ink); g.fillEllipse(lx - lr, ly - lr, lr * 2.f, lr * 2.f);
            }
        }
    }

    Label* createSliderTextBox(Slider& s) override {
        auto* l = LookAndFeel_V4::createSliderTextBox(s);
        l->setFont(mono(15.f, true));
        l->setJustificationType(Justification::centred);
        return l;
    }

    Colour toneOf(Button& b) const {
        const String tone = b.getProperties().getWithDefault("tone", "accent");
        return tone == "warn" ? t->warn : tone == "accent" ? t->accent : t->ink;
    }

    void drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(2.f);
        const bool on = b.getToggleState();
        const float rad = b.getProperties().getWithDefault("pill", false) ? r.getHeight() * 0.5f : 7.f;
        if (b.getProperties().getWithDefault("segment", false)) {  // sits in a recessed track drawn by its parent
            if (on) drawRaised(g, *t, r, rad, over);
            return;
        }
        if (on || down) drawWell(g, *t, r, rad, on ? toneOf(b) : Colours::transparentBlack);
        else drawRaised(g, *t, r, rad, over);
    }

    void drawButtonText(Graphics& g, TextButton& b, bool, bool) override {
        const bool on = b.getToggleState(), seg = b.getProperties().getWithDefault("segment", false);
        const bool accentText = b.getProperties().getWithDefault("accentText", false);
        const Colour col = on ? (seg ? t->ink : toneOf(b)) : (seg ? t->muted : (!b.isEnabled() ? t->muted : accentText ? t->accent : t->ink));
        const auto r = b.getLocalBounds().toFloat();
        const String icon = b.getProperties().getWithDefault("icon", "");
        const Font f = sans(float(b.getProperties().getWithDefault("fontSize", 13.0)), true);
        g.setFont(f);
        if (icon.isEmpty()) { g.setColour(col); g.drawText(b.getButtonText(), r, Justification::centred); return; }
        const float is = jmin(16.f, r.getHeight() * 0.5f);
        if (b.getButtonText().isEmpty()) { drawIcon(g, icon, r.withSizeKeepingCentre(is, is), seg && on ? t->accent : col); return; }
        const float tw = GlyphArrangement::getStringWidth(f, b.getButtonText());
        const float x0 = r.getCentreX() - (is + 7.f + tw) * 0.5f;
        drawIcon(g, icon, Rectangle<float>(x0, r.getCentreY() - is * 0.5f, is, is), seg && on ? t->accent : col);
        g.setColour(col);
        g.drawText(b.getButtonText(), Rectangle<float>(x0 + is + 7.f, r.getY(), tw + 4.f, r.getHeight()), Justification::centredLeft);
    }

    void apply() {
        setColour(Slider::textBoxTextColourId, t->ink);
        setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour(Slider::textBoxHighlightColourId, t->accentSoft);
        setColour(Label::textColourId, t->ink);
        setColour(Label::textWhenEditingColourId, t->ink);
        setColour(Label::backgroundWhenEditingColourId, t->sunk);
        setColour(Label::outlineWhenEditingColourId, t->accent);
        setColour(TextEditor::textColourId, t->ink);
        setColour(TextEditor::highlightColourId, t->accentSoft);
        setColour(CaretComponent::caretColourId, t->accent);
        setColour(TooltipWindow::backgroundColourId, t->capBottom);
        setColour(TooltipWindow::textColourId, t->ink);
    }
};

// ---------------------------------------------------------------- knob
class Knob : public Component {
public:
    Knob(AudioProcessorValueTreeState& st, const char* id, String title_, String unit, int dec, float dialSize, const char* role = "")
        : title(std::move(title_)), dial(dialSize) {
        slider.getProperties().set("role", role);
        slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters(MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle(Slider::TextBoxBelow, false, 104, 20);
        slider.setMouseDragSensitivity(220);
        addAndMakeVisible(slider);
        att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(st, id, slider);
        if (auto* p = st.getParameter(id)) slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
        slider.textFromValueFunction = [dec, unit](double v) { return fmtDb(float(v), dec) + (unit.isEmpty() ? "" : " " + unit); };
        slider.valueFromTextFunction = [](const String& s) { return s.retainCharacters("-+.0123456789").getDoubleValue(); };
        slider.updateText();
        setInterceptsMouseClicks(false, true);
    }
    void lookAndFeelChanged() override {
        // the value box is rebuilt by the slider; re-apply the text so the new colours show
        slider.updateText();
    }
    void setLive(float db) { slider.getProperties().set("live", db); slider.repaint(); }
    int preferredWidth() const { return int(dial) + 34; }
    int preferredHeight() const { return int(dial) + 44; }
    void resized() override {
        auto r = getLocalBounds();
        r.removeFromBottom(18);
        slider.setBounds(r);
    }
    void paint(Graphics& g) override {
        auto* lf = dynamic_cast<Look*>(&getLookAndFeel());
        const Theme& t = lf ? *lf->t : kDark;
        g.setFont(sans(14.5f, true));
        g.setColour(t.ink);
        g.drawText(title, getLocalBounds().removeFromBottom(18), Justification::centred);
    }
    Slider slider;

private:
    String title;
    float dial;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> att;
};

// ---------------------------------------------------------------- graph
class Graph : public Component {
public:
    Graph(CorridorAudioProcessor& p, const Theme*& th) : proc(p), t(th) { hist.reserve(kHist); }

    std::vector<corridor::Snapshot> hist;
    bool analyzing = false;
    float analyzeSeconds = 0.f;

    float yOf(float v) const { return kTop + jlimit(0.f, kDbRange, -v) / kDbRange * (float(getHeight()) - 6.f - kTop); }
    float vOf(float y) const { return -jlimit(0.f, kDbRange, (y - kTop) / (float(getHeight()) - 6.f - kTop) * kDbRange); }

    void paint(Graphics& g) override {
        const Theme& c = *t;
        const float W = float(getWidth()), H = float(getHeight()), scaleW = 34.f, x1 = W - scaleW;
        g.fillAll(c.graph);
        if (c.dark) {  // faint centre lift, like a lit screen
            g.setGradientFill(ColourGradient(Colour(0xff0c0d0f), W * 0.5f, H * 0.4f, c.graph, 0.f, 0.f, true));
            g.fillRect(getLocalBounds().toFloat());
        }
        // grid + scale
        g.setFont(sans(12.f));
        for (int v = 0; v >= -66; v -= 6) {
            const float y = std::round(yOf(float(v))) + 0.5f;
            g.setColour(c.muted.withAlpha(v % 12 == 0 ? 0.22f : 0.11f)); g.fillRect(0.f, y, x1, 1.f);
            g.setColour(c.muted); g.drawText(v == 0 ? "0" : String(v), Rectangle<float>(x1, y - 7.f, scaleW - 5.f, 14.f), Justification::centredRight);
        }
        // traces
        const int n = int(hist.size());
        const float sx = x1 / float(kHist - 1);
        auto X = [&](int k) { return x1 - float(n - 1 - k) * sx; };
        if (n > 1) {
            Path fillP; fillP.startNewSubPath(X(0), H);
            Path inP, outP;
            for (int k = 0; k < n; ++k) {
                const float yi = yOf(hist[size_t(k)].inRms), yo = yOf(hist[size_t(k)].outRms);
                fillP.lineTo(X(k), yi);
                if (k == 0) { inP.startNewSubPath(X(k), yi); outP.startNewSubPath(X(k), yo); }
                else { inP.lineTo(X(k), yi); outP.lineTo(X(k), yo); }
            }
            fillP.lineTo(X(n - 1), H); fillP.closeSubPath();
            g.setColour(c.inTrace.withAlpha(c.dark ? 0.14f : 0.12f)); g.fillPath(fillP);
            g.setColour(c.inTrace.withAlpha(0.9f)); g.strokePath(inP, PathStrokeType(1.4f, PathStrokeType::curved));
            g.setColour(c.accent); g.strokePath(outP, PathStrokeType(2.f, PathStrokeType::mitered, PathStrokeType::rounded));
        }
        // analyze shading
        if (analyzing && n > 1) {
            const int span = jmin(n - 1, int(analyzeSeconds * 50.f));
            g.setColour(c.accent.withAlpha(0.08f));
            g.fillRect(Rectangle<float>(X(n - 1 - span), kTop, X(n - 1) - X(n - 1 - span), H - kTop));
        }
        // reference lines
        auto& st = proc.state();
        auto line = [&](const char* id, Colour col, float d1, float d2) {
            const float y = std::round(yOf(st.getRawParameterValue(id)->load())) + 0.5f;
            const bool hot = hover == id || dragId == id;
            if (d1 <= 0.f) { g.setColour(col); g.fillRect(0.f, y - (hot ? 1.f : 0.6f), x1, hot ? 2.2f : 1.3f); }
            else { const float dashes[] = {d1, d2}; g.setColour(col); g.drawDashedLine(Line<float>(0.f, y, x1, y), dashes, 2, hot ? 2.2f : 1.3f); }
        };
        line(ids::ceiling, c.limit, 2.f, 3.f);
        line(ids::threshold, c.thr, 6.f, 4.f);
        line(ids::target, c.accent, 0.f, 0.f);

        // legend for the two traces, in the band above 0 dB
        g.setFont(sans(13.f));
        g.setColour(c.inTrace); g.fillRoundedRectangle(14.f, 24.f, 18.f, 3.f, 1.5f);
        g.setColour(c.ink); g.drawText("Input", Rectangle<float>(38.f, 17.f, 60.f, 17.f), Justification::centredLeft);
        g.setColour(c.accent); g.fillRoundedRectangle(100.f, 24.f, 18.f, 3.f, 1.5f);
        g.setColour(c.ink); g.drawText("Output", Rectangle<float>(124.f, 17.f, 70.f, 17.f), Justification::centredLeft);
        g.setColour(c.muted); g.drawText("RMS, last 10 s", Rectangle<float>(190.f, 17.f, 160.f, 17.f), Justification::centredLeft);

        // inner shadow: the graph sits recessed in the panel like a screen
        const float sh = c.dark ? 0.75f : 0.14f;
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(sh), 0.f, 0.f, Colours::transparentBlack, 0.f, 16.f, false));
        g.fillRect(0.f, 0.f, W, 16.f);
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(sh * 0.7f), 0.f, 0.f, Colours::transparentBlack, 10.f, 0.f, false));
        g.fillRect(0.f, 0.f, 10.f, H);
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(sh * 0.7f), W, 0.f, Colours::transparentBlack, W - 10.f, 0.f, false));
        g.fillRect(W - 10.f, 0.f, 10.f, H);
    }

    // Line labels, drawn above the glass panel. A label whose line runs under the panel rests on
    // the panel's top edge with a small arrow, so it is always readable.
    float chipLimit = 1.0e6f;  // graph-local y of the glass panel's top
    void paintTags(Graphics& g) {
        const Theme& c = *t;
        auto& st = proc.state();
        auto chip = [&](const char* id, const String& label, Colour col) {
            const float v = st.getRawParameterValue(id)->load();
            float y = std::round(yOf(v)) + 0.5f;
            const bool under = y > chipLimit - 12.f;
            if (under) y = chipLimit - 14.f;
            const bool hot = hover == id || dragId == id;
            const String txt = label + "  " + fmtDb(v, 1);
            auto f = sans(13.f, true); g.setFont(f);
            const float tw = GlyphArrangement::getStringWidth(f, txt) + (under ? 30.f : 16.f);
            Rectangle<float> box(8.f, y - 10.f, tw, 20.f);
            Path bp; bp.addRoundedRectangle(box, 5.f);
            g.setColour(hot ? col : (c.dark ? Colour(0xf20a0b0d) : Colour(0xf7ffffff))); g.fillPath(bp);
            g.setColour(col); g.strokePath(bp, PathStrokeType(1.2f));
            g.setColour(hot ? c.graph : col);
            g.drawText(txt, box.withTrimmedLeft(8.f), Justification::centredLeft);
            if (under) {  // the line is below: point at it
                Path a; const float ax = box.getRight() - 13.f, ay = box.getCentreY();
                a.addTriangle(ax - 4.f, ay - 2.f, ax + 4.f, ay - 2.f, ax, ay + 3.f);
                g.fillPath(a);
            }
        };
        chip(ids::ceiling, "Ceiling", c.limit);
        chip(ids::threshold, "Threshold", c.thr);
        chip(ids::target, "Target", c.accent);
    }

    const char* hit(float y) const {
        const char* best = nullptr; float bd = 9.f;
        for (const char* id : {ids::target, ids::threshold, ids::ceiling}) {
            const float d = std::abs(yOf(proc.state().getRawParameterValue(id)->load()) - y);
            if (d < bd) { bd = d; best = id; }
        }
        return best;
    }
    void mouseMove(const MouseEvent& e) override {
        const char* h = hit(float(e.y));
        if (h != hover) { hover = h; repaint(); }
        setMouseCursor(h ? MouseCursor::UpDownResizeCursor : MouseCursor::NormalCursor);
    }
    void mouseExit(const MouseEvent&) override { hover = nullptr; repaint(); }
    void mouseDown(const MouseEvent& e) override {
        dragId = hit(float(e.y));
        if (dragId) if (auto* p = proc.state().getParameter(dragId)) { p->beginChangeGesture(); mouseDrag(e); }
    }
    void mouseDrag(const MouseEvent& e) override {
        if (!dragId) return;
        if (auto* p = proc.state().getParameter(dragId)) p->setValueNotifyingHost(p->convertTo0to1(vOf(float(e.y))));
        repaint();
    }
    void mouseUp(const MouseEvent&) override {
        if (dragId) if (auto* p = proc.state().getParameter(dragId)) p->endChangeGesture();
        dragId = nullptr;
    }

private:
    CorridorAudioProcessor& proc;
    const Theme*& t;
    const char* hover = nullptr;
    const char* dragId = nullptr;
};

class TagLayer : public Component {
public:
    explicit TagLayer(Graph& gr) : graph(gr) { setInterceptsMouseClicks(false, false); }
    void paint(Graphics& g) override { graph.paintTags(g); }

private:
    Graph& graph;
};

// ---------------------------------------------------------------- peak meter (dBFS)
class Meter : public Component {
public:
    Meter(bool isInput, const Theme*& th, corridor::Processor& d) : input(isInput), t(th), dsp(d) {}
    float peak = -120.f, hold = -120.f;
    void paint(Graphics& g) override {
        const Theme& c = *t;
        const float W = float(getWidth()), H = float(getHeight()), top = kTop, bot = H - 6.f;
        g.setGradientFill(ColourGradient(c.dark ? Colour(0xff0d0e10) : Colour(0xfff6f7f8), 0.f, 0.f, c.bg, 0.f, H, false));
        g.fillAll();
        g.setColour(c.edge); g.fillRect(input ? W - 1.f : 0.f, 0.f, 1.f, H);
        auto y = [&](float v) { return top + jlimit(0.f, kDbRange, -v) / kDbRange * (bot - top); };
        const float bw = 12.f, bx = input ? W - bw - 7.f : 7.f;
        drawWell(g, c, Rectangle<float>(bx - 3.f, top - 3.f, bw + 6.f, bot - top + 6.f), 4.f);
        // LED segments, lit from the bottom up to the current peak
        const float segH = 3.f, gap = 1.6f;
        for (float yy = bot - segH; yy >= top - 0.5f; yy -= segH + gap) {
            const float v = -(yy + segH * 0.5f - top) / (bot - top) * kDbRange;
            const Colour col = v > -6.f ? c.limit : v > -18.f ? c.warn : c.meterLo;
            const bool lit = v <= peak, isHold = hold > -70.f && std::abs(v - hold) < kDbRange / (bot - top) * (segH + gap) * 0.5f;
            g.setColour(lit || isHold ? col : col.withAlpha(c.dark ? 0.1f : 0.13f));
            g.fillRoundedRectangle(bx, yy, bw, segH, 1.f);
        }
        g.setColour(Colours::white.withAlpha(c.dark ? 0.05f : 0.25f));
        g.fillRect(bx, top, bw * 0.4f, bot - top);  // glass
        // the same marks as the graph: every 6 dB from 0 to -66, at the same heights
        g.setFont(sans(11.f));
        for (int v = 0; v >= -66; v -= 6) {
            const float yy = std::round(y(float(v))) + 0.5f;
            const bool major = v % 12 == 0;
            g.setColour(c.muted.withAlpha(major ? 0.9f : 0.5f));
            g.fillRect(input ? bx - 6.f : bx + bw + 3.f, yy, major ? 4.f : 3.f, 1.f);
            g.setColour(major ? c.ink.withAlpha(0.85f) : c.muted);
            g.drawText(v == 0 ? "0" : String(v), Rectangle<float>(input ? 0.f : bx + bw + 8.f, yy - 6.f, input ? bx - 9.f : W - bx - bw - 8.f, 12.f),
                       input ? Justification::centredRight : Justification::centredLeft);
        }
        auto hb = Rectangle<float>(5.f, 8.f, W - 10.f, 20.f);
        drawWell(g, c, hb, 4.f);
        g.setFont(mono(11.f, true)); g.setColour(hold > -0.1f ? c.limit : c.ink);
        g.drawText(hold > -70.f ? fmtDb(hold, 1) : "-", hb, Justification::centred);
        g.setFont(sans(11.f, true)); g.setColour(c.muted);
        g.drawText(input ? "In" : "Out", Rectangle<float>(0.f, 32.f, W, 14.f), Justification::centred);
    }
    void mouseDown(const MouseEvent&) override { dsp.resetPeakHolds(); }

private:
    bool input;
    const Theme*& t;
    corridor::Processor& dsp;
};

// ---------------------------------------------------------------- deck (controls blended over the graph)
class Deck : public Component {
public:
    Deck(const Theme*& th, Component& behind) : t(th), backdrop(behind) { setInterceptsMouseClicks(false, true); }
    String gainText = "+0.0";
    int gainSign = 0;
    String latencyText;
    Rectangle<int> gainBox, modeTrack;
    void paint(Graphics& g) override {
        const Theme& c = *t;
        // frosted glass: the graph behind, rendered small and stretched back (a cheap blur), then tinted
        const auto panel = getLocalBounds().toFloat().reduced(10.f, 6.f);
        Path p; p.addRoundedRectangle(panel, 18.f);
        DropShadow(Colours::black.withAlpha(c.dark ? 0.55f : 0.18f), 22, {0, 6}).drawForPath(g, p);
        {
            Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(p);
            const auto src = backdrop.getLocalArea(this, panel.toNearestInt());
            const Image blur = backdrop.createComponentSnapshot(src.getIntersection(backdrop.getLocalBounds()), false, 0.14f);
            g.setImageResamplingQuality(Graphics::highResamplingQuality);
            g.drawImage(blur, panel, RectanglePlacement::stretchToFit);
            g.setColour(c.dark ? Colour(0xb30b0c10) : Colour(0xbffbfcfe)); g.fillPath(p);
            g.setGradientFill(ColourGradient(Colours::white.withAlpha(c.dark ? 0.07f : 0.55f), 0.f, panel.getY(),
                                             Colours::white.withAlpha(0.f), 0.f, panel.getY() + panel.getHeight() * 0.45f, false));
            g.fillPath(p);
        }
        g.setColour(Colours::white.withAlpha(c.dark ? 0.12f : 0.9f)); g.strokePath(p, PathStrokeType(1.f));
        g.setColour(Colours::white.withAlpha(c.dark ? 0.22f : 1.f));
        g.drawLine(panel.getX() + 18.f, panel.getY() + 0.5f, panel.getRight() - 18.f, panel.getY() + 0.5f, 1.f);
        if (!modeTrack.isEmpty()) drawWell(g, c, modeTrack.toFloat(), float(modeTrack.getHeight()) * 0.5f);
        // applied gain, in an inset display
        const auto disp = gainBox.withHeight(42).toFloat();
        drawWell(g, c, disp, 8.f);
        g.setFont(mono(24.f, true));
        g.setColour(gainSign > 0 ? c.boost : gainSign < 0 ? c.cut : c.ink); g.drawText(gainText + " dB", disp, Justification::centred);
        g.setFont(sans(13.5f, true)); g.setColour(c.ink);
        g.drawText("Applied gain", gainBox.withTrimmedTop(46).withHeight(16), Justification::centred);
        // latency, bottom-right corner
        g.setFont(sans(12.f)); g.setColour(c.muted);
        g.drawText(latencyText, getLocalBounds().removeFromBottom(30).reduced(28, 0), Justification::centredRight);
    }

private:
    const Theme*& t;
    Component& backdrop;
};

// ---------------------------------------------------------------- updates: About panel and prompt
// The About panel is the "Settings row" of the update-check spec: version, last result, one action.
class AboutPanel : public Component {
public:
    AboutPanel(updates::Checker& c, const Theme*& th) : checker(c), t(th) {
        for (auto* b : {&action, &close}) { b->getProperties().set("fontSize", 13.0); addAndMakeVisible(*b); }
        close.getProperties().set("icon", "close");
        close.setButtonText({});
        action.onClick = [this] {
            if (checker.known().valid()) checker.install(); else checker.check(true);
        };
        close.onClick = [this] { setVisible(false); };
        link.setFont(sans(13.f), false, Justification::centredLeft);
        addAndMakeVisible(link);
    }
    void refresh() {
        const auto r = checker.known();
        const auto ph = checker.phase();
        action.setButtonText(ph == updates::Checker::Phase::Checking ? "Checking..."
                             : ph == updates::Checker::Phase::Downloading ? "Downloading " + String(int(checker.progress() * 100.f)) + "%"
                             : r.valid() ? "Update to " + r.version : "Check for updates");
        action.setEnabled(ph == updates::Checker::Phase::Idle);
        action.getProperties().set("icon", r.valid() ? "download" : "refresh");
        action.getProperties().set("accentText", r.valid());
        link.setURL(URL(r.valid() ? r.page : String(updates::kReleasesPage)));
        link.setColour(HyperlinkButton::textColourId, t->ink);
        repaint();
    }
    void resized() override {
        close.setBounds(getWidth() - 40, 10, 30, 30);
        action.setBounds(20, getHeight() - 56, 190, 36);
        link.setButtonText("Release page");
        link.setBounds(226, getHeight() - 56, 120, 36);
    }
    void paint(Graphics& g) override {
        const Theme& c = *t;
        auto r = getLocalBounds().toFloat().reduced(1.f);
        Path p; p.addRoundedRectangle(r, 12.f);
        DropShadow(Colours::black.withAlpha(c.dark ? 0.7f : 0.25f), 24, {0, 8}).drawForPath(g, p);  // it floats
        g.setColour(c.dark ? Colour(0xff111214) : c.surface); g.fillPath(p);
        g.setColour(c.dark ? Colour(0xff2e2e2e) : c.line); g.strokePath(p, PathStrokeType(1.f));
        drawIcon(g, "logo", Rectangle<float>(20.f, 20.f, 22.f, 20.f), c.accent);
        g.setColour(c.ink); g.setFont(sans(16.f, true));
        g.drawText("Corridor Leveler " + updates::currentVersion(), Rectangle<float>(52.f, 16.f, r.getWidth() - 100.f, 28.f), Justification::centredLeft);
        g.setFont(sans(13.5f)); g.setColour(c.ink);
        g.drawFittedText(checker.status(), Rectangle<int>(20, 56, getWidth() - 40, 40), Justification::topLeft, 2);
        const String msg = checker.message();
        g.setColour(c.muted); g.setFont(sans(13.f));
        g.drawFittedText(msg.isNotEmpty() ? msg : "The check is a plain request to GitHub. Nothing about you or your computer is sent.",
                         Rectangle<int>(20, 98, getWidth() - 40, 36), Justification::topLeft, 2);
    }

private:
    updates::Checker& checker;
    const Theme*& t;
    TextButton action{"Check for updates"}, close{"x"};
    HyperlinkButton link{"Release page", URL(updates::kReleasesPage)};
};

// The prompt: once per start, Update and Ignore. Escape or a click outside closes it for this start.
class UpdatePrompt : public Component {
public:
    UpdatePrompt(updates::Checker& c, const Theme*& th) : checker(c), t(th) {
        for (auto* b : {&update, &ignore}) { b->getProperties().set("fontSize", 14.0); addAndMakeVisible(*b); }
        update.getProperties().set("icon", "download");
        update.getProperties().set("accentText", true);
        update.onClick = [this] { checker.install(); };
        ignore.onClick = [this] { checker.ignoreKnown(); };
        setWantsKeyboardFocus(true);
    }
    void refresh() {
        const bool busy = checker.phase() == updates::Checker::Phase::Downloading;
        update.setEnabled(!busy); ignore.setEnabled(!busy);
        repaint();
    }
    Rectangle<int> box() const { return getLocalBounds().withSizeKeepingCentre(500, 190); }
    void resized() override {
        const auto b = box();
        update.setBounds(b.getX() + 24, b.getBottom() - 60, 140, 38);
        ignore.setBounds(b.getX() + 176, b.getBottom() - 60, 110, 38);
    }
    void mouseDown(const MouseEvent& e) override {
        if (!box().contains(e.getPosition()) && checker.phase() != updates::Checker::Phase::Downloading) checker.dismissPrompt();
    }
    bool keyPressed(const KeyPress& k) override {
        if (k == KeyPress::escapeKey && checker.phase() != updates::Checker::Phase::Downloading) { checker.dismissPrompt(); return true; }
        return false;
    }
    void paint(Graphics& g) override {
        const Theme& c = *t;
        g.fillAll(Colours::black.withAlpha(0.55f));
        const auto b = box().toFloat();
        Path p; p.addRoundedRectangle(b, 14.f);
        DropShadow(Colours::black.withAlpha(0.6f), 30, {0, 10}).drawForPath(g, p);
        g.setColour(c.dark ? Colour(0xff111214) : c.surface); g.fillPath(p);
        g.setColour(c.dark ? Colour(0xff2e2e2e) : c.line); g.strokePath(p, PathStrokeType(1.f));
        const auto r = checker.known();
        const String cur = updates::currentVersion();
        g.setColour(c.ink); g.setFont(sans(20.f, true));
        g.drawText("Corridor Leveler " + r.version + " is available", b.reduced(24.f).withHeight(30.f), Justification::centredLeft);
        g.setFont(sans(14.f));
        String body;
        if (checker.phase() == updates::Checker::Phase::Downloading)
            body = "Downloading... " + String(int(checker.progress() * 100.f)) + "%. The installer opens when it's done.";
        else {
            const String size = r.bytes > 0 ? " (" + String(double(r.bytes) / 1048576.0, 1) + " MB)" : String();
            body = "You have " + cur + ". Update downloads the new version" + size + " and opens its installer. When it asks, close OBS or your DAW. "
                   "Your settings and sessions are kept.";
            if (checker.message().isNotEmpty()) body = checker.message();
        }
        g.drawFittedText(body, b.reduced(24.f).withTrimmedTop(40.f).withHeight(64.f).toNearestInt(), Justification::topLeft, 3);
        if (checker.phase() == updates::Checker::Phase::Downloading) {
            const auto bar = Rectangle<float>(b.getX() + 24.f, b.getBottom() - 92.f, b.getWidth() - 48.f, 8.f);
            drawWell(g, c, bar, 4.f);
            g.setColour(c.accent); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * checker.progress()), 4.f);
        }
    }

private:
    updates::Checker& checker;
    const Theme*& t;
    TextButton update{"Update"}, ignore{"Ignore"};
};

}  // namespace

// ---------------------------------------------------------------- content (design size)
class CorridorContent : public Component, private ChangeListener {
public:
    explicit CorridorContent(CorridorAudioProcessor& p)
        : proc(p),
          graph(p, theme), meterIn(true, theme, p.core()), meterOut(false, theme, p.core()), deck(theme, graph),
          threshold(p.state(), ids::threshold, "Threshold", "dB", 1, 72.f, "thr"),
          target(p.state(), ids::target, "Target", "dB RMS", 1, 96.f, "tgt"),
          response(p.state(), ids::response, "Response", "", 0, 72.f, "resp"),
          range(p.state(), ids::range, "Range", "dB", 1, 72.f, "rng"),
          ceiling(p.state(), ids::ceiling, "Ceiling", "dBFS", 1, 54.f, "ceil"),
          tags(graph), about(*checker, theme), prompt(*checker, theme) {
        theme = p.darkTheme ? &kDark : &kLight;
        look.t = theme; look.apply();
        setLookAndFeel(&look);

        addAndMakeVisible(meterIn); addAndMakeVisible(meterOut);
        addAndMakeVisible(graph);
        addAndMakeVisible(deck);
        addAndMakeVisible(tags);
        for (auto* k : {&threshold, &target, &response, &range, &ceiling}) deck.addAndMakeVisible(*k);

        for (auto* b : {&ride, &hold}) {
            b->setClickingTogglesState(false);
            b->getProperties().set("pill", true); b->getProperties().set("fontSize", 13.0);
            b->getProperties().set("segment", true);
            deck.addAndMakeVisible(*b);
        }
        ride.getProperties().set("icon", "wave");
        hold.getProperties().set("icon", "lock");
        analyze.getProperties().set("icon", "target");
        ride.onClick = [this] { setMode(0); };
        hold.onClick = [this] { setMode(1); };
        analyze.getProperties().set("fontSize", 14.0);
        analyze.onClick = [this] {
            if (proc.core().telemetry().analyzing.load()) proc.core().requestAnalyzeLock();
            else proc.core().requestAnalyzeStart();
        };
        deck.addChildComponent(analyze);
        status.setJustificationType(Justification::centred);
        status.setFont(sans(14.f));
        status.setInterceptsMouseClicks(false, false);
        deck.addAndMakeVisible(status);

        for (auto* b : {&ease, &bypass, &themeBtn}) {
            b->setClickingTogglesState(true);
            b->getProperties().set("fontSize", 12.5);
            addAndMakeVisible(*b);
        }
        ease.getProperties().set("icon", "glide");
        ease.getProperties().set("tone", "accent");
        bypass.getProperties().set("icon", "power");
        bypass.getProperties().set("tone", "warn");
        themeBtn.getProperties().set("icon", p.darkTheme ? "moon" : "sun");
        themeBtn.setButtonText({});
        themeBtn.setTooltip("Switch between dark and light");
        easeAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::ease, ease);
        bypassAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::bypass, bypass);
        ease.setTooltip("Off: the gain freezes below the threshold. On: after 2 s of silence it glides back to 0 dB.");
        themeBtn.setToggleState(p.darkTheme, dontSendNotification);
        themeBtn.onClick = [this] { applyTheme(themeBtn.getToggleState()); };

        // updates: version in the header opens the About panel; a known update gets its own button
        versionBtn.setButtonText("v" + updates::currentVersion());
        versionBtn.getProperties().set("fontSize", 12.5);
        versionBtn.onClick = [this] { about.refresh(); about.setVisible(!about.isVisible()); about.toFront(false); };
        addAndMakeVisible(versionBtn);
        updateBtn.getProperties().set("fontSize", 12.5);
        updateBtn.getProperties().set("icon", "download");
        updateBtn.getProperties().set("accentText", true);
        updateBtn.onClick = [this] { if (checker->known().valid()) checker->install(); };
        addChildComponent(updateBtn);
        addChildComponent(about);
        addChildComponent(prompt);
        checker->addChangeListener(this);
        checker->check(false);  // silent; at most one prompt per host session
        setSize(CorridorEditor::designW, CorridorEditor::designH);
        sendLookAndFeelChange();  // children were added after the look was set: let them pick up its colours
    }
    ~CorridorContent() override { checker->removeChangeListener(this); setLookAndFeel(nullptr); }

    void changeListenerCallback(ChangeBroadcaster*) override {
        const auto r = checker->known();
        updateBtn.setVisible(r.valid());
        updateBtn.setButtonText(checker->phase() == updates::Checker::Phase::Downloading
                                    ? "Downloading " + String(int(checker->progress() * 100.f)) + "%"
                                    : "Update to " + r.version);
        const bool showPrompt = checker->promptVisible() || (checker->phase() == updates::Checker::Phase::Downloading && prompt.isVisible());
        prompt.setVisible(showPrompt && r.valid());
        if (prompt.isVisible()) { prompt.toFront(true); prompt.refresh(); }
        about.refresh();
        resized();
    }

    void applyTheme(bool dark) {
        proc.darkTheme = dark;
        theme = dark ? &kDark : &kLight;
        themeBtn.getProperties().set("icon", dark ? "moon" : "sun");
        look.t = theme; look.apply();
        sendLookAndFeelChange();
        repaint();
    }

    void setMode(int m) {
        if (auto* prm = proc.state().getParameter(ids::mode)) {
            prm->beginChangeGesture(); prm->setValueNotifyingHost(prm->convertTo0to1(float(m))); prm->endChangeGesture();
        }
    }

    void paint(Graphics& g) override {
        const Theme& c = *theme;
        g.fillAll(c.bg);
        // header plate
        g.setGradientFill(ColourGradient(c.dark ? Colour(0xff17181b) : Colour(0xffffffff), 0.f, 0.f,
                                         c.dark ? Colour(0xff050506) : Colour(0xffeceef0), 0.f, 46.f, false));
        g.fillRect(0, 0, getWidth(), 46);
        g.setColour(c.hilite); g.fillRect(0, 0, getWidth(), 1);
        drawIcon(g, "logo", Rectangle<float>(16.f, 12.f, 24.f, 22.f), c.accent);
        auto f = sans(16.f, true); f.setExtraKerningFactor(0.18f);
        g.setFont(f); g.setColour(c.ink);
        g.drawText("CORRIDOR", Rectangle<int>(50, 0, 240, 46), Justification::centredLeft);
        g.setFont(sans(13.f)); g.setColour(c.muted);
        g.drawText("Leveler", Rectangle<int>(176, 0, 120, 46), Justification::centredLeft);
        g.setColour(c.edge); g.fillRect(0, 46, getWidth(), 1);
    }

    void resized() override {
        // header
        auto head = Rectangle<int>(0, 0, getWidth(), 46).reduced(12, 8);
        themeBtn.setBounds(head.removeFromRight(40));
        head.removeFromRight(10);
        bypass.setBounds(head.removeFromRight(104));
        head.removeFromRight(10);
        ease.setBounds(head.removeFromRight(170));
        head.removeFromRight(10);
        versionBtn.setBounds(head.removeFromRight(84));
        if (updateBtn.isVisible()) { head.removeFromRight(10); updateBtn.setBounds(head.removeFromRight(170)); }
        about.setBounds(getWidth() - 12 - 420, 52, 420, 190);
        prompt.setBounds(getLocalBounds());
        // body
        auto body = Rectangle<int>(0, 47, getWidth(), getHeight() - 47);
        meterIn.setBounds(body.removeFromLeft(52));
        meterOut.setBounds(body.removeFromRight(52));
        graph.setBounds(body);
        const int deckH = 198;
        deck.setBounds(body.getX(), body.getBottom() - deckH, body.getWidth(), deckH);
        tags.setBounds(graph.getBounds());
        graph.chipLimit = float(deck.getY() - graph.getY() + 6);

        // deck: knobs and mode controls centred as one row
        const int gap = 28, centerW = 160, gainW = 110;
        Knob* left[] = {&threshold, &target};
        Knob* right[] = {&response, &range, &ceiling};
        int total = centerW + gainW + gap * 6;
        for (auto* k : left) total += k->preferredWidth();
        for (auto* k : right) total += k->preferredWidth();
        int x = (deck.getWidth() - total) / 2;
        const int rowBottom = deckH - 40;
        auto place = [&](Knob* k) { k->setBounds(x, rowBottom - k->preferredHeight(), k->preferredWidth(), k->preferredHeight()); x += k->preferredWidth() + gap; };
        for (auto* k : left) place(k);
        ride.setBounds(x, rowBottom - 92, centerW / 2, 34);
        hold.setBounds(x + centerW / 2, rowBottom - 92, centerW / 2, 34);
        deck.modeTrack = Rectangle<int>(x, rowBottom - 92, centerW, 34);
        analyze.setBounds(x, rowBottom - 48, centerW, 32);
        x += centerW + gap;
        for (auto* k : right) place(k);
        deck.gainBox = Rectangle<int>(x, rowBottom - 80, gainW, 64);
        status.setBounds(0, deckH - 36, deck.getWidth(), 22);
    }

    void tick() {
        auto& dsp = proc.core();
        const auto& t = dsp.telemetry();
        const int n = dsp.copyHistory(scratch, kHist);
        graph.hist.assign(scratch, scratch + n);
        graph.analyzing = t.analyzing.load();
        graph.analyzeSeconds = t.analyzeSeconds.load();
        meterIn.peak = t.inPeak.load(); meterIn.hold = t.inPeakHold.load();
        meterOut.peak = t.outPeak.load(); meterOut.hold = t.outPeakHold.load();
        target.setLive(t.outRms.load());
        threshold.setLive(t.inRms.load());

        const bool holdMode = proc.state().getRawParameterValue(ids::mode)->load() > 0.5f;
        ride.setToggleState(!holdMode, dontSendNotification);
        hold.setToggleState(holdMode, dontSendNotification);
        analyze.setVisible(holdMode);
        analyze.setButtonText(t.analyzing.load() ? "Lock" : t.holdValid.load() ? "Analyze again" : "Analyze");
        analyze.getProperties().set("icon", t.analyzing.load() ? "lock" : "target");
        analyze.setToggleState(t.analyzing.load(), dontSendNotification);

        const bool byp = proc.state().getRawParameterValue(ids::bypass)->load() > 0.5f;
        const float g = byp ? 0.f : t.gain.load();
        deck.gainText = fmtDb(g, 1, true);
        deck.gainSign = g > 0.05f ? 1 : g < -0.05f ? -1 : 0;
        const double fs = dsp.sampleRate();
        deck.latencyText = "Latency " + String(1000.0 * dsp.latencySamples() / fs, 1) + " ms  (" + String(dsp.latencySamples()) +
                           " samples @ " + String(fs / 1000.0, 1) + " kHz)";
        status.setText(sentence(byp, holdMode), dontSendNotification);
        status.setColour(Label::textColourId, theme->ink);

        graph.repaint(); tags.repaint(); meterIn.repaint(); meterOut.repaint(); deck.repaint();
    }

    String sentence(bool byp, bool holdMode) const {
        const auto& t = proc.core().telemetry();
        auto& st = proc.state();
        const float L = t.level.load(), g = t.gain.load();
        const float tgt = st.getRawParameterValue(ids::target)->load();
        const float thr = st.getRawParameterValue(ids::threshold)->load();
        const float cei = st.getRawParameterValue(ids::ceiling)->load();
        const float rng = st.getRawParameterValue(ids::range)->load();
        if (byp) return "Bypassed. The audio passes through unchanged (still delayed by the latency, so nothing jumps).";
        if (holdMode) {
            if (t.analyzing.load())
                return "Analyzing: " + String(t.analyzeSeconds.load(), 1) + " s, " + String(t.analyzeActiveSeconds.load(), 1) +
                       " s above the threshold. Press Lock after a typical section (it locks by itself at 20 s).";
            if (t.analyzeFailed.load()) return "Not enough sound above the threshold. Play a louder section, or lower the threshold, and analyze again.";
            if (t.holdValid.load()) {
                const int lim = t.holdLimit.load();
                return "Holding " + fmtDb(t.holdGain.load(), 1, true) + " dB. Measured " + fmtDb(t.holdMeasured.load(), 1) + " dB RMS over " +
                       String(t.holdSeconds.load(), 1) + " s." + (lim == 1 ? " Limited by the ceiling." : lim == 2 ? " Limited by the range." : "");
            }
            return "Holding " + fmtDb(g, 1, true) + " dB. Play a typical section and press Analyze.";
        }
        switch (corridor::State(t.state.load())) {
            case corridor::State::OnTarget:
                return "On target: " + fmtDb(L + g, 1) + " dB RMS with " + fmtDb(g, 1, true) + " dB applied." +
                       (t.percussive.load() ? " Percussive material: following the groove, not single hits." : "");
            case corridor::State::Boosting: return "Coming in at " + fmtDb(L, 1) + " dB RMS, below the " + fmtDb(tgt, 1) + " target. Lifting it (" + fmtDb(g, 1, true) + " dB so far).";
            case corridor::State::Cutting: return "Coming in at " + fmtDb(L, 1) + " dB RMS, above the " + fmtDb(tgt, 1) + " target. Lowering it (" + fmtDb(g, 1, true) + " dB so far).";
            case corridor::State::Ceiling: return "More boost would push peaks past the " + fmtDb(cei, 1) + " dBFS ceiling. Held at " + fmtDb(g, 1, true) + " dB.";
            case corridor::State::Range: return "It needs more than the +/-" + String(rng, 1) + " dB range allows. Held at " + fmtDb(g, 1, true) + " dB.";
            case corridor::State::Returning: return "Silence for over 2 s. Easing back to 0 dB (" + fmtDb(g, 1, true) + " dB).";
            default:
                return t.inRms.load() < thr ? "Below the " + fmtDb(thr, 1) + " dB threshold (silence, breaths or noise). Not adjusting; gain stays at " + fmtDb(g, 1, true) + " dB."
                                            : "Sound just started. Measuring it before acting.";
        }
    }

private:
    CorridorAudioProcessor& proc;
    const Theme* theme = &kDark;
    Look look;
    Graph graph;
    Meter meterIn, meterOut;
    Deck deck;
    Knob threshold, target, response, range, ceiling;
    TextButton ride{"Ride"}, hold{"Hold"}, analyze{"Analyze"};
    TextButton ease{"Ease in silence"}, bypass{"Bypass"}, themeBtn{"Dark theme"};
    TextButton versionBtn{"v0"}, updateBtn{"Update"};
    TagLayer tags;
    SharedResourcePointer<updates::Checker> checker;  // declared before the panels that use it
    AboutPanel about;
    UpdatePrompt prompt;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> easeAtt, bypassAtt;
    Label status;
    corridor::Snapshot scratch[kHist];
};

// ---------------------------------------------------------------- editor
CorridorEditor::CorridorEditor(CorridorAudioProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    const int savedWidth = p.editorWidth;  // read first: setting limits triggers resized()
    content = std::make_unique<CorridorContent>(p);
    addAndMakeVisible(*content);
    setResizable(true, true);
    setResizeLimits(700, 394, 2240, 1260);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(double(designW) / double(designH));
    const int w = jlimit(700, 2240, savedWidth);
    setSize(w, int(std::round(w * double(designH) / designW)));
    startTimerHz(30);
}

CorridorEditor::~CorridorEditor() { stopTimer(); }

void CorridorEditor::paint(Graphics& g) { g.fillAll(Colours::black); }

void CorridorEditor::resized() {
    const float s = float(getWidth()) / float(designW);
    content->setBounds(0, 0, designW, designH);
    content->setTransform(AffineTransform::scale(s));
    proc.editorWidth = getWidth();
}

void CorridorEditor::timerCallback() { content->tick(); }
