// Corridor Leveler - plugin window.
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Design size 1120 x 630, scaled as a whole. A header strip; the input peak meter on the left
// edge and the output on the right; between them a charcoal level graph (0 to -80 dB) whose lower
// part fades out under the controls. Threshold and Response on the left, a larger Target in the
// middle with the Ride / Hold switch under it, Range and Ceiling on the right. In Hold mode an
// analysis panel (150 px) opens under the window with the Analyze / Lock key and a live input
// waveform. Matte dials with yellow value rings, raised keys, recessed wells.
#include "PluginEditor.h"

#include "Updates.h"

#include <cmath>
#include <map>
#include <vector>

using namespace juce;

namespace {

// ---------------------------------------------------------------- palette
// Every colour the window uses. Lighting (shadows, highlights) is black or white at an alpha.
namespace K {
const Colour page{0xff121214}, chrome{0xff1b1b1e}, chromeTop{0xff26262a}, seam{0xff0c0c0e}, frame{0xff2e2e33};
const Colour graphTop{0xff2a2a2e}, graphBottom{0xff1a1a1d};
const Colour ink{0xffe8e8ea}, label{0xffc9c9cd}, muted{0xff9d9da3};
const Colour yellow{0xfff5c41c}, green{0xff8cc63f}, red{0xffff5b5b}, trace{0xffb2b2b7}, thrMid{0xffc6c62a};
const Colour meterLit{0xff5cc834}, meterLow{0xff2f8a1d}, meterWarn{0xffd8c425}, meterOff{0xff2a321f};
const Colour meterBgTop{0xff202023}, meterBgBottom{0xff151517};
const Colour knobHi{0xff303035}, knobLo{0xff141416}, skirtTop{0xff45454b}, skirtMid{0xff222226}, skirtBottom{0xff0b0b0c};
const Colour btnTop{0xff4a4a50}, btnBottom{0xff35353a}, btnEdge{0xff5d5d64}, btnHover{0xff58585e}, btnUnder{0xff1d1d20}, btnPressed{0xff232326};
const Colour well{0xff141416}, wellDeep{0xff0e0e10}, tagBg{0xd9141416};
const Colour drawerTop{0xff1a1a1d}, drawerBottom{0xff232326}, floatBg{0xff1f1f22}, floatEdge{0xff3a3a40};
}  // namespace K

Font sans(float h, bool bold = false, float trackPx = 0.f) {
    Font f(FontOptions(h, bold ? Font::bold : Font::plain));
    if (trackPx > 0.f) f.setExtraKerningFactor(trackPx / h);
    return f;
}
Font mono(float h, bool bold = false) {
    return Font(FontOptions(Font::getDefaultMonospacedFontName(), h, bold ? Font::bold : Font::plain));
}

String fmtDb(float v, int dec = 1, bool sign = false) {
    String s = String(std::abs(v), dec);
    const bool zero = s == String(0.0, dec);
    if (v < 0 && !zero) return "-" + s;
    if (sign && v > 0 && !zero) return "+" + s;
    return s;
}
const String kPlusMinus = String(CharPointer_UTF8("\xc2\xb1"));

constexpr float kTop = 48.f, kBottomPad = 14.f, kDbRange = 80.f;  // graph and meters share this scale
constexpr int kHead = 40, kMeterW = 66, kHist = 500;              // 10 s at 20 ms per entry
constexpr int kWaveEntries = 300;                                 // 6 s in the analysis panel
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
        p.addCentredArc(cx, cy + s * 0.04f, s * 0.38f, s * 0.38f, 0.f, 0.7f, 2.f * kPi - 0.7f, true);
        p.startNewSubPath(cx, y + s * 0.04f); p.lineTo(cx, cy - s * 0.02f);
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
        g.fillRect(x, y + s * 0.1f, s, s * 0.12f);
        g.fillRect(x, y + s * 0.78f, s, s * 0.12f);
        for (int k = 0; k <= 30; ++k) {
            const float t = float(k) / 30.f, env = 1.f - t * 0.72f;
            const float px = x + s * t, py = cy - std::sin(t * 4.f * kPi) * s * 0.2f * env;
            if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
        }
        g.strokePath(p, PathStrokeType(jmax(1.4f, s * 0.08f), PathStrokeType::curved, PathStrokeType::rounded));
    }
}

// A recessed well: dark fill, shadow falling in from the top edge, a faint lit lip at the bottom.
void drawWell(Graphics& g, Rectangle<float> r, float rad, Colour fill = K::well) {
    Path p; p.addRoundedRectangle(r, rad);
    g.setColour(fill); g.fillPath(p);
    {
        Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(p);
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.7f), 0.f, r.getY(), Colours::transparentBlack, 0.f, r.getY() + jmin(10.f, r.getHeight() * 0.5f), false));
        g.fillRect(r);
    }
    g.setColour(Colours::black.withAlpha(0.8f)); g.strokePath(p, PathStrokeType(1.f));
    g.setColour(Colours::white.withAlpha(0.07f));
    g.drawLine(r.getX() + rad, r.getBottom() + 0.5f, r.getRight() - rad, r.getBottom() + 0.5f, 1.f);
}

// A raised key: lower edge and drop shadow, lit top bevel, a sheen on the upper half.
void drawKey(Graphics& g, Rectangle<float> r, bool hot, float rad = 7.f) {
    Path under; under.addRoundedRectangle(r.translated(0.f, 2.f), rad);
    DropShadow(Colours::black.withAlpha(0.6f), 8, {0, 3}).drawForPath(g, under);
    g.setColour(K::btnUnder); g.fillPath(under);
    Path face; face.addRoundedRectangle(r, rad);
    g.setGradientFill(ColourGradient(hot ? K::btnHover : K::btnTop, 0.f, r.getY(), K::btnBottom, 0.f, r.getBottom(), false));
    g.fillPath(face);
    g.setGradientFill(ColourGradient(Colours::white.withAlpha(0.10f), 0.f, r.getY(), Colours::white.withAlpha(0.f), 0.f, r.getCentreY(), false));
    g.fillRoundedRectangle(r.reduced(1.f).withHeight(r.getHeight() * 0.5f), rad - 1.f);
    g.setColour(K::btnEdge); g.strokePath(face, PathStrokeType(1.f));
    g.setColour(Colours::white.withAlpha(0.2f));
    g.drawLine(r.getX() + rad, r.getY() + 1.5f, r.getRight() - rad, r.getY() + 1.5f, 1.f);
}

// A pressed key: sunk into a well, with a faint yellow glow.
void drawPressed(Graphics& g, Rectangle<float> r, float rad = 7.f) {
    drawWell(g, r, rad, K::btnPressed);
    g.setGradientFill(ColourGradient(K::yellow.withAlpha(0.10f), r.getCentreX(), r.getCentreY(), K::yellow.withAlpha(0.f),
                                     r.getCentreX() + r.getWidth() * 0.6f, r.getCentreY(), true));
    g.fillRoundedRectangle(r, rad);
}

