// Corridor Leveler - plugin window.
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A monitoring screen in flat, opaque surfaces. Resizing the window never scales the text: the graph and the
// dials take the extra room, and the Output and Mode panels keep their width.
//
//   top bar    wordmark (About), preset, A / B, undo, redo, the live status sentence, theme, bypass
//   level      the input and output level over the last 10 s, with the Target, Threshold and Ceiling
//              lines (drag them), the two meters on the same scale, and under it the gain lane:
//              how much Corridor boosted or cut, moment by moment
//   dials      Threshold, Response, Target, Range, Ceiling
//   readouts   the output level (the screen's answer), how far it is from the target, gain, input,
//              peaks, balance and latency
//   mode       Ride / Hold, rise, fall, output trim, return to 0 dB in silence
//
// Colour goes on data only: the output in the accent, the reference lines in their own colours, the
// meter zones. Every selection (A / B, Ride / Hold, RMS / Peak) is one 1px accent line as wide as its label.
// Text is Inter with its tabular figures on by default, so live numbers hold still while they update.
// In Hold mode an analysis panel (166 px) opens under the window.
#include "PluginEditor.h"

#include "BinaryData.h"
#include "Updates.h"

#include <cmath>
#include <vector>

using namespace juce;

namespace {

constexpr float kPi = MathConstants<float>::pi;
constexpr int kHist = 500;          // 10 s at 20 ms per entry
constexpr int kWaveEntries = 300;   // 6 s in the analysis panel

// ---------------------------------------------------------------- geometry (one 16 px gutter everywhere)
// Worked out from the window's real size. At the default 1120 x 630:
//   columns: 16 + 760 + 16 + 312 + 16 = 1120  (the left column takes any extra width)
//   rows:    top bar 52, then 12 + 384 + 16 + 150 + 16 = 630 on the left (the level panel takes any extra
//            height); 12 + 240 + 16 + 278 + 16 on the right (the mode panel takes it)
// Below 1000 x 630 the window is compact: the level panel and the dials run the full width, the output
// readouts become one line at the top of the level panel, and the mode panel opens from the top bar.
constexpr float kTopBar = 52.f;
constexpr float kRailW = 312.f, kDialH = 150.f, kReadH = 240.f;
constexpr int kFullW = 1000, kFullH = 630;  // the full layout from here up; compact below
constexpr float kDbRange = 80.f;
constexpr float kBarW = 6.f, kBarGap = 2.f;
struct Geo {
    float w = 1120.f, h = 630.f;
    bool compact = false;
    Rectangle<float> level, dial, read, mode, strip;  // strip: the compact readout line in the level panel
    float px0 = 0, px1 = 0;   // plot, left to right (the meters sit either side)
    float gt = 0, gb = 0;     // level plot, 0 to -80 dB
    float lt = 0, lb = 0;     // gain lane
    float inBarX = 0, outBarX = 0;
    void layout(float width, float height) {
        w = width; h = height;
        compact = w < float(kFullW) || h < float(kFullH);
        const float left = compact ? w - 32.f : w - 48.f - kRailW;
        level = {16.f, 64.f, left, h - 64.f - 16.f - kDialH - 16.f};
        dial = {16.f, h - 16.f - kDialH, left, kDialH};
        if (compact) {
            read = {}; strip = {level.getX() + 16.f, level.getY() + 8.f, level.getWidth() - 32.f, 48.f};
            mode = {w - 16.f - kRailW, kTopBar + 6.f, kRailW, 294.f};  // a panel that opens over the window
        } else {
            strip = {};
            read = {left + 32.f, 64.f, kRailW, kReadH};
            mode = {left + 32.f, 64.f + kReadH + 16.f, kRailW, h - (64.f + kReadH + 16.f) - 16.f};
        }
        inBarX = level.getX() + 16.f;
        outBarX = level.getRight() - 16.f - (kBarW * 2.f + kBarGap);
        px0 = level.getX() + 72.f;
        px1 = level.getRight() - 76.f;
        lb = level.getBottom() - 16.f; lt = lb - 60.f;
        gt = level.getY() + (compact ? 80.f : 60.f); gb = lt - 16.f;
    }
    float yOf(float v) const { return gt + jlimit(0.f, kDbRange, -v) / kDbRange * (gb - gt); }
    float vOf(float y) const { return -jlimit(0.f, kDbRange, (y - gt) / (gb - gt) * kDbRange); }
};

// ---------------------------------------------------------------- palette (the window is always dark)
struct Pal {
    bool dark;
    Colour ground, surface, raised, hover, rule, control;
    Colour text, secondary, muted;
    Colour accent, positive, negative, warning;
    Colour inLine, grid, track, shade;
};
// Analyzer style: a flat charcoal ground, cards a shade darker, grey text and one gold accent (the output,
// the Target, every value arc and anything switched on). Olive is the threshold and the safe meter zone,
// yellow the meter's middle zone, red the ceiling and clipping. Text greys pass 4.5:1 on the cards.
const Pal kDark{true,
                Colour(0xff1e1e22), Colour(0xff19191c), Colour(0xff2a2a2f), Colour(0xff34343a), Colour(0xff2c2c31), Colour(0xff45454c),
                Colour(0xffe8e8ec), Colour(0xffb8b8c0), Colour(0xff8e8e98),
                Colour(0xffffc21a), Colour(0xff9cc23a), Colour(0xffff4d4d), Colour(0xffe6c229),
                Colour(0xff7c7c84), Colour(0xff27272c), Colour(0xff0f0f11), Colour(0x00000000)};
const Colour kKnobRim(0xff5a5a62), kFrame(0xff4a4a52);

// ---------------------------------------------------------------- type: Inter Tabular, Regular and SemiBold
// Mostly regular weight; SemiBold for names, titles, the chosen option and the answer figure.
struct FontSet {
    Typeface::Ptr sans, sansSemi;
    FontSet() {
        sans = Typeface::createSystemTypefaceFor(CorridorFonts::InterTabularRegular_ttf, size_t(CorridorFonts::InterTabularRegular_ttfSize));
        sansSemi = Typeface::createSystemTypefaceFor(CorridorFonts::InterTabularSemiBold_ttf, size_t(CorridorFonts::InterTabularSemiBold_ttfSize));
    }
};
// The window holds one FontSet for its lifetime; this borrows it (or makes it, outside a window).
FontSet& fonts() { SharedResourcePointer<FontSet> p; return *p; }
Font face(Typeface::Ptr t, float px) { return t != nullptr ? Font(FontOptions(t).withPointHeight(px)) : Font(FontOptions().withPointHeight(px)); }
Font text(float px, bool semi = false) { return face(semi ? fonts().sansSemi : fonts().sans, px); }
Font num(float px, bool semi = false) { return face(semi ? fonts().sansSemi : fonts().sans, px); }  // figures: the same face, tabular
float widthOf(const Font& f, const String& s) { return GlyphArrangement::getStringWidth(f, s); }

// Splits "400 ms" into the figure "400" and the unit " ms", so the unit can be set smaller and in grey.
std::pair<String, String> splitFigure(const String& s) {
    const String numChars = String(CharPointer_UTF8("0123456789.+\xe2\x88\x92\xc2\xb1-"));
    int i = 0;
    while (i < s.length() && numChars.containsChar(s[i])) ++i;
    return {s.substring(0, i), s.substring(i)};
}
// draws a figure (mono, ink) followed by its unit (text face, grey), aligned as one string
void drawFigure(Graphics& g, const String& s, Rectangle<float> r, Justification j, float px, Colour ink, Colour unitCol) {
    const auto [fig, unit] = splitFigure(s);
    const Font ff = num(px), uf = text(px - 1.f);
    const float fw = widthOf(ff, fig), uw = widthOf(uf, unit), total = fw + uw;
    float x = j.testFlags(Justification::horizontallyCentred) ? r.getCentreX() - total * 0.5f
            : j.testFlags(Justification::right) ? r.getRight() - total : r.getX();
    g.setFont(ff); g.setColour(ink);
    g.drawText(fig, Rectangle<float>(x, r.getY(), fw + 2.f, r.getHeight()), Justification::centredLeft);
    g.setFont(uf); g.setColour(unitCol);
    g.drawText(unit, Rectangle<float>(x + fw, r.getY(), uw + 2.f, r.getHeight()), Justification::centredLeft);
}

String fmtDb(float v, int dec = 1, bool sign = false) {
    String s = String(std::abs(v), dec);
    const bool zero = s == String(0.0, dec);
    if (v < 0 && !zero) return String(CharPointer_UTF8("\xe2\x88\x92")) + s;  // a real minus sign
    if (sign && v > 0 && !zero) return "+" + s;
    return s;
}
const String kPlusMinus = String(CharPointer_UTF8("\xc2\xb1"));
const String kDot = String(CharPointer_UTF8(" \xc2\xb7 "));
const String kDash = String(CharPointer_UTF8("\xe2\x80\x94"));  // a missing value
const String kMinusInf = String(CharPointer_UTF8("\xe2\x88\x92\xe2\x88\x9e"));  // a silent peak meter
constexpr float kSilent = -90.f;                                // below this a level reads as no signal

bool peakMode(CorridorAudioProcessor& p) { return p.state().getRawParameterValue(ids::detect)->load() > 0.5f; }
String levelUnit(CorridorAudioProcessor& p) { return peakMode(p) ? " dBFS peak" : " dB RMS"; }
String speedWord(double ms) { return ms < 50 ? "Very fast" : ms < 200 ? "Fast" : ms < 900 ? "Medium" : "Slow"; }
String balanceText(float b) { return std::abs(b) < 0.05f ? String("0.0") : String(std::abs(b), 1) + (b > 0 ? " L" : " R"); }

void card(Graphics& g, const Pal& c, Rectangle<float> r) {
    g.setColour(c.surface); g.fillRoundedRectangle(r, 6.f);
    if (!c.dark) { g.setColour(c.rule); g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f); }
}
// a floating panel (menu, popover, dialog): flat, with a 1 px edge
void floating(Graphics& g, const Pal& c, Rectangle<float> r) {
    Path p; p.addRoundedRectangle(r, 6.f);
    g.setColour(c.dark ? c.raised : c.surface); g.fillPath(p);
    g.setColour(c.dark ? c.control.withAlpha(0.6f) : c.rule); g.strokePath(p, PathStrokeType(1.f));
}
// a reference line's mark, scaled down, so a dial's swatch is the line it moves
void lineMark(Graphics& g, Line<float> l, Colour col, bool dashed, float w) {
    g.setColour(col);
    if (!dashed) { g.drawLine(l, w); return; }
    const float dashes[] = {4.f, 3.f};
    g.drawDashedLine(l, dashes, 2, w);
}

// ---------------------------------------------------------------- icons (outline, 1.5 px)
void drawIcon(Graphics& g, const String& name, Rectangle<float> a, Colour col, bool dark = true) {
    const float s = jmin(a.getWidth(), a.getHeight());
    const auto b = a.withSizeKeepingCentre(s, s);
    const float cx = b.getCentreX(), cy = b.getCentreY(), x = b.getX(), y = b.getY();
    const PathStrokeType st(1.5f, PathStrokeType::curved, PathStrokeType::rounded);
    g.setColour(col);
    Path p;
    if (name == "power") {
        p.addCentredArc(cx, cy + s * 0.05f, s * 0.36f, s * 0.36f, 0.f, 0.7f, 2.f * kPi - 0.7f, true);
        p.startNewSubPath(cx, y + s * 0.06f); p.lineTo(cx, cy);
        g.strokePath(p, st);
    } else if (name == "theme") {
        if (dark) {  // moon
            Path m; m.addEllipse(cx - s * 0.36f, cy - s * 0.36f, s * 0.72f, s * 0.72f);
            m.addEllipse(cx - s * 0.08f, cy - s * 0.58f, s * 0.68f, s * 0.68f);
            m.setUsingNonZeroWinding(false);
            Graphics::ScopedSaveState ss(g);
            Path clip; clip.addEllipse(cx - s * 0.36f, cy - s * 0.36f, s * 0.72f, s * 0.72f);
            g.reduceClipRegion(clip);
            g.fillPath(m);
        } else {     // sun
            p.addEllipse(cx - s * 0.18f, cy - s * 0.18f, s * 0.36f, s * 0.36f);
            for (int k = 0; k < 8; ++k) {
                const float an = float(k) * kPi / 4.f;
                p.startNewSubPath(cx + std::cos(an) * s * 0.32f, cy + std::sin(an) * s * 0.32f);
                p.lineTo(cx + std::cos(an) * s * 0.46f, cy + std::sin(an) * s * 0.46f);
            }
            g.strokePath(p, st);
        }
    } else if (name == "undo" || name == "redo") {  // a hooked arrow: undo points back, redo forward
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
    }
}

