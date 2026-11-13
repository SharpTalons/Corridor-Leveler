// Loads the built plugin binaries the way a DAW does and checks them; also renders the plugin
// window to PNG files so the interface can be reviewed without a DAW.
//   corridor_plugin_check --vst3 <path> [--vst2 <path>] [--snapshots <dir>]
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../plugin/src/PluginEditor.h"
#include "../plugin/src/PluginProcessor.h"
#include "../plugin/src/Updates.h"

using namespace juce;

int checkVst2Binary(const char* path);  // tests/vst2_host.cpp (FST)

static void setenv_portable() {  // keep the window snapshots off the network
#ifdef _WIN32
    _putenv_s("CORRIDOR_NO_UPDATE_CHECK", "1");
#else
    setenv("CORRIDOR_NO_UPDATE_CHECK", "1", 1);
#endif
}

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("  FAIL %s  ", #c); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static float runSine(AudioProcessor& p, double sr, float levelDb, double seconds, double& phase, int block = 512) {
    AudioBuffer<float> buf(2, block);
    MidiBuffer midi;
    const float amp = std::pow(10.f, levelDb / 20.f) * std::sqrt(2.f);
    const int blocks = int(seconds * sr / block);
    double sum = 0; long cnt = 0;
    for (int b = 0; b < blocks; ++b) {
        for (int k = 0; k < block; ++k) {
            const float v = amp * float(std::sin(phase)); phase += 2 * MathConstants<double>::pi * 220.0 / sr;
            buf.setSample(0, k, v); buf.setSample(1, k, v);
        }
        p.processBlock(buf, midi);
        if (b >= blocks - int(sr / block)) for (int k = 0; k < block; ++k) { sum += double(buf.getSample(0, k)) * buf.getSample(0, k); ++cnt; }
    }
    return float(10 * std::log10(sum / double(cnt)));
}

static AudioProcessorParameter* findParam(AudioProcessor& p, const String& name) {
    for (auto* prm : p.getParameters()) if (prm->getName(64) == name) return prm;
    return nullptr;
}

static void checkBinary(AudioPluginFormatManager& fm, const String& path, const String& label) {
    std::printf("%s: %s\n", label.toRawUTF8(), path.toRawUTF8());
    OwnedArray<PluginDescription> descs;
    for (auto* f : fm.getFormats())
        if (f->fileMightContainThisPluginType(path)) f->findAllTypesForFile(descs, path);
    CHECK(descs.size() == 1, "found %d plugin(s)", descs.size());
    if (descs.isEmpty()) return;
    std::printf("  name '%s', format %s, category '%s'\n", descs[0]->name.toRawUTF8(), descs[0]->pluginFormatName.toRawUTF8(), descs[0]->category.toRawUTF8());
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        String err;
        auto inst = fm.createPluginInstance(*descs[0], sr, 512, err);
        CHECK(inst != nullptr, "could not load: %s", err.toRawUTF8());
        if (!inst) return;
        inst->setPlayConfigDetails(2, 2, sr, 512);
        inst->prepareToPlay(sr, 512);
        double ph = 0;
        const float out = runSine(*inst, sr, -32.f, 10.0, ph);
        const int lat = inst->getLatencySamples();
        std::printf("  %5.1f kHz: -32 dB RMS in -> %.2f dB RMS out, latency %d samples\n", sr / 1000, out, lat);
        CHECK(std::fabs(out + 20.f) < 1.0f, "leveling at %g", sr);
        CHECK(lat == int(std::lround(sr * 0.005)), "latency %d at %g", lat, sr);
        if (auto* byp = findParam(*inst, "Bypass")) {
            byp->setValueNotifyingHost(1.f);
            const float b = runSine(*inst, sr, -32.f, 2.0, ph);
            CHECK(std::fabs(b + 32.f) < 0.3f, "bypass level %.2f", b);
        } else CHECK(false, "no Bypass parameter");
        CHECK(inst->hasEditor(), "no editor");
        inst->releaseResources();
    }
}

