// Loads the built plugin binaries the way a DAW does and checks them; also renders the plugin
// window to PNG files so the interface can be reviewed without a DAW.
//   corridor_plugin_check --vst3 <path> [--vst2 <path>] [--snapshots <dir>]
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>

#include "../plugin/src/PluginEditor.h"
#include "../plugin/src/PluginProcessor.h"

using namespace juce;

int checkVst2Binary(const char* path);  // tests/vst2_host.cpp (FST)

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
    for (bool dark : {true, false}) {
        proc.darkTheme = dark;
        for (int w : {1120, 840}) {
            proc.editorWidth = w;
            std::unique_ptr<AudioProcessorEditor> ed(proc.createEditor());
            static_cast<CorridorEditor*>(ed.get())->refreshNow();
            CHECK(ed->getWidth() == w, "editor width %d", ed->getWidth());
            CHECK(std::abs(ed->getHeight() - int(std::round(w * 630.0 / 1120.0))) <= 1, "editor height %d", ed->getHeight());
            CHECK(ed->isResizable(), "editor not resizable");
            const Image img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            const File f = dir.getChildFile(String("window-") + (dark ? "dark" : "light") + "-" + String(w) + ".png");
            f.deleteFile();
            FileOutputStream os(f);
            PNGImageFormat().writeImageToStream(img, os);
            std::printf("  wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
        }
    }
}

int main(int argc, char** argv) {
    ScopedJuceInitialiser_GUI init;
    String vst3, vst2, snapDir;
    for (int i = 1; i + 1 < argc; i += 2) {
        const String k(argv[i]), v(argv[i + 1]);
        if (k == "--vst3") vst3 = v; else if (k == "--vst2") vst2 = v; else if (k == "--snapshots") snapDir = v;
    }
    AudioPluginFormatManager fm;
    fm.addFormat(std::make_unique<VST3PluginFormat>());
    if (vst3.isNotEmpty()) checkBinary(fm, vst3, "VST3");
    if (vst2.isNotEmpty()) failures += checkVst2Binary(vst2.toRawUTF8());
    if (snapDir.isNotEmpty()) snapshots(File(snapDir));
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("plugin checks passed\n");
    return 0;
}