// The mark: a level held between two walls, the corridor. The walls in ink, the level in the accent.
void drawMark(Graphics& g, Rectangle<float> a, const Pal& c) {
    const float s = jmin(a.getWidth(), a.getHeight());
    const auto b = a.withSizeKeepingCentre(s, s);
    const float x = b.getX(), y = b.getY(), cy = b.getCentreY();
    g.setColour(c.text);
    g.fillRect(x, y + s * 0.06f, s, s * 0.12f);
    g.fillRect(x, y + s * 0.82f, s, s * 0.12f);
    Path p;
    for (int k = 0; k <= 30; ++k) {
        const float t = float(k) / 30.f, env = 1.f - t * 0.72f;
        const float px = x + s * t, py = cy - std::sin(t * 4.f * kPi) * s * 0.2f * env;
        if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
    }
    g.setColour(c.accent);
    g.strokePath(p, PathStrokeType(jmax(1.5f, s * 0.09f), PathStrokeType::curved, PathStrokeType::rounded));
}

// ---------------------------------------------------------------- look and feel
class Look : public LookAndFeel_V4 {
public:
    const Pal* pal = &kDark;
    const Pal& p() const { return *pal; }

    // every font JUCE makes for us (tooltips, menus, editors) uses the interface typeface too
    Typeface::Ptr getTypefaceForFont(const Font& f) override { return f.isBold() ? fonts().sansSemi : fonts().sans; }

    void apply() {
        const Pal& c = p();
        setColour(Label::textColourId, c.text);
        setColour(Label::textWhenEditingColourId, c.text);
        setColour(Label::backgroundWhenEditingColourId, c.raised);
        setColour(Label::outlineWhenEditingColourId, c.accent);
        setColour(TextEditor::textColourId, c.text);
        setColour(TextEditor::backgroundColourId, c.raised);
        setColour(TextEditor::outlineColourId, c.control);
        setColour(TextEditor::highlightColourId, c.accent.withAlpha(0.35f));
        setColour(TextEditor::focusedOutlineColourId, c.accent);
        setColour(CaretComponent::caretColourId, c.text);
        setColour(TooltipWindow::backgroundColourId, c.dark ? c.raised : c.surface);
        setColour(TooltipWindow::textColourId, c.text);
        setColour(TooltipWindow::outlineColourId, c.control);
        setColour(PopupMenu::backgroundColourId, c.dark ? c.raised : c.surface);
        setColour(PopupMenu::textColourId, c.text);
        setColour(PopupMenu::highlightedBackgroundColourId, c.hover);
        setColour(PopupMenu::highlightedTextColourId, c.text);
        setColour(PopupMenu::headerTextColourId, c.muted);
        setColour(HyperlinkButton::textColourId, c.text);
    }

    // the colour and the dash of the line a dial moves
    Colour roleColour(const String& role) const {
        const Pal& c = p();
        return role == "data" ? c.accent : role == "thr" ? c.positive : role == "ceil" ? c.negative : c.muted;
    }

    // A dial in the analyzer style: a dark matte disc edged by a thin light ring and a dark outline. The
    // value is a gold arc just inside the rim (no track behind it) and a gold dot near the edge is the
    // pointer. Threshold and Target add a thin outer arc for the live level: olive for the input on
    // Threshold, grey for the output on Target.
    void drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s) override {
        const Pal& c = p();
        const float cx = float(x) + float(w) * 0.5f, cy = float(y) + float(h) * 0.5f;
        const float r = float(jmin(w, h)) * 0.5f - 9.f;  // the disc
        const float ang = a0 + pos * (a1 - a0);
        const String role = s.getProperties().getWithDefault("role", "");
        const bool hot = s.isMouseOverOrDragging();
        auto arc = [&](float rad, float from, float to, float thick, Colour col) {
            Path a; a.addCentredArc(cx, cy, rad, rad, 0.f, from, to, true);
            g.setColour(col); g.strokePath(a, PathStrokeType(thick, PathStrokeType::curved, PathStrokeType::butt));
        };
        const var live = s.getProperties()["live"];
        if (!live.isVoid()) {
            const double lv = double(live);
            if (lv > s.getMinimum()) {
                const float la = a0 + float(s.valueToProportionOfLength(jmin(lv, s.getMaximum()))) * (a1 - a0);
                arc(r + 5.5f, a0, la, 2.f, role == "thr" ? c.positive : c.secondary);
            }
        }
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillEllipse(cx - r - 2.f, cy - r - 2.f, (r + 2.f) * 2.f, (r + 2.f) * 2.f);
        g.setColour(hot ? c.hover : c.raised);
        g.fillEllipse(cx - r, cy - r, r * 2.f, r * 2.f);
        g.setColour(hot ? c.secondary : kKnobRim);
        g.drawEllipse(cx - r + 0.75f, cy - r + 0.75f, (r - 0.75f) * 2.f, (r - 0.75f) * 2.f, 1.5f);
        if (pos > 0.003f) arc(r - 4.f, a0, ang, 3.f, c.accent);
        const float pr = r - 10.f, d = jmax(5.f, r * 0.15f);
        g.setColour(c.accent);
        g.fillEllipse(cx + std::sin(ang) * pr - d * 0.5f, cy - std::cos(ang) * pr - d * 0.5f, d, d);
        if (s.hasKeyboardFocus(false)) { g.setColour(c.accent); g.drawEllipse(cx - r - 8.f, cy - r - 8.f, (r + 8.f) * 2.f, (r + 8.f) * 2.f, 1.5f); }
    }

    // the thin sliders for rise, fall and trim
    void drawLinearSlider(Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s) override {
        const Pal& c = p();
        const float cy = std::round(float(y) + float(h) * 0.5f), x0 = float(x) + 6.f, x1 = float(x + w) - 6.f;
        const float px = jlimit(x0, x1, pos);
        g.setColour(c.track); g.fillRect(x0, cy - 1.f, x1 - x0, 2.f);
        g.setColour(c.accent); g.fillRect(x0, cy - 1.f, px - x0, 2.f);
        const float tr = s.isMouseOverOrDragging() ? 7.f : 6.f;
        g.setColour(c.raised); g.fillEllipse(px - tr, cy - tr, tr * 2.f, tr * 2.f);
        g.setColour(s.isMouseOverOrDragging() ? c.secondary : kKnobRim); g.drawEllipse(px - tr + 0.75f, cy - tr + 0.75f, (tr - 0.75f) * 2.f, (tr - 0.75f) * 2.f, 1.5f);
        if (s.hasKeyboardFocus(false)) { g.setColour(c.accent); g.drawEllipse(px - 10.f, cy - 10.f, 20.f, 20.f, 1.5f); }
    }

    // Buttons, by property "kind":
    //   control - a pill with a 1px edge (preset, Update); a toggle fills with ink when on, red for Bypass
    //   icon    - an icon alone; a quiet disc on hover (theme, bypass, undo, redo, close)
    //   primary - the one filled button on a panel: inverted ink (Analyze, Update in the prompt, Got it)
    //   switch  - a small toggle that fills with ink when on (return to 0 dB in silence)
    //   plain   - text only
    void drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down) override {
        const Pal& c = p();
        const String kind = b.getProperties().getWithDefault("kind", "control");
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = b.getToggleState();
        if (kind == "switch") {
            const auto t = Rectangle<float>(r.getRight() - 32.f, r.getCentreY() - 9.f, 32.f, 18.f);
            g.setColour(on ? c.accent : c.track); g.fillRoundedRectangle(t, 9.f);
            const float kx = on ? t.getRight() - 16.f : t.getX() + 2.f;
            g.setColour(on ? c.surface : c.secondary);
            g.fillEllipse(kx, t.getY() + 2.f, 14.f, 14.f);
            if (b.hasKeyboardFocus(false)) { g.setColour(c.accent); g.drawRoundedRectangle(t.expanded(3.f), 12.f, 1.5f); }
            return;
        }
        if (kind == "icon") {
            const float d = jmin(r.getWidth(), r.getHeight());
            const auto disc = r.withSizeKeepingCentre(d, d);
            const bool alarm = bool(b.getProperties().getWithDefault("danger", false)) && on;
            if (alarm) { g.setColour(c.negative.withAlpha(c.dark ? 0.18f : 0.12f)); g.fillEllipse(disc); }
            else if (over || down) { g.setColour(c.hover); g.fillEllipse(disc); }
            if (b.hasKeyboardFocus(false)) { g.setColour(c.accent); g.drawEllipse(disc.expanded(1.f), 1.5f); }
            return;
        }
        const float rad = 4.f;
        if (kind == "primary") {
            g.setColour(!b.isEnabled() ? c.track : over || down ? c.accent.brighter(0.15f) : c.accent);
            g.fillRoundedRectangle(r, rad);
        } else if (kind == "control") {
            const bool danger = b.getProperties().getWithDefault("danger", false);
            if (on && b.getClickingTogglesState() && danger) { g.setColour(c.negative); g.fillRoundedRectangle(r, rad); }
            else {
                g.setColour(over || down ? c.hover : c.raised); g.fillRoundedRectangle(r, rad);
                g.setColour(c.control); g.drawRoundedRectangle(r, rad, 1.f);
            }
        } else if (over) {
            g.setColour(c.hover); g.fillRoundedRectangle(r, rad);
        }
        if (b.hasKeyboardFocus(false)) { g.setColour(c.accent); g.drawRoundedRectangle(r.expanded(2.f), rad + 2.f, 1.5f); }
    }

    void drawButtonText(Graphics& g, TextButton& b, bool over, bool) override {
        const Pal& c = p();
        const String kind = b.getProperties().getWithDefault("kind", "control");
        if (kind == "switch") return;  // its label is drawn by the panel
        const bool on = b.getToggleState();
        const bool alarm = bool(b.getProperties().getWithDefault("danger", false)) && on;
        const bool filledToggle = kind == "control" && on && b.getClickingTogglesState();
        const Colour col = !b.isEnabled() ? c.muted.withAlpha(0.6f)
                         : kind == "primary" ? c.surface
                         : filledToggle ? (alarm ? Colours::white : c.accent)
                         : alarm ? c.negative
                         : kind == "icon" ? (over ? c.text : c.secondary)
                         : on || over ? c.text : c.secondary;
        const auto r = b.getLocalBounds().toFloat();
        const String icon = b.getProperties().getWithDefault("icon", "");
        const String trailing = b.getProperties().getWithDefault("trailing", "");
        const Font f = text(float(b.getProperties().getWithDefault("fontSize", 13.0)), kind == "primary");
        g.setFont(f);
        if (icon.isNotEmpty() && b.getButtonText().isEmpty()) {
            const float is = jmin(16.f, r.getHeight() * 0.55f);
            drawIcon(g, icon, r.withSizeKeepingCentre(is, is), col, p().dark);
            return;
        }
        if (trailing.isNotEmpty()) {  // text on the left, a chevron on the right
            g.setColour(col);
            g.drawFittedText(b.getButtonText(), r.reduced(14.f, 0.f).withTrimmedRight(16.f).toNearestInt(), Justification::centredLeft, 1, 0.9f);
            drawIcon(g, trailing, Rectangle<float>(r.getRight() - 27.f, r.getCentreY() - 7.f, 14.f, 14.f), c.muted);
            return;
        }
        if (icon.isNotEmpty()) {
            const float is = 14.f, tw = widthOf(f, b.getButtonText());
            const float x0 = r.getCentreX() - (is + 7.f + tw) * 0.5f;
            drawIcon(g, icon, Rectangle<float>(x0, r.getCentreY() - is * 0.5f, is, is), col, p().dark);
            g.setColour(col);
            g.drawText(b.getButtonText(), Rectangle<float>(x0 + is + 7.f, r.getY(), tw + 4.f, r.getHeight()), Justification::centredLeft);
            return;
        }
        g.setColour(col);
        g.drawText(b.getButtonText(), r, Justification::centred);
    }

    void drawPopupMenuBackground(Graphics& g, int w, int h) override {
        const Pal& c = p();
        g.fillAll(c.dark ? c.raised : c.surface);
        g.setColour(c.control); g.drawRect(0, 0, w, h, 1);
    }
    Font getPopupMenuFont() override { return text(13.f); }
    int getPopupMenuBorderSize() override { return 6; }
    Font getLabelFont(Label& l) override { return l.getFont(); }
    void drawTooltip(Graphics& g, const String& t, int w, int h) override {
        const Pal& c = p();
        g.fillAll(c.dark ? c.raised : c.surface);
        g.setColour(c.control); g.drawRect(0, 0, w, h, 1);
        g.setColour(c.text); g.setFont(text(12.f));
        g.drawFittedText(t, Rectangle<int>(0, 0, w, h).reduced(8, 4), Justification::centredLeft, 4);
    }
    Rectangle<int> getTooltipBounds(const String& t, Point<int> pos, Rectangle<int> parent) override {
        const int w = jmin(320, int(widthOf(text(12.f), t)) + 20);
        const int lines = int(std::ceil(widthOf(text(12.f), t) / float(w - 16))) ;
        const int h = 10 + 16 * jmax(1, lines);
        return Rectangle<int>(pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12, pos.y + 16, w, h).constrainedWithin(parent);
    }
};

