// Corridor Leveler - plugin window (Liquid Glass design).
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Design size 1120 x 630, scaled as a whole. A full-bleed level graph (0 to -80 dB) with glass pieces
// floating over it: the header, the two meters and the readouts. Under the dials the graph frosts and
// fades into the window colour, so the controls sit in the graph itself. Glass is drawn from a small,
// blurred copy of the graph, magnified a little under each piece and edged with a lit rim.
// In Hold mode an analysis sheet (150 px) slides out under the window.
#include "PluginEditor.h"

#include "Updates.h"
#include "BinaryData.h"

#include <cmath>
#include <map>
#include <vector>

using namespace juce;

namespace {

constexpr int W = CorridorEditor::designW, H = CorridorEditor::designH;
constexpr float kPi = MathConstants<float>::pi;
constexpr int kHist = 500;          // 10 s at 20 ms per entry
constexpr int kWaveEntries = 300;   // 6 s in the analysis sheet

// ---------------------------------------------------------------- geometry
constexpr float PX0 = 90.f, PX1 = 1030.f, GT = 118.f, GB = 596.f, kDbRange = 80.f;
const Rectangle<float> kHeader{12.f, 12.f, float(W) - 24.f, 42.f};
constexpr float kMeterY = 64.f, kMeterH = 554.f, kMeterW = 66.f;
const Rectangle<float> kDeck{226.f, 408.f, 668.f, 206.f};
constexpr float kFadeTop = 408.f - 44.f;
float yOf(float v) { return GT + jlimit(0.f, kDbRange, -v) / kDbRange * (GB - GT); }
float vOf(float y) { return -jlimit(0.f, kDbRange, (y - GT) / (GB - GT) * kDbRange); }

// ---------------------------------------------------------------- palette
struct Pal {
    Colour bg, ink, label, muted;
    Colour data, dataText, thr, thrText, ceil, ceilText, in;
    Colour mSafe, mWarn, mHot, boost, cut;
    Colour grid, glass, glassStrong, rim, rimSoft, shade, bloomA, bloomB, meterOff, on, panel;
};
// Tuned for readability under glare: near-black ground, white ink, every text grey at 9:1 or more,
// brighter data hues and grid, and more opaque glass so text never sits on a busy backdrop.
const Pal kDark{Colour(0xff0a0a0c), Colour(0xffffffff), Colour(0xffdedee3), Colour(0xffb9b9c0),
                Colour(0xff5ee8d4), Colour(0xff7ff0df), Colour(0xffffc65a), Colour(0xffffd27a), Colour(0xffff8676), Colour(0xffff9d90), Colour(0xccc4d4e8),
                Colour(0xff4fe08d), Colour(0xffffcc4d), Colour(0xffff6656), Colour(0xff8cbcff), Colour(0xffffa678),
                Colour(0x2bffffff), Colour(0x17ffffff), Colour(0x2effffff), Colour(0x8cffffff), Colour(0x38ffffff), Colour(0x80000000),
                Colour(0x05ffffff), Colour(0x04ffffff), Colour(0x24ffffff), Colour(0x47ffffff), Colour(0xf51a1a1f)};

// IBM Plex Sans: open, distinct letterforms, tabular figures and a plain zero
const Typeface::Ptr& face(bool bold) {
    static const Typeface::Ptr regular = Typeface::createSystemTypefaceFor(CorridorFonts::IBMPlexSansRegular_ttf,
                                                                           size_t(CorridorFonts::IBMPlexSansRegular_ttfSize));
    static const Typeface::Ptr heavy = Typeface::createSystemTypefaceFor(CorridorFonts::IBMPlexSansBold_ttf,
                                                                         size_t(CorridorFonts::IBMPlexSansBold_ttfSize));
    return bold ? heavy : regular;
}
Font sans(float h, bool bold = false) { return Font(FontOptions(face(bold)).withHeight(h)); }

String fmtDb(float v, int dec = 1, bool sign = false) {
    String s = String(std::abs(v), dec);
    const bool zero = s == String(0.0, dec);
    if (v < 0 && !zero) return String(CharPointer_UTF8("\xe2\x88\x92")) + s;  // a real minus sign
    if (sign && v > 0 && !zero) return "+" + s;
    return s;
}
const String kPlusMinus = String(CharPointer_UTF8("\xc2\xb1"));
const String kDot = String(CharPointer_UTF8(" \xc2\xb7 "));

String speedWord(double ms) { return ms < 50 ? "Very fast" : ms < 200 ? "Fast" : ms < 900 ? "Medium" : "Slow"; }

// ---------------------------------------------------------------- icons
void drawIcon(Graphics& g, const String& name, Rectangle<float> a, Colour col) {
    const float s = jmin(a.getWidth(), a.getHeight());
    const auto b = a.withSizeKeepingCentre(s, s);
    const float cx = b.getCentreX(), cy = b.getCentreY(), x = b.getX(), y = b.getY();
    const PathStrokeType st(jmax(1.5f, s * 0.1f), PathStrokeType::curved, PathStrokeType::rounded);
    g.setColour(col);
    Path p;
    if (name == "undo" || name == "redo") {  // a hooked arrow: undo points back, redo forward
        const float d = name == "undo" ? -1.f : 1.f;
        p.startNewSubPath(cx - d * s * 0.3f, cy + s * 0.3f);
        p.quadraticTo(cx - d * s * 0.3f, cy - s * 0.24f, cx + d * s * 0.3f, cy - s * 0.24f);
        p.startNewSubPath(cx + d * s * 0.08f, cy - s * 0.46f); p.lineTo(cx + d * s * 0.3f, cy - s * 0.24f); p.lineTo(cx + d * s * 0.08f, cy - s * 0.02f);
        g.strokePath(p, st);
    } else if (name == "chevron-down" || name == "chevron-up") {
        const float d = name == "chevron-down" ? 1.f : -1.f;
        p.startNewSubPath(cx - s * 0.25f, cy - d * s * 0.12f); p.lineTo(cx, cy + d * s * 0.12f); p.lineTo(cx + s * 0.25f, cy - d * s * 0.12f);
        g.strokePath(p, st);
    } else if (name == "download") {
        p.startNewSubPath(cx, y + s * 0.1f); p.lineTo(cx, y + s * 0.62f);
        p.startNewSubPath(cx - s * 0.24f, y + s * 0.4f); p.lineTo(cx, y + s * 0.64f); p.lineTo(cx + s * 0.24f, y + s * 0.4f);
        p.startNewSubPath(x + s * 0.14f, y + s * 0.76f); p.lineTo(x + s * 0.14f, y + s * 0.9f); p.lineTo(x + s * 0.86f, y + s * 0.9f); p.lineTo(x + s * 0.86f, y + s * 0.76f);
        g.strokePath(p, st);
    } else if (name == "close") {
        p.startNewSubPath(x + s * 0.28f, y + s * 0.28f); p.lineTo(x + s * 0.72f, y + s * 0.72f);
        p.startNewSubPath(x + s * 0.72f, y + s * 0.28f); p.lineTo(x + s * 0.28f, y + s * 0.72f);
        g.strokePath(p, st);
    } else if (name == "logo") {  // a level held between two walls: the corridor
        g.fillRoundedRectangle(x, y + s * 0.08f, s, s * 0.14f, s * 0.07f);
        g.fillRoundedRectangle(x, y + s * 0.78f, s, s * 0.14f, s * 0.07f);
        for (int k = 0; k <= 30; ++k) {
            const float t = float(k) / 30.f, env = 1.f - t * 0.72f;
            const float px = x + s * t, py = cy - std::sin(t * 4.f * kPi) * s * 0.22f * env;
            if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
        }
        g.strokePath(p, PathStrokeType(jmax(1.5f, s * 0.09f), PathStrokeType::curved, PathStrokeType::rounded));
    }
}

// ---------------------------------------------------------------- glass
// The shared state every glass piece draws from: the palette, the blurred graph and where the light is.
struct GlassContext {
    const Pal* pal = &kDark;
    Image blur;                 // the graph, drawn small and blurred; maps onto the whole 1120 x 630 design
    Component* root = nullptr;  // the design-size content component
    Point<float> light{-0.5f, -1.f};
};

struct GlassOpts {
    Colour tint;              // transparent = the palette's glass tint
    float lens = 1.05f;       // how much the backdrop is magnified under the glass
    bool shadow = true;
    float shadowRadius = 20.f, shadowY = 7.f;
    float rimWidth = 1.4f;
};

// Draws a glass shape in `comp`'s coordinates. The backdrop is mapped through the component's position
// in the content, so every piece shows the part of the graph that is really behind it.
void drawGlass(Graphics& g, const GlassContext& gc, Component& comp, const Path& shape, GlassOpts o = {}) {
    const Pal& p = *gc.pal;
    const auto b = shape.getBounds();
    if (o.shadow) DropShadow(p.shade, int(o.shadowRadius), {0, int(o.shadowY)}).drawForPath(g, shape);
    {
        Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(shape);
        if (gc.blur.isValid() && gc.root != nullptr) {
            const auto origin = gc.root->getLocalPoint(&comp, Point<float>());
            const auto t = AffineTransform::scale(float(W) / float(gc.blur.getWidth()), float(H) / float(gc.blur.getHeight()))
                               .translated(-origin.x, -origin.y)
                               .scaled(o.lens, o.lens, b.getCentreX(), b.getCentreY());
            g.setImageResamplingQuality(Graphics::mediumResamplingQuality);
            g.drawImageTransformed(gc.blur, t);
        }
        g.setColour(o.tint.isTransparent() ? p.glass : o.tint);
        g.fillRect(b);
        // light pooling on the lit side: the glass has thickness
        const float lx = b.getCentreX() + gc.light.x * b.getWidth() * 0.5f, ly = b.getCentreY() + gc.light.y * b.getHeight() * 0.5f;
        g.setGradientFill(ColourGradient(Colours::white.withAlpha(0.10f), lx, ly, Colours::white.withAlpha(0.f),
                                         lx + jmax(b.getWidth(), b.getHeight()) * 0.9f, ly, true));
        g.fillRect(b);
    }
    // rim: bright where the light catches the edge, a softer catch on the far side
    const float rx0 = b.getCentreX() + gc.light.x * b.getWidth() * 0.5f, ry0 = b.getCentreY() + gc.light.y * b.getHeight() * 0.5f;
    const float rx1 = b.getCentreX() - gc.light.x * b.getWidth() * 0.5f, ry1 = b.getCentreY() - gc.light.y * b.getHeight() * 0.5f;
    ColourGradient rim(p.rim, rx0, ry0, Colours::white.withAlpha(0.28f), rx1, ry1, false);
    rim.addColour(0.35, Colours::white.withAlpha(0.04f));
    rim.addColour(0.7, Colours::white.withAlpha(0.02f));
    g.setGradientFill(rim);
    g.strokePath(shape, PathStrokeType(o.rimWidth));
}
Path capsule(Rectangle<float> r, float rad = -1.f) { Path p; p.addRoundedRectangle(r, rad < 0.f ? r.getHeight() * 0.5f : rad); return p; }
Path circle(Rectangle<float> r) { Path p; p.addEllipse(r); return p; }

// ---------------------------------------------------------------- look and feel
class Look : public LookAndFeel_V4 {
public:
    GlassContext* gc = nullptr;
    const Pal& p() const { return *gc->pal; }