// ---------------------------------------------------------------- look and feel
class Look : public LookAndFeel_V4 {
public:
    Look() {
        setColour(Label::textColourId, K::muted);
        setColour(Label::textWhenEditingColourId, K::ink);
        setColour(Label::backgroundWhenEditingColourId, K::well);
        setColour(Label::outlineWhenEditingColourId, K::yellow);
        setColour(TextEditor::textColourId, K::ink);
        setColour(TextEditor::highlightColourId, K::yellow.withAlpha(0.3f));
        setColour(CaretComponent::caretColourId, K::yellow);
        setColour(TooltipWindow::backgroundColourId, K::floatBg);
        setColour(TooltipWindow::textColourId, K::ink);
        setColour(TooltipWindow::outlineColourId, K::floatEdge);
        setColour(HyperlinkButton::textColourId, K::label);
    }

    // Static parts of a dial (groove, body) are rendered once per size and pixel density.
    std::map<String, Image> cache;
    template <typename Fn> void drawCached(Graphics& g, const String& key, Rectangle<float> area, Fn&& draw) {
        const float sc = jlimit(1.f, 4.f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const String k = key + "|" + String(area.getWidth(), 1) + "x" + String(area.getHeight(), 1) + "@" + String(sc, 2);
        auto it = cache.find(k);
        if (it == cache.end()) {
            if (cache.size() > 64) cache.clear();
            Image img(Image::ARGB, jmax(1, roundToInt(area.getWidth() * sc)), jmax(1, roundToInt(area.getHeight() * sc)), true);
            { Graphics gi(img); gi.addTransform(AffineTransform::scale(sc)); draw(gi); }
            it = cache.emplace(k, img).first;
        }
        g.drawImage(it->second, area, RectanglePlacement::stretchToFit);
    }

    // A raised matte dial in a groove: contact shadow, a bevelled skirt lit from above, a dished
    // face, a yellow value ring (green to yellow for Threshold) and a yellow dot pointer.
    void drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s) override {
        const auto box = Rectangle<float>(float(x), float(y), float(w), float(h));
        const float lcx = float(w) * 0.5f, lcy = float(h) * 0.5f;
        const float cx = float(x) + lcx, cy = float(y) + lcy;
        const float ringR = float(jmin(w, h)) * 0.5f - 6.f, r = ringR - 7.f;
        const float ang = a0 + pos * (a1 - a0);
        const bool grad = s.getProperties().getWithDefault("role", "") == "thr";

        drawCached(g, "groove", box, [&](Graphics& k) {
            Path gr1; gr1.addCentredArc(lcx, lcy, ringR + 1.f, ringR + 1.f, 0.f, a0, a1, true);
            k.setColour(Colours::white.withAlpha(0.06f));
            k.strokePath(gr1, PathStrokeType(7.f, PathStrokeType::curved, PathStrokeType::rounded));
            Path gr2; gr2.addCentredArc(lcx, lcy, ringR, ringR, 0.f, a0, a1, true);
            k.setColour(Colours::black.withAlpha(0.75f));
            k.strokePath(gr2, PathStrokeType(6.f, PathStrokeType::curved, PathStrokeType::rounded));
        });
        if (pos > 0.003f) {
            Path val; val.addCentredArc(cx, cy, ringR, ringR, 0.f, a0, ang, true);
            if (grad) {
                ColourGradient cg(K::green, cx + ringR * std::sin(a0), cy - ringR * std::cos(a0), K::yellow, cx + ringR * std::sin(a1), cy - ringR * std::cos(a1), false);
                cg.addColour(0.55, K::thrMid);
                g.setGradientFill(cg);
            } else {
                g.setColour(K::yellow);
            }
            g.setOpacity(0.18f);
            g.strokePath(val, PathStrokeType(10.f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setOpacity(1.f);
            g.strokePath(val, PathStrokeType(4.f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        // live level marker on the ring (Target shows the output level, Threshold the input)
        const var live = s.getProperties()["live"];
        if (!live.isVoid()) {
            const double lv = double(live);
            if (lv >= s.getMinimum() && lv <= s.getMaximum()) {
                const float la = a0 + float((lv - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) * (a1 - a0);
                g.setColour(K::ink);
                g.fillEllipse(cx + ringR * std::sin(la) - 2.6f, cy - ringR * std::cos(la) - 2.6f, 5.2f, 5.2f);
            }
        }
        drawCached(g, "body", box, [&](Graphics& k) {
            Path skirt; skirt.addEllipse(lcx - r, lcy - r, r * 2.f, r * 2.f);
            DropShadow(Colours::black.withAlpha(0.8f), int(r * 0.6f), {0, int(r * 0.22f)}).drawForPath(k, skirt);
            ColourGradient sk(K::skirtTop, lcx, lcy - r, K::skirtBottom, lcx, lcy + r, false);
            sk.addColour(0.5, K::skirtMid);
            k.setGradientFill(sk); k.fillPath(skirt);
            const float fr = r * 0.86f;
            Path face; face.addEllipse(lcx - fr, lcy - fr, fr * 2.f, fr * 2.f);
            k.setGradientFill(ColourGradient(K::knobLo, lcx, lcy - fr, K::knobHi, lcx, lcy + fr, false));  // dished
            k.fillPath(face);
            k.setGradientFill(ColourGradient(Colours::white.withAlpha(0.10f), lcx - r * 0.28f, lcy - r * 0.42f,
                                             Colours::white.withAlpha(0.f), lcx - r * 0.28f + r * 0.75f, lcy - r * 0.42f, true));
            k.fillPath(face);
            k.setColour(Colours::black.withAlpha(0.7f)); k.strokePath(face, PathStrokeType(1.f));
            ColourGradient rim(Colours::white.withAlpha(0.30f), lcx, lcy - r, Colours::black.withAlpha(0.5f), lcx, lcy + r, false);
            rim.addColour(0.45, Colours::white.withAlpha(0.04f));
            Path rp; rp.addEllipse(lcx - r + 0.5f, lcy - r + 0.5f, r * 2.f - 1.f, r * 2.f - 1.f);
            k.setGradientFill(rim); k.strokePath(rp, PathStrokeType(1.2f));
        });
        if (s.isMouseOverOrDragging()) { g.setColour(Colours::white.withAlpha(0.12f)); g.drawEllipse(cx - r - 1.5f, cy - r - 1.5f, (r + 1.5f) * 2.f, (r + 1.5f) * 2.f, 1.f); }
        if (s.hasKeyboardFocus(false)) { g.setColour(K::yellow); g.drawEllipse(cx - ringR - 5.f, cy - ringR - 5.f, (ringR + 5.f) * 2.f, (ringR + 5.f) * 2.f, 1.5f); }
        // yellow dot pointer with a soft glow
        const float pr = r * 0.74f, px = cx + pr * std::sin(ang), py = cy - pr * std::cos(ang), dr = jmax(2.6f, r * 0.075f);
        g.setColour(K::yellow.withAlpha(0.35f)); g.fillEllipse(px - r * 0.14f, py - r * 0.14f, r * 0.28f, r * 0.28f);
        g.setColour(K::yellow); g.fillEllipse(px - dr, py - dr, dr * 2.f, dr * 2.f);
    }

    // Buttons come in three kinds (property "kind"):
    //   key     - a raised key that sinks into a well, text lit yellow, when on (default)
    //   segment - one side of the Ride / Hold switch: the selected side stands up as a key with
    //             yellow text; the other side is plain text in the slot
    //   flat    - header toggles: text or an icon that lights yellow when on
    void drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down) override {
        const String kind = b.getProperties().getWithDefault("kind", "key");
        const auto r = b.getLocalBounds().toFloat().reduced(1.f, 2.f);
        const bool on = b.getToggleState();
        if (kind == "key") {
            if (on || down) drawPressed(g, r); else drawKey(g, r, over && b.isEnabled());
        } else if (kind == "segment") {
            if (on) drawKey(g, r, over);
        }
        if (b.hasKeyboardFocus(false)) { g.setColour(K::yellow); g.drawRoundedRectangle(r.expanded(2.f), 8.f, 1.5f); }
    }

    void drawButtonText(Graphics& g, TextButton& b, bool over, bool) override {
        const String kind = b.getProperties().getWithDefault("kind", "key");
        const bool on = b.getToggleState();
        const bool lit = on || (kind == "key" && bool(b.getProperties().getWithDefault("accentText", false)));
        const Colour col = !b.isEnabled() ? K::muted
                         : lit ? K::yellow
                         : kind == "key" ? K::label
                         : over ? K::ink : K::muted;
        const auto r = b.getLocalBounds().toFloat().translated(0.f, on && kind == "key" ? 1.f : 0.f);
        const String icon = b.getProperties().getWithDefault("icon", "");
        const float size = float(b.getProperties().getWithDefault("fontSize", 12.5));
        const Font f = sans(size, true, float(b.getProperties().getWithDefault("track", 0.6)));
        g.setFont(f);
        if (icon.isEmpty()) { g.setColour(col); g.drawText(b.getButtonText(), r, Justification::centred); return; }
        const float is = jmin(16.f, r.getHeight() * 0.55f);
        if (b.getButtonText().isEmpty()) { drawIcon(g, icon, r.withSizeKeepingCentre(is, is), col); return; }
        const float tw = GlyphArrangement::getStringWidth(f, b.getButtonText());
        const float x0 = r.getCentreX() - (is + 7.f + tw) * 0.5f;
        drawIcon(g, icon, Rectangle<float>(x0, r.getCentreY() - is * 0.5f, is, is), col);
        g.setColour(col);
        g.drawText(b.getButtonText(), Rectangle<float>(x0 + is + 7.f, r.getY(), tw + 4.f, r.getHeight()), Justification::centredLeft);
    }
};

// ---------------------------------------------------------------- dial with its label and value
class Knob : public Component {
public:
    Knob(AudioProcessorValueTreeState& st, const char* id, String title_, std::function<String(double)> fmt, float radius, const char* role = "")
        : title(std::move(title_)), r(radius) {
        slider.getProperties().set("role", role);
        slider.setTitle(title);                // screen readers announce the control by name
        slider.setWantsKeyboardFocus(true);    // arrow keys adjust it once it is focused
        slider.setSliderStyle(Slider::RotaryVerticalDrag);
        slider.setRotaryParameters(kPi * 1.25f, kPi * 2.75f, true);
        slider.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        slider.setMouseDragSensitivity(220);
        addAndMakeVisible(slider);
        att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(st, id, slider);
        // after the attachment, which installs the parameter's own text functions
        slider.textFromValueFunction = fmt;
        slider.valueFromTextFunction = [](const String& s) { return s.retainCharacters("-+.0123456789").getDoubleValue(); };
        if (auto* p = st.getParameter(id)) slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
        // the value under the label; double-click it to type a value
        value.setJustificationType(Justification::centred);
        value.setFont(mono(12.f));
        value.setEditable(false, true, false);
        value.setTooltip("Double-click to type a value");
        value.onTextChange = [this] { slider.setValue(slider.valueFromTextFunction(value.getText()), sendNotificationSync); refresh(); };
        addAndMakeVisible(value);
        slider.onValueChange = [this] { refresh(); };
        refresh();
        setInterceptsMouseClicks(false, true);
    }
    int dialSize() const { return int(2.f * (r + 13.f)); }
    int preferredWidth() const { return jmax(120, dialSize() + 16); }
    int preferredHeight() const { return dialSize() + 38; }
    void placeCentredOn(Point<int> c) { setBounds(c.x - preferredWidth() / 2, c.y - dialSize() / 2, preferredWidth(), preferredHeight()); }
    void refresh() {
        if (!value.isBeingEdited()) value.setText(slider.getTextFromValue(slider.getValue()), dontSendNotification);
        value.setColour(Label::textColourId, slider.isMouseOverOrDragging() || value.isMouseOver() ? K::yellow : K::muted);
    }
    void setLive(float db) { slider.getProperties().set("live", db); slider.repaint(); }
    void focusOfChildComponentChanged(FocusChangeType) override { slider.repaint(); }
    void resized() override {
        const int d = dialSize();
        slider.setBounds((getWidth() - d) / 2, 0, d, d);
        value.setBounds(0, d + 17, getWidth(), 18);
    }
    void paint(Graphics& g) override {
        g.setFont(sans(r > 40.f ? 13.5f : 12.5f, true, 1.4f));
        g.setColour(K::label);
        g.drawText(title.toUpperCase(), Rectangle<int>(0, dialSize() + 1, getWidth(), 16), Justification::centred);
    }
    Slider slider;

private:
    String title;
    float r;
    Label value;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> att;
};

// ---------------------------------------------------------------- graph
float yOf(float v, float height) { return kTop + jlimit(0.f, kDbRange, -v) / kDbRange * (height - kBottomPad - kTop); }

class Graph : public Component {
public:
    explicit Graph(CorridorAudioProcessor& p) : proc(p) { hist.reserve(kHist); }

    std::vector<corridor::Snapshot> hist;
    bool analyzing = false;
    float analyzeSeconds = 0.f;
    float fadeTop = 1.0e6f;  // graph-local y where the controls begin

    float yOf(float v) const { return ::yOf(v, float(getHeight())); }
    float vOf(float y) const { return -jlimit(0.f, kDbRange, (y - kTop) / (float(getHeight()) - kBottomPad - kTop) * kDbRange); }
    float traceRight() const { return float(getWidth()) - 34.f; }

    void paint(Graphics& g) override {
        const float W = float(getWidth()), H = float(getHeight()), x1 = traceRight();
        g.setGradientFill(ColourGradient(K::graphTop, 0.f, 0.f, K::graphBottom, 0.f, H, false));
        g.fillAll();
        // grid every 5 dB, labelled every 10 at the right edge
        for (int v = 0; v >= -80; v -= 5) {
            const float y = std::round(yOf(float(v))) + 0.5f;
            g.setColour(Colours::white.withAlpha(v % 10 == 0 ? 0.075f : 0.035f));
            g.fillRect(0.f, y, x1, 1.f);
            if (v % 10 == 0) {
                g.setFont(mono(11.f)); g.setColour(K::muted);
                g.drawText(String(v), Rectangle<float>(x1, y - 7.f, W - x1 - 8.f, 14.f), Justification::centredRight);
            }
        }
        g.setColour(Colours::white.withAlpha(0.025f));
        for (int i = 1; i < 10; ++i) g.fillRect(x1 * float(i) / 10.f, kTop, 1.f, H - kBottomPad - kTop);

        // traces: input grey over a faint fill, output bold red
        const int n = int(hist.size());
        const float sx = x1 / float(kHist - 1);
        auto X = [&](int k) { return x1 - float(n - 1 - k) * sx; };
        if (n > 1) {
            Path fillP, inP, outP;
            fillP.startNewSubPath(X(0), H - kBottomPad);
            for (int k = 0; k < n; ++k) {
                const float yi = yOf(hist[size_t(k)].inRms), yo = yOf(hist[size_t(k)].outRms);
                fillP.lineTo(X(k), yi);
                if (k == 0) { inP.startNewSubPath(X(k), yi); outP.startNewSubPath(X(k), yo); }
                else { inP.lineTo(X(k), yi); outP.lineTo(X(k), yo); }
            }
            fillP.lineTo(X(n - 1), H - kBottomPad); fillP.closeSubPath();
            g.setGradientFill(ColourGradient(K::trace.withAlpha(0.16f), 0.f, kTop, K::trace.withAlpha(0.02f), 0.f, H, false));
            g.fillPath(fillP);
            g.setColour(K::trace.withAlpha(0.75f)); g.strokePath(inP, PathStrokeType(1.2f, PathStrokeType::curved));
            g.setColour(K::red); g.strokePath(outP, PathStrokeType(2.6f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        if (analyzing && n > 1) {
            const int span = jmin(n - 1, int(analyzeSeconds * 50.f));
            g.setColour(K::yellow.withAlpha(0.07f));
            g.fillRect(Rectangle<float>(X(n - 1 - span), kTop, X(n - 1) - X(n - 1 - span), H - kBottomPad - kTop));
        }
        // reference lines
        auto& st = proc.state();
        auto line = [&](const char* id, Colour col, float d1, float d2, float width) {
            const float y = std::round(yOf(st.getRawParameterValue(id)->load())) + 0.5f;
            const bool hot = hover == id || dragId == id;
            g.setColour(col);
            if (d1 <= 0.f) g.fillRect(0.f, y - (hot ? 1.2f : width * 0.5f), x1, hot ? 2.4f : width);
            else { const float dashes[] = {d1, d2}; g.drawDashedLine(Line<float>(0.f, y, x1, y), dashes, 2, hot ? 2.4f : width); }
        };
        line(ids::ceiling, K::ink.withAlpha(0.55f), 5.f, 4.f, 1.2f);
        line(ids::threshold, K::green, 7.f, 5.f, 1.2f);
        line(ids::target, K::yellow, 0.f, 0.f, 1.8f);
        // the controls sit on the graph: fade it out behind them
        if (fadeTop < H) {
            ColourGradient fade(K::graphBottom.withAlpha(0.f), 0.f, fadeTop, K::graphBottom.withAlpha(0.96f), 0.f, H, false);
            fade.addColour(0.45, K::graphBottom.withAlpha(0.82f));
            g.setGradientFill(fade); g.fillRect(0.f, fadeTop, W, H - fadeTop);
        }
        // legend
        g.setFont(sans(11.f, true, 1.2f));
        g.setColour(K::trace.withAlpha(0.8f)); g.fillRect(W - 344.f, 15.f, 16.f, 2.f);
        g.setColour(K::muted); g.drawText("INPUT", Rectangle<float>(W - 322.f, 8.f, 70.f, 16.f), Justification::centredLeft);
        g.setColour(K::red); g.fillRect(W - 262.f, 14.5f, 16.f, 3.f);
        g.setColour(K::muted); g.drawText("OUTPUT", Rectangle<float>(W - 240.f, 8.f, 80.f, 16.f), Justification::centredLeft);
        g.drawText("RMS, LAST 10 S", Rectangle<float>(W - 160.f, 8.f, 120.f, 16.f), Justification::centredRight);
        // a screen set into the panel: shadow falls in from the top and both sides
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.6f), 0.f, 0.f, Colours::transparentBlack, 0.f, 18.f, false));
        g.fillRect(0.f, 0.f, W, 18.f);
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.45f), 0.f, 0.f, Colours::transparentBlack, 12.f, 0.f, false));
        g.fillRect(0.f, 0.f, 12.f, H);
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.45f), W, 0.f, Colours::transparentBlack, W - 12.f, 0.f, false));
        g.fillRect(W - 12.f, 0.f, 12.f, H);
    }

    // Line labels, drawn above the controls layer. A label whose line runs under the controls is
    // pinned at the top of the fade with a small arrow pointing down at it.
    void paintTags(Graphics& g) {
        auto& st = proc.state();
        auto chip = [&](const char* id, const String& label, Colour col) {
            const float v = st.getRawParameterValue(id)->load();
            float y = std::round(yOf(v)) + 0.5f;
            const bool under = y > fadeTop + 6.f;
            if (under) y = fadeTop + 6.f;
            const String txt = label + "  " + fmtDb(v, 1);
            const Font f = sans(11.f, true, 0.8f);
            const float tw = GlyphArrangement::getStringWidth(f, txt) + (under ? 30.f : 20.f);
            const Rectangle<float> box(10.f, y - 10.f, tw, 20.f);
            g.setColour(K::tagBg); g.fillRoundedRectangle(box, 5.f);
            g.setColour(col); g.drawRoundedRectangle(box, 5.f, 1.f);
            g.setFont(f); g.drawText(txt, box.withTrimmedLeft(9.f), Justification::centredLeft);
            if (under) {
                Path a; const float ax = box.getRight() - 12.f, ay = box.getCentreY();
                a.addTriangle(ax - 4.f, ay - 2.f, ax + 4.f, ay - 2.f, ax, ay + 3.f);
                g.fillPath(a);
            }
        };
        chip(ids::ceiling, "CEILING", K::ink.withAlpha(0.7f));
        chip(ids::threshold, "THRESHOLD", K::green);
        chip(ids::target, "TARGET", K::yellow);
    }

    const char* hit(Point<float> p) const {
        if (p.x > traceRight() || p.y > fadeTop + 10.f) return nullptr;  // below that, the controls own the clicks
        const char* best = nullptr; float bd = 8.f;
        for (const char* id : {ids::target, ids::threshold, ids::ceiling}) {
            const float d = std::abs(yOf(proc.state().getRawParameterValue(id)->load()) - p.y);
            if (d < bd) { bd = d; best = id; }
        }
        return best;
    }
    void mouseMove(const MouseEvent& e) override {
        const char* h = hit(e.position);
        if (h != hover) { hover = h; repaint(); }
        setMouseCursor(h ? MouseCursor::UpDownResizeCursor : MouseCursor::NormalCursor);
    }
    void mouseExit(const MouseEvent&) override { hover = nullptr; repaint(); }
    void mouseDown(const MouseEvent& e) override {
        dragId = hit(e.position);
        if (dragId) if (auto* p = proc.state().getParameter(dragId)) { p->beginChangeGesture(); mouseDrag(e); }
    }
    void mouseDrag(const MouseEvent& e) override {
        if (!dragId) return;
        if (auto* p = proc.state().getParameter(dragId)) p->setValueNotifyingHost(p->convertTo0to1(vOf(e.position.y)));
        repaint();
    }
    void mouseUp(const MouseEvent&) override {
        if (dragId) if (auto* p = proc.state().getParameter(dragId)) p->endChangeGesture();
        dragId = nullptr;
    }