// ---------------------------------------------------------------- a button that opens an undo step before it acts
class PlainButton : public TextButton {
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
// Laid out in one column of the dial card: the name (with the mark of the line it moves), the dial,
// the value in figures.
class Knob : public Component {
public:
    Knob(CorridorAudioProcessor& proc, const char* id, String title_, std::function<String(double)> fmt, const char* role,
         std::pair<String, String> ends = {})
        : title(std::move(title_)), endLabels(std::move(ends)) {
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
        editor.setFont(num(13.f));
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
    void setLive(float db) { slider.getProperties().set("live", db); }

    static constexpr int kDial = 72, kDialTop = 26, kValueTop = 106;
    void resized() override {
        slider.setBounds((getWidth() - kDial) / 2, kDialTop, kDial, kDial);
        editor.setBounds((getWidth() - 104) / 2, kValueTop - 2, 104, 24);
    }
    void paint(Graphics& g) override {
        auto* look = dynamic_cast<Look*>(&getLookAndFeel());
        if (look == nullptr) return;
        const Pal& c = look->p();
        const float w = float(getWidth()), cx = w * 0.5f;
        const String role = slider.getProperties()["role"].toString();
        // name, led by the mark of the line it moves
        const Font tf = text(11.5f).withExtraKerningFactor(0.06f);
        const String name = title.toUpperCase();
        const bool hasMark = role.isNotEmpty();
        const float tw = widthOf(tf, name) + (hasMark ? 20.f : 0.f);
        float tx = cx - tw * 0.5f;
        if (hasMark) { lineMark(g, Line<float>(tx, 9.5f, tx + 13.f, 9.5f), look->roleColour(role), role != "data", role == "data" ? 2.f : 1.5f); tx += 20.f; }
        g.setFont(tf); g.setColour(slider.isMouseOverOrDragging() ? c.text : c.secondary);
        g.drawText(name, Rectangle<float>(tx, 0.f, tw, 19.f), Justification::centredLeft);
        if (endLabels.first.isNotEmpty()) {  // which way is which: the start of the sweep is fast, the end slow
            const float dy = float(kDialTop) + float(kDial) - 6.f, half = float(kDial) * 0.5f;
            g.setFont(text(11.f)); g.setColour(c.muted);
            const float need = jmax(widthOf(text(11.f), endLabels.first), widthOf(text(11.f), endLabels.second)) + 6.f;
            if (cx - half - 4.f >= need) {  // room beside the dial
                g.drawText(endLabels.first, Rectangle<float>(cx - half - 4.f - need, dy - 8.f, need, 16.f), Justification::centredRight);
                g.drawText(endLabels.second, Rectangle<float>(cx + half + 4.f, dy - 8.f, need, 16.f), Justification::centredLeft);
            } else {  // a narrow column: just under the two ends of the sweep
                const float uy = float(kDialTop) + float(kDial) - 4.f;
                g.drawText(endLabels.first, Rectangle<float>(cx - half - 6.f, uy, need, 14.f), Justification::centredLeft);
                g.drawText(endLabels.second, Rectangle<float>(cx + half + 6.f - need, uy, need, 14.f), Justification::centredRight);
            }
        }
        if (!isEditing())
            drawFigure(g, slider.getTextFromValue(slider.getValue()), Rectangle<float>(0.f, float(kValueTop), w, 20.f), Justification::centred, 13.f, c.text, c.muted);
    }
    DialSlider slider;

private:
    String title;
    std::pair<String, String> endLabels;
    TextEditor editor;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> att;
};

// ---------------------------------------------------------------- underline tabs (Ride / Hold, A / B)
// The selected label is in ink with a 1px accent line exactly as wide as it, on the component's bottom
// edge, which is placed on the nearest existing rule.
class Tabs : public Component, public SettableTooltipClient {
public:
    explicit Tabs(StringArray labels_) : labels(std::move(labels_)) { setWantsKeyboardFocus(true); }
    std::function<void(int)> onSelect;
    int selected = 0;
    float fontSize = 14.f;
    float slotWidth = 0.f;  // 0 = share the width equally
    std::function<const Pal&()> pal;
    void setSelected(int s) {
        if (s == selected) return;
        selected = s;
        repaint();
        if (auto* h = getAccessibilityHandler()) h->notifyAccessibilityEvent(AccessibilityEvent::valueChanged);
    }
    std::unique_ptr<AccessibilityHandler> createAccessibilityHandler() override {
        struct Value : AccessibilityTextValueInterface {
            explicit Value(Tabs& o) : owner(o) {}
            bool isReadOnly() const override { return false; }
            String getCurrentValueAsString() const override { return owner.labels[owner.selected]; }
            void setValueAsString(const String& v) override { if (const int i = owner.labels.indexOf(v, true); i >= 0) owner.choose(i); }
            Tabs& owner;
        };
        return std::make_unique<AccessibilityHandler>(
            *this, AccessibilityRole::comboBox,
            AccessibilityActions().addAction(AccessibilityActionType::press, [this] { choose((selected + 1) % labels.size()); }),
            AccessibilityHandler::Interfaces{std::make_unique<Value>(*this)});
    }
    Rectangle<float> slot(int i) const {
        const float sw = slotWidth > 0.f ? slotWidth : float(getWidth()) / float(labels.size());
        return {float(i) * sw, 0.f, sw, float(getHeight())};
    }
    void paint(Graphics& g) override {
        const Pal& c = pal();
        const Font f = text(fontSize, true);
        g.setFont(f);
        for (int i = 0; i < labels.size(); ++i) {
            const auto r = slot(i);
            const bool sel = i == selected;
            g.setColour(sel ? c.accent : hoverIndex == i ? c.text : c.muted);
            const float tw = widthOf(f, labels[i]);
            const float tx = r.getX();
            g.drawText(labels[i], Rectangle<float>(tx, 0.f, tw + 2.f, float(getHeight()) - 1.f), Justification::centredLeft);
            if (sel) { g.setColour(c.accent); g.fillRect(tx, float(getHeight()) - 1.f, tw, 1.f); }
        }
        if (hasKeyboardFocus(false)) {
            const auto r = slot(selected);
            g.setColour(c.accent);
            g.drawRoundedRectangle(Rectangle<float>(r.getX() - 6.f, 4.f, widthOf(f, labels[selected]) + 12.f, float(getHeight()) - 9.f), 4.f, 1.5f);
        }
    }
    int indexAt(Point<float> p) const { for (int i = 0; i < labels.size(); ++i) if (slot(i).contains(p)) return i; return -1; }
    void mouseMove(const MouseEvent& e) override { hoverIndex = indexAt(e.position); repaint(); }
    void mouseExit(const MouseEvent&) override { hoverIndex = -1; repaint(); }
    void mouseDown(const MouseEvent& e) override { if (const int i = indexAt(e.position); i >= 0) choose(i); }
    bool keyPressed(const KeyPress& k) override {
        if (k.isKeyCode(KeyPress::leftKey)) { choose(jmax(0, selected - 1)); return true; }
        if (k.isKeyCode(KeyPress::rightKey)) { choose(jmin(labels.size() - 1, selected + 1)); return true; }
        if (k == KeyPress::spaceKey || k == KeyPress::returnKey) { choose((selected + 1) % labels.size()); return true; }
        return false;
    }
    void choose(int i) { if (i != selected && onSelect) onSelect(i); }

private:
    StringArray labels;
    int hoverIndex = -1;
};

// ---------------------------------------------------------------- the window ground, the cards and the level card's contents
struct MeterVals { float peakL = -120.f, peakR = -120.f, hold = -120.f; };

class Scene : public Component {
public:
    Scene(CorridorAudioProcessor& p, const Pal*& pal_, const Geo& geo) : proc(p), pal(pal_), G(geo) { hist.reserve(kHist); }
    std::vector<corridor::Snapshot> hist;
    MeterVals in, out;
    bool analyzing = false, bypassed = false;
    float analyzeSeconds = 0.f;
    const char* hover = nullptr;
    const char* dragId = nullptr;
    const char* emphasis = nullptr;  // a line whose dial is in use

    void paint(Graphics& g) override {
        const Pal& c = *pal;
        g.fillAll(c.ground);
        g.setColour(c.rule); g.fillRect(0.f, kTopBar - 1.f, float(getWidth()), 1.f);
        card(g, c, G.level); card(g, c, G.dial);
        if (!G.compact) { card(g, c, G.read); card(g, c, G.mode); }
        // dial card: one column per dial, split by 1px dividers
        const float colW = (G.dial.getWidth() - 32.f) / 5.f;
        g.setColour(c.rule);
        for (int i = 1; i < 5; ++i) g.fillRect(G.dial.getX() + 16.f + colW * float(i), G.dial.getY() + 20.f, 1.f, G.dial.getHeight() - 40.f);
        paintLevel(g, c);
    }

