// Corridor Leveler - plugin window.
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Layout (design size 1120 x 630): header strip; input peak meter on the left edge, output on
// the right; between them a scrolling RMS level graph on one even 0 to -70 dB scale, with a gain
// lane on top. The controls sit at the bottom, blended over the lower part of the graph.
#include "PluginEditor.h"

#include <cmath>
#include <vector>

using namespace juce;

namespace {

// ---------------------------------------------------------------- theme
struct Theme {
    Colour bg, surface, sunk, graph, ink, muted, line, accent, accentSoft, boost, cut, limit, warn, ok;
};
const Theme kLight{Colour(0xffffffff), Colour(0xffffffff), Colour(0xffebebeb), Colour(0xffffffff), Colour(0xff000000),
                   Colour(0xff5f6368), Colour(0xffdcdcdc), Colour(0xff17756a), Colour(0xffcfe5e1), Colour(0xff2672b5),
                   Colour(0xffc24d33), Colour(0xffc7302b), Colour(0xffb7862a), Colour(0xff2b7d47)};
const Theme kDark{Colour(0xff000000), Colour(0xff000000), Colour(0xff1c1c1c), Colour(0xff000000), Colour(0xffffffff),
                  Colour(0xffa3a3a3), Colour(0xff2e2e2e), Colour(0xff4cc2ae), Colour(0xff1f3b37), Colour(0xff62a9e8),
                  Colour(0xffee7b5f), Colour(0xfff0605a), Colour(0xffe0b354), Colour(0xff6cc58b)};

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

constexpr float kLane = 48.f, kTop = kLane + 22.f, kDbRange = 70.f;
constexpr int kHist = 500;  // 10 s at 20 ms per entry

// ---------------------------------------------------------------- look and feel
class Look : public LookAndFeel_V4 {
public:
    const Theme* t = &kDark;

    void drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s) override {
        const float size = float(jmin(w, h)) - 6.f;
        const float cx = float(x) + float(w) * 0.5f, cy = float(y) + float(h) * 0.5f, R = size * 0.5f;
        const float tw = jmax(5.f, R * 0.15f), rr = R - tw * 0.5f;
        const float ang = a0 + pos * (a1 - a0);

        Path track; track.addCentredArc(cx, cy, rr, rr, 0.f, a0, a1, true);
        g.setColour(t->sunk); g.strokePath(track, PathStrokeType(tw, PathStrokeType::curved, PathStrokeType::rounded));
        if (pos > 0.002f) {
            Path val; val.addCentredArc(cx, cy, rr, rr, 0.f, a0, ang, true);
            g.setColour(t->accent); g.strokePath(val, PathStrokeType(tw, PathStrokeType::curved, PathStrokeType::rounded));
        }
        const float br = R * 0.68f;
        Path body; body.addEllipse(cx - br, cy - br, br * 2.f, br * 2.f);
        DropShadow(Colours::black.withAlpha(0.32f), int(R * 0.35f), {0, int(R * 0.12f)}).drawForPath(g, body);
        g.setColour(t->surface); g.fillPath(body);
        g.setColour(s.isMouseOverOrDragging() ? t->muted : t->line); g.strokePath(body, PathStrokeType(1.4f));
        const float ir = br * 0.62f, dr = jmax(2.5f, R * 0.09f);
        g.setColour(t->ink);
        g.fillEllipse(cx + ir * std::sin(ang) - dr, cy - ir * std::cos(ang) - dr, dr * 2.f, dr * 2.f);

        // live level marker on the ring (Target shows the output level, Threshold the input)
        const var live = s.getProperties()["live"];
        if (!live.isVoid()) {
            const double lv = double(live);
            if (lv > s.getMinimum() - 15.0) {
                const float lp = float(jlimit(0.0, 1.0, (lv - s.getMinimum()) / (s.getMaximum() - s.getMinimum())));
                const float la = a0 + lp * (a1 - a0), lr = tw * 0.42f;
                const float lx = cx + rr * std::sin(la), ly = cy - rr * std::cos(la);
                g.setColour(t->bg); g.fillEllipse(lx - lr, ly - lr, lr * 2.f, lr * 2.f);
                g.setColour(t->ink); g.drawEllipse(lx - lr, ly - lr, lr * 2.f, lr * 2.f, 1.8f);
            }
        }
    }