private:
    CorridorAudioProcessor& proc;
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
    Meter(bool isInput, corridor::Processor& d) : input(isInput), dsp(d) {}
    float peak = -120.f, hold = -120.f;
    void paint(Graphics& g) override {
        const float W = float(getWidth()), H = float(getHeight()), top = kTop, bot = H - kBottomPad;
        g.setGradientFill(ColourGradient(K::meterBgTop, 0.f, 0.f, K::meterBgBottom, 0.f, H, false));
        g.fillAll();
        g.setColour(K::seam); g.fillRect(input ? W - 1.f : 0.f, 0.f, 1.f, H);
        auto y = [&](float v) { return ::yOf(v, H); };
        const float bw = 7.f, gap = 3.f, bx = input ? W - 12.f - bw * 2.f - gap : 12.f;
        drawWell(g, Rectangle<float>(bx - 3.f, top - 3.f, bw * 2.f + gap + 6.f, bot - top + 6.f), 3.f, K::wellDeep);
        for (int ch = 0; ch < 2; ++ch) {
            const float cx = bx + float(ch) * (bw + gap);
            g.setColour(K::meterOff); g.fillRect(cx, top, bw, bot - top);
            if (peak > -kDbRange) {
                const float t = y(jmin(0.f, peak));
                ColourGradient cg(K::meterLow, 0.f, bot, K::red, 0.f, top, false);
                const double warn = 1.0 - 6.0 / kDbRange, clip = 1.0 - 1.0 / kDbRange;
                cg.addColour(warn - 0.001, K::meterLit); cg.addColour(warn, K::meterWarn);
                cg.addColour(clip, K::meterWarn); cg.addColour(clip + 0.001, K::red);
                g.setGradientFill(cg); g.fillRect(cx, t, bw, bot - t);
            }
            if (hold > -kDbRange) { g.setColour(K::yellow); g.fillRect(cx, y(jmin(0.f, hold)) - 1.f, bw, 2.f); }
            // rounded glass over each bar
            ColourGradient glass(Colours::white.withAlpha(0.16f), cx, 0.f, Colours::black.withAlpha(0.35f), cx + bw, 0.f, false);
            glass.addColour(0.45, Colours::white.withAlpha(0.03f));
            g.setGradientFill(glass); g.fillRect(cx, top, bw, bot - top);
        }
        // the same marks as the graph
        for (int v = 0; v >= -80; v -= 5) {
            const float yy = std::round(y(float(v))) + 0.5f;
            const bool major = v % 10 == 0;
            g.setColour(Colours::white.withAlpha(major ? 0.35f : 0.16f));
            g.fillRect(input ? bx - 7.f : bx + bw * 2.f + gap + 3.f, yy, major ? 4.f : 3.f, 1.f);
            if (!major) continue;
            g.setFont(mono(11.f)); g.setColour(K::muted);
            if (input) g.drawText(String(v), Rectangle<float>(0.f, yy - 7.f, bx - 10.f, 14.f), Justification::centredRight);
            else g.drawText(String(v), Rectangle<float>(bx + bw * 2.f + gap + 9.f, yy - 7.f, W, 14.f), Justification::centredLeft);
        }
        // peak readout and name
        const auto rb = Rectangle<float>(8.f, 3.f, W - 16.f, 18.f);
        drawWell(g, rb, 4.f, K::wellDeep);
        g.setFont(mono(11.5f));
        g.setColour(hold > -1.f ? K::red : hold > -6.f ? K::yellow : K::label);
        g.drawText(hold > -kDbRange ? fmtDb(hold, 1) : "-inf", rb, Justification::centred);
        g.setFont(sans(11.f, true, 1.4f)); g.setColour(K::muted);
        g.drawText(input ? "IN" : "OUT", Rectangle<float>(0.f, 25.f, W, 16.f), Justification::centred);
    }
    void mouseDown(const MouseEvent&) override { dsp.resetPeakHolds(); }