    // every font JUCE makes for us (tooltips, menus, editors) uses the interface typeface too
    Typeface::Ptr getTypefaceForFont(const Font& f) override { return face(f.isBold()); }

    void apply() {
        const Pal& c = p();
        setColour(Label::textColourId, c.muted);
        setColour(Label::textWhenEditingColourId, c.ink);
        setColour(Label::backgroundWhenEditingColourId, c.panel);
        setColour(Label::outlineWhenEditingColourId, c.data);
        setColour(TextEditor::textColourId, c.ink);
        setColour(TextEditor::backgroundColourId, c.panel);
        setColour(TextEditor::highlightColourId, c.data.withAlpha(0.35f));
        setColour(TextEditor::focusedOutlineColourId, c.data);
        setColour(CaretComponent::caretColourId, c.ink);
        setColour(TooltipWindow::backgroundColourId, c.panel);
        setColour(TooltipWindow::textColourId, c.ink);
        setColour(TooltipWindow::outlineColourId, c.rimSoft);
        setColour(PopupMenu::backgroundColourId, c.panel);
        setColour(PopupMenu::textColourId, c.ink);
        setColour(PopupMenu::highlightedBackgroundColourId, c.on);
        setColour(PopupMenu::highlightedTextColourId, c.ink);
        setColour(PopupMenu::headerTextColourId, c.muted);
        setColour(HyperlinkButton::textColourId, c.ink);
    }

    Colour roleColour(const String& role) const {
        const Pal& c = p();
        return role == "data" ? c.data : role == "thr" ? c.thr : role == "ceil" ? c.ceil : c.label;
    }
    Colour roleText(const String& role) const {
        const Pal& c = p();
        return role == "data" ? c.dataText : role == "thr" ? c.thrText : role == "ceil" ? c.ceilText : c.ink;
    }

    // A glass dial: a coloured value ring in a faint track, a clear glass disc, a coloured pointer.
    void drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s) override {
        const Pal& c = p();
        const float cx = float(x) + float(w) * 0.5f, cy = float(y) + float(h) * 0.5f;
        const float ringR = float(jmin(w, h)) * 0.5f - 7.f, r = ringR - 9.f;
        const float ang = a0 + pos * (a1 - a0);
        const String role = s.getProperties().getWithDefault("role", "");
        const Colour col = roleColour(role);
        const PathStrokeType ring(6.f, PathStrokeType::curved, PathStrokeType::rounded);
        Path track; track.addCentredArc(cx, cy, ringR, ringR, 0.f, a0, a1, true);
        g.setColour(Colours::white.withAlpha(0.12f));
        g.strokePath(track, ring);
        if (pos > 0.003f) {
            Path val; val.addCentredArc(cx, cy, ringR, ringR, 0.f, a0, ang, true);
            g.setColour(col); g.strokePath(val, ring);
        }
        // live level on the ring: Target shows the output level, Threshold the input level
        const var live = s.getProperties()["live"];
        if (!live.isVoid()) {
            const double lv = double(live);
            if (lv >= s.getMinimum() && lv <= s.getMaximum()) {
                const float la = a0 + float(s.valueToProportionOfLength(lv)) * (a1 - a0);
                g.setColour(c.ink);
                g.fillEllipse(cx + ringR * std::sin(la) - 2.3f, cy - ringR * std::cos(la) - 2.3f, 4.6f, 4.6f);
            }
        }
        const bool hot = s.isMouseOverOrDragging();
        drawGlass(g, *gc, s, circle({cx - r, cy - r, r * 2.f, r * 2.f}),
                  {hot ? c.glassStrong : Colour(), 1.12f, true, 16.f, 6.f, 1.4f});
        // pointer: a short coloured capsule set into the glass
        const float p0 = r * 0.42f, p1 = r * 0.78f;
        g.setColour(col);
        g.drawLine(cx + std::sin(ang) * p0, cy - std::cos(ang) * p0, cx + std::sin(ang) * p1, cy - std::cos(ang) * p1, jmax(3.f, r * 0.1f));
        if (s.hasKeyboardFocus(false)) { g.setColour(c.ink); g.drawEllipse(cx - ringR - 8.f, cy - ringR - 8.f, (ringR + 8.f) * 2.f, (ringR + 8.f) * 2.f, 2.f); }
    }

    // The thin sliders in the Rise / Fall / Trim panel.
    void drawLinearSlider(Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s) override {
        const Pal& c = p();
        const float cy = float(y) + float(h) * 0.5f, x0 = float(x) + 8.f, x1 = float(x + w) - 8.f;
        g.setColour(c.meterOff); g.fillRoundedRectangle(x0, cy - 2.f, x1 - x0, 4.f, 2.f);
        const float px = jlimit(x0, x1, pos);
        g.setColour(c.ink.withAlpha(0.85f)); g.fillRoundedRectangle(x0, cy - 2.f, px - x0, 4.f, 2.f);
        drawGlass(g, *gc, s, circle({px - 9.f, cy - 9.f, 18.f, 18.f}), {c.glassStrong, 1.1f, true, 8.f, 2.f, 1.2f});
        if (s.hasKeyboardFocus(false)) { g.setColour(c.ink); g.drawEllipse(px - 12.f, cy - 12.f, 24.f, 24.f, 2.f); }
    }

    // Buttons, by property "kind":
    //   capsule - a glass capsule (Update, panel actions)
    //   circle  - a round glass button with an icon (undo, redo)
    // A capsule with property "toggle" is an on/off switch (Bypass, Return to 0 dB, Rise / fall / trim): off it is
    // plain glass with a grey dot; on it fills with solid ink (red for "danger") and the dot lights. The fill is
    // neutral on purpose: teal belongs to the output and the target, so a switch never reads as data.
    //   key     - the large Analyze / Lock key; filled with ink while analyzing
    //   plain   - text only (Got it, links in panels)
    void drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down) override {
        const Pal& c = p();
        const String kind = b.getProperties().getWithDefault("kind", "capsule");
        auto r = b.getLocalBounds().toFloat().reduced(1.f);
        const bool on = b.getToggleState();
        if (kind == "circle") r = r.withSizeKeepingCentre(jmin(r.getWidth(), r.getHeight()), jmin(r.getWidth(), r.getHeight()));
        const Path shape = kind == "circle" ? circle(r) : capsule(r);
        if (kind == "key") {
            if (on) { DropShadow(c.shade, 12, {0, 4}).drawForPath(g, shape); g.setColour(c.ink); g.fillPath(shape); }
            else drawGlass(g, *gc, b, shape, {c.glassStrong, 1.05f, true, 14.f, 5.f, 1.4f});
        } else if (isToggle(b) && on) {
            const Colour fill = b.getProperties().getWithDefault("danger", false) ? c.ceil : c.ink;
            g.setColour(over || down ? fill.brighter(0.15f) : fill); g.fillPath(shape);
        } else if (kind != "plain") {
            drawGlass(g, *gc, b, shape, {over || down ? c.glassStrong : Colour(), 1.08f, false, 0.f, 0.f, 1.3f});
        } else if (over) {
            g.setColour(c.on); g.fillPath(shape);
        }
        if (b.hasKeyboardFocus(false)) { g.setColour(c.ink); g.strokePath(kind == "circle" ? circle(r.expanded(3.f)) : capsule(r.expanded(3.f)), PathStrokeType(2.f)); }
    }

    static bool isToggle(const Button& b) { return b.getProperties().getWithDefault("toggle", false); }

    // the indicator light on a toggle: a dark dot on the light fill when on, a grey dot on glass when off
    static void drawToggleDot(Graphics& g, const Pal& c, Point<float> centre, bool on) {
        const float d = 8.f;
        const auto dot = Rectangle<float>(d, d).withCentre(centre);
        if (on) { g.setColour(c.bg); g.fillEllipse(dot); }
        else { g.setColour(c.muted.withAlpha(0.6f)); g.fillEllipse(dot); }
    }

    void drawButtonText(Graphics& g, TextButton& b, bool over, bool) override {
        const Pal& c = p();
        const String kind = b.getProperties().getWithDefault("kind", "capsule");
        const bool on = b.getToggleState(), tog = isToggle(b);
        const Colour col = !b.isEnabled() ? c.muted.withAlpha(0.5f)
                         : (kind == "key" && on) || (tog && on) || bool(b.getProperties().getWithDefault("inverse", false)) ? c.bg
                         : on || over ? c.ink : c.label;
        constexpr float dotW = 8.f, dotGap = 7.f;
        const auto r = b.getLocalBounds().toFloat();
        const String icon = b.getProperties().getWithDefault("icon", "");
        const String trailing = b.getProperties().getWithDefault("trailing", "");
        const Font f = sans(float(b.getProperties().getWithDefault("fontSize", 13.0)), true);
        g.setFont(f);
        if (icon.isNotEmpty() && b.getButtonText().isEmpty()) {
            const float is = jmin(18.f, r.getHeight() * 0.55f);
            drawIcon(g, icon, r.withSizeKeepingCentre(is, is), col);
            return;
        }
        if (trailing.isNotEmpty()) {  // text on the left, a chevron on the right
            auto tr = r.reduced(14.f, 0.f).withTrimmedRight(16.f);
            if (tog) { drawToggleDot(g, c, {tr.getX() + dotW * 0.5f, r.getCentreY()}, on); tr.removeFromLeft(dotW + dotGap); }
            g.setColour(col);
            g.drawFittedText(b.getButtonText(), tr.toNearestInt(), Justification::centredLeft, 1, 0.9f);
            drawIcon(g, trailing, Rectangle<float>(r.getRight() - 26.f, r.getCentreY() - 7.f, 14.f, 14.f), tog && on ? c.bg : c.label);
            return;
        }
        if (tog) {  // the dot and the label, centred together
            const float tw = jmin(r.getWidth() - 28.f - dotW - dotGap, GlyphArrangement::getStringWidth(f, b.getButtonText()));
            const float x0 = r.getCentreX() - (dotW + dotGap + tw) * 0.5f;
            drawToggleDot(g, c, {x0 + dotW * 0.5f, r.getCentreY()}, on);
            g.setColour(col);
            g.drawFittedText(b.getButtonText(), Rectangle<float>(x0 + dotW + dotGap, r.getY(), tw + 2.f, r.getHeight()).toNearestInt(),
                             Justification::centredLeft, 1, 0.9f);
            return;
        }
        if (icon.isNotEmpty()) {
            const float is = 15.f, tw = GlyphArrangement::getStringWidth(f, b.getButtonText());
            const float x0 = r.getCentreX() - (is + 7.f + tw) * 0.5f;
            drawIcon(g, icon, Rectangle<float>(x0, r.getCentreY() - is * 0.5f, is, is), col);
            g.setColour(col);
            g.drawText(b.getButtonText(), Rectangle<float>(x0 + is + 7.f, r.getY(), tw + 4.f, r.getHeight()), Justification::centredLeft);
            return;
        }
        g.setColour(col);
        g.drawText(b.getButtonText(), r, Justification::centred);
    }

    // the preset menu
    void drawPopupMenuBackground(Graphics& g, int w, int h) override {
        g.setColour(p().panel); g.fillRoundedRectangle(0.f, 0.f, float(w), float(h), 12.f);
        g.setColour(p().rimSoft); g.drawRoundedRectangle(0.5f, 0.5f, float(w) - 1.f, float(h) - 1.f, 12.f, 1.f);
    }
    Font getPopupMenuFont() override { return sans(14.f); }
    int getPopupMenuBorderSize() override { return 6; }
};