    void paintLevel(Graphics& g, const Pal& c) {
        const float cx0 = G.level.getX() + 16.f, cx1 = G.level.getRight() - 16.f;
        // header: the title, then the two series named by their own marks (compact: the readout line instead)
        if (G.compact) {
            g.setColour(c.rule); g.fillRect(cx0, G.strip.getBottom() + 4.f, cx1 - cx0, 1.f);
        } else {
        g.setFont(text(16.f, true)); g.setColour(c.text);
        g.drawText("Level", Rectangle<float>(cx0, G.level.getY() + 16.f, 120.f, 20.f), Justification::centredLeft);
        {
            const Font lf = text(12.f);
            g.setFont(lf);
            float x = cx1;
            auto item = [&](const String& label, std::function<void(float)> mark) {
                const float tw = widthOf(lf, label);
                x -= tw; g.setColour(c.muted); g.drawText(label, Rectangle<float>(x, G.level.getY() + 16.f, tw + 2.f, 20.f), Justification::centredLeft);
                x -= 22.f; mark(x); x -= 20.f;
            };
            item(peakMode(proc) ? "Peak, last 10 s" : "RMS, last 10 s", [](float) {});
            x += 16.f;
            item("Input", [&](float mx) { g.setColour(c.inLine.withAlpha(0.35f)); g.fillRect(mx, G.level.getY() + 22.f, 14.f, 8.f); g.setColour(c.inLine); g.fillRect(mx, G.level.getY() + 22.f, 14.f, 1.f); });
            item("Output", [&](float mx) { g.setColour(c.accent); g.fillRect(mx, G.level.getY() + 25.f, 14.f, 2.f); });
        }
        }
        // grid: a line every 10 dB (fainter every 5) and every second; one dB scale, beside the output meter,
        // read by the plot and both meters alike
        g.setFont(num(11.f));
        for (int v = 0; v >= -80; v -= 5) {
            const float y = std::round(G.yOf(float(v)));
            g.setColour(v % 10 == 0 ? c.grid : c.grid.interpolatedWith(c.surface, 0.5f)); g.fillRect(G.px0, y, G.px1 - G.px0, 1.f);
            if (v % 10 != 0) continue;
            g.setColour(c.muted);
            g.drawText(fmtDb(float(v), 0), Rectangle<float>(G.px1 + 8.f, y - 7.f, G.outBarX - G.px1 - 12.f, 14.f), Justification::centredLeft);
        }
        {
            const float sxg = (G.px1 - G.px0) / float(kHist - 1);
            for (int sec = 0; sec <= 10; ++sec) {
                g.setColour(sec % 5 == 0 ? c.grid : c.grid.interpolatedWith(c.surface, 0.5f));
                g.fillRect(std::round(G.px1 - float(sec * 50) * sxg), G.gt, 1.f, G.gb - G.gt);
            }
        }
        paintMeter(g, c, G.inBarX, in, "In peak");
        paintMeter(g, c, G.outBarX, out, "Out peak");

        const int n = int(hist.size());
        const float x0 = G.px0, x1 = G.px1, sx = (x1 - x0) / float(kHist - 1);
        auto X = [&](int k) { return x1 - float(n - 1 - k) * sx; };
        if (analyzing && n > 1) {  // the stretch being measured in Hold
            const int span = jmin(n - 1, int(analyzeSeconds * 50.f));
            const float ax = X(n - 1 - span);
            g.setColour(c.accent.withAlpha(c.dark ? 0.08f : 0.07f));
            g.fillRect(Rectangle<float>(ax, G.gt, x1 - ax, G.gb - G.gt));
        }
        const bool peak = peakMode(proc);  // the graph draws what the target measures
        if (n > 1) {
            Path fillP, inP, outP;
            fillP.startNewSubPath(X(0), G.gb);
            for (int k = 0; k < n; ++k) {
                const auto& h = hist[size_t(k)];
                const float yi = G.yOf(peak ? h.inPk : h.inRms), yo = G.yOf(peak ? h.outPk : h.outRms);
                fillP.lineTo(X(k), yi);
                if (k == 0) { inP.startNewSubPath(X(k), yi); outP.startNewSubPath(X(k), yo); }
                else { inP.lineTo(X(k), yi); outP.lineTo(X(k), yo); }
            }
            fillP.lineTo(X(n - 1), G.gb); fillP.closeSubPath();
            g.setColour(c.inLine.withAlpha(c.dark ? 0.16f : 0.22f)); g.fillPath(fillP);
            g.setColour(c.inLine); g.strokePath(inP, PathStrokeType(1.f));
            g.setColour(c.accent); g.strokePath(outP, PathStrokeType(2.f, PathStrokeType::mitered, PathStrokeType::butt));
        }
        // the three reference lines, labelled at their left end
        auto& st = proc.state();
        struct Ref { const char* id; const char* name; Colour col; bool dashed; };
        const Ref refs[3] = {{ids::ceiling, "Ceiling", c.negative, true}, {ids::target, "Target", c.accent, false}, {ids::threshold, "Threshold", c.positive, true}};
        float labelY[3];
        for (int i = 0; i < 3; ++i) {
            const float y = std::round(G.yOf(st.getRawParameterValue(refs[i].id)->load())) + 0.5f;
            const bool hot = hover == refs[i].id || dragId == refs[i].id || emphasis == refs[i].id;
            lineMark(g, Line<float>(x0, y, x1, y), refs[i].col, refs[i].dashed, hot ? 2.f : 1.f);
            // the handle: a round node at the right end, edged in the card colour so it sits on top
            const float hr = hot ? 6.f : 5.f;
            g.setColour(c.surface); g.fillEllipse(x1 - 10.f - hr - 1.5f, y - hr - 1.5f, (hr + 1.5f) * 2.f, (hr + 1.5f) * 2.f);
            g.setColour(refs[i].col); g.fillEllipse(x1 - 10.f - hr, y - hr, hr * 2.f, hr * 2.f);
            labelY[i] = y - 16.f;
        }
        for (int i = 1; i < 3; ++i) labelY[i] = jmax(labelY[i], labelY[i - 1] + 15.f);  // never on top of each other
        const Font lf = text(11.f, true);
        g.setFont(lf);
        for (int i = 0; i < 3; ++i) {
            const float v = st.getRawParameterValue(refs[i].id)->load();
            const String s = String(refs[i].name) + "  " + fmtDb(v);
            const auto r = Rectangle<float>(x0 + 6.f, labelY[i], widthOf(lf, s) + 8.f, 14.f);
            g.setColour(c.surface); g.fillRect(r);
            g.setColour(refs[i].col);
            g.drawText(s, r.withTrimmedLeft(4.f), Justification::centredLeft);
        }
        paintGainLane(g, c, X, n);
    }

    // The gain lane: what Corridor did, moment by moment. Up is a boost, down a cut, on a scale as wide
    // as the range. The limiter's catches are the short red marks.
    template <typename XF>
    void paintGainLane(Graphics& g, const Pal& c, XF X, int n) {
        const float rng = proc.state().getRawParameterValue(ids::range)->load();
        const float scale = jmax(6.f, std::ceil(rng / 6.f) * 6.f);
        const float mid = std::round((G.lt + G.lb) * 0.5f), half = (G.lb - G.lt) * 0.5f;
        auto gy = [&](float db) { return mid - jlimit(-1.f, 1.f, db / scale) * half; };
        g.setColour(c.grid); g.fillRect(G.px0, G.lt, G.px1 - G.px0, 1.f); g.fillRect(G.px0, G.lb, G.px1 - G.px0, 1.f);
        g.setColour(c.track); g.fillRect(G.px0, mid, G.px1 - G.px0, 1.f);
        g.setFont(num(11.f)); g.setColour(c.muted);
        const auto lbl = [&](float y, const String& s) {
            g.drawText(s, Rectangle<float>(G.level.getX() + 8.f, y - 7.f, G.px0 - 8.f - G.level.getX() - 12.f, 14.f), Justification::centredRight);
        };
        lbl(G.lt, "+" + String(int(scale))); lbl(mid, "0"); lbl(G.lb, fmtDb(-scale, 0));
        g.setFont(text(12.f)); g.setColour(c.muted);
        g.drawText("Gain", Rectangle<float>(G.px1 + 8.f, mid - 16.f, 60.f, 16.f), Justification::centredLeft);
        g.drawText("dB", Rectangle<float>(G.px1 + 8.f, mid, 60.f, 16.f), Justification::centredLeft);
        if (n < 2) return;
        Path line, area;
        area.startNewSubPath(X(0), mid);
        for (int k = 0; k < n; ++k) {
            const float y = gy(hist[size_t(k)].gain);
            area.lineTo(X(k), y);
            if (k == 0) line.startNewSubPath(X(k), y); else line.lineTo(X(k), y);
        }
        area.lineTo(X(n - 1), mid); area.closeSubPath();
        g.setColour(c.secondary.withAlpha(c.dark ? 0.16f : 0.2f)); g.fillPath(area);
        g.setColour(c.secondary); g.strokePath(line, PathStrokeType(1.5f));
        g.setColour(c.negative);
        for (int k = 0; k < n; ++k)
            if (const float lim = hist[size_t(k)].limiterDb; lim < -0.1f) g.fillRect(X(k), mid, 1.5f, jmax(2.f, gy(lim) - mid));
    }

    // a meter: left and right bars in fixed zones (green up to -18 dBFS, amber to -6, red above)
    void paintMeter(Graphics& g, const Pal& c, float bx, const MeterVals& m, const String& name) {
        const float top = G.yOf(0.f), bot = G.yOf(-kDbRange);
        const bool clip = m.hold > -0.5f;
        g.setFont(text(12.f, clip)); g.setColour(clip ? c.negative : c.muted);
        const float lw = 64.f, lx = bx < G.level.getCentreX() ? G.level.getX() + 12.f : G.level.getRight() - 12.f - lw;
        g.drawText(clip ? "Clip" : name, Rectangle<float>(lx, G.gt - 20.f, lw, 16.f), bx < G.level.getCentreX() ? Justification::centredLeft : Justification::centredRight);
        const float pk[2] = {m.peakL, m.peakR};
        for (int ch = 0; ch < 2; ++ch) {
            const float x = bx + float(ch) * (kBarW + kBarGap);
            g.setColour(c.track); g.fillRect(x, top, kBarW, bot - top);
            if (pk[ch] > -kDbRange) {
                const float v = jmin(0.f, pk[ch]);
                const float t = G.yOf(v), y18 = G.yOf(-18.f), y6 = G.yOf(-6.f);
                g.setColour(c.positive); g.fillRect(x, jmax(t, y18), kBarW, bot - jmax(t, y18));
                if (v > -18.f) { g.setColour(c.warning); g.fillRect(x, jmax(t, y6), kBarW, y18 - jmax(t, y6)); }
                if (v > -6.f) { g.setColour(c.negative); g.fillRect(x, t, kBarW, y6 - t); }
            }
            if (m.hold > -kDbRange) { g.setColour(c.accent); g.fillRect(x, std::round(G.yOf(jmin(0.f, m.hold))) - 1.f, kBarW, 2.f); }
            g.setColour(clip ? c.negative : c.track); g.fillRect(x, top - 6.f, kBarW, 3.f);
        }
    }