private:
    bool input;
    corridor::Processor& dsp;
};

// ---------------------------------------------------------------- deck: drawn above the graph, under the controls
class Deck : public Component {
public:
    Deck() { setInterceptsMouseClicks(false, false); }
    Rectangle<float> switchSlot, gainBox;
    Point<float> latencyAt;
    String latencyText, gainText = "+0.0";
    bool gainActive = false;
    void paint(Graphics& g) override {
        // the header casts a shadow on the graph
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.45f), 0.f, float(kHead), Colours::transparentBlack, 0.f, float(kHead) + 8.f, false));
        g.fillRect(0.f, float(kHead), float(getWidth()), 8.f);
        drawWell(g, switchSlot, 10.f);
        g.setFont(sans(11.f, true, 1.f)); g.setColour(K::muted);
        g.drawText(latencyText, Rectangle<float>(latencyAt.x, latencyAt.y - 8.f, 360.f, 16.f), Justification::centredLeft);
        drawWell(g, gainBox, 7.f, K::wellDeep);  // the screen's key number
        g.setFont(sans(11.f, true, 1.f)); g.setColour(K::muted);
        g.drawText("APPLIED GAIN", gainBox.reduced(12.f, 6.f).withHeight(14.f), Justification::centredLeft);
        g.setFont(mono(21.f)); g.setColour(gainActive ? K::yellow : K::ink);
        g.drawText(gainText + " dB", gainBox.reduced(12.f, 4.f).withTrimmedTop(18.f), Justification::centredRight);
    }
};