// ---------------------------------------------------------------- a button that opens an undo step before it acts
class GlassButton : public TextButton {
public:
    using TextButton::TextButton;
    std::function<void()> onPress;  // runs before the click changes anything
    void mouseDown(const MouseEvent& e) override { if (onPress && isEnabled()) onPress(); TextButton::mouseDown(e); }
    bool keyPressed(const KeyPress& k) override {
        if (onPress && isEnabled() && (k == KeyPress::spaceKey || k == KeyPress::returnKey)) onPress();
        return TextButton::keyPressed(k);
    }
};

// ---------------------------------------------------------------- dial slider: type a value, Alt-click to reset
class DialSlider : public Slider {
public:
    std::function<void()> onEdit, onGestureStart;
    double resetValue = 0.0;
    void mouseDown(const MouseEvent& e) override {
        if (e.mods.isAltDown()) {  // Alt-click resets
            if (onGestureStart) onGestureStart();
            setValue(resetValue, sendNotificationSync);
            return;
        }
        Slider::mouseDown(e);
    }
    void mouseDoubleClick(const MouseEvent&) override { if (onEdit) onEdit(); }
    bool keyPressed(const KeyPress& k) override {
        if (k == KeyPress::returnKey) { if (onEdit) onEdit(); return true; }
        if (onGestureStart && (k.isKeyCode(KeyPress::upKey) || k.isKeyCode(KeyPress::downKey) || k.isKeyCode(KeyPress::leftKey) ||
                               k.isKeyCode(KeyPress::rightKey) || k.isKeyCode(KeyPress::pageUpKey) || k.isKeyCode(KeyPress::pageDownKey)))
            onGestureStart();
        return Slider::keyPressed(k);
    }
    void mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& w) override {
        const auto now = Time::getMillisecondCounter();
        if (onGestureStart && now - lastWheel > 600) onGestureStart();  // one undo step per burst of scrolling
        lastWheel = now;
        Slider::mouseWheelMove(e, w);
    }

private:
    uint32 lastWheel = 0;
};

// ---------------------------------------------------------------- dial with its name, value and editor
class Knob : public Component {
public:
    Knob(CorridorAudioProcessor& proc, const char* id, String title_, std::function<String(double)> fmt, float radius, const char* role,
         std::pair<String, String> ends = {})
        : title(std::move(title_)), r(radius), endLabels(std::move(ends)) {
        auto& st = proc.state();
        slider.getProperties().set("role", role);
        slider.setTitle(title);
        slider.setWantsKeyboardFocus(true);
        slider.setSliderStyle(Slider::RotaryVerticalDrag);
        slider.setRotaryParameters(kPi * 1.25f, kPi * 2.75f, true);
        slider.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        slider.setMouseDragSensitivity(220);
        slider.setDoubleClickReturnValue(false, 0.0);
        addAndMakeVisible(slider);
        att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(st, id, slider);
        slider.textFromValueFunction = fmt;  // after the attachment, which installs the parameter's own
        if (auto* prm = st.getParameter(id)) slider.resetValue = prm->convertFrom0to1(prm->getDefaultValue());
        slider.onGestureStart = [&proc] { proc.undo.beginNewTransaction(); };
        slider.onDragStart = [&proc] { proc.undo.beginNewTransaction(); };
        slider.onEdit = [this] { openEditor(); };
        slider.onValueChange = [this] { repaint(); };
        // the editor: a small field over the value; Enter applies, Escape cancels
        editor.setJustification(Justification::centred);
        editor.setFont(sans(13.f, true));
        editor.setIndents(4, 3);
        editor.onReturnKey = [this, &proc] { commit(proc); };
        editor.onEscapeKey = [this] { closeEditor(); };
        editor.onFocusLost = [this, &proc] { if (editor.isVisible()) commit(proc); };
        addChildComponent(editor);
        setInterceptsMouseClicks(false, true);
    }
    void openEditor() {
        const double v = slider.getValue();
        editor.setText(std::abs(v) >= 100.0 ? String(roundToInt(v)) : String(v, 1), false);
        editor.setVisible(true);
        editor.grabKeyboardFocus();
        editor.selectAll();
    }
    void commit(CorridorAudioProcessor& proc) {
        String t = editor.getText().replace(",", ".").replace(String(CharPointer_UTF8("\xe2\x88\x92")), "-").trim();
        const bool seconds = t.endsWithIgnoreCase("s") && !t.endsWithIgnoreCase("ms");
        double v = t.retainCharacters("-+.0123456789").getDoubleValue();
        if (seconds && endLabels.first.isNotEmpty()) v *= 1000.0;  // "0.5 s" works for Response
        if (t.containsAnyOf("0123456789")) { proc.undo.beginNewTransaction(); slider.setValue(v, sendNotificationSync); }
        closeEditor();
    }
    void closeEditor() { editor.setVisible(false); slider.grabKeyboardFocus(); }
    bool isEditing() const { return editor.isVisible(); }
    bool inUse() const { return slider.isMouseOverOrDragging() || isEditing(); }

    int ringR() const { return int(r + 9.f); }
    int dialSize() const { return (ringR() + 7) * 2; }
    void placeAt(Point<int> c) {
        const int w = jmax(150, dialSize() + 20), h = dialSize() + 46;
        setBounds(c.x - w / 2, c.y - dialSize() / 2, w, h);
    }
    void setLive(float db) { slider.getProperties().set("live", db); }
    void resized() override {
        const int d = dialSize();
        slider.setBounds((getWidth() - d) / 2, 0, d, d);
        editor.setBounds((getWidth() - 100) / 2, d + 13, 100, 24);
    }
    void paint(Graphics& g) override {
        auto* look = dynamic_cast<Look*>(&getLookAndFeel());
        if (look == nullptr) return;
        const Pal& c = look->p();
        const float cx = float(getWidth()) * 0.5f, cy = float(dialSize()) * 0.5f, rr = float(ringR());
        if (endLabels.first.isNotEmpty()) {  // which way is which: the start of the sweep is fast, the end slow
            const float ex = std::sin(kPi * 0.75f) * (rr + 2.f), ey = std::cos(kPi * 0.25f) * (rr + 2.f) + 10.f;
            g.setFont(sans(11.f)); g.setColour(c.muted);
            g.drawText(endLabels.first, Rectangle<float>(cx - ex - 60.f, cy + ey - 8.f, 60.f, 16.f), Justification::centredRight);
            g.drawText(endLabels.second, Rectangle<float>(cx + ex, cy + ey - 8.f, 60.f, 16.f), Justification::centredLeft);
        }
        const float ly = cy + rr + 16.f;
        g.setFont(sans(r > 40.f ? 15.f : 13.5f, true)); g.setColour(c.ink);
        g.drawText(title, Rectangle<float>(0.f, ly - 9.f, float(getWidth()), 18.f), Justification::centred);
        if (!isEditing()) {
            const String role = slider.getProperties()["role"].toString();
            g.setFont(sans(12.5f)); g.setColour(slider.isMouseOverOrDragging() ? look->roleText(role) : c.muted);
            g.drawText(slider.getTextFromValue(slider.getValue()), Rectangle<float>(0.f, ly + 8.f, float(getWidth()), 18.f), Justification::centred);
        }
    }
    DialSlider slider;

private:
    String title;
    float r;
    std::pair<String, String> endLabels;
    TextEditor editor;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> att;
};

// ---------------------------------------------------------------- segmented control (Ride / Hold, A / B)
// A glass capsule whose selected side is a brighter droplet that slides across.
class Segmented : public Component {
public:
    Segmented(GlassContext& g_, StringArray labels_) : gc(g_), labels(std::move(labels_)) { setWantsKeyboardFocus(true); }
    std::function<void(int)> onSelect;
    int selected = 0;
    float thumb = 0.f;  // animated position of the droplet, in segments
    float fontSize = 13.5f;
    void setSelected(int s) {
        if (s == selected) return;
        selected = s;
        if (auto* h = getAccessibilityHandler()) h->notifyAccessibilityEvent(AccessibilityEvent::valueChanged);
    }
    std::unique_ptr<AccessibilityHandler> createAccessibilityHandler() override {
        struct Value : AccessibilityTextValueInterface {
            explicit Value(Segmented& o) : owner(o) {}
            bool isReadOnly() const override { return false; }
            String getCurrentValueAsString() const override { return owner.labels[owner.selected]; }
            void setValueAsString(const String& v) override { if (const int i = owner.labels.indexOf(v, true); i >= 0) owner.choose(i); }
            Segmented& owner;
        };
        return std::make_unique<AccessibilityHandler>(
            *this, AccessibilityRole::comboBox,
            AccessibilityActions().addAction(AccessibilityActionType::press, [this] { choose((selected + 1) % labels.size()); }),
            AccessibilityHandler::Interfaces{std::make_unique<Value>(*this)});
    }
    void advance() { const float to = float(selected); thumb = std::abs(to - thumb) < 0.01f ? to : thumb + (to - thumb) * 0.3f; }
    void paint(Graphics& g) override {
        const Pal& c = *gc.pal;
        const auto r = getLocalBounds().toFloat().reduced(1.f);
        drawGlass(g, gc, *this, capsule(r), {Colours::black.withAlpha(0.18f), 1.f, false, 0.f, 0.f, 1.2f});
        const float sw = r.getWidth() / float(labels.size());
        drawGlass(g, gc, *this, capsule(Rectangle<float>(r.getX() + 2.f + thumb * sw, r.getY() + 2.f, sw - 4.f, r.getHeight() - 4.f)),
                  {c.glassStrong, 1.2f, true, 8.f, 2.f, 1.2f});
        g.setFont(sans(fontSize, true));
        for (int i = 0; i < labels.size(); ++i) {
            g.setColour(i == selected ? c.ink : (hoverIndex == i ? c.label : c.muted));
            g.drawText(labels[i], Rectangle<float>(r.getX() + float(i) * sw, r.getY(), sw, r.getHeight()), Justification::centred);
        }
        if (hasKeyboardFocus(false)) { g.setColour(c.ink); g.strokePath(capsule(r.expanded(3.f)), PathStrokeType(2.f)); }
    }
    int indexAt(Point<float> p) const { return jlimit(0, labels.size() - 1, int(p.x / (float(getWidth()) / float(labels.size())))); }
    void mouseMove(const MouseEvent& e) override { hoverIndex = indexAt(e.position); repaint(); }
    void mouseExit(const MouseEvent&) override { hoverIndex = -1; repaint(); }
    void mouseDown(const MouseEvent& e) override { choose(indexAt(e.position)); }
    bool keyPressed(const KeyPress& k) override {
        if (k.isKeyCode(KeyPress::leftKey)) { choose(jmax(0, selected - 1)); return true; }
        if (k.isKeyCode(KeyPress::rightKey)) { choose(jmin(labels.size() - 1, selected + 1)); return true; }
        if (k == KeyPress::spaceKey || k == KeyPress::returnKey) { choose((selected + 1) % labels.size()); return true; }
        return false;
    }
    void choose(int i) { if (i != selected && onSelect) onSelect(i); }

private:
    GlassContext& gc;
    StringArray labels;
    int hoverIndex = -1;
};