    // drag the Target, Threshold and Ceiling lines; click a meter to clear its peak hold
    const char* hit(Point<float> p) const {
        if (p.x < G.px0 || p.x > G.px1 || p.y < G.gt - 8.f || p.y > G.gb + 8.f) return nullptr;
        const char* best = nullptr; float bd = 8.f;
        for (const char* id : {ids::target, ids::threshold, ids::ceiling}) {
            const float d = std::abs(G.yOf(proc.state().getRawParameterValue(id)->load()) - p.y);
            if (d < bd) { bd = d; best = id; }
        }
        return best;
    }
    bool onMeter(Point<float> p) const {
        return p.y >= G.gt - 24.f && p.y <= G.gb && ((p.x >= G.inBarX - 6.f && p.x <= G.inBarX + 20.f) || (p.x >= G.outBarX - 6.f && p.x <= G.outBarX + 20.f));
    }
    void mouseMove(const MouseEvent& e) override {
        hover = hit(e.position);
        setMouseCursor(hover ? MouseCursor::UpDownResizeCursor : onMeter(e.position) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    }
    void mouseExit(const MouseEvent&) override { hover = nullptr; }
    void mouseDown(const MouseEvent& e) override {
        if (onMeter(e.position)) { proc.core().resetPeakHolds(); return; }
        dragId = hit(e.position);
        if (dragId) if (auto* p = proc.state().getParameter(dragId)) { proc.undo.beginNewTransaction(); p->beginChangeGesture(); mouseDrag(e); }
    }
    void mouseDrag(const MouseEvent& e) override {
        if (dragId) if (auto* p = proc.state().getParameter(dragId)) p->setValueNotifyingHost(p->convertTo0to1(G.vOf(e.position.y)));
    }
    void mouseUp(const MouseEvent&) override {
        if (dragId) if (auto* p = proc.state().getParameter(dragId)) p->endChangeGesture();
        dragId = nullptr;
    }

private:
    CorridorAudioProcessor& proc;
    const Pal*& pal;
    const Geo& G;
};

// ---------------------------------------------------------------- readouts: the output level first, then what made it
class Readouts : public Component {
public:
    explicit Readouts(const Pal*& p) : pal(p) { setInterceptsMouseClicks(false, false); }
    float outLevel = -120.f, gain = 0.f, inLevel = -120.f, inHold = -120.f, outHold = -120.f, inBal = 0.f, outBal = 0.f;
    bool peak = false;  // the levels are peaks (dBFS) rather than RMS
    String verdict, latency;
    int verdictTone = 0;  // 1 good (on target), 0 plain, -1 quiet (waiting, bypassed)
    bool compact = false;  // one line at the top of the level panel
    static constexpr float kTabsW = 100.f;  // room left for the RMS / Peak switch at the right
    void paintLine(Graphics& g, const Pal& c) {
        const float w = float(getWidth()), h = float(getHeight());
        const bool live = outLevel > kSilent;
        const Font big = num(26.f, true), unit = text(13.f);
        const String fig = live ? fmtDb(outLevel) : kDash;
        g.setFont(big); g.setColour(live ? c.text : c.muted);
        const float fw = widthOf(big, fig);
        g.drawText(fig, Rectangle<float>(0.f, 0.f, fw + 4.f, h), Justification::centredLeft);
        const String u = peak ? "dBFS peak" : "dB RMS";
        g.setFont(unit); g.setColour(c.muted);
        const float uw = widthOf(unit, u);
        g.drawText(u, Rectangle<float>(fw + 6.f, 2.f, uw + 4.f, h), Justification::centredLeft);
        // applied gain, at the right before the switch
        const String gv = fmtDb(gain, 1, true);
        const Font gf = num(15.f, true), lf = text(12.f);
        const float gx1 = w - kTabsW - 16.f, gvw = widthOf(gf, gv), gdw = widthOf(lf, " dB"), glw = widthOf(lf, "Applied gain ");
        g.setFont(lf); g.setColour(c.muted);
        g.drawText(" dB", Rectangle<float>(gx1 - gdw, 2.f, gdw + 2.f, h), Justification::centredLeft);
        g.setFont(gf); g.setColour(c.text);
        g.drawText(gv, Rectangle<float>(gx1 - gdw - gvw, 0.f, gvw + 2.f, h), Justification::centredLeft);
        g.setFont(lf); g.setColour(c.muted);
        const float glx = gx1 - gdw - gvw - glw;
        g.drawText("Applied gain", Rectangle<float>(glx, 2.f, glw, h), Justification::centredLeft);
        // the verdict, between the figure and the gain
        const float vx = fw + uw + 22.f;
        g.setFont(text(13.f, verdictTone > 0));
        g.setColour(verdictTone > 0 ? c.positive : verdictTone < 0 ? c.muted : c.text);
        g.drawFittedText(verdict, Rectangle<int>(int(vx), 0, int(glx - 20.f - vx), int(h)), Justification::centredLeft, 1, 0.9f);
    }
    void paint(Graphics& g) override {
        const Pal& c = *pal;
        if (compact) { paintLine(g, c); return; }
        const float w = float(getWidth()), x0 = 16.f, x1 = w - 16.f;
        g.setFont(text(13.f, true)); g.setColour(c.text);
        g.drawText("Output", Rectangle<float>(x0, 14.f, 120.f, 18.f), Justification::centredLeft);
        // (the RMS / Peak switch sits at the right of this row)
        // the answer figure: the output level the engine steers, in what the target measures
        const bool live = outLevel > kSilent;
        const Font big = num(34.f, true), unit = text(15.f);
        const String fig = live ? fmtDb(outLevel) : kDash;
        g.setFont(big); g.setColour(live ? c.text : c.muted);
        const float fw = widthOf(big, fig);
        g.drawText(fig, Rectangle<float>(x0 - 1.f, 38.f, fw + 4.f, 44.f), Justification::centredLeft);
        g.setFont(unit); g.setColour(c.muted);
        g.drawText(peak ? "dBFS peak" : "dB RMS", Rectangle<float>(x0 + fw + 8.f, 50.f, 120.f, 28.f), Justification::centredLeft);
        // how it stands against the target, in words
        g.setFont(text(13.f, verdictTone > 0));
        g.setColour(verdictTone > 0 ? c.positive : verdictTone < 0 ? c.muted : c.text);
        g.drawFittedText(verdict, Rectangle<int>(int(x0), 84, int(x1 - x0), 18), Justification::centredLeft, 1, 0.9f);
        // the rows: figure right-aligned, unit after it in grey
        struct Row { String name, value, unit; };
        auto lv = [](float v) { return v > kSilent ? fmtDb(v) : kDash; };
        auto pk = [](float v) { return v > -kDbRange ? fmtDb(v) : kMinusInf; };
        const Row rows[5] = {{"Applied gain", fmtDb(gain, 1, true), "dB"},
                             {"Input", lv(inLevel), peak ? "dBFS" : "dB"},
                             {"Peak in" + kDot + "out", pk(inHold) + kDot + pk(outHold), "dBFS"},
                             {"Balance in" + kDot + "out", balanceText(inBal) + kDot + balanceText(outBal), "dB"},
                             {"Latency", latency, "ms"}};
        float y = 114.f;
        const Font vf = num(13.f), uf = text(12.f), nf = text(13.f);
        for (const auto& r : rows) {
            g.setColour(c.rule); g.fillRect(x0, y, x1 - x0, 1.f);
            g.setFont(nf); g.setColour(c.secondary);
            g.drawText(r.name, Rectangle<float>(x0, y + 1.f, 150.f, 23.f), Justification::centredLeft);
            const float uw = r.unit.isEmpty() ? 0.f : widthOf(uf, r.unit) + 5.f;
            g.setFont(uf); g.setColour(c.muted);
            g.drawText(r.unit, Rectangle<float>(x1 - uw + 5.f, y + 1.f, uw, 23.f), Justification::centredLeft);
            g.setFont(vf); g.setColour(c.text);
            g.drawText(r.value, Rectangle<float>(x0 + 110.f, y + 1.f, x1 - uw - x0 - 110.f, 23.f), Justification::centredRight);
            y += 24.f;
        }
    }

private:
    const Pal*& pal;
};

// ---------------------------------------------------------------- the top bar: mark, name and the live status sentence
class Header : public Component, public SettableTooltipClient {
public:
    explicit Header(const Pal*& p) : pal(p) { setInterceptsMouseClicks(false, false); }
    String status;
    Colour dot;
    Rectangle<float> statusArea;
    void paint(Graphics& g) override {
        const Pal& c = *pal;
        drawMark(g, Rectangle<float>(16.f, 16.f, 20.f, 20.f), c);
        const Font nf = text(15.f, true);
        g.setFont(nf); g.setColour(c.text);
        g.drawText("Corridor", Rectangle<float>(46.f, 0.f, 90.f, kTopBar - 1.f), Justification::centredLeft);
        g.setFont(text(15.f)); g.setColour(c.muted);
        g.drawText("Leveler", Rectangle<float>(46.f + widthOf(nf, "Corridor") + 5.f, 0.f, 80.f, kTopBar - 1.f), Justification::centredLeft);
        if (status.isNotEmpty() && statusArea.getWidth() > 60.f) {
            g.setColour(dot); g.fillEllipse(statusArea.getX(), statusArea.getCentreY() - 3.f, 6.f, 6.f);
            g.setFont(text(12.5f)); g.setColour(c.secondary);
            g.drawFittedText(status, statusArea.withTrimmedLeft(14.f).toNearestInt(), Justification::centredLeft, 2, 0.92f);
        }
    }

private:
    const Pal*& pal;
};

// ---------------------------------------------------------------- the mode card: Ride / Hold, rise, fall, trim, return to 0 dB
class ModePanel : public Component {
public:
    ModePanel(CorridorAudioProcessor& p, const Pal*& pal_) : tabs({"Ride", "Hold"}), proc(p), pal(pal_) {
        setTitle("Mode and timing");
        tabs.setTitle("Ride or Hold");
        tabs.slotWidth = 64.f;
        tabs.pal = [this]() -> const Pal& { return *pal; };
        tabs.onSelect = [this](int i) {
            proc.undo.beginNewTransaction();
            if (auto* prm = proc.state().getParameter(ids::mode)) { prm->beginChangeGesture(); prm->setValueNotifyingHost(prm->convertTo0to1(float(i))); prm->endChangeGesture(); }
        };
        addAndMakeVisible(tabs);
        add(rise, riseValue, ids::rise, "Rise", [](double v) { return String(roundToInt(v)) + " ms"; });
        add(fall, fallValue, ids::fall, "Fall", [](double v) { return String(roundToInt(v)) + " ms"; });
        add(trim, trimValue, ids::trim, "Output trim", [](double v) { return fmtDb(float(v), 1, true) + " dB"; });
        ease.getProperties().set("kind", "switch");
        ease.setClickingTogglesState(true);
        ease.setTitle("Return to 0 dB in silence");
        ease.setTooltip("Off: the gain freezes below the threshold. On: after 2 s of silence it glides back to 0 dB.");
        ease.onPress = [this] { proc.undo.beginNewTransaction(); };
        easeAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::ease, ease);
        addAndMakeVisible(ease);
    }
    Tabs tabs;
    static constexpr int kRow0 = 80, kPitch = 34;
    int foot() const { return jmax(212, getHeight() - 82); }  // the rule above the last row: at the panel's foot
    void resized() override {
        const int w = getWidth();
        tabs.setBounds(16, 6, 140, 37);  // its bottom edge is the rule at y 43
        int y = kRow0;
        for (auto& r : rows) {
            r.slider->setBounds(100, y, w - 100 - 96, 26);
            r.value->setBounds(w - 16 - 24 - 64, y, 64, 26);
            y += kPitch;
        }
        ease.setBounds(w - 16 - 40, foot() + 10, 40, 26);
    }
    void refresh() {
        for (auto& r : rows) if (!r.value->isBeingEdited()) r.value->setText(splitFigure(r.slider->getTextFromValue(r.slider->getValue())).first, dontSendNotification);
        const bool hold = proc.state().getRawParameterValue(ids::mode)->load() > 0.5f;
        tabs.setSelected(hold ? 1 : 0);
        repaint();
    }
    bool floatingPanel = false;  // compact: it opens over the window from the top bar
    void paint(Graphics& g) override {
        const Pal& c = *pal;
        const float w = float(getWidth()), x0 = 16.f, x1 = w - 16.f;
        if (floatingPanel) {
            g.setColour(c.raised); g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.f);
            g.setColour(c.control); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.f, 1.f);
        }
        g.setColour(c.rule); g.fillRect(x0, 42.f, x1 - x0, 1.f);
        const bool hold = proc.state().getRawParameterValue(ids::mode)->load() > 0.5f;
        g.setFont(text(12.f)); g.setColour(c.muted);
        g.drawText(hold ? "Holds one fixed correction, measured below." : "Rides the gain continuously toward the target.",
                   Rectangle<float>(x0, 50.f, x1 - x0, 18.f), Justification::centredLeft);
        g.setFont(text(13.f)); g.setColour(c.secondary);
        int y = kRow0;
        for (auto* name : {"Rise", "Fall", "Output trim"}) {
            g.setFont(text(13.f)); g.setColour(c.secondary);
            g.drawText(name, Rectangle<int>(int(x0), y, 84, 26), Justification::centredLeft);
            g.setFont(text(12.f)); g.setColour(c.muted);
            g.drawText(String(name) == "Output trim" ? "dB" : "ms", Rectangle<float>(x1 - 20.f, float(y), 20.f, 26.f), Justification::centredLeft);
            y += kPitch;
        }
        g.setFont(text(12.f)); g.setColour(c.muted);
        g.drawFittedText("Rise lifts quiet passages, fall pulls loud ones down. A slow rise and a fast fall suit voice.",
                         Rectangle<int>(int(x0), y + 2, int(x1 - x0), 32), Justification::topLeft, 2, 1.f);
        const float fy = float(foot());
        g.setColour(c.rule); g.fillRect(x0, fy, x1 - x0, 1.f);
        g.setFont(text(13.f)); g.setColour(c.text);
        g.drawText("Return to 0 dB in silence", Rectangle<float>(x0, fy + 10.f, x1 - x0 - 48.f, 26.f), Justification::centredLeft);
        g.setFont(text(12.f)); g.setColour(c.muted);
        g.drawFittedText(ease.getToggleState() ? "On: after 2 s of silence the gain glides back to 0 dB."
                                               : "Off: the gain freezes while it is quiet.",
                         Rectangle<int>(int(x0), int(fy) + 36, int(x1 - x0 - 48.f), 32), Justification::topLeft, 2, 1.f);
        for (auto* v : {&riseValue, &fallValue, &trimValue}) v->setColour(Label::textColourId, c.text);
    }

private:
    struct Row { Slider* slider; Label* value; };
    void add(Slider& s, Label& v, const char* id, const char* name, std::function<String(double)> fmt) {
        s.setSliderStyle(Slider::LinearHorizontal);
        s.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        s.setTitle(name);
        s.setWantsKeyboardFocus(true);
        s.onDragStart = [this] { proc.undo.beginNewTransaction(); };
        addAndMakeVisible(s);
        atts.push_back(std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.state(), id, s));
        s.textFromValueFunction = fmt;
        v.setEditable(true, true, false);
        v.setJustificationType(Justification::centredRight);
        v.setFont(num(13.f));
        v.setTitle(String(name) + " value");
        v.setTooltip("Click to type a value");
        v.onTextChange = [this, &s, &v] {
            const String t = v.getText().replace(",", ".").replace(String(CharPointer_UTF8("\xe2\x88\x92")), "-");
            if (t.containsAnyOf("0123456789")) { proc.undo.beginNewTransaction(); s.setValue(t.retainCharacters("-+.0123456789").getDoubleValue(), sendNotificationSync); }
            refresh();
        };
        addAndMakeVisible(v);
        rows[rowCount++] = {&s, &v};
    }
    CorridorAudioProcessor& proc;
    const Pal*& pal;
    Slider rise, fall, trim;
    Label riseValue, fallValue, trimValue;
    PlainButton ease{"Return to 0 dB in silence"};
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> easeAtt;
    Row rows[3]{};
    int rowCount = 0;
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment>> atts;
};