// ---------------------------------------------------------------- header status: a dot and a short sentence
class StatusLine : public Component, public SettableTooltipClient {
public:
    String text;
    Colour dot = K::muted;
    void paint(Graphics& g) override {
        const Font f = sans(13.5f);
        const float tw = jmin(float(getWidth()) - 16.f, GlyphArrangement::getStringWidth(f, text));
        const float x0 = (float(getWidth()) - tw - 14.f) * 0.5f, cy = float(getHeight()) * 0.5f;
        g.setColour(dot); g.fillEllipse(x0, cy - 3.5f, 7.f, 7.f);
        g.setFont(f); g.setColour(K::ink);
        g.drawText(text, Rectangle<float>(x0 + 14.f, 0.f, tw + 2.f, float(getHeight())), Justification::centredLeft, true);
    }
};

// ---------------------------------------------------------------- analysis panel (Hold mode)
class Drawer : public Component {
public:
    explicit Drawer(CorridorAudioProcessor& p) : proc(p) {
        analyze.getProperties().set("fontSize", 12.5);
        analyze.getProperties().set("track", 1.0);
        analyze.setTitle("Analyze");
        analyze.onClick = [this] {
            if (proc.core().telemetry().analyzing.load()) proc.core().requestAnalyzeLock();
            else proc.core().requestAnalyzeStart();
        };
        addAndMakeVisible(analyze);
        setTitle("Analyze and hold");
    }
    std::vector<corridor::Snapshot> wave;
    TextButton analyze{"ANALYZE"};