// ---------------------------------------------------------------- the graph and everything behind the glass
class Scene : public Component {
public:
    Scene(CorridorAudioProcessor& p, GlassContext& g_) : proc(p), gc(g_) { hist.reserve(kHist); }
    std::vector<corridor::Snapshot> hist;
    int histWritten = 0;  // the engine's running count of history entries, so a redraw is skipped only when nothing moved
    bool analyzing = false;
    float analyzeSeconds = 0.f;
    const char* hover = nullptr;
    const char* dragId = nullptr;

    // background, grid, traces and the three lines: what the glass shows blurred
    void drawBackdrop(Graphics& g) {
        const Pal& c = *gc.pal;
        g.fillAll(c.bg);
        auto bloom = [&](float x, float y, float rad, Colour col) {
            g.setGradientFill(ColourGradient(col, x, y, col.withAlpha(0.f), x + rad, y, true));
            g.fillRect(0, 0, W, H);
        };
        bloom(160.f, 40.f, 520.f, c.bloomA); bloom(1000.f, 600.f, 560.f, c.bloomB);
        for (int v = -5; v >= -80; v -= 10) {  // the 5 dB lines, faint; drawScale draws and labels every 10 dB
            g.setColour(c.grid.withMultipliedAlpha(0.45f));
            g.fillRect(PX0 + 22.f, std::round(yOf(float(v))), PX1 - PX0 - 44.f, 1.f);
        }
        const float x0 = PX0 + 22.f, x1 = PX1 - 22.f;
        const int n = int(hist.size());
        const float sx = (x1 - x0) / float(kHist - 1);
        auto X = [&](int k) { return x1 - float(n - 1 - k) * sx; };
        if (n > 1) {
            Path fillP, inP, outP;
            fillP.startNewSubPath(X(0), GB);
            for (int k = 0; k < n; ++k) {
                const float yi = yOf(hist[size_t(k)].inRms), yo = yOf(hist[size_t(k)].outRms);
                fillP.lineTo(X(k), yi);
                if (k == 0) { inP.startNewSubPath(X(k), yi); outP.startNewSubPath(X(k), yo); }
                else { inP.lineTo(X(k), yi); outP.lineTo(X(k), yo); }
            }
            fillP.lineTo(X(n - 1), GB); fillP.closeSubPath();
            g.setGradientFill(ColourGradient(c.in.withMultipliedAlpha(0.45f), 0.f, GT, c.in.withAlpha(0.f), 0.f, GB, false));
            g.fillPath(fillP);
            g.setColour(c.in); g.strokePath(inP, PathStrokeType(1.3f, PathStrokeType::curved));
            g.setColour(c.data); g.strokePath(outP, PathStrokeType(2.4f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        if (analyzing && n > 1) {
            const int span = jmin(n - 1, int(analyzeSeconds * 50.f));
            g.setColour(c.data.withAlpha(0.08f));
            g.fillRect(Rectangle<float>(X(n - 1 - span), GT, X(n - 1) - X(n - 1 - span), GB - GT));
        }
        auto& st = proc.state();
        auto line = [&](const char* id, Colour col, float d1, float d2, float width) {
            const float y = std::round(yOf(st.getRawParameterValue(id)->load())) + 0.5f;
            const float w = (hover == id || dragId == id) ? 2.6f : width;
            g.setColour(col);
            if (d1 <= 0.f) g.fillRect(x0, y - w * 0.5f, x1 - x0, w);
            else { const float dashes[] = {d1, d2}; g.drawDashedLine(Line<float>(x0, y, x1, y), dashes, 2, w); }
        };
        line(ids::ceiling, c.ceil, 5.f, 5.f, 1.3f);
        line(ids::threshold, c.thr, 7.f, 5.f, 1.4f);
        line(ids::target, c.data, 0.f, 0.f, 2.f);
    }

    // a small blurred copy of the backdrop for the glass, remade only when something behind the glass changed
    void renderBlur() {
        if (const auto key = backdropKey(); gc.blur.isValid() && key == lastKey) return;
        else lastKey = key;
        constexpr int bw = W / 5, bh = H / 5;
        if (!blurSrc.isValid()) blurSrc = Image(Image::ARGB, bw, bh, true);
        {
            Graphics g(blurSrc);
            g.addTransform(AffineTransform::scale(float(bw) / float(W), float(bh) / float(H)));
            drawBackdrop(g);
        }
        if (!gc.blur.isValid()) gc.blur = Image(Image::ARGB, bw, bh, true);
        if (!kernelReady) { kernel.createGaussianBlur(2.6f); kernelReady = true; }
        kernel.applyToImage(gc.blur, blurSrc, blurSrc.getBounds());
    }

    // everything drawBackdrop reads, folded into one number
    uint64 backdropKey() const {
        uint64 h = 1469598103934665603ull;
        auto mix = [&h](const void* p, size_t n) { for (size_t k = 0; k < n; ++k) { h ^= static_cast<const uint8*>(p)[k]; h *= 1099511628211ull; } };
        mix(&histWritten, sizeof histWritten);
        mix(&analyzing, sizeof analyzing); mix(&analyzeSeconds, sizeof analyzeSeconds);
        mix(&hover, sizeof hover); mix(&dragId, sizeof dragId);
        for (const char* id : {ids::ceiling, ids::threshold, ids::target}) { const float v = proc.state().getRawParameterValue(id)->load(); mix(&v, sizeof v); }
        return h;
    }

    void paint(Graphics& g) override {
        const Pal& c = *gc.pal;
        drawBackdrop(g);
        // legend
        const float lx = PX1 - 330.f, ly = 80.f;
        g.setFont(sans(12.f));
        g.setColour(c.in); g.fillRect(lx, ly - 1.f, 16.f, 2.f);
        g.setColour(c.muted); g.drawText("Input", Rectangle<float>(lx + 22.f, ly - 8.f, 60.f, 16.f), Justification::centredLeft);
        g.setColour(c.data); g.fillRect(lx + 76.f, ly - 1.5f, 16.f, 3.f);
        g.setColour(c.muted); g.drawText("Output", Rectangle<float>(lx + 98.f, ly - 8.f, 70.f, 16.f), Justification::centredLeft);
        g.drawText("RMS, last 10 s", Rectangle<float>(PX1 - 222.f, ly - 8.f, 200.f, 16.f), Justification::centredRight);
        // the controls sit in the graph: below here it frosts over and fades into the window colour
        if (gc.blur.isValid()) {
            constexpr int bands = 16;
            const float span = float(H) - kFadeTop;
            for (int i = 0; i < bands; ++i) {
                Graphics::ScopedSaveState ss(g);
                g.reduceClipRegion(Rectangle<int>(0, int(kFadeTop + span * float(i) / bands), W, int(span / bands) + 2));
                g.setOpacity(jmin(1.f, std::pow(float(i + 1) / bands, 0.8f)));
                g.drawImage(gc.blur, Rectangle<float>(0.f, 0.f, float(W), float(H)), RectanglePlacement::stretchToFit);
            }
        }
        ColourGradient f(c.bg.withAlpha(0.f), 0.f, kFadeTop, c.bg.withAlpha(0.9f), 0.f, float(H), false);
        f.addColour(0.4, c.bg.withAlpha(0.62f));
        g.setGradientFill(f); g.fillRect(0.f, kFadeTop, float(W), float(H) - kFadeTop);
        drawScale(g);
    }

    // The graph's own dB scale, drawn crisp over everything: a line every 10 dB at exactly the heights the meters
    // mark, labelled at both edges. Labels skip the two readout boxes in the bottom corners.
    void drawScale(Graphics& g) {
        const Pal& c = *gc.pal;
        const float x0 = PX0 + 22.f, x1 = PX1 - 22.f;
        g.setFont(sans(12.f, true));
        for (int v = 0; v >= -int(kDbRange); v -= 10) {
            const float y = std::round(yOf(float(v)));
            g.setColour(c.grid.withMultipliedAlpha(y > kFadeTop ? 0.7f : 1.f));
            g.fillRect(x0, y, x1 - x0, 1.f);
            if (y > 540.f) continue;  // the "From target" and "Applied gain" boxes sit here
            g.setColour(c.label);
            const String t = fmtDb(float(v), 0);
            g.drawText(t, Rectangle<float>(PX0 - 14.f, y - 8.f, 30.f, 16.f), Justification::centredRight);  // ends before the traces begin
            g.drawText(t, Rectangle<float>(PX1 - 14.f, y - 8.f, 30.f, 16.f), Justification::centredLeft);
        }
    }

    // drag the Target, Threshold and Ceiling lines (above the controls)
    const char* hit(Point<float> p) const {
        if (p.x < PX0 + 22.f || p.x > PX1 - 22.f || p.y < GT - 8.f || p.y > GB + 8.f || kDeck.contains(p)) return nullptr;
        const char* best = nullptr; float bd = 8.f;
        for (const char* id : {ids::target, ids::threshold, ids::ceiling}) {
            const float d = std::abs(yOf(proc.state().getRawParameterValue(id)->load()) - p.y);
            if (d < bd) { bd = d; best = id; }
        }
        return best;
    }
    void mouseMove(const MouseEvent& e) override {
        hover = hit(e.position);
        setMouseCursor(hover ? MouseCursor::UpDownResizeCursor : MouseCursor::NormalCursor);
    }
    void mouseExit(const MouseEvent&) override { hover = nullptr; }
    void mouseDown(const MouseEvent& e) override {
        dragId = hit(e.position);
        if (dragId) if (auto* p = proc.state().getParameter(dragId)) { proc.undo.beginNewTransaction(); p->beginChangeGesture(); mouseDrag(e); }
    }
    void mouseDrag(const MouseEvent& e) override {
        if (dragId) if (auto* p = proc.state().getParameter(dragId)) p->setValueNotifyingHost(p->convertTo0to1(vOf(e.position.y)));
    }
    void mouseUp(const MouseEvent&) override {
        if (dragId) if (auto* p = proc.state().getParameter(dragId)) p->endChangeGesture();
        dragId = nullptr;
    }

private:
    CorridorAudioProcessor& proc;
    GlassContext& gc;
    Image blurSrc;
    ImageConvolutionKernel kernel{7};
    bool kernelReady = false;
    uint64 lastKey = 0;
};

// ---------------------------------------------------------------- a meter: a glass capsule with L and R bars
class Meter : public Component {
public:
    Meter(bool isInput, GlassContext& g_, corridor::Processor& d) : input(isInput), gc(g_), dsp(d) {}
    float peakL = -120.f, peakR = -120.f, hold = -120.f, balance = 0.f;
    void paint(Graphics& g) override {
        const Pal& c = *gc.pal;
        const float w = float(getWidth()), cx = w * 0.5f;
        auto ly = [](float v) { return yOf(v) - kMeterY; };  // the graph's scale, in this component's coordinates
        const float top = ly(0.f), bot = ly(-kDbRange);
        drawGlass(g, gc, *this, capsule(getLocalBounds().toFloat(), 24.f), {Colour(), 1.05f, true, 18.f, 7.f, 1.4f});
        auto zone = [&](float v) { return v > -6.f ? c.mHot : v > -18.f ? c.mWarn : c.mSafe; };
        // clip light, peak readout in its zone colour, name
        g.setColour(hold > -0.5f ? c.mHot : c.meterOff); g.fillRoundedRectangle(cx - 12.f, 8.f, 24.f, 5.f, 2.5f);
        g.setFont(sans(11.5f, true)); g.setColour(hold > -kDbRange ? zone(hold) : c.muted);
        g.drawText(hold > -kDbRange ? fmtDb(hold) : String(CharPointer_UTF8("\xe2\x80\x93")), Rectangle<float>(0.f, 17.f, w, 16.f), Justification::centred);
        g.setFont(sans(11.f)); g.setColour(c.muted);
        g.drawText(input ? "In peak" : "Out peak", Rectangle<float>(0.f, 33.f, w, 16.f), Justification::centred);  // the target is RMS; say this is not
        // left and right bars in fixed zones: green up to -18 dBFS, amber to -6, red above
        const float bw = 5.f, gap = 3.f, bx = input ? 12.f : w - 12.f - bw * 2.f - gap;
        const double f18 = (kDbRange - 18.0) / kDbRange, f6 = (kDbRange - 6.0) / kDbRange;
        ColourGradient zg(c.mSafe, 0.f, bot, c.mHot, 0.f, top, false);
        zg.addColour(f18 - 0.001, c.mSafe); zg.addColour(f18, c.mWarn); zg.addColour(f6 - 0.001, c.mWarn); zg.addColour(f6, c.mHot);
        const float pk[2] = {peakL, peakR};
        for (int ch = 0; ch < 2; ++ch) {
            const float x = bx + float(ch) * (bw + gap);
            g.setColour(c.meterOff); g.fillRoundedRectangle(x, top, bw, bot - top, 2.5f);
            if (pk[ch] > -kDbRange) {
                const float t = ly(jmin(0.f, pk[ch]));
                g.setGradientFill(zg); g.fillRoundedRectangle(x, t, bw, bot - t, 2.5f);
            }
            if (hold > -kDbRange) { g.setColour(zone(hold)); g.fillRect(x - 1.f, ly(jmin(0.f, hold)) - 1.f, bw + 2.f, 2.f); }
        }
        // the graph's marks: a tick every 5 dB, a number every 10, at the same heights
        const float tickX = input ? bx + bw * 2.f + gap + 3.f : bx - 7.f;
        g.setFont(sans(11.f));
        for (int v = 0; v >= -80; v -= 5) {
            const float y = std::round(ly(float(v))) + 0.5f;
            const bool major = v % 10 == 0;
            g.setColour(c.muted.withMultipliedAlpha(major ? 0.9f : 0.5f));
            g.fillRect(tickX, y, major ? 4.f : 3.f, 1.f);
            if (!major) continue;
            g.setColour(c.muted);
            if (input) g.drawText(fmtDb(float(v), 0), Rectangle<float>(tickX + 7.f, y - 7.f, 30.f, 14.f), Justification::centredLeft);
            else g.drawText(fmtDb(float(v), 0), Rectangle<float>(tickX - 33.f, y - 7.f, 30.f, 14.f), Justification::centredRight);
        }
        // stereo balance: L and R at the ends of a short track, the dot leans to the louder side
        const float by = bot + 11.f, half = 15.f, lean = jlimit(-1.f, 1.f, balance / 4.f);
        g.setColour(c.muted); g.setFont(sans(11.f, true));
        g.drawText("L", Rectangle<float>(cx - half - 13.f, by - 7.f, 12.f, 14.f), Justification::centred);
        g.drawText("R", Rectangle<float>(cx + half + 1.f, by - 7.f, 12.f, 14.f), Justification::centred);
        g.setColour(c.meterOff); g.fillRoundedRectangle(cx - half, by - 1.5f, half * 2.f, 3.f, 1.5f);
        g.setColour(c.muted); g.fillRect(cx - 0.5f, by - 4.f, 1.f, 8.f);
        g.setColour(c.ink); g.fillEllipse(cx - lean * half - 3.5f, by - 3.5f, 7.f, 7.f);
    }
    void mouseDown(const MouseEvent&) override { dsp.resetPeakHolds(); }

private:
    bool input;
    GlassContext& gc;
    corridor::Processor& dsp;
};

// ---------------------------------------------------------------- readouts: from target, applied gain, latency
class Readouts : public Component {
public:
    explicit Readouts(GlassContext& g_) : gc(g_) { setInterceptsMouseClicks(false, false); }
    String fromTarget = "Waiting", gainText = "0.0", latency;
    bool onTarget = false;
    void paint(Graphics& g) override {
        const Pal& c = *gc.pal;
        auto box = [&](Rectangle<float> r, const String& name, const String& value, Colour col, float size) {
            drawGlass(g, gc, *this, capsule(r, 22.f), {Colour(), 1.05f, true, 18.f, 7.f, 1.4f});
            g.setFont(sans(12.f)); g.setColour(c.muted);
            g.drawText(name, r.reduced(16.f, 9.f).withHeight(16.f), Justification::centredLeft);
            g.setFont(sans(size, true)); g.setColour(col);
            g.drawFittedText(value, r.reduced(14.f, 6.f).withTrimmedTop(20.f).toNearestInt(), Justification::centredRight, 1, 0.8f);
        };
        box({90.f, 548.f, 124.f, 58.f}, "From target", fromTarget, onTarget ? c.dataText : c.ink, 17.f);
        box({kDeck.getRight() + 12.f, 548.f, 124.f, 58.f}, "Applied gain", gainText + " dB", c.ink, 22.f);  // the sign says boost or cut
        g.setFont(sans(11.f)); g.setColour(c.muted);
        g.drawText(latency, Rectangle<float>(94.f, GB + 14.f, 300.f, 16.f), Justification::centredLeft);
    }

private:
    GlassContext& gc;
};

// ---------------------------------------------------------------- line labels, shown while their line or dial is in use
class Tags : public Component {
public:
    Tags(CorridorAudioProcessor& p, GlassContext& g_) : proc(p), gc(g_) { setInterceptsMouseClicks(false, false); }
    bool show[3] = {false, false, false};  // ceiling, threshold, target
    void paint(Graphics& g) override {
        const Pal& c = *gc.pal;
        const char* idsList[3] = {ids::ceiling, ids::threshold, ids::target};
        const char* names[3] = {"Ceiling", "Threshold", "Target"};
        const Colour cols[3] = {c.ceil, c.thr, c.data}, texts[3] = {c.ceilText, c.thrText, c.dataText};
        for (int i = 0; i < 3; ++i) {
            if (!show[i]) continue;
            const float v = proc.state().getRawParameterValue(idsList[i])->load();
            const float lineY = yOf(v);
            const bool under = lineY > kFadeTop + 24.f;
            const float y = under ? kFadeTop + 22.f : lineY;
            const String label = String(names[i]) + "  " + fmtDb(v);
            const float w = GlyphArrangement::getStringWidth(sans(12.f, true), label) + (under ? 40.f : 28.f);
            const Rectangle<float> r(PX0 + 26.f, y - 12.f, w, 24.f);
            drawGlass(g, gc, *this, capsule(r), {Colour(), 1.06f, false, 0.f, 0.f, 1.3f});
            g.setColour(cols[i]); g.fillEllipse(r.getX() + 7.5f, y - 3.5f, 7.f, 7.f);
            g.setFont(sans(12.f, true)); g.setColour(texts[i]);
            g.drawText(label, r.withTrimmedLeft(20.f), Justification::centredLeft);
            if (under) {
                Path a; const float ax = r.getRight() - 12.f;
                a.addTriangle(ax - 4.f, y - 2.f, ax + 4.f, y - 2.f, ax, y + 3.f);
                g.fillPath(a);
            }
        }
    }

private:
    CorridorAudioProcessor& proc;
    GlassContext& gc;
};

// ---------------------------------------------------------------- the header plate: glass, mark, name and status
class Header : public Component, public SettableTooltipClient {
public:
    explicit Header(GlassContext& g_) : gc(g_) { setInterceptsMouseClicks(false, false); }
    String status;
    Colour dot;
    Rectangle<float> statusArea;
    void paint(Graphics& g) override {
        const Pal& c = *gc.pal;
        drawGlass(g, gc, *this, capsule(getLocalBounds().toFloat().reduced(0.5f)), {Colour(), 1.05f, true, 20.f, 7.f, 1.4f});
        drawIcon(g, "logo", Rectangle<float>(18.f, 11.f, 20.f, 20.f), c.ink);
        g.setFont(sans(16.f, true)); g.setColour(c.ink);
        g.drawText("Corridor", Rectangle<float>(48.f, 0.f, 90.f, 42.f), Justification::centredLeft);
        const float nw = GlyphArrangement::getStringWidth(sans(16.f, true), "Corridor");
        g.setFont(sans(16.f)); g.setColour(c.muted);
        g.drawText("Leveler", Rectangle<float>(48.f + nw + 6.f, 0.f, 80.f, 42.f), Justification::centredLeft);
        // status: a dot and a short sentence, centred in the space left between the controls
        if (status.isNotEmpty() && statusArea.getWidth() > 40.f) {
            const Font f = sans(13.5f);
            const float tw = jmin(statusArea.getWidth() - 24.f, GlyphArrangement::getStringWidth(f, status));
            const float x0 = statusArea.getCentreX() - (tw + 14.f) * 0.5f;
            g.setColour(dot); g.fillEllipse(x0, 21.f - 4.f, 8.f, 8.f);
            g.setFont(f); g.setColour(c.ink);
            g.drawText(status, Rectangle<float>(x0 + 14.f, 0.f, tw + 2.f, 42.f), Justification::centredLeft, true);
        }
    }

private:
    GlassContext& gc;
};

// ---------------------------------------------------------------- Rise / Fall / Trim panel
class MorePanel : public Component {
public:
    MorePanel(CorridorAudioProcessor& p, GlassContext& g_) : proc(p), gc(g_) {
        setTitle("Rise, fall and output trim");
        add(rise, riseValue, ids::rise, "Rise", [](double v) { return String(roundToInt(v)) + " ms"; });
        add(fall, fallValue, ids::fall, "Fall", [](double v) { return String(roundToInt(v)) + " ms"; });
        add(trim, trimValue, ids::trim, "Output trim", [](double v) { return fmtDb(float(v), 1, true) + " dB"; });
    }
    void resized() override {
        int y = 14;
        for (auto* row : {&rows[0], &rows[1], &rows[2]}) {
            row->slider->setBounds(96, y, 120, 28);
            row->value->setBounds(222, y + 2, 64, 24);
            y += 36;
        }
    }
    void refresh() { for (auto& r : rows) if (!r.value->isBeingEdited()) r.value->setText(r.slider->getTextFromValue(r.slider->getValue()), dontSendNotification); }
    void paint(Graphics& g) override {
        const Pal& c = *gc.pal;
        drawGlass(g, gc, *this, capsule(getLocalBounds().toFloat().reduced(0.5f), 20.f), {c.panel, 1.03f, true, 24.f, 10.f, 1.4f});
        g.setFont(sans(13.f, true)); g.setColour(c.ink);
        int y = 14;
        for (auto* name : {"Rise", "Fall", "Output trim"}) { g.drawText(name, Rectangle<int>(16, y, 80, 28), Justification::centredLeft); y += 36; }
        g.setFont(sans(11.5f)); g.setColour(c.muted);
        g.drawFittedText("Rise: how fast quiet passages are lifted. Fall: how fast loud ones are pulled down. A slow rise and a fast fall suit voice.",
                         Rectangle<int>(16, y + 2, getWidth() - 32, 34), Justification::topLeft, 2, 1.f);
        if (auto* look = dynamic_cast<Look*>(&getLookAndFeel())) {
            for (auto* v : {&riseValue, &fallValue, &trimValue}) {
                v->setColour(Label::textColourId, c.ink);
                v->setColour(Label::backgroundColourId, c.on);
            }
            ignoreUnused(look);
        }
    }

private:
    struct Row { Slider* slider; Label* value; };
    void add(Slider& s, Label& v, const char* id, const char* name, std::function<String(double)> fmt) {
        s.setSliderStyle(Slider::LinearHorizontal);
        s.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        s.setTitle(name);
        s.onDragStart = [this] { proc.undo.beginNewTransaction(); };
        addAndMakeVisible(s);
        atts.push_back(std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.state(), id, s));
        s.textFromValueFunction = fmt;
        v.setEditable(true, true, false);
        v.setJustificationType(Justification::centredRight);
        v.setFont(sans(13.f));
        v.setTitle(String(name) + " value");
        v.onTextChange = [this, &s, &v] {
            const String t = v.getText().replace(",", ".").replace(String(CharPointer_UTF8("\xe2\x88\x92")), "-");
            if (t.containsAnyOf("0123456789")) { proc.undo.beginNewTransaction(); s.setValue(t.retainCharacters("-+.0123456789").getDoubleValue(), sendNotificationSync); }
            refresh();
        };
        addAndMakeVisible(v);
        rows[rowCount++] = {&s, &v};
    }
    CorridorAudioProcessor& proc;
    GlassContext& gc;
    Slider rise, fall, trim;
    Label riseValue, fallValue, trimValue;
    Row rows[3]{};
    int rowCount = 0;
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment>> atts;
};

// ---------------------------------------------------------------- first-run tip
class Tip : public Component {
public:
    explicit Tip(GlassContext& g_) : gc(g_) {
        ok.getProperties().set("kind", "plain");
        ok.getProperties().set("inverse", true);  // light text on the ink pill
        ok.getProperties().set("fontSize", 13.5);
        ok.onClick = [this] { if (onDismiss) onDismiss(); };
        addAndMakeVisible(ok);
        setTitle("Getting started");
    }
    std::function<void()> onDismiss;
    void resized() override { ok.setBounds(getWidth() - 96, (getHeight() - 34) / 2, 80, 34); }
    void paint(Graphics& g) override {
        const Pal& c = *gc.pal;
        drawGlass(g, gc, *this, capsule(getLocalBounds().toFloat().reduced(0.5f), 20.f), {c.panel, 1.03f, true, 24.f, 10.f, 1.4f});
        g.setFont(sans(13.f, true)); g.setColour(c.ink);
        g.drawText("Getting started", Rectangle<int>(16, 12, getWidth() - 120, 18), Justification::centredLeft);
        g.setFont(sans(13.f)); g.setColour(c.label);
        g.drawFittedText("Set Target where you want your voice. Then lower Threshold until the room noise sits below its dashed line.",
                         Rectangle<int>(16, 30, getWidth() - 124, 36), Justification::topLeft, 2, 1.f);
        g.setColour(c.ink); g.fillRoundedRectangle(ok.getBounds().toFloat(), 17.f);
    }

private:
    GlassContext& gc;
    GlassButton ok{"Got it"};
};

// ---------------------------------------------------------------- the Hold sheet under the window
class Drawer : public Component {
public:
    Drawer(CorridorAudioProcessor& p, GlassContext& g_) : proc(p), gc(g_) {
        analyze.getProperties().set("kind", "key");
        analyze.getProperties().set("fontSize", 14.0);
        analyze.setTitle("Analyze or lock");
        analyze.onClick = [this] {
            if (proc.core().telemetry().analyzing.load()) proc.core().requestAnalyzeLock();
            else proc.core().requestAnalyzeStart();
        };
        addAndMakeVisible(analyze);
        setTitle("Analyze and hold");
    }
    std::vector<corridor::Snapshot> wave;
    GlassButton analyze{"Analyze"};
    void refresh() {
        const auto& t = proc.core().telemetry();
        const bool an = t.analyzing.load();
        analyze.setButtonText(an ? "Lock" : t.holdValid.load() ? "Analyze again" : "Analyze");
        analyze.setToggleState(an, dontSendNotification);
        repaint();
    }
    void resized() override { analyze.setBounds(36, 36, 196, 36); }
    void paint(Graphics& g) override {
        const Pal& c = *gc.pal;
        const float w = float(getWidth()), h = float(getHeight());
        Path sheet; sheet.addRoundedRectangle(18.f, -24.f, w - 36.f, h - 6.f + 24.f, 26.f, 26.f, false, false, true, true);
        DropShadow(c.shade, 24, {0, 10}).drawForPath(g, sheet);
        g.setColour(c.panel); g.fillPath(sheet);
        g.setColour(c.rimSoft); g.strokePath(sheet, PathStrokeType(1.f));
        const auto& t = proc.core().telemetry();
        g.setFont(sans(15.f, true)); g.setColour(c.ink);
        g.drawText("Analyze & hold", Rectangle<float>(36.f, 10.f, 200.f, 22.f), Justification::centredLeft);
        // readout: words first, the key figure in the data colour
        struct ReadLine { String text; bool hot; };
        std::vector<ReadLine> lines;
        if (t.analyzing.load()) {
            lines = {{"Analyzing " + String(t.analyzeSeconds.load(), 1) + " s", true},
                     {"Above threshold " + String(t.analyzeActiveSeconds.load(), 1) + " s", false},
                     {"Locks by itself at 20 s", false}};
        } else if (t.analyzeFailed.load()) {
            lines = {{"Not enough sound above", false}, {"the threshold. Analyze again.", false}};
        } else if (t.holdValid.load()) {
            lines = {{"Holding " + fmtDb(t.holdGain.load(), 1, true) + " dB", true},
                     {"Measured " + fmtDb(t.holdMeasured.load(), 1) + " dB RMS", false},
                     {"over " + String(t.holdSeconds.load(), 1) + " s", false}};
        } else {
            lines = {{"Press Analyze, then play", false}, {"a typical section.", false}};
        }
        g.setFont(sans(12.5f));
        float y = 78.f;
        for (const auto& l : lines) {
            g.setColour(l.hot ? c.dataText : c.muted);
            g.drawText(l.text, Rectangle<float>(36.f, y, 210.f, 18.f), Justification::centredLeft);
            y += 16.f;
        }
        g.setFont(sans(11.f)); g.setColour(c.muted);
        g.drawText("Shortcut A: analyze, then lock", Rectangle<float>(36.f, h - 24.f, 220.f, 14.f), Justification::centredLeft);
        // waveform: input peaks above the centre line, RMS below; the measured stretch in the data colour
        const auto wr = Rectangle<float>(262.f, 14.f, w - 262.f - 36.f, h - 40.f);
        g.setColour(Colours::black.withAlpha(0.28f));
        g.fillRoundedRectangle(wr, 18.f);
        g.setColour(c.rimSoft); g.drawRoundedRectangle(wr, 18.f, 1.f);
        const float mid = wr.getCentreY(), half = wr.getHeight() * 0.5f - 8.f;
        auto amp = [](float db) { return jlimit(0.f, 1.f, (db + 66.f) / 60.f); };  // -66 dB at the centre, -6 at the edge
        g.setColour(c.grid); g.fillRect(wr.getX(), mid, wr.getWidth(), 1.f);
        const float thr = proc.state().getRawParameterValue(ids::threshold)->load(), ta = amp(thr) * half;
        const float dashes[] = {4.f, 4.f};
        g.setColour(c.thr.withAlpha(0.8f));
        g.drawDashedLine(Line<float>(wr.getX() + 6.f, mid - ta, wr.getRight() - 6.f, mid - ta), dashes, 2, 1.f);
        g.drawDashedLine(Line<float>(wr.getX() + 6.f, mid + ta, wr.getRight() - 6.f, mid + ta), dashes, 2, 1.f);
        const int n = int(wave.size());
        const float step = (wr.getWidth() - 12.f) / float(kWaveEntries);
        for (int i = 0; i < n; ++i) {
            const auto& s = wave[size_t(i)];
            const bool act = s.inRms >= thr;
            g.setColour((s.analyzing ? c.data : c.in).withMultipliedAlpha(act ? 1.f : 0.35f));
            const float x = wr.getRight() - 6.f - float(n - i) * step;
            const float up = amp(s.inPeak) * half, down = amp(s.inRms) * half;
            g.fillRect(x, mid - up, jmax(1.f, step * 0.8f), up + down);
        }
        g.setFont(sans(12.f)); g.setColour(c.muted);
        g.drawText("Input, last 6 s. Peak above, RMS below; dashed: threshold", wr.reduced(12.f, 8.f).withHeight(14.f), Justification::centredLeft);
    }

private:
    CorridorAudioProcessor& proc;
    GlassContext& gc;
};

// ---------------------------------------------------------------- updates: About panel and prompt
class AboutPanel : public Component {
public:
    AboutPanel(updates::Checker& c, GlassContext& g_) : checker(c), gc(g_) {
        for (auto* b : {&action, &close}) addAndMakeVisible(*b);
        close.getProperties().set("icon", "close");
        close.getProperties().set("kind", "circle");
        close.setTitle("Close");
        close.setButtonText({});
        action.onClick = [this] { if (checker.known().valid()) checker.install(); else checker.check(true); };
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
        action.getProperties().set("icon", r.valid() ? "download" : "");
        link.setURL(URL(r.valid() ? r.page : String(updates::kReleasesPage)));
        link.setColour(HyperlinkButton::textColourId, gc.pal->ink);
        repaint();
    }
    void resized() override {
        close.setBounds(getWidth() - 44, 12, 30, 30);
        action.setBounds(20, getHeight() - 54, 190, 34);
        link.setButtonText("Release page");
        link.setBounds(226, getHeight() - 54, 120, 34);
    }
    void paint(Graphics& g) override {
        const Pal& c = *gc.pal;
        drawGlass(g, gc, *this, capsule(getLocalBounds().toFloat().reduced(1.f), 20.f), {c.panel, 1.03f, true, 24.f, 10.f, 1.4f});
        drawIcon(g, "logo", Rectangle<float>(20.f, 18.f, 22.f, 22.f), c.ink);
        g.setColour(c.ink); g.setFont(sans(16.f, true));
        g.drawText("Corridor Leveler " + updates::currentVersion(), Rectangle<float>(52.f, 14.f, float(getWidth()) - 104.f, 28.f), Justification::centredLeft);
        g.setFont(sans(13.5f)); g.setColour(c.ink);
        g.drawFittedText(checker.status(), Rectangle<int>(20, 54, getWidth() - 40, 40), Justification::topLeft, 2);
        const String msg = checker.message();
        g.setColour(c.muted); g.setFont(sans(13.f));
        g.drawFittedText(msg.isNotEmpty() ? msg : "The check is a plain request to GitHub. Nothing about you or your computer is sent.",
                         Rectangle<int>(20, 96, getWidth() - 40, 36), Justification::topLeft, 2);
    }

private:
    updates::Checker& checker;
    GlassContext& gc;
    GlassButton action{"Check for updates"}, close{"x"};
    HyperlinkButton link{"Release page", URL(updates::kReleasesPage)};
};

class UpdatePrompt : public Component {
public:
    UpdatePrompt(updates::Checker& c, GlassContext& g_) : checker(c), gc(g_) {
        for (auto* b : {&update, &ignore}) { b->getProperties().set("fontSize", 14.0); addAndMakeVisible(*b); }
        update.getProperties().set("icon", "download");
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
        const Pal& c = *gc.pal;
        g.fillAll(Colours::black.withAlpha(0.5f));
        const auto b = box().toFloat();
        drawGlass(g, gc, *this, capsule(b, 22.f), {c.panel, 1.03f, true, 30.f, 10.f, 1.4f});
        const auto r = checker.known();
        g.setColour(c.ink); g.setFont(sans(20.f, true));
        g.drawText("Corridor Leveler " + r.version + " is available", b.reduced(24.f).withHeight(30.f), Justification::centredLeft);
        g.setFont(sans(14.f)); g.setColour(c.label);
        String body;
        if (checker.phase() == updates::Checker::Phase::Downloading)
            body = "Downloading... " + String(int(checker.progress() * 100.f)) + "%. The installer opens when it's done.";
        else {
            const String size = r.bytes > 0 ? " (" + String(double(r.bytes) / 1048576.0, 1) + " MB)" : String();
            body = "You have " + updates::currentVersion() + ". Update downloads the new version" + size +
                   " and opens its installer. When it asks, close OBS or your DAW. Your settings and sessions are kept.";
            if (checker.message().isNotEmpty()) body = checker.message();
        }
        g.drawFittedText(body, b.reduced(24.f).withTrimmedTop(40.f).withHeight(64.f).toNearestInt(), Justification::topLeft, 3);
        if (checker.phase() == updates::Checker::Phase::Downloading) {
            const auto bar = Rectangle<float>(b.getX() + 24.f, b.getBottom() - 92.f, b.getWidth() - 48.f, 6.f);
            g.setColour(c.meterOff); g.fillRoundedRectangle(bar, 3.f);
            g.setColour(c.data); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * checker.progress()), 3.f);
        }
    }

private:
    updates::Checker& checker;
    GlassContext& gc;
    GlassButton update{"Update"}, ignore{"Ignore"};
};