static void checkUpdates() {
    std::printf("update check: version comparison and release parsing\n");
    using updates::isNewer;
    CHECK(isNewer("0.10.0", "0.9.9"), "0.10.0 > 0.9.9");
    CHECK(!isNewer("1.0.0", "1.0.0"), "equal is not newer");
    CHECK(isNewer("v0.1.23", "0.1.22"), "leading v");
    CHECK(!isNewer("0.1.22-beta", "0.1.22"), "suffix ignored");
    CHECK(!isNewer("0.1.21", "0.1.22"), "older is not newer");
    CHECK(isNewer("1", "0.9.9"), "short versions pad");
    const auto r = updates::parseLatest(R"({"tag_name":"v0.1.23","html_url":"https://github.com/o/r/releases/tag/v0.1.23",
        "assets":[{"name":"notes.txt","browser_download_url":"https://x/notes.txt","size":10},
                  {"name":"CorridorLeveler-0.1.23-Windows-Setup.exe","browser_download_url":"https://github.com/o/r/releases/download/v0.1.23/CorridorLeveler-0.1.23-Windows-Setup.exe","size":5425143}]})");
    CHECK(r.valid() && r.version == "0.1.23", "version '%s'", r.version.toRawUTF8());
    CHECK(r.asset.endsWith("CorridorLeveler-0.1.23-Windows-Setup.exe") && r.bytes == 5425143, "asset '%s'", r.asset.toRawUTF8());
    CHECK(!updates::parseLatest("{}").valid(), "no release");
    CHECK(!updates::parseLatest("not json").valid(), "bad json");
    const auto fb = updates::parseLatest(R"({"tag_name":"v0.2.5"})");
    CHECK(fb.asset.endsWith("/download/v0.2.5/CorridorLeveler-0.2.5-Windows-Setup.exe"), "fallback '%s'", fb.asset.toRawUTF8());
    std::printf("  current version %s\n", updates::currentVersion().toRawUTF8());
}

static void snapshots(const File& dir) {
    dir.createDirectory();
    CorridorAudioProcessor proc;
    proc.setPlayConfigDetails(2, 2, 48000, 512);
    proc.prepareToPlay(48000, 512);
    // a scripted session so the graph has history: quiet talk, silence, loud, normal
    double ph = 0;
    runSine(proc, 48000, -31.f, 3.0, ph);
    runSine(proc, 48000, -70.f, 1.5, ph);
    runSine(proc, 48000, -13.f, 2.5, ph);
    runSine(proc, 48000, -24.f, 3.0, ph);
    proc.tipDismissedThisRun = true;  // the screenshots show the window itself, not the first-run tip
    auto save = [&](const Image& img, const String& name) {
        const File f = dir.getChildFile(name);
        f.deleteFile();
        FileOutputStream os(f);
        PNGImageFormat().writeImageToStream(img, os);
        std::printf("  wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    };
    for (int w : {1120, 960, 480}) {
        proc.editorWidth = w;
        std::unique_ptr<AudioProcessorEditor> ed(proc.createEditor());
        static_cast<CorridorEditor*>(ed.get())->refreshNow();
        CHECK(ed->getWidth() == w, "editor width %d", ed->getWidth());
        CHECK(std::abs(ed->getHeight() - int(std::round(w * 630.0 / 1120.0))) <= 1, "editor height %d", ed->getHeight());
        CHECK(ed->isResizable(), "editor not resizable");
        const Image img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        if (w == 1120) {  // the update prompt, as a person would see it
            SharedResourcePointer<updates::Checker> chk;
            updates::Release r; r.version = "0.2.0"; r.bytes = 5435658; r.page = updates::kReleasesPage; r.asset = "https://example.invalid/x.exe";
            chk->setKnownForTest(r, true);
            MessageManager::getInstance()->runDispatchLoopUntil(200);
            save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), "window-update-prompt.png");
            chk->setKnownForTest({}, false);
            MessageManager::getInstance()->runDispatchLoopUntil(100);
        }
        save(img, "window-" + String(w) + ".png");
    }
    // Hold mode: the analysis panel opens under the window while a section is measured
    {
        proc.editorWidth = 1120;
        if (auto* m = proc.state().getParameter(ids::mode)) m->setValueNotifyingHost(1.f);
        std::unique_ptr<AudioProcessorEditor> ed(proc.createEditor());
        proc.core().requestAnalyzeStart();
        runSine(proc, 48000, -28.f, 2.0, ph);
        runSine(proc, 48000, -70.f, 0.6, ph);
        runSine(proc, 48000, -22.f, 1.4, ph);
        static_cast<CorridorEditor*>(ed.get())->refreshNow();
        const int h = int(std::round(1120.0 * (630 + CorridorEditor::drawerH) / 1120.0));
        CHECK(std::abs(ed->getHeight() - h) <= 1, "hold-mode editor height %d, expected %d", ed->getHeight(), h);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), "window-hold.png");
        if (auto* m = proc.state().getParameter(ids::mode)) m->setValueNotifyingHost(0.f);
    }
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // every line reaches the CI log at once, even if a step hangs
    ScopedJuceInitialiser_GUI init;
    setenv_portable();
    String vst3, vst2, snapDir;
    for (int i = 1; i + 1 < argc; i += 2) {
        const String k(argv[i]), v(argv[i + 1]);
        if (k == "--vst3") vst3 = v; else if (k == "--vst2") vst2 = v; else if (k == "--snapshots") snapDir = v;
    }
    AudioPluginFormatManager fm;
    fm.addFormat(std::make_unique<VST3PluginFormat>());
    if (vst3.isNotEmpty()) checkBinary(fm, vst3, "VST3");
    if (vst2.isNotEmpty()) failures += checkVst2Binary(vst2.toRawUTF8());
    checkUpdates();
    if (snapDir.isNotEmpty()) snapshots(File(snapDir));
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("plugin checks passed\n");
    return 0;
}