    void refresh() {
        const auto& t = proc.core().telemetry();
        const bool an = t.analyzing.load();
        analyze.setButtonText(an ? "LOCK" : t.holdValid.load() ? "ANALYZE AGAIN" : "ANALYZE");
        analyze.setToggleState(an, dontSendNotification);
        repaint();
    }
    void resized() override { analyze.setBounds(30, 40, 188, 34); }
    void paint(Graphics& g) override {
        const float W = float(getWidth()), H = float(getHeight());
        const auto panel = Rectangle<float>(14.f, 0.f, W - 28.f, H - 6.f);
        Path p; p.addRoundedRectangle(panel.getX(), panel.getY(), panel.getWidth(), panel.getHeight(), 10.f, 10.f, false, false, true, true);
        DropShadow(Colours::black.withAlpha(0.45f), 18, {0, 8}).drawForPath(g, p);
        g.setGradientFill(ColourGradient(K::drawerTop, 0.f, 0.f, K::drawerBottom, 0.f, H, false)); g.fillPath(p);
        g.setColour(K::frame); g.strokePath(p, PathStrokeType(1.f));
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.8f), 0.f, 0.f, Colours::transparentBlack, 0.f, 14.f, false));
        g.fillRect(panel.withHeight(14.f));  // the window's edge casts a shadow into the panel

        const auto& t = proc.core().telemetry();
        g.setFont(sans(13.f, true)); g.setColour(K::label);
        g.drawText("Analyze & hold", Rectangle<float>(30.f, 12.f, 190.f, 20.f), Justification::centredLeft);
        // readout
        StringArray lines; Array<bool> hot;
        if (t.analyzing.load()) {
            lines.add("Analyzing " + String(t.analyzeSeconds.load(), 1) + " s"); hot.add(true);
            lines.add("Above threshold " + String(t.analyzeActiveSeconds.load(), 1) + " s"); hot.add(false);
            lines.add("Locks by itself at 20 s"); hot.add(false);
        } else if (t.analyzeFailed.load()) {
            lines.add("Not enough sound above"); hot.add(false);
            lines.add("the threshold. Analyze again."); hot.add(false);
        } else if (t.holdValid.load()) {
            lines.add("Holding " + fmtDb(t.holdGain.load(), 1, true) + " dB"); hot.add(true);
            lines.add("Measured " + fmtDb(t.holdMeasured.load(), 1) + " dB RMS"); hot.add(false);
            lines.add("over " + String(t.holdSeconds.load(), 1) + " s"); hot.add(false);
        } else {
            lines.add("Press Analyze, then play"); hot.add(false);
            lines.add("a typical section."); hot.add(false);
        }
        g.setFont(mono(12.5f));
        for (int i = 0; i < lines.size(); ++i) {
            g.setColour(hot[i] ? K::yellow : K::muted);
            g.drawText(lines[i], Rectangle<float>(30.f, 84.f + float(i) * 18.f, 200.f, 18.f), Justification::centredLeft);
        }
        // waveform: input peaks above the centre line, RMS below; yellow where it is being measured
        const auto wr = Rectangle<float>(240.f, 12.f, W - 240.f - 30.f, H - 32.f);
        drawWell(g, wr, 6.f, K::wellDeep);
        const float mid = wr.getCentreY(), half = wr.getHeight() * 0.5f - 6.f;
        auto amp = [](float db) { return jlimit(0.f, 1.f, (db + 66.f) / 60.f); };  // -66 dB at the centre, -6 at the edge
        g.setColour(Colours::white.withAlpha(0.06f)); g.fillRect(wr.getX(), mid, wr.getWidth(), 1.f);
        const float thr = proc.state().getRawParameterValue(ids::threshold)->load();
        const float ta = amp(thr) * half;
        const float dashes[] = {4.f, 4.f};
        g.setColour(K::green.withAlpha(0.55f));
        g.drawDashedLine(Line<float>(wr.getX() + 2.f, mid - ta, wr.getRight() - 2.f, mid - ta), dashes, 2, 1.f);
        g.drawDashedLine(Line<float>(wr.getX() + 2.f, mid + ta, wr.getRight() - 2.f, mid + ta), dashes, 2, 1.f);
        const int n = int(wave.size());
        const float step = (wr.getWidth() - 4.f) / float(kWaveEntries);
        for (int i = 0; i < n; ++i) {
            const auto& s = wave[size_t(i)];
            const bool act = s.inRms >= thr;
            const Colour c = s.analyzing ? (act ? K::yellow : K::yellow.withAlpha(0.35f)) : K::trace.withAlpha(act ? 0.8f : 0.3f);
            const float x = wr.getRight() - 2.f - float(n - i) * step;
            const float up = amp(s.inPeak) * half, down = amp(s.inRms) * half;
            g.setColour(c); g.fillRect(x, mid - up, jmax(1.f, step * 0.8f), up + down);
        }
        g.setFont(sans(11.f, true, 1.f)); g.setColour(K::muted);
        g.drawText("INPUT, LAST 6 S   PEAK ABOVE, RMS BELOW, DASHED: THRESHOLD", wr.reduced(9.f, 6.f).withHeight(14.f), Justification::centredLeft);
    }

private:
    CorridorAudioProcessor& proc;
};

// ---------------------------------------------------------------- updates: About panel and prompt
// The About panel is the "Settings row" of the update-check spec: version, last result, one action.
class AboutPanel : public Component {
public:
    explicit AboutPanel(updates::Checker& c) : checker(c) {
        for (auto* b : {&action, &close}) { b->getProperties().set("fontSize", 12.5); b->getProperties().set("track", 0.3); addAndMakeVisible(*b); }
        close.getProperties().set("icon", "close");
        close.getProperties().set("kind", "flat");
        close.setTitle("Close");
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
        repaint();
    }
    void resized() override {
        close.setBounds(getWidth() - 40, 10, 30, 30);
        action.setBounds(20, getHeight() - 56, 190, 36);
        link.setButtonText("Release page");
        link.setBounds(226, getHeight() - 56, 120, 36);
    }
    void paint(Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1.f);
        Path p; p.addRoundedRectangle(r, 10.f);
        DropShadow(Colours::black.withAlpha(0.7f), 24, {0, 8}).drawForPath(g, p);  // it floats
        g.setColour(K::floatBg); g.fillPath(p);
        g.setColour(K::floatEdge); g.strokePath(p, PathStrokeType(1.f));
        drawIcon(g, "logo", Rectangle<float>(20.f, 20.f, 22.f, 20.f), K::yellow);
        g.setColour(K::ink); g.setFont(sans(16.f, true));
        g.drawText("Corridor Leveler " + updates::currentVersion(), Rectangle<float>(52.f, 16.f, r.getWidth() - 100.f, 28.f), Justification::centredLeft);
        g.setFont(sans(13.5f)); g.setColour(K::ink);
        g.drawFittedText(checker.status(), Rectangle<int>(20, 56, getWidth() - 40, 40), Justification::topLeft, 2);
        const String msg = checker.message();
        g.setColour(K::muted); g.setFont(sans(13.f));
        g.drawFittedText(msg.isNotEmpty() ? msg : "The check is a plain request to GitHub. Nothing about you or your computer is sent.",
                         Rectangle<int>(20, 98, getWidth() - 40, 36), Justification::topLeft, 2);
    }

private:
    updates::Checker& checker;
    TextButton action{"Check for updates"}, close{"x"};
    HyperlinkButton link{"Release page", URL(updates::kReleasesPage)};
};