// ---------------------------------------------------------------- first-run tip, floating over the level plot
class Tip : public Component {
public:
    explicit Tip(const Pal*& p) : pal(p) {
        ok.getProperties().set("kind", "primary");
        ok.onClick = [this] { if (onDismiss) onDismiss(); };
        addAndMakeVisible(ok);
        setTitle("Getting started");
    }
    std::function<void()> onDismiss;
    void resized() override { ok.setBounds(getWidth() - 16 - 76, (getHeight() - 32) / 2, 76, 32); }
    void paint(Graphics& g) override {
        const Pal& c = *pal;
        floating(g, c, getLocalBounds().toFloat().reduced(8.f, 6.f).withTrimmedTop(-6.f).withTrimmedBottom(10.f).withTrimmedLeft(-8.f).withTrimmedRight(-8.f));
        g.setFont(text(13.f, true)); g.setColour(c.text);
        g.drawText("Getting started", Rectangle<int>(16, 12, getWidth() - 124, 18), Justification::centredLeft);
        g.setFont(text(13.f)); g.setColour(c.secondary);
        g.drawFittedText("Set Target where you want your voice. Then lower Threshold until the room noise sits below its dashed line.",
                         Rectangle<int>(16, 32, getWidth() - 124, 36), Justification::topLeft, 2, 1.f);
    }

private:
    const Pal*& pal;
    PlainButton ok{"Got it"};
};

// ---------------------------------------------------------------- the Hold panel under the window
class Drawer : public Component {
public:
    Drawer(CorridorAudioProcessor& p, const Pal*& pal_) : proc(p), pal(pal_) {
        analyze.getProperties().set("kind", "primary");
        analyze.getProperties().set("fontSize", 14.0);
        analyze.setTitle("Analyze or lock");
        analyze.setTooltip("Shortcut A: analyze, then lock");
        analyze.onClick = [this] {
            if (proc.core().telemetry().analyzing.load()) proc.core().requestAnalyzeLock();
            else proc.core().requestAnalyzeStart();
        };
        addAndMakeVisible(analyze);
        setTitle("Analyze and hold");
    }
    std::vector<corridor::Snapshot> wave;
    PlainButton analyze{"Analyze"};
    Rectangle<float> panel() const { return {16.f, 0.f, float(getWidth()) - 32.f, float(getHeight()) - 16.f}; }
    void refresh() {
        const auto& t = proc.core().telemetry();
        const bool an = t.analyzing.load();
        analyze.setButtonText(an ? "Lock" : t.holdValid.load() ? "Analyze again" : "Analyze");
        repaint();
    }
    void resized() override { analyze.setBounds(32, 46, 168, 34); }
    void paint(Graphics& g) override {
        const Pal& c = *pal;
        const auto pr = panel();
        card(g, c, pr);
        const auto& t = proc.core().telemetry();
        g.setFont(text(16.f, true)); g.setColour(c.text);
        g.drawText("Analyze and hold", Rectangle<float>(32.f, 14.f, 220.f, 22.f), Justification::centredLeft);
        // what is happening, in words; the key figure in ink
        struct ReadLine { String text; bool key; };
        std::vector<ReadLine> lines;
        if (t.analyzing.load()) {
            lines = {{"Analyzing for " + String(t.analyzeSeconds.load(), 1) + " s", true},
                     {String(t.analyzeActiveSeconds.load(), 1) + " s above the threshold", false},
                     {"Locks by itself at 20 s", false}};
        } else if (t.analyzeFailed.load()) {
            lines = {{"Not enough sound above the threshold.", true}, {"Play a louder section and analyze again.", false}};
        } else if (t.holdValid.load()) {
            lines = {{"Holding " + fmtDb(t.holdGain.load(), 1, true) + " dB", true},
                     {"Measured " + fmtDb(t.holdMeasured.load(), 1) + " dB RMS", false},
                     {"over " + String(t.holdSeconds.load(), 1) + " s", false}};
        } else {
            lines = {{"Press Analyze, then play", true}, {"a typical section.", false}};
        }
        float y = 92.f;
        for (const auto& l : lines) {
            g.setFont(text(13.f, l.key)); g.setColour(l.key ? c.text : c.muted);
            g.drawText(l.text, Rectangle<float>(32.f, y, 240.f, 16.f), Justification::centredLeft);
            y += 17.f;
        }
        // waveform: input peaks above the centre line, RMS below; the measured stretch in the accent
        const auto wr = Rectangle<float>(296.f, 14.f, pr.getRight() - 16.f - 296.f, pr.getBottom() - 14.f - 14.f);
        g.setFont(text(12.f)); g.setColour(c.muted);
        g.drawText("Input, last 6 s", Rectangle<float>(wr.getX(), wr.getY(), 200.f, 16.f), Justification::centredLeft);
        g.drawText("Peak above the line, RMS below. Dashed: the threshold.", Rectangle<float>(wr.getRight() - 360.f, wr.getY(), 360.f, 16.f), Justification::centredRight);
        const auto plot = wr.withTrimmedTop(24.f);
        g.setColour(c.dark ? c.ground : c.raised); g.fillRect(plot);
        const float mid = std::round(plot.getCentreY()), half = plot.getHeight() * 0.5f - 6.f;
        auto amp = [](float db) { return jlimit(0.f, 1.f, (db + 66.f) / 60.f); };  // -66 dB at the centre, -6 at the edge
        g.setColour(c.track); g.fillRect(plot.getX(), mid, plot.getWidth(), 1.f);
        const float thr = proc.state().getRawParameterValue(ids::threshold)->load(), ta = amp(thr) * half;
        lineMark(g, Line<float>(plot.getX(), mid - ta, plot.getRight(), mid - ta), c.warning, true, 1.f);
        lineMark(g, Line<float>(plot.getX(), mid + ta, plot.getRight(), mid + ta), c.warning, true, 1.f);
        const int n = int(wave.size());
        const float step = (plot.getWidth() - 8.f) / float(kWaveEntries);
        for (int i = 0; i < n; ++i) {
            const auto& s = wave[size_t(i)];
            const bool act = s.inRms >= thr;
            g.setColour((s.analyzing ? c.accent.withMultipliedAlpha(0.55f) : c.inLine.withMultipliedAlpha(0.7f)).withMultipliedAlpha(act ? 1.f : 0.45f));
            const float x = plot.getRight() - 4.f - float(n - i) * step;
            const float up = amp(s.inPeak) * half, down = amp(s.inRms) * half;
            g.fillRect(x, mid - up, step + 0.4f, up + down);
        }
    }

private:
    CorridorAudioProcessor& proc;
    const Pal*& pal;
};

// ---------------------------------------------------------------- updates: About panel and prompt
class AboutPanel : public Component {
public:
    AboutPanel(updates::Checker& c, const Pal*& p) : checker(c), pal(p) {
        for (auto* b : {&action, &close}) addAndMakeVisible(*b);
        close.getProperties().set("icon", "close");
        close.getProperties().set("kind", "icon");
        close.setTitle("Close");
        close.setButtonText({});
        action.onClick = [this] { if (checker.known().valid()) checker.install(); else checker.check(true); };
        close.onClick = [this] { setVisible(false); };
        link.setFont(text(13.f), false, Justification::centredLeft);
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
        action.getProperties().set("kind", r.valid() ? "primary" : "control");
        link.setURL(URL(r.valid() ? r.page : String(updates::kReleasesPage)));
        link.setColour(HyperlinkButton::textColourId, pal->text);
        repaint();
    }
    void resized() override {
        close.setBounds(getWidth() - 48, 18, 30, 30);
        action.setBounds(24, getHeight() - 58, 180, 32);
        link.setButtonText("Release page");
        link.setBounds(220, getHeight() - 58, 120, 32);
    }
    void paint(Graphics& g) override {
        const Pal& c = *pal;
        floating(g, c, getLocalBounds().toFloat().reduced(8.f));
        drawMark(g, Rectangle<float>(24.f, 24.f, 20.f, 20.f), c);
        g.setColour(c.text); g.setFont(text(15.f, true));
        g.drawText("Corridor Leveler", Rectangle<float>(54.f, 20.f, 160.f, 28.f), Justification::centredLeft);
        g.setFont(num(13.f)); g.setColour(c.muted);
        g.drawText(updates::currentVersion(), Rectangle<float>(54.f + widthOf(text(15.f, true), "Corridor Leveler") + 8.f, 20.f, 100.f, 28.f), Justification::centredLeft);
        g.setFont(text(13.f)); g.setColour(c.text);
        g.drawFittedText(checker.status(), Rectangle<int>(24, 58, getWidth() - 48, 36), Justification::topLeft, 2);
        const String msg = checker.message();
        g.setColour(c.muted); g.setFont(text(12.f));
        g.drawFittedText(msg.isNotEmpty() ? msg : "The check is a plain request to GitHub. Nothing about you or your computer is sent.",
                         Rectangle<int>(24, 96, getWidth() - 48, 32), Justification::topLeft, 2);
    }

private:
    updates::Checker& checker;
    const Pal*& pal;
    PlainButton action{"Check for updates"}, close{"x"};
    HyperlinkButton link{"Release page", URL(updates::kReleasesPage)};
};