// per-user settings (the one-time tip): opened when needed and closed straight away,
// so nothing outlives the plugin
PropertiesFile::Options settingsOptions() {
    PropertiesFile::Options o;
    o.applicationName = "settings";
    o.filenameSuffix = "settings";
    o.folderName = "CorridorLeveler";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}
bool tipSeen() { PropertiesFile f(settingsOptions()); return f.getBoolValue("tipSeen", false); }
void markTipSeen() { PropertiesFile f(settingsOptions()); f.setValue("tipSeen", true); f.saveIfNeeded(); }

}  // namespace

// ---------------------------------------------------------------- content (design size)
class CorridorContent : public Component, private ChangeListener {
public:
    explicit CorridorContent(CorridorAudioProcessor& p)
        : proc(p), scene(p, gc), meterIn(true, gc, p.core()), meterOut(false, gc, p.core()), readouts(gc), tags(p, gc), header(gc),
          threshold(p, ids::threshold, "Threshold", [](double v) { return fmtDb(float(v)) + " dB"; }, 30.f, "thr"),
          response(p, ids::response, "Response", [](double v) { return String(roundToInt(v)) + " ms" + kDot + speedWord(v); }, 30.f, "", {"Fast", "Slow"}),
          target(p, ids::target, "Target", [](double v) { return fmtDb(float(v)) + " dB RMS"; }, 44.f, "data"),
          range(p, ids::range, "Range", [](double v) { return kPlusMinus + fmtDb(float(v)) + " dB"; }, 30.f, ""),
          ceiling(p, ids::ceiling, "Ceiling", [](double v) { return fmtDb(float(v)) + " dBFS"; }, 30.f, "ceil"),
          rideHold(gc, {"Ride", "Hold"}), ab(gc, {"A", "B"}), more(p, gc), tip(gc), drawer(p, gc), about(*checker, gc), prompt(*checker, gc) {
        gc.root = this;
        look.gc = &gc;
        look.apply();
        setLookAndFeel(&look);
        setWantsKeyboardFocus(true);

        addAndMakeVisible(scene);
        addAndMakeVisible(meterIn); addAndMakeVisible(meterOut);
        addAndMakeVisible(readouts);
        for (auto* k : {&threshold, &response, &target, &range, &ceiling}) addAndMakeVisible(*k);
        addAndMakeVisible(rideHold);
        rideHold.setTitle("Ride or Hold");
        rideHold.onSelect = [this](int i) { proc.undo.beginNewTransaction(); setParam(ids::mode, float(i)); };
        moreBtn.getProperties().set("trailing", "chevron-down");
        moreBtn.getProperties().set("toggle", true);
        moreBtn.getProperties().set("fontSize", 12.5);
        moreBtn.setTitle("Rise, fall and output trim");
        moreBtn.onClick = [this] { more.setVisible(!more.isVisible()); if (more.isVisible()) { more.refresh(); more.toFront(false); } };
        addAndMakeVisible(moreBtn);
        addChildComponent(more);
        addAndMakeVisible(tags);
        addAndMakeVisible(header);

        // header controls
        presetBtn.getProperties().set("trailing", "chevron-down");
        presetBtn.setTitle("Presets");
        presetBtn.onClick = [this] { showPresets(); };
        addAndMakeVisible(presetBtn);
        ab.fontSize = 13.f;
        ab.setTitle("Settings A or B");
        ab.onSelect = [this](int i) { proc.switchSlot(i); };
        addAndMakeVisible(ab);
        for (auto* b : {&undoBtn, &redoBtn}) { b->getProperties().set("kind", "circle"); b->setButtonText({}); addAndMakeVisible(*b); }
        undoBtn.getProperties().set("icon", "undo"); undoBtn.setTitle("Undo"); undoBtn.setTooltip("Undo (Ctrl+Z)");
        redoBtn.getProperties().set("icon", "redo"); redoBtn.setTitle("Redo"); redoBtn.setTooltip("Redo (Ctrl+Shift+Z)");
        undoBtn.onClick = [this] { proc.undo.undo(); };
        redoBtn.onClick = [this] { proc.undo.redo(); };
        bypassBtn.getProperties().set("toggle", true); bypassBtn.getProperties().set("danger", true);
        bypassBtn.setTitle("Bypass"); bypassBtn.setTooltip("Bypass (keeps the same latency, so nothing jumps)");
        bypassBtn.setClickingTogglesState(true);
        bypassBtn.onPress = [this] { proc.undo.beginNewTransaction(); };
        bypassAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::bypass, bypassBtn);
        addAndMakeVisible(bypassBtn);
        easeBtn.setClickingTogglesState(true);
        easeBtn.getProperties().set("toggle", true);
        easeBtn.setTitle("Return to 0 dB in silence");
        easeBtn.setTooltip("Off: the gain freezes below the threshold. On: after 2 s of silence it glides back to 0 dB.");
        easeBtn.onPress = [this] { proc.undo.beginNewTransaction(); };
        easeAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::ease, easeBtn);
        addAndMakeVisible(easeBtn);
        aboutBtn.getProperties().set("kind", "plain");
        aboutBtn.setTitle("About Corridor Leveler and updates");
        aboutBtn.setTooltip("Version " + updates::currentVersion() + " and updates");
        aboutBtn.setColour(TextButton::textColourOffId, Colours::transparentBlack);
        aboutBtn.onClick = [this] { about.refresh(); about.setVisible(!about.isVisible()); about.toFront(false); };
        addAndMakeVisible(aboutBtn);
        updateBtn.getProperties().set("icon", "download");
        updateBtn.getProperties().set("fontSize", 12.5);
        updateBtn.onClick = [this] { if (checker->known().valid()) checker->install(); };
        addChildComponent(updateBtn);

        // the one-time tip
        tip.setVisible(!p.tipDismissedThisRun && !tipSeen());
        tip.onDismiss = [this] { tip.setVisible(false); proc.tipDismissedThisRun = true; markTipSeen(); };
        addChildComponent(tip);

        addChildComponent(drawer);
        addChildComponent(about);
        addChildComponent(prompt);
        checker->addChangeListener(this);
        checker->check(false);  // silent; at most one prompt per host session
        setSize(W, H);
        sendLookAndFeelChange();
        addMouseListener(this, true);  // a click anywhere lets Ctrl+Z and A reach the window
    }
    void mouseDown(const MouseEvent&) override {
        if (auto* f = Component::getCurrentlyFocusedComponent(); f == nullptr || !isParentOf(f)) grabKeyboardFocus();
    }
    ~CorridorContent() override { checker->removeChangeListener(this); setLookAndFeel(nullptr); }

    std::function<void(bool)> onDrawerChange;

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

    void setParam(const char* id, float v) {
        if (auto* prm = proc.state().getParameter(id)) { prm->beginChangeGesture(); prm->setValueNotifyingHost(prm->convertTo0to1(v)); prm->endChangeGesture(); }
    }

    void showPresets() {
        PopupMenu m;
        m.addSectionHeader("Presets");
        const auto& list = CorridorAudioProcessor::presets();
        for (int i = 0; i < int(list.size()); ++i) m.addItem(i + 1, list[size_t(i)].name, true, proc.presetName == list[size_t(i)].name);
        m.showMenuAsync(PopupMenu::Options().withTargetComponent(&presetBtn).withMinimumWidth(210),
                        [this](int r) { if (r > 0) proc.applyPreset(r - 1); });
    }

    bool keyPressed(const KeyPress& k) override {
        const auto mods = k.getModifiers();
        const int code = k.getKeyCode();
        if (mods.isCommandDown() && (code == 'Z' || code == 'z')) { mods.isShiftDown() ? proc.undo.redo() : proc.undo.undo(); return true; }
        if (mods.isCommandDown() && (code == 'Y' || code == 'y')) { proc.undo.redo(); return true; }
        if (!mods.isAnyModifierKeyDown() && (code == 'A' || code == 'a') && drawer.isVisible()) { drawer.analyze.triggerClick(); return true; }
        return false;
    }

    void resized() override {
        scene.setBounds(0, 0, W, H);
        meterIn.setBounds(12, int(kMeterY), int(kMeterW), int(kMeterH));
        meterOut.setBounds(W - 12 - int(kMeterW), int(kMeterY), int(kMeterW), int(kMeterH));
        readouts.setBounds(0, 0, W, H);
        tags.setBounds(0, 0, W, H);
        header.setBounds(kHeader.toNearestInt());
        // header, left to right: wordmark (About), preset, A / B, undo, redo ... status ... return to 0 dB, bypass
        aboutBtn.setBounds(18, 12, 160, 42);
        presetBtn.setBounds(200, 18, 156, 30);
        ab.setBounds(364, 18, 72, 30);
        undoBtn.setBounds(444, 18, 30, 30);
        redoBtn.setBounds(478, 18, 30, 30);
        bypassBtn.setBounds(W - 24 - 108, 18, 108, 30);
        easeBtn.setBounds(bypassBtn.getX() - 8 - 208, 18, 208, 30);
        int statusRight = easeBtn.getX() - 12;
        if (updateBtn.isVisible()) { updateBtn.setBounds(easeBtn.getX() - 8 - 136, 18, 136, 30); statusRight = updateBtn.getX() - 8; }
        header.statusArea = Rectangle<float>(520.f - kHeader.getX(), 0.f, float(statusRight - 520), 42.f);
        // controls: Threshold and Response left, Target in the middle with Ride / Hold under it, Range and Ceiling right
        threshold.placeAt({290, 496});
        response.placeAt({400, 496});
        target.placeAt({560, 478});
        range.placeAt({720, 496});
        ceiling.placeAt({830, 496});
        rideHold.setBounds(560 - 76, 578, 152, 30);
        moreBtn.setBounds(int(kDeck.getX()) + 116, 578, 128, 30);
        more.setBounds(236, 572 - 168, 300, 168);
        tip.setBounds(W / 2 - 235, 70, 470, 76);
        about.setBounds(W - 12 - 420, 62, 420, 190);
        prompt.setBounds(0, 0, W, H);
        drawer.setBounds(0, H, W, CorridorEditor::drawerH);
    }

    void tick() {
        auto& dsp = proc.core();
        const auto& t = dsp.telemetry();
        scene.histWritten = dsp.historyWritten();
        const int n = dsp.copyHistory(scratch, kHist);
        scene.hist.assign(scratch, scratch + n);
        scene.analyzing = t.analyzing.load();
        scene.analyzeSeconds = t.analyzeSeconds.load();
        scene.renderBlur();

        const bool byp = proc.state().getRawParameterValue(ids::bypass)->load() > 0.5f;
        bypassBtn.setButtonText(byp ? "Bypassed" : "Bypass");
        meterIn.peakL = t.inPeakL.load(); meterIn.peakR = t.inPeakR.load(); meterIn.hold = t.inPeakHold.load(); meterIn.balance = t.inBalance.load();
        meterOut.peakL = byp ? t.inPeakL.load() : t.outPeakL.load(); meterOut.peakR = byp ? t.inPeakR.load() : t.outPeakR.load();
        meterOut.hold = byp ? t.inPeakHold.load() : t.outPeakHold.load(); meterOut.balance = t.outBalance.load();
        target.setLive(t.outRms.load());
        threshold.setLive(t.inRms.load());

        const bool holdMode = proc.state().getRawParameterValue(ids::mode)->load() > 0.5f;
        rideHold.setSelected(holdMode ? 1 : 0);
        rideHold.advance();
        ab.setSelected(proc.slot);
        ab.advance();
        presetBtn.setButtonText(proc.presetName + (proc.presetEdited() ? " (edited)" : ""));
        undoBtn.setEnabled(proc.undo.canUndo());
        redoBtn.setEnabled(proc.undo.canRedo());
        moreBtn.setToggleState(more.isVisible(), dontSendNotification);
        moreBtn.getProperties().set("trailing", more.isVisible() ? "chevron-up" : "chevron-down");
        if (more.isVisible()) more.refresh();

        if (holdMode != drawer.isVisible()) {
            drawer.setVisible(holdMode);
            if (onDrawerChange) onDrawerChange(holdMode);
        }
        if (holdMode) {
            const int w = jmin(n, kWaveEntries);
            drawer.wave.assign(scratch + (n - w), scratch + n);
            drawer.refresh();
        }

        // readouts
        const float g = byp ? 0.f : t.gain.load();
        readouts.gainText = fmtDb(g, 1, true);
        const float tgt = proc.state().getRawParameterValue(ids::target)->load();
        const float out = t.outRms.load(), thr = proc.state().getRawParameterValue(ids::threshold)->load();
        const bool active = !holdMode && !byp && t.inRms.load() >= thr;
        const float d = out - tgt;
        readouts.onTarget = active && std::abs(d) < 1.f;
        readouts.fromTarget = holdMode ? "Holding" : !active ? "Waiting" : std::abs(d) < 1.f ? "On target"
                                                                         : fmtDb(std::abs(d)) + (d > 0 ? " dB over" : " dB under");
        const double fs = dsp.sampleRate();
        readouts.latency = "Latency " + String(1000.0 * dsp.latencySamples() / fs, 1) + " ms (" + String(dsp.latencySamples()) + " samples at " + String(fs / 1000.0, 1) + " kHz)";

        // line labels while their line or dial is in use
        tags.show[0] = scene.hover == ids::ceiling || scene.dragId == ids::ceiling || ceiling.inUse();
        tags.show[1] = scene.hover == ids::threshold || scene.dragId == ids::threshold || threshold.inUse();
        tags.show[2] = scene.hover == ids::target || scene.dragId == ids::target || target.inUse();

        header.status = shortStatus(byp, holdMode);
        header.dot = stateColour(byp, holdMode);
        const String longText = sentence(byp, holdMode);
        header.setTitle("Status");
        header.setDescription(longText);  // screen readers hear the full sentence
        header.setTooltip(longText);

        repaint();
    }

    Colour stateColour(bool byp, bool holdMode) const {
        const Pal& c = *gc.pal;
        if (byp) return c.muted;
        if (holdMode) return c.ink;
        switch (corridor::State(proc.core().telemetry().state.load())) {
            case corridor::State::OnTarget: return c.data;
            case corridor::State::Boosting: return c.boost;
            case corridor::State::Cutting: return c.cut;
            case corridor::State::Ceiling: return c.ceil;
            case corridor::State::Range: return c.label;
            case corridor::State::Returning: return c.label;
            case corridor::State::Waiting:
            case corridor::State::Held:
            case corridor::State::Analyzing:
            case corridor::State::Unset:
            default: return c.muted;
        }
    }

    String shortStatus(bool byp, bool holdMode) const {
        const auto& t = proc.core().telemetry();
        const float g = t.gain.load();
        if (byp) return "Bypassed";
        if (holdMode) {
            if (t.analyzing.load()) return "Analyzing " + String(t.analyzeSeconds.load(), 1) + " s" + kDot + "press Lock";
            if (t.analyzeFailed.load()) return "Not enough sound. Analyze again";
            if (t.holdValid.load()) return "Holding " + fmtDb(t.holdGain.load(), 1, true) + " dB";
            return "Hold: press Analyze, then play";
        }
        switch (corridor::State(t.state.load())) {
            case corridor::State::OnTarget: return "On target";
            case corridor::State::Boosting: return "Boosting (" + fmtDb(g, 1, true) + " dB)";
            case corridor::State::Cutting: return "Cutting (" + fmtDb(g, 1, true) + " dB)";
            case corridor::State::Ceiling: return "Held back by the ceiling";
            case corridor::State::Range: return "At the edge of the range";
            case corridor::State::Returning: return "Returning to 0 dB";
            case corridor::State::Waiting:
            case corridor::State::Held:
            case corridor::State::Analyzing:
            case corridor::State::Unset:
            default: {
                const float thr = proc.state().getRawParameterValue(ids::threshold)->load();
                return t.inRms.load() < thr ? "Below the threshold, not adjusting" : "Measuring";
            }
        }
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
            case corridor::State::Range: return "It needs more than the " + kPlusMinus + String(rng, 1) + " dB range allows. Held at " + fmtDb(g, 1, true) + " dB.";
            case corridor::State::Returning: return "Silence for over 2 s. Returning to 0 dB (" + fmtDb(g, 1, true) + " dB).";
            case corridor::State::Waiting:
            case corridor::State::Held:
            case corridor::State::Analyzing:
            case corridor::State::Unset:
            default:
                return t.inRms.load() < thr ? "Below the " + fmtDb(thr, 1) + " dB threshold (silence, breaths or noise). Not adjusting; gain stays at " + fmtDb(g, 1, true) + " dB."
                                            : "Sound just started. Measuring it before acting.";
        }
    }