// The prompt: once per start, Update and Ignore. Escape or a click outside closes it for this start.
class UpdatePrompt : public Component {
public:
    explicit UpdatePrompt(updates::Checker& c) : checker(c) {
        for (auto* b : {&update, &ignore}) { b->getProperties().set("fontSize", 13.5); b->getProperties().set("track", 0.3); addAndMakeVisible(*b); }
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
        g.fillAll(Colours::black.withAlpha(0.55f));
        const auto b = box().toFloat();
        Path p; p.addRoundedRectangle(b, 12.f);
        DropShadow(Colours::black.withAlpha(0.6f), 30, {0, 10}).drawForPath(g, p);
        g.setColour(K::floatBg); g.fillPath(p);
        g.setColour(K::floatEdge); g.strokePath(p, PathStrokeType(1.f));
        const auto r = checker.known();
        const String cur = updates::currentVersion();
        g.setColour(K::ink); g.setFont(sans(20.f, true));
        g.drawText("Corridor Leveler " + r.version + " is available", b.reduced(24.f).withHeight(30.f), Justification::centredLeft);
        g.setFont(sans(14.f)); g.setColour(K::label);
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
            drawWell(g, bar, 4.f);
            g.setColour(K::yellow); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * checker.progress()), 4.f);
        }
    }

private:
    updates::Checker& checker;
    TextButton update{"Update"}, ignore{"Ignore"};
};

}  // namespace

// ---------------------------------------------------------------- content (design size)
class CorridorContent : public Component, private ChangeListener {
public:
    explicit CorridorContent(CorridorAudioProcessor& p)
        : proc(p), graph(p), meterIn(true, p.core()), meterOut(false, p.core()), tags(graph), drawer(p),
          threshold(p.state(), ids::threshold, "Threshold", [](double v) { return fmtDb(float(v)) + " dB"; }, 34.f, "thr"),
          response(p.state(), ids::response, "Response", [](double v) { return String(v < 34 ? "Slow " : v > 66 ? "Fast " : "Medium ") + String(roundToInt(v)); }, 34.f),
          target(p.state(), ids::target, "Target", [](double v) { return fmtDb(float(v)) + " dB RMS"; }, 50.f, "tgt"),
          range(p.state(), ids::range, "Range", [](double v) { return kPlusMinus + fmtDb(float(v)) + " dB"; }, 34.f),
          ceiling(p.state(), ids::ceiling, "Ceiling", [](double v) { return fmtDb(float(v)) + " dBFS"; }, 34.f),
          about(*checker), prompt(*checker) {
        setLookAndFeel(&look);
        addAndMakeVisible(graph);
        addAndMakeVisible(meterIn); addAndMakeVisible(meterOut);
        addAndMakeVisible(deck);
        addAndMakeVisible(tags);
        for (auto* k : {&threshold, &response, &target, &range, &ceiling}) addAndMakeVisible(*k);
        addChildComponent(drawer);

        for (auto* b : {&ride, &hold}) {
            b->setClickingTogglesState(false);
            b->getProperties().set("kind", "segment");
            addAndMakeVisible(*b);
        }
        ride.setTitle("Ride: adjust the gain continuously");
        hold.setTitle("Hold: measure a section and lock one gain");
        ride.onClick = [this] { setMode(0); };
        hold.onClick = [this] { setMode(1); };

        for (auto* b : {&ease, &bypass}) {
            b->setClickingTogglesState(true);
            b->getProperties().set("kind", "flat");
            addAndMakeVisible(*b);
        }
        ease.getProperties().set("fontSize", 11.5);
        ease.getProperties().set("track", 1.3);
        ease.setTooltip("Off: the gain freezes below the threshold. On: after 2 s of silence it glides back to 0 dB.");
        bypass.getProperties().set("icon", "power");
        bypass.setButtonText({});
        bypass.setTitle("Bypass");
        bypass.setTooltip("Bypass (keeps the same latency, so nothing jumps)");
        easeAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::ease, ease);
        bypassAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::bypass, bypass);

        addAndMakeVisible(status);
        graph.setTitle("Level graph");
        graph.setDescription("Input and output level (RMS) over the last 10 seconds, with the target, threshold and ceiling lines. Drag a line to change it.");
        meterIn.setTitle("Input peak meter");
        meterOut.setTitle("Output peak meter");

        // updates: the version in the header opens the About panel; a known update gets its own key
        versionBtn.setButtonText("v" + updates::currentVersion());
        versionBtn.getProperties().set("fontSize", 11.5);
        versionBtn.getProperties().set("track", 0.3);
        versionBtn.setTitle("About Corridor Leveler and updates");
        versionBtn.onClick = [this] { about.refresh(); about.setVisible(!about.isVisible()); about.toFront(false); };
        addAndMakeVisible(versionBtn);
        updateBtn.getProperties().set("fontSize", 11.5);
        updateBtn.getProperties().set("track", 0.3);
        updateBtn.getProperties().set("icon", "download");
        updateBtn.getProperties().set("accentText", true);
        updateBtn.onClick = [this] { if (checker->known().valid()) checker->install(); };
        addChildComponent(updateBtn);
        addChildComponent(about);
        addChildComponent(prompt);
        checker->addChangeListener(this);
        checker->check(false);  // silent; at most one prompt per host session
        setSize(CorridorEditor::designW, CorridorEditor::designH);
    }
    ~CorridorContent() override { checker->removeChangeListener(this); setLookAndFeel(nullptr); }

    std::function<void(bool)> onDrawerChange;  // the editor grows by the panel's height
    bool drawerOpen() const { return drawer.isVisible(); }

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

    void setMode(int m) {
        if (auto* prm = proc.state().getParameter(ids::mode)) {
            prm->beginChangeGesture(); prm->setValueNotifyingHost(prm->convertTo0to1(float(m))); prm->endChangeGesture();
        }
    }

    void paint(Graphics& g) override {
        g.fillAll(K::page);
        // header plate
        g.setGradientFill(ColourGradient(K::chromeTop, 0.f, 0.f, K::chrome, 0.f, float(kHead), false));
        g.fillRect(0, 0, getWidth(), kHead);
        g.setColour(Colours::white.withAlpha(0.10f)); g.fillRect(0, 0, getWidth(), 1);
        g.setColour(K::seam); g.fillRect(0, kHead - 1, getWidth(), 1);
        drawIcon(g, "logo", Rectangle<float>(18.f, 10.f, 20.f, 20.f), K::yellow);
        g.setFont(sans(14.f, true, 3.f)); g.setColour(K::ink);
        g.drawText("CORRIDOR", Rectangle<int>(48, 0, 120, kHead), Justification::centredLeft);
        g.setFont(sans(12.f, false, 2.f)); g.setColour(K::muted);
        g.drawText("LEVELER", Rectangle<int>(150, 0, 100, kHead), Justification::centredLeft);
    }

    void resized() override {
        const int W = getWidth(), H = CorridorEditor::designH;
        // header, right to left: bypass, ease, version, update
        auto head = Rectangle<int>(0, 0, W, kHead).reduced(12, 6);
        bypass.setBounds(head.removeFromRight(40));
        head.removeFromRight(8);
        ease.setBounds(head.removeFromRight(150));
        head.removeFromRight(8);
        versionBtn.setBounds(head.removeFromRight(76));
        if (updateBtn.isVisible()) { head.removeFromRight(8); updateBtn.setBounds(head.removeFromRight(150)); }
        head.removeFromRight(8);
        head.removeFromLeft(250 - 12);
        status.setBounds(head);
        about.setBounds(W - 12 - 420, kHead + 8, 420, 190);
        prompt.setBounds(0, 0, W, H);

        // body: meters at the edges, the graph between them
        meterIn.setBounds(0, kHead, kMeterW, H - kHead);
        meterOut.setBounds(W - kMeterW, kHead, kMeterW, H - kHead);
        graph.setBounds(kMeterW, kHead, W - 2 * kMeterW, H - kHead);
        tags.setBounds(graph.getBounds());
        deck.setBounds(0, 0, W, H);

        // controls: Threshold, Response | Target with Ride / Hold under it | Range, Ceiling
        const int cx = W / 2, row = 492;
        threshold.placeCentredOn({cx - 270, row});
        response.placeCentredOn({cx - 160, row});
        target.placeCentredOn({cx, 466});
        range.placeCentredOn({cx + 160, row});
        ceiling.placeCentredOn({cx + 270, row});
        graph.fadeTop = float(threshold.getY() - 40 - graph.getY());
        const int by = 466 + 50 + 7 + 44, bw = 64, bh = 26;
        ride.setBounds(cx - bw, by, bw, bh);
        hold.setBounds(cx, by, bw, bh);
        deck.switchSlot = Rectangle<float>(float(cx - bw - 4), float(by - 4), float(bw * 2 + 8), float(bh + 8));
        deck.latencyAt = {float(kMeterW + 14), float(H - 22)};
        deck.gainBox = Rectangle<float>(float(W - kMeterW - 176), float(H - 64), 162.f, 52.f);

        drawer.setBounds(0, H, W, CorridorEditor::drawerH);
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
        for (auto* k : {&threshold, &response, &target, &range, &ceiling}) k->refresh();

        const bool holdMode = proc.state().getRawParameterValue(ids::mode)->load() > 0.5f;
        ride.setToggleState(!holdMode, dontSendNotification);
        hold.setToggleState(holdMode, dontSendNotification);
        if (holdMode != drawer.isVisible()) {
            drawer.setVisible(holdMode);
            if (onDrawerChange) onDrawerChange(holdMode);
        }
        if (holdMode) {
            const int w = jmin(n, kWaveEntries);
            drawer.wave.assign(scratch + (n - w), scratch + n);
            drawer.refresh();
        }

        const bool byp = proc.state().getRawParameterValue(ids::bypass)->load() > 0.5f;
        const float g = byp ? 0.f : t.gain.load();
        deck.gainText = fmtDb(g, 1, true);
        deck.gainActive = std::abs(g) >= 0.05f;
        const double fs = dsp.sampleRate();
        deck.latencyText = "LATENCY " + String(1000.0 * dsp.latencySamples() / fs, 1) + " ms  (" + String(dsp.latencySamples()) +
                           " samples at " + String(fs / 1000.0, 1) + " kHz)";
        status.text = shortStatus(byp, holdMode);
        status.dot = stateColour(byp, holdMode);
        const String longText = sentence(byp, holdMode);
        status.setTooltip(longText);
        status.setTitle("Status");
        status.setDescription(longText);  // screen readers read the full sentence

        graph.repaint(); tags.repaint(); meterIn.repaint(); meterOut.repaint(); deck.repaint(); status.repaint();
    }

    // The status dot: green on target, yellow while adjusting or holding, red when the ceiling or
    // range holds it back, grey when waiting or bypassed.
    Colour stateColour(bool byp, bool holdMode) const {
        if (byp) return K::muted;
        if (holdMode) return K::yellow;
        switch (corridor::State(proc.core().telemetry().state.load())) {
            case corridor::State::OnTarget: return K::green;
            case corridor::State::Boosting:
            case corridor::State::Cutting: return K::yellow;
            case corridor::State::Ceiling:
            case corridor::State::Range: return K::red;
            case corridor::State::Returning: return K::label;
            default: return K::muted;
        }
    }

    String shortStatus(bool byp, bool holdMode) const {
        const auto& t = proc.core().telemetry();
        const float g = t.gain.load();
        if (byp) return "Bypassed";
        if (holdMode) {
            if (t.analyzing.load()) return "Analyzing " + String(t.analyzeSeconds.load(), 1) + " s. Press Lock after a typical section";
            if (t.analyzeFailed.load()) return "Not enough sound above the threshold. Analyze again";
            if (t.holdValid.load()) return "Holding " + fmtDb(t.holdGain.load(), 1, true) + " dB";
            return "Hold: play a typical section, then press Analyze";
        }
        switch (corridor::State(t.state.load())) {
            case corridor::State::OnTarget: return "On target";
            case corridor::State::Boosting: return "Boosting (" + fmtDb(g, 1, true) + " dB)";
            case corridor::State::Cutting: return "Cutting (" + fmtDb(g, 1, true) + " dB)";
            case corridor::State::Ceiling: return "Held back by the ceiling";
            case corridor::State::Range: return "At the edge of the range";
            case corridor::State::Returning: return "Easing back to 0 dB";
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
            case corridor::State::Returning: return "Silence for over 2 s. Easing back to 0 dB (" + fmtDb(g, 1, true) + " dB).";
            default:
                return t.inRms.load() < thr ? "Below the " + fmtDb(thr, 1) + " dB threshold (silence, breaths or noise). Not adjusting; gain stays at " + fmtDb(g, 1, true) + " dB."
                                            : "Sound just started. Measuring it before acting.";
        }
    }