    Label* createSliderTextBox(Slider& s) override {
        auto* l = LookAndFeel_V4::createSliderTextBox(s);
        l->setFont(mono(14.f, true));
        l->setJustificationType(Justification::centred);
        return l;
    }

    void drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1.f);
        const bool on = b.getToggleState();
        const float rad = b.getProperties().getWithDefault("pill", false) ? r.getHeight() * 0.5f : 8.f;
        if (on) { g.setColour(b.getProperties().getWithDefault("accentOn", false) ? t->accent : t->ink); g.fillRoundedRectangle(r, rad); }
        else {
            g.setColour(over || down ? t->sunk : t->surface); g.fillRoundedRectangle(r, rad);
            g.setColour(over ? t->muted : t->line); g.drawRoundedRectangle(r, rad, 1.f);
        }
    }

    void drawButtonText(Graphics& g, TextButton& b, bool, bool) override {
        const bool on = b.getToggleState();
        g.setColour(on ? t->bg : (b.isEnabled() ? t->ink : t->muted));
        g.setFont(sans(float(b.getProperties().getWithDefault("fontSize", 13.0)), true));
        g.drawText(b.getButtonText(), b.getLocalBounds(), Justification::centred);
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
    }
};

// ---------------------------------------------------------------- knob
class Knob : public Component {
public:
    Knob(AudioProcessorValueTreeState& st, const char* id, String title_, String unit, int dec, float dialSize)
        : title(std::move(title_)), dial(dialSize) {
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
        auto f = sans(12.5f, true); f.setExtraKerningFactor(0.12f);
        g.setFont(f);
        g.setColour(t.ink);
        g.drawText(title.toUpperCase(), getLocalBounds().removeFromBottom(18), Justification::centred);
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
        // grid + scale
        g.setFont(mono(10.5f));
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
            g.setColour(c.muted.withAlpha(0.1f)); g.fillPath(fillP);
            g.setColour(c.muted.withAlpha(0.9f)); g.strokePath(inP, PathStrokeType(1.2f, PathStrokeType::curved));
            g.setColour(c.accent); g.strokePath(outP, PathStrokeType(2.4f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        // analyze shading
        if (analyzing && n > 1) {
            const int span = jmin(n - 1, int(analyzeSeconds * 50.f));
            g.setColour(c.accent.withAlpha(0.08f));
            g.fillRect(Rectangle<float>(X(n - 1 - span), kTop, X(n - 1) - X(n - 1 - span), H - kTop));
        }
        // reference lines
        auto& st = proc.state();
        auto tag = [&](const char* id, const String& label, Colour col, float d1, float d2) {
            const float v = st.getRawParameterValue(id)->load();
            const float y = std::round(yOf(v)) + 0.5f;
            const bool hot = hover == id || dragId == id;
            if (d1 <= 0.f) { g.setColour(col); g.fillRect(0.f, y - (hot ? 1.f : 0.6f), x1, hot ? 2.2f : 1.3f); }
            else {
                const float dashes[] = {d1, d2};
                g.setColour(col); g.drawDashedLine(Line<float>(0.f, y, x1, y), dashes, 2, hot ? 2.2f : 1.3f);
            }
            const String txt = label + " " + fmtDb(v, 1);
            auto f = mono(10.5f, true); g.setFont(f);
            const float tw = GlyphArrangement::getStringWidth(f, txt) + 12.f;
            Rectangle<float> box(8.f, y - 9.f, tw, 18.f);
            g.setColour(hot ? col : c.graph.withAlpha(0.92f)); g.fillRect(box);
            g.setColour(col); g.drawRect(box, 1.f);
            g.setColour(hot ? c.graph : col); g.drawText(txt, box.withTrimmedLeft(6.f), Justification::centredLeft);
        };
        tag(ids::ceiling, "CEILING", c.limit, 2.f, 3.f);
        tag(ids::threshold, "THRESHOLD", c.muted, 6.f, 4.f);
        tag(ids::target, "TARGET", c.accent, 0.f, 0.f);

        // gain lane
        const float range = jmax(1.f, st.getRawParameterValue(ids::range)->load());
        const float mid = 8.f + kLane * 0.5f, half = kLane * 0.5f - 4.f;
        auto yG = [&](float v) { return mid - jlimit(-range, range, v) / range * half; };
        g.setColour(c.surface.withAlpha(0.75f)); g.fillRect(0.f, 8.f, x1, kLane);
        g.setColour(c.line); g.fillRect(0.f, std::round(mid), x1, 1.f);
        g.fillRect(0.f, std::round(yG(range)), x1, 1.f); g.fillRect(0.f, std::round(yG(-range)), x1, 1.f);
        if (n > 1) {
            for (int sign : {1, -1}) {
                Path a; a.startNewSubPath(X(0), mid);
                for (int k = 0; k < n; ++k) a.lineTo(X(k), yG(sign > 0 ? jmax(0.f, hist[size_t(k)].gain) : jmin(0.f, hist[size_t(k)].gain)));
                a.lineTo(X(n - 1), mid); a.closeSubPath();
                g.setColour((sign > 0 ? c.boost : c.cut).withAlpha(0.55f)); g.fillPath(a);
            }
            g.setColour(c.limit);
            for (int k = 0; k < n; ++k)
                if (hist[size_t(k)].limiterDb < -0.2f) g.fillRect(X(k) - sx * 0.5f, 8.f + kLane - 4.f, jmax(1.5f, sx), 4.f);
        }
        g.setFont(mono(10.5f, true)); g.setColour(c.muted);
        g.drawText("GAIN  +/-" + String(range, 0) + " dB", Rectangle<float>(10.f, 10.f, 200.f, 14.f), Justification::centredLeft);
        g.drawText("+" + String(range, 0), Rectangle<float>(x1, yG(range) - 6.f, scaleW - 5.f, 12.f), Justification::centredRight);
        g.drawText("0", Rectangle<float>(x1, mid - 6.f, scaleW - 5.f, 12.f), Justification::centredRight);
        g.drawText("-" + String(range, 0), Rectangle<float>(x1, yG(-range) - 6.f, scaleW - 5.f, 12.f), Justification::centredRight);
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

// ---------------------------------------------------------------- peak meter (dBFS)
class Meter : public Component {
public:
    Meter(bool isInput, const Theme*& th, corridor::Processor& d) : input(isInput), t(th), dsp(d) {}
    float peak = -120.f, hold = -120.f;
    void paint(Graphics& g) override {
        const Theme& c = *t;
        const float W = float(getWidth()), H = float(getHeight()), top = kTop, bot = H - 6.f;
        g.fillAll(c.surface);
        g.setColour(c.line); g.fillRect(input ? W - 1.f : 0.f, 0.f, 1.f, H);
        auto y = [&](float v) { return top + jlimit(0.f, kDbRange, -v) / kDbRange * (bot - top); };
        const float bw = 12.f, bx = input ? W - bw - 6.f : 6.f;
        g.setColour(c.sunk); g.fillRect(bx, top, bw, bot - top);
        auto seg = [&](float lo, float hi, Colour col) {
            if (peak <= lo) return;
            const float ya = y(jmin(peak, hi)), yb = y(lo);
            g.setColour(col); g.fillRect(bx, ya, bw, yb - ya);
        };
        seg(-70.f, -18.f, c.accent); seg(-18.f, -6.f, c.warn); seg(-6.f, 6.f, c.limit);
        if (hold > -70.f) { g.setColour(hold > -0.1f ? c.limit : c.ink); g.fillRect(bx, y(jmin(hold, 0.f)) - 1.f, bw, 2.f); }
        g.setFont(mono(10.f)); g.setColour(c.muted);
        for (int v = 0; v >= -60; v -= 12)
            g.drawText(v == 0 ? "0" : String(v), Rectangle<float>(input ? 0.f : bx + bw + 3.f, y(float(v)) - 6.f, input ? bx - 4.f : W - bx - bw - 3.f, 12.f),
                       input ? Justification::centredRight : Justification::centredLeft);
        g.setFont(mono(11.f, true)); g.setColour(hold > -0.1f ? c.limit : c.ink);
        g.drawText(hold > -70.f ? fmtDb(hold, 1) : "-", Rectangle<float>(0.f, 10.f, W, 16.f), Justification::centred);
        g.setFont(mono(10.f)); g.setColour(c.muted);
        g.drawText(input ? "IN" : "OUT", Rectangle<float>(0.f, 28.f, W, 14.f), Justification::centred);
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
    explicit Deck(const Theme*& th) : t(th) { setInterceptsMouseClicks(false, true); }
    String gainText = "+0.0";
    int gainSign = 0;
    String latencyText;
    Rectangle<int> gainBox;
    void paint(Graphics& g) override {
        const Theme& c = *t;
        const float H = float(getHeight());
        ColourGradient grad(c.graph.withAlpha(0.f), 0.f, 0.f, c.graph.withAlpha(0.78f), 0.f, H, false);
        grad.addColour(0.3, c.graph.withAlpha(0.55f));
        g.setGradientFill(grad); g.fillRect(getLocalBounds());
        // applied gain
        g.setColour(gainSign > 0 ? c.boost : gainSign < 0 ? c.cut : c.ink);
        g.setFont(sans(32.f, true));
        g.drawText(gainText, gainBox.withHeight(36), Justification::centred);
        auto f = sans(11.5f, true); f.setExtraKerningFactor(0.12f); g.setFont(f); g.setColour(c.ink);
        g.drawText("GAIN  DB", gainBox.withTrimmedTop(38).withHeight(16), Justification::centred);
        // latency, bottom-right corner
        g.setFont(mono(10.5f)); g.setColour(c.muted);
        g.drawText(latencyText, getLocalBounds().removeFromBottom(22).reduced(12, 0), Justification::centredRight);
    }

private:
    const Theme*& t;
};

}  // namespace

// ---------------------------------------------------------------- content (design size)
class CorridorContent : public Component {
public:
    explicit CorridorContent(CorridorAudioProcessor& p)
        : proc(p),
          graph(p, theme), meterIn(true, theme, p.core()), meterOut(false, theme, p.core()), deck(theme),
          threshold(p.state(), ids::threshold, "Threshold", "dB", 1, 72.f),
          target(p.state(), ids::target, "Target", "dB RMS", 1, 96.f),
          response(p.state(), ids::response, "Response", "", 0, 72.f),
          range(p.state(), ids::range, "Range", "dB", 1, 72.f),
          ceiling(p.state(), ids::ceiling, "Ceiling", "dBFS", 1, 54.f) {
        theme = p.darkTheme ? &kDark : &kLight;
        look.t = theme; look.apply();
        setLookAndFeel(&look);

        addAndMakeVisible(meterIn); addAndMakeVisible(meterOut);
        addAndMakeVisible(graph);
        addAndMakeVisible(deck);
        for (auto* k : {&threshold, &target, &response, &range, &ceiling}) deck.addAndMakeVisible(*k);

        for (auto* b : {&ride, &hold}) {
            b->setClickingTogglesState(false);
            b->getProperties().set("pill", true); b->getProperties().set("fontSize", 13.0);
            deck.addAndMakeVisible(*b);
        }
        ride.onClick = [this] { setMode(0); };
        hold.onClick = [this] { setMode(1); };
        analyze.getProperties().set("fontSize", 14.0);
        analyze.onClick = [this] {
            if (proc.core().telemetry().analyzing.load()) proc.core().requestAnalyzeLock();
            else proc.core().requestAnalyzeStart();
        };
        deck.addChildComponent(analyze);
        status.setJustificationType(Justification::centred);
        status.setFont(sans(13.5f));
        status.setInterceptsMouseClicks(false, false);
        deck.addAndMakeVisible(status);

        for (auto* b : {&ease, &bypass, &themeBtn}) {
            b->setClickingTogglesState(true);
            b->getProperties().set("fontSize", 12.5);
            addAndMakeVisible(*b);
        }
        ease.getProperties().set("accentOn", true);
        easeAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::ease, ease);
        bypassAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::bypass, bypass);
        ease.setTooltip("Off: the gain freezes below the threshold. On: after 2 s of silence it glides back to 0 dB.");
        themeBtn.setToggleState(p.darkTheme, dontSendNotification);
        themeBtn.onClick = [this] { applyTheme(themeBtn.getToggleState()); };
        setSize(CorridorEditor::designW, CorridorEditor::designH);
        sendLookAndFeelChange();  // children were added after the look was set: let them pick up its colours
    }
    ~CorridorContent() override { setLookAndFeel(nullptr); }

    void applyTheme(bool dark) {
        proc.darkTheme = dark;
        theme = dark ? &kDark : &kLight;
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
        auto f = sans(16.f, true); f.setExtraKerningFactor(0.18f);
        g.setFont(f); g.setColour(c.ink);
        g.drawText("CORRIDOR", Rectangle<int>(16, 0, 240, 46), Justification::centredLeft);
        g.setFont(mono(11.f)); g.setColour(c.muted);
        g.drawText("LEVELER", Rectangle<int>(142, 0, 120, 46), Justification::centredLeft);
        g.setColour(c.line); g.fillRect(0, 46, getWidth(), 1);
    }

    void resized() override {
        // header
        auto head = Rectangle<int>(0, 0, getWidth(), 46).reduced(12, 8);
        themeBtn.setBounds(head.removeFromRight(100));
        head.removeFromRight(8);
        bypass.setBounds(head.removeFromRight(84));
        head.removeFromRight(8);
        ease.setBounds(head.removeFromRight(150));
        // body
        auto body = Rectangle<int>(0, 47, getWidth(), getHeight() - 47);
        meterIn.setBounds(body.removeFromLeft(52));
        meterOut.setBounds(body.removeFromRight(52));
        graph.setBounds(body);
        const int deckH = 186;
        deck.setBounds(body.getX(), body.getBottom() - deckH, body.getWidth(), deckH);

        // deck: knobs and mode controls centred as one row
        const int gap = 28, centerW = 160, gainW = 110;
        Knob* left[] = {&threshold, &target};
        Knob* right[] = {&response, &range, &ceiling};
        int total = centerW + gainW + gap * 6;
        for (auto* k : left) total += k->preferredWidth();
        for (auto* k : right) total += k->preferredWidth();
        int x = (deck.getWidth() - total) / 2;
        const int rowBottom = deckH - 30;
        auto place = [&](Knob* k) { k->setBounds(x, rowBottom - k->preferredHeight(), k->preferredWidth(), k->preferredHeight()); x += k->preferredWidth() + gap; };
        for (auto* k : left) place(k);
        ride.setBounds(x, rowBottom - 92, centerW / 2, 34);
        hold.setBounds(x + centerW / 2, rowBottom - 92, centerW / 2, 34);
        analyze.setBounds(x, rowBottom - 48, centerW, 32);
        x += centerW + gap;
        for (auto* k : right) place(k);
        deck.gainBox = Rectangle<int>(x, rowBottom - 70, gainW, 56);
        status.setBounds(0, deckH - 26, deck.getWidth(), 22);
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
        analyze.setButtonText(t.analyzing.load() ? "LOCK" : t.holdValid.load() ? "ANALYZE AGAIN" : "ANALYZE");
        analyze.setToggleState(t.analyzing.load(), dontSendNotification);
        analyze.getProperties().set("accentOn", true);

        const bool byp = proc.state().getRawParameterValue(ids::bypass)->load() > 0.5f;
        const float g = byp ? 0.f : t.gain.load();
        deck.gainText = fmtDb(g, 1, true);
        deck.gainSign = g > 0.05f ? 1 : g < -0.05f ? -1 : 0;
        const double fs = dsp.sampleRate();
        deck.latencyText = "Latency " + String(1000.0 * dsp.latencySamples() / fs, 1) + " ms  (" + String(dsp.latencySamples()) +
                           " samples @ " + String(fs / 1000.0, 1) + " kHz)";
        status.setText(sentence(byp, holdMode), dontSendNotification);
        status.setColour(Label::textColourId, theme->ink);

        graph.repaint(); meterIn.repaint(); meterOut.repaint(); deck.repaint();
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
    TextButton ride{"RIDE"}, hold{"HOLD"}, analyze{"ANALYZE"};
    TextButton ease{"Ease in silence"}, bypass{"Bypass"}, themeBtn{"Dark theme"};
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