class UpdatePrompt : public Component {
public:
    UpdatePrompt(updates::Checker& c, const Pal*& p) : checker(c), pal(p) {
        for (auto* b : {&update, &ignore}) { b->getProperties().set("fontSize", 13.0); addAndMakeVisible(*b); }
        update.getProperties().set("kind", "primary");
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
    Rectangle<int> box() const { return getLocalBounds().withSizeKeepingCentre(480, 196); }
    void resized() override {
        const auto b = box();
        update.setBounds(b.getX() + 24, b.getBottom() - 56, 124, 34);
        ignore.setBounds(b.getX() + 160, b.getBottom() - 56, 96, 34);
    }
    void mouseDown(const MouseEvent& e) override {
        if (!box().contains(e.getPosition()) && checker.phase() != updates::Checker::Phase::Downloading) checker.dismissPrompt();
    }
    bool keyPressed(const KeyPress& k) override {
        if (k == KeyPress::escapeKey && checker.phase() != updates::Checker::Phase::Downloading) { checker.dismissPrompt(); return true; }
        return false;
    }
    void paint(Graphics& g) override {
        const Pal& c = *pal;
        g.fillAll(Colours::black.withAlpha(c.dark ? 0.55f : 0.35f));
        const auto b = box().toFloat();
        floating(g, c, b);
        const auto r = checker.known();
        g.setColour(c.text); g.setFont(text(18.f, true));
        g.drawText("Version " + r.version + " is available", b.reduced(24.f).withHeight(28.f), Justification::centredLeft);
        g.setFont(text(13.f)); g.setColour(c.secondary);
        String body;
        if (checker.phase() == updates::Checker::Phase::Downloading)
            body = "Downloading " + String(int(checker.progress() * 100.f)) + "%. The installer opens when it's done.";
        else {
            const String size = r.bytes > 0 ? " (" + String(double(r.bytes) / 1048576.0, 1) + " MB)" : String();
            body = "You have " + updates::currentVersion() + ". Update downloads the new version" + size +
                   " and opens its installer. When it asks, close OBS or your DAW. Your settings and sessions are kept.";
            if (checker.message().isNotEmpty()) body = checker.message();
        }
        g.drawFittedText(body, b.reduced(24.f).withTrimmedTop(38.f).withHeight(60.f).toNearestInt(), Justification::topLeft, 3);
        if (checker.phase() == updates::Checker::Phase::Downloading) {
            const auto bar = Rectangle<float>(b.getX() + 24.f, b.getBottom() - 86.f, b.getWidth() - 48.f, 6.f);
            g.setColour(c.track); g.fillRoundedRectangle(bar, 3.f);
            g.setColour(c.accent); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * checker.progress()), 3.f);
        }
    }