private:
    CorridorAudioProcessor& proc;
    Look look;
    Graph graph;
    Meter meterIn, meterOut;
    Deck deck;
    TagLayer tags;
    Drawer drawer;
    Knob threshold, response, target, range, ceiling;
    TextButton ride{"RIDE"}, hold{"HOLD"};
    TextButton ease{"EASE IN SILENCE"}, bypass{"Bypass"};
    TextButton versionBtn{"v0"}, updateBtn{"Update"};
    StatusLine status;
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
    width0 = jlimit(960, 2240, savedWidth);
    drawerOpen = p.state().getRawParameterValue(ids::mode)->load() > 0.5f;
    applyDrawer(drawerOpen);
    content->tick();  // fill the window before it is first shown
    startTimerHz(30);
}

CorridorEditor::~CorridorEditor() { stopTimer(); }

int CorridorEditor::contentHeight() const { return designH + (drawerOpen ? drawerH : 0); }

// Hold mode opens the analysis panel under the window: the window grows by the panel's height
// and keeps its width; the aspect ratio follows.
void CorridorEditor::applyDrawer(bool open) {
    drawerOpen = open;
    const double ratio = double(designW) / double(contentHeight());
    const int w = getWidth() > 0 ? getWidth() : width0;
    setResizeLimits(960, int(std::round(960 / ratio)), 2240, int(std::round(2240 / ratio)));  // below 960 px the smallest labels fall under ~10 px
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(ratio);
    setSize(w, int(std::round(w / ratio)));
    resized();
}

void CorridorEditor::paint(Graphics& g) { g.fillAll(Colour(0xff121214)); }

void CorridorEditor::resized() {
    const float s = float(getWidth()) / float(designW);
    content->setBounds(0, 0, designW, contentHeight());
    content->setTransform(AffineTransform::scale(s));
    if (getWidth() > 0) proc.editorWidth = getWidth();
}

void CorridorEditor::timerCallback() { content->tick(); }