private:
    CorridorAudioProcessor& proc;
    GlassContext gc;
    Look look;
    Scene scene;
    Meter meterIn, meterOut;
    Readouts readouts;
    Tags tags;
    Header header;
    Knob threshold, response, target, range, ceiling;
    Segmented rideHold, ab;
    GlassButton moreBtn{"Rise, fall, trim"};
    MorePanel more;
    GlassButton presetBtn{"Default"}, undoBtn{"Undo"}, redoBtn{"Redo"}, bypassBtn{"Bypass"};
    GlassButton easeBtn{"Return to 0 dB in silence"}, aboutBtn{""}, updateBtn{"Update"};
    Tip tip;
    Drawer drawer;
    TooltipWindow tooltips{this, 600};
    SharedResourcePointer<updates::Checker> checker;  // declared before the panels that use it
    AboutPanel about;
    UpdatePrompt prompt;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> easeAtt, bypassAtt;
    corridor::Snapshot scratch[kHist];
};

// ---------------------------------------------------------------- editor
CorridorEditor::CorridorEditor(CorridorAudioProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    const int savedWidth = p.editorWidth;  // read first: setting limits triggers resized()
    content = std::make_unique<CorridorContent>(p);
    content->onDrawerChange = [this](bool open) { applyDrawer(open); };
    addAndMakeVisible(*content);
    setResizable(true, true);
    setWantsKeyboardFocus(false);
    width0 = jlimit(minW, maxW, savedWidth);
    drawerOpen = p.state().getRawParameterValue(ids::mode)->load() > 0.5f;
    applyDrawer(drawerOpen);
    content->tick();  // fill the window before it is first shown
    startTimerHz(30);
}

CorridorEditor::~CorridorEditor() { stopTimer(); }

int CorridorEditor::contentHeight() const { return designH + (drawerOpen ? drawerH : 0); }

// Hold mode opens the analysis sheet under the window: the window grows by its height and keeps its width.
void CorridorEditor::applyDrawer(bool open) {
    drawerOpen = open;
    const double ratio = double(designW) / double(contentHeight());
    const int w = getWidth() > 0 ? getWidth() : width0;
    setResizeLimits(minW, int(std::round(minW / ratio)), maxW, int(std::round(maxW / ratio)));  // the whole layout scales; at 720 px it is drawn at 64%
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(ratio);
    setSize(w, int(std::round(w / ratio)));
    resized();
}

void CorridorEditor::paint(Graphics& g) { g.fillAll(kDark.bg); }

void CorridorEditor::resized() {
    const float s = float(getWidth()) / float(designW);
    content->setBounds(0, 0, designW, contentHeight());
    content->setTransform(AffineTransform::scale(s));
    if (getWidth() > 0) proc.editorWidth = getWidth();
}

void CorridorEditor::timerCallback() { content->tick(); }
