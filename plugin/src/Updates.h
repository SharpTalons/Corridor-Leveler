// Corridor Leveler - in-app update check.
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Follows the update-check spec in fubulatu-lang/Design-Constants, adapted to a Windows plugin:
// - asks this project's public GitHub releases for the newest version (no token, no identifier:
//   a plain GET with a User-Agent naming the app);
// - compares MAJOR.MINOR.PATCH numerically;
// - offers a newer version once per start (per host process), with Update and Ignore;
//   Ignore is remembered per version, a manual check always asks;
// - offline, falls back to the last answer saved;
// - downloads the installer over HTTPS, size-capped, with progress, then starts it.
#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <mutex>

namespace updates {

inline constexpr const char* kLatestUrl = "https://api.github.com/repos/fubulatu-lang/obs-dynamic-range/releases/latest";
inline constexpr const char* kReleasesPage = "https://github.com/fubulatu-lang/obs-dynamic-range/releases";
inline constexpr juce::int64 kMaxBytes = 200LL * 1024 * 1024;

struct Release {
    juce::String version;   // without the leading "v"
    juce::String page;      // release page
    juce::String asset;     // installer download URL (https)
    juce::int64 bytes = 0;  // installer size
    bool valid() const { return version.isNotEmpty(); }
};

// Pure helpers (tested in tests/plugin_check.cpp).
bool isNewer(const juce::String& latest, const juce::String& current);
Release parseLatest(const juce::String& json);  // invalid Release when no app release is found
juce::String currentVersion();

// One per process, shared by every plugin window (juce::SharedResourcePointer<Checker>).
class Checker : public juce::ChangeBroadcaster, private juce::Thread {
public:
    Checker();
    ~Checker() override;

    enum class Phase { Idle, Checking, Downloading };

    // Starts a background check. manual = the person pressed "Check for updates".
    void check(bool manual);
    // Downloads the known update and starts its installer.
    void install();
    void ignoreKnown();
    void dismissPrompt() { prompt = false; sendChangeMessage(); }
    // Test hook: pretend a check found this release (used by the window snapshots).
    void setKnownForTest(const Release& r, bool showPrompt) {
        { std::lock_guard<std::mutex> g(lock); update = r; statusText = "Corridor Leveler " + r.version + " is available. You have " + currentVersion() + "."; }
        prompt = showPrompt;
        sendChangeMessage();
    }

    // State, read on the message thread after a change message.
    Phase phase() const { return phaseNow.load(); }
    float progress() const { return progressNow.load(); }  // 0..1 while downloading
    bool promptVisible() const { return prompt; }
    Release known() const;            // newer release, or invalid
    juce::String status() const;      // one sentence for the About panel
    juce::String message() const;     // short result of the last action, may be empty

private:
    void run() override;
    void doCheck(bool manual);
    void doInstall();
    void finishCheck(const Release& r, bool reached, bool manual);
    void maybePrompt();
    juce::PropertiesFile& props();

    std::unique_ptr<juce::PropertiesFile> store;
    mutable std::mutex lock;
    Release update;
    juce::String statusText, messageText;
    std::atomic<bool> prompt{false};
    std::atomic<Phase> phaseNow{Phase::Idle};
    std::atomic<float> progressNow{0.f};
    std::atomic<int> job{0};  // 1 = auto check, 2 = manual check, 3 = install
    static std::atomic<bool> promptedThisRun;
};

}  // namespace updates