private:
    updates::Checker& checker;
    const Pal*& pal;
    PlainButton update{"Update"}, ignore{"Ignore"};
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
        : proc(p), pal(&kDark), scene(p, pal, geo), readouts(pal), header(pal),
          threshold(p, ids::threshold, "Threshold", [](double v) { return fmtDb(float(v)) + " dB"; }, "thr"),
          response(p, ids::response, "Response", [](double v) { return String(roundToInt(v)) + " ms" + kDot + speedWord(v); }, "", {"Fast", "Slow"}),
          target(p, ids::target, "Target", [&p](double v) { return fmtDb(float(v)) + levelUnit(p); }, "data"),
          range(p, ids::range, "Range", [](double v) { return kPlusMinus + fmtDb(float(v)) + " dB"; }, ""),
          ceiling(p, ids::ceiling, "Ceiling", [](double v) { return fmtDb(float(v)) + " dBFS"; }, "ceil"),
          ab({"A", "B"}), detect({"RMS", "Peak"}), mode(p, pal), tip(pal), drawer(p, pal), about(*checker, pal), prompt(*checker, pal) {
        look.pal = pal;
        look.apply();
        setLookAndFeel(&look);
        setWantsKeyboardFocus(true);

        addAndMakeVisible(scene);
        addAndMakeVisible(readouts);
        addAndMakeVisible(header);
        for (auto* k : {&threshold, &response, &target, &range, &ceiling}) addAndMakeVisible(*k);
        addAndMakeVisible(mode);

        // top bar controls
        presetBtn.getProperties().set("trailing", "chevron-down");
        presetBtn.setTitle("Presets");
        presetBtn.onClick = [this] { showPresets(); };
        modeBtn.getProperties().set("trailing", "chevron-down");
        modeBtn.setTitle("Mode and timing");
        modeBtn.setTooltip("Ride or Hold, rise, fall, output trim and return to 0 dB");
        modeBtn.onClick = [this] { modeOpen = !modeOpen; resized(); };
        addChildComponent(modeBtn);
        addAndMakeVisible(presetBtn);
        ab.setTitle("Settings A or B");
        ab.slotWidth = 30.f;
        ab.pal = [this]() -> const Pal& { return *pal; };
        ab.onSelect = [this](int i) { proc.switchSlot(i); };
        addAndMakeVisible(ab);
        detect.setTitle("Target measures RMS or peak");
        detect.setTooltip("RMS: the average loudness lands on the target. Peak: the highest peaks land on the target.");
        detect.slotWidth = 50.f;
        detect.fontSize = 13.f;
        detect.pal = [this]() -> const Pal& { return *pal; };
        detect.onSelect = [this](int i) {
            proc.undo.beginNewTransaction();
            if (auto* prm = proc.state().getParameter(ids::detect)) { prm->beginChangeGesture(); prm->setValueNotifyingHost(prm->convertTo0to1(float(i))); prm->endChangeGesture(); }
        };
        addAndMakeVisible(detect);
        for (auto* b : {&undoBtn, &redoBtn}) { b->getProperties().set("kind", "icon"); b->setButtonText({}); addAndMakeVisible(*b); }
        undoBtn.getProperties().set("icon", "undo"); undoBtn.setTitle("Undo"); undoBtn.setTooltip("Undo (Ctrl+Z)");
        redoBtn.getProperties().set("icon", "redo"); redoBtn.setTitle("Redo"); redoBtn.setTooltip("Redo (Ctrl+Shift+Z)");
        undoBtn.onClick = [this] { proc.undo.undo(); };
        redoBtn.onClick = [this] { proc.undo.redo(); };
        bypassBtn.getProperties().set("kind", "control"); bypassBtn.getProperties().set("danger", true);
        bypassBtn.setTitle("Bypass"); bypassBtn.setTooltip("Bypass (keeps the same latency, so nothing jumps)");
        addAndMakeVisible(bypassBtn);
        bypassBtn.setClickingTogglesState(true);
        bypassBtn.onPress = [this] { proc.undo.beginNewTransaction(); };
        bypassAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::bypass, bypassBtn);
        aboutBtn.getProperties().set("kind", "plain");
        aboutBtn.setTitle("About Corridor Leveler and updates");
        aboutBtn.setTooltip("Version " + updates::currentVersion() + " and updates");
        aboutBtn.onClick = [this] { about.refresh(); about.setVisible(!about.isVisible()); about.toFront(false); };
        aboutBtn.setAlpha(0.f);  // the header draws the name; the button only takes the click
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
        setSize(CorridorEditor::designW, CorridorEditor::designH);
        sendLookAndFeelChange();
        addMouseListener(this, true);  // a click anywhere lets Ctrl+Z and A reach the window
    }
    void mouseDown(const MouseEvent& e) override {
        if (modeOpen && e.eventComponent != &modeBtn && !mode.isParentOf(e.eventComponent) && e.eventComponent != &mode) {
            modeOpen = false;  // a click anywhere else closes the compact mode panel
            resized();
        }
        if (auto* f = Component::getCurrentlyFocusedComponent(); f == nullptr || !isParentOf(f)) grabKeyboardFocus();
    }
    ~CorridorContent() override { checker->removeChangeListener(this); setLookAndFeel(nullptr); }

    std::function<void(bool)> onDrawerChange;
    void setModeOpen(bool open) { modeOpen = open; resized(); }
    int drawerSpace = 0;  // the height the Hold panel takes at the bottom, set by the editor

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
        if (k == KeyPress::escapeKey && modeOpen) { modeOpen = false; resized(); return true; }
        if (mods.isCommandDown() && (code == 'Z' || code == 'z')) { mods.isShiftDown() ? proc.undo.redo() : proc.undo.undo(); return true; }
        if (mods.isCommandDown() && (code == 'Y' || code == 'y')) { proc.undo.redo(); return true; }
        if (!mods.isAnyModifierKeyDown() && (code == 'A' || code == 'a') && drawer.isVisible()) { drawer.analyze.triggerClick(); return true; }
        return false;
    }

    // a plain edge round the window, lighter than the ground, so it stands apart from a black host
    void paintOverChildren(Graphics& g) override { g.setColour(kFrame); g.drawRect(getLocalBounds().toFloat(), 1.5f); }

    void resized() override {
        const int W = getWidth(), H = getHeight() - drawerSpace;
        geo.layout(float(W), float(H));
        scene.setBounds(0, 0, W, H);
        header.setBounds(0, 0, W, int(kTopBar));
        const bool compact = geo.compact;
        if (!compact) modeOpen = false;
        readouts.compact = compact;
        readouts.setBounds((compact ? geo.strip : geo.read).toNearestInt());
        mode.floatingPanel = compact;
        mode.setBounds(geo.mode.toNearestInt());
        mode.setVisible(!compact || modeOpen);
        if (compact && modeOpen) mode.toFront(false);
        // top bar, left to right: name (About), preset, A / B, undo, redo, the status sentence ... update, theme, bypass
        aboutBtn.setBounds(12, 8, 160, 36);
        const int presetW = compact ? 140 : 172, abX = 196 + presetW + 24;  // compact: a narrower preset menu
        presetBtn.setBounds(196, 11, presetW, 30);
        ab.setBounds(abX, 0, 60, int(kTopBar));  // its bottom edge is the top bar's rule
        undoBtn.setBounds(abX + 72, 11, 30, 30);
        redoBtn.setBounds(abX + 106, 11, 30, 30);
        const int statusX = abX + 160;
        bypassBtn.setBounds(W - 16 - 100, 11, 100, 30);
        int statusRight = bypassBtn.getX() - 16;
        modeBtn.setVisible(compact);
        modeBtn.setToggleState(modeOpen, dontSendNotification);
        if (compact) { modeBtn.setBounds(bypassBtn.getX() - 8 - 96, 11, 96, 30); statusRight = modeBtn.getX() - 16; }
        if (updateBtn.isVisible()) { updateBtn.setBounds(statusRight + 16 - 12 - 150, 11, 150, 30); statusRight = updateBtn.getX() - 16; }
        if (compact)  // at the right end of the readout line
            detect.setBounds(int(geo.strip.getRight() - Readouts::kTabsW), int(geo.strip.getCentreY()) - 15, int(Readouts::kTabsW), 30);
        else          // in the Output panel's title row
            detect.setBounds(int(geo.read.getRight()) - 16 - 100, int(geo.read.getY()) + 6, 100, 30);
        // the status sentence, where there is room for it (compact, the readout line says how it stands)
        header.statusArea = statusRight - statusX >= 220 ? Rectangle<float>(float(statusX), 6.f, float(statusRight - statusX), kTopBar - 13.f)
                                                         : Rectangle<float>();
        // dials: five equal columns in the dial card
        const float colW = (geo.dial.getWidth() - 32.f) / 5.f;
        int i = 0;
        for (auto* k : {&threshold, &response, &target, &range, &ceiling}) {
            const int x = int(std::round(geo.dial.getX() + 16.f + colW * float(i++)));
            k->setBounds(x, int(geo.dial.getY()) + 14, int(colW), 128);
        }
        tip.setBounds(int(geo.px0) + 8, int(geo.gt) + 26, jmin(500, int(geo.px1 - geo.px0) - 16), 92);
        about.setBounds(8, 46, 420, 196);
        prompt.setBounds(getLocalBounds());
        drawer.setBounds(0, H, W, CorridorEditor::drawerH);
    }

    void tick() {
        auto& dsp = proc.core();
        const auto& t = dsp.telemetry();
        const int n = dsp.copyHistory(scratch, kHist);
        scene.hist.assign(scratch, scratch + n);
        scene.analyzing = t.analyzing.load() && proc.state().getRawParameterValue(ids::mode)->load() > 0.5f;  // only Hold measures
        scene.analyzeSeconds = t.analyzeSeconds.load();

        const bool byp = proc.state().getRawParameterValue(ids::bypass)->load() > 0.5f;
        scene.bypassed = byp;
        scene.in = {t.inPeakL.load(), t.inPeakR.load(), t.inPeakHold.load()};
        scene.out = byp ? scene.in : MeterVals{t.outPeakL.load(), t.outPeakR.load(), t.outPeakHold.load()};
        // the level the engine steers (what From target and the Target dial's live dot compare)
        const bool pkMode = peakMode(proc);
        const float steered = t.levelValid.load() ? t.level.load() + (byp ? 0.f : t.gain.load()) : (pkMode ? t.outPk.load() : t.outRms.load());
        target.setLive(steered);
        threshold.setLive(t.inRms.load());
        const int det = pkMode ? 1 : 0;
        if (det != detect.selected) target.repaint();  // its value shows the unit
        detect.setSelected(det);
        bypassBtn.setButtonText(byp ? "Bypassed" : "Bypass");
        scene.emphasis = target.inUse() ? ids::target : threshold.inUse() ? ids::threshold : ceiling.inUse() ? ids::ceiling : nullptr;

        const bool holdMode = proc.state().getRawParameterValue(ids::mode)->load() > 0.5f;
        ab.setSelected(proc.slot);
        presetBtn.setButtonText(proc.presetName + (proc.presetEdited() ? " (edited)" : ""));
        undoBtn.setEnabled(proc.undo.canUndo());
        redoBtn.setEnabled(proc.undo.canRedo());
        mode.refresh();
        modeBtn.setButtonText(holdMode ? "Hold" : "Ride");

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
        const float tgt = proc.state().getRawParameterValue(ids::target)->load();
        const float out = steered, thr = proc.state().getRawParameterValue(ids::threshold)->load();
        const bool active = !holdMode && !byp && t.inRms.load() >= thr;
        const float d = out - tgt;
        readouts.peak = pkMode;
        readouts.outLevel = out;
        readouts.gain = g;
        readouts.inLevel = pkMode ? t.inPk.load() : t.inRms.load();
        readouts.inHold = t.inPeakHold.load();
        readouts.outHold = byp ? t.inPeakHold.load() : t.outPeakHold.load();
        readouts.inBal = t.inBalance.load();
        readouts.outBal = t.outBalance.load();
        const String tgtText = fmtDb(tgt) + (pkMode ? " dBFS" : " dB");
        if (byp) { readouts.verdict = "Bypassed, passing through unchanged"; readouts.verdictTone = -1; }
        else if (holdMode) {
            readouts.verdict = t.analyzing.load() ? "Measuring for Hold, " + String(t.analyzeSeconds.load(), 1) + " s"
                             : t.holdValid.load() ? "Holding a fixed " + fmtDb(t.holdGain.load(), 1, true) + " dB"
                                                  : "Hold: press Analyze below";
            readouts.verdictTone = 0;
        }
        else if (!active) { readouts.verdict = "Below the threshold, not adjusting"; readouts.verdictTone = -1; }
        else if (std::abs(d) < 1.f) { readouts.verdict = "On the " + tgtText + " target"; readouts.verdictTone = 1; }
        else { readouts.verdict = fmtDb(std::abs(d)) + (d > 0 ? " dB over" : " dB under") + " the " + tgtText + " target"; readouts.verdictTone = 0; }
        const double fs = dsp.sampleRate();
        readouts.latency = String(1000.0 * dsp.latencySamples() / fs, 1);

        header.status = sentence(byp, holdMode);
        header.dot = stateColour(byp, holdMode);
        header.setTitle("Status");
        header.setDescription(header.status);  // screen readers hear the full sentence
        header.setTooltip(header.status + "\nLatency " + readouts.latency + " ms (" + String(dsp.latencySamples()) + " samples at " + String(fs / 1000.0, 1) + " kHz).");

        repaint();
    }

    Colour stateColour(bool byp, bool holdMode) const {
        const Pal& c = *pal;
        if (byp) return c.negative;
        if (holdMode) return c.text;
        switch (corridor::State(proc.core().telemetry().state.load())) {
            case corridor::State::OnTarget: return c.positive;
            case corridor::State::Boosting:
            case corridor::State::Cutting: return c.accent;
            case corridor::State::Ceiling: return c.negative;
            case corridor::State::Range: return c.warning;
            case corridor::State::Returning: return c.secondary;
            case corridor::State::Waiting:
            case corridor::State::Held:
            case corridor::State::Analyzing:
            case corridor::State::Unset:
            default: return c.muted;
        }
    }

    // the status as one plain sentence; its figures are the same values the readouts show
    String sentence(bool byp, bool holdMode) const {
        const auto& t = proc.core().telemetry();
        auto& st = proc.state();
        const float in = peakMode(proc) ? t.inPk.load() : t.inRms.load(), g = t.gain.load();
        const float tgt = st.getRawParameterValue(ids::target)->load();
        const float thr = st.getRawParameterValue(ids::threshold)->load();
        const float cei = st.getRawParameterValue(ids::ceiling)->load();
        const float rng = st.getRawParameterValue(ids::range)->load();
        if (byp) return "Bypassed. The audio passes through unchanged, still delayed by the latency so nothing jumps.";
        if (holdMode) {
            if (t.analyzing.load())
                return "Analyzing: " + String(t.analyzeSeconds.load(), 1) + " s, " + String(t.analyzeActiveSeconds.load(), 1) +
                       " s above the threshold. Press Lock after a typical section; it locks by itself at 20 s.";
            if (t.analyzeFailed.load()) return "Not enough sound above the threshold. Play a louder section, or lower the threshold, and analyze again.";
            if (t.holdValid.load()) {
                const int lim = t.holdLimit.load();
                return "Holding " + fmtDb(t.holdGain.load(), 1, true) + " dB, measured at " + fmtDb(t.holdMeasured.load(), 1) + " dB RMS over " +
                       String(t.holdSeconds.load(), 1) + " s." + (lim == 1 ? " Limited by the ceiling." : lim == 2 ? " Limited by the range." : "");
            }
            return "Holding " + fmtDb(g, 1, true) + " dB. Play a typical section and press Analyze.";
        }
        switch (corridor::State(t.state.load())) {
            case corridor::State::OnTarget:
                return "On target with " + fmtDb(g, 1, true) + " dB applied." +
                       (t.percussive.load() ? " Percussive material: following the groove, not single hits." : "");
            case corridor::State::Boosting: return "Coming in at " + fmtDb(in, 1) + levelUnit(proc) + ", below the " + fmtDb(tgt, 1) + " dB target. Lifting it, " + fmtDb(g, 1, true) + " dB so far.";
            case corridor::State::Cutting: return "Coming in at " + fmtDb(in, 1) + levelUnit(proc) + ", above the " + fmtDb(tgt, 1) + " dB target. Lowering it, " + fmtDb(g, 1, true) + " dB so far.";
            case corridor::State::Ceiling: return "More boost would push peaks over " + String(int(corridor::kPeakLimitDb)) + " dB into the " + fmtDb(cei, 1) + " dBFS ceiling's limiter. Held at " + fmtDb(g, 1, true) + " dB.";
            case corridor::State::Range: return "It needs more than the " + kPlusMinus + String(rng, 1) + " dB range allows. Held at " + fmtDb(g, 1, true) + " dB.";
            case corridor::State::Returning: return "Silent for over 2 s. Returning to 0 dB, now " + fmtDb(g, 1, true) + " dB.";
            case corridor::State::Waiting:
            case corridor::State::Held:
            case corridor::State::Analyzing:
            case corridor::State::Unset:
            default:
                return t.inRms.load() < thr ? "Below the " + fmtDb(thr, 1) + " dB threshold: silence, breaths or noise. Not adjusting; the gain stays at " + fmtDb(g, 1, true) + " dB."
                                : "Sound just started. Measuring it before acting.";
        }
    }

private:
    CorridorAudioProcessor& proc;
    SharedResourcePointer<FontSet> fontHold;  // the typefaces live as long as the window
    const Pal* pal;
    Geo geo;  // declared before everything that reads it
    Look look;
    Scene scene;
    Readouts readouts;
    Header header;
    Knob threshold, response, target, range, ceiling;
    Tabs ab, detect;
    ModePanel mode;
    PlainButton presetBtn{"Default"}, undoBtn{"Undo"}, redoBtn{"Redo"}, bypassBtn{"Bypass"}, modeBtn{"Ride"};
    bool modeOpen = false;  // compact: the mode panel is open over the window
    PlainButton aboutBtn{""}, updateBtn{"Update"};
    Tip tip;
    Drawer drawer;
    TooltipWindow tooltips{this, 600};
    SharedResourcePointer<updates::Checker> checker;  // declared before the panels that use it
    AboutPanel about;
    UpdatePrompt prompt;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
    corridor::Snapshot scratch[kHist];
};

// ---------------------------------------------------------------- editor
CorridorEditor::CorridorEditor(CorridorAudioProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    const int savedW = p.editorWidth, savedH = p.editorHeight;  // read first: setting limits triggers resized()
    content = std::make_unique<CorridorContent>(p);
    content->onDrawerChange = [this](bool open) { applyDrawer(open); };
    addAndMakeVisible(*content);
    setResizable(true, true);
    setWantsKeyboardFocus(false);
    size0 = {jlimit(minW, maxW, savedW), jlimit(minH, maxH, savedH)};
    drawerOpen = p.state().getRawParameterValue(ids::mode)->load() > 0.5f;
    applyDrawer(drawerOpen);
    content->tick();  // fill the window before it is first shown
    startTimerHz(30);
}

CorridorEditor::~CorridorEditor() { stopTimer(); }

int CorridorEditor::drawerSpace() const { return drawerOpen ? drawerH : 0; }

// Hold mode opens the analysis panel under the window: the window grows by its height and keeps its width.
// The window resizes freely; its contents are laid out again at the new size, never scaled.
void CorridorEditor::applyDrawer(bool open) {
    const int mainH = getHeight() > 0 ? getHeight() - drawerSpace() : size0.y;
    const int w = getWidth() > 0 ? getWidth() : size0.x;
    drawerOpen = open;
    setResizeLimits(minW, minH + drawerSpace(), maxW, maxH + drawerSpace());
    setSize(w, mainH + drawerSpace());
    resized();
}

void CorridorEditor::paint(Graphics& g) { g.fillAll(kDark.ground); }

void CorridorEditor::resized() {
    content->drawerSpace = drawerSpace();
    content->setBounds(getLocalBounds());
    if (getWidth() > 0) { proc.editorWidth = getWidth(); proc.editorHeight = getHeight() - drawerSpace(); }
}

void CorridorEditor::timerCallback() { content->tick(); }

void CorridorEditor::openModePanel(bool open) { content->setModeOpen(open); }
