// Corridor Leveler - in-app update check.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Updates.h"

namespace updates {

using namespace juce;

std::atomic<bool> Checker::promptedThisRun{false};

namespace {

String stripVersion(String v) {
    v = v.trim();
    if (v.startsWithIgnoreCase("v")) v = v.substring(1);
    const int dash = v.indexOfChar('-');
    return dash >= 0 ? v.substring(0, dash) : v;
}

const char* kAgentHeaders = "Accept: application/vnd.github+json\r\nUser-Agent: CorridorLeveler";

// Opens an HTTPS stream, following at most five redirects by hand so none can switch to HTTP.
std::unique_ptr<WebInputStream> openHttps(String address, int timeoutMs, int& status, const Thread* owner) {
    for (int hop = 0; hop <= 5; ++hop) {
        if (!address.startsWithIgnoreCase("https://")) return nullptr;
        auto s = std::make_unique<WebInputStream>(URL(address), false);
        s->withExtraHeaders(kAgentHeaders).withConnectionTimeout(timeoutMs).withNumRedirectsToFollow(0);
        if (owner != nullptr && owner->threadShouldExit()) return nullptr;
        if (!s->connect(nullptr)) { status = s->getStatusCode(); return nullptr; }
        status = s->getStatusCode();
        if (status >= 300 && status < 400) {
            const auto headers = s->getResponseHeaders();
            String next;
            for (const auto& key : headers.getAllKeys())
                if (key.equalsIgnoreCase("location")) next = headers[key];
            if (next.isEmpty()) return nullptr;
            address = next.trim();
            continue;
        }
        return status == 200 ? std::move(s) : nullptr;
    }
    return nullptr;
}

}  // namespace

bool isNewer(const String& latest, const String& current) {
    auto parts = [](const String& v) {
        StringArray p = StringArray::fromTokens(stripVersion(v), ".", "");
        while (p.size() < 3) p.add("0");
        int out[3];
        for (int i = 0; i < 3; ++i) out[i] = p[i].containsOnly("0123456789") ? p[i].getIntValue() : 0;
        return std::array<int, 3>{out[0], out[1], out[2]};
    };
    const auto a = parts(latest), b = parts(current);
    for (int i = 0; i < 3; ++i)
        if (a[size_t(i)] != b[size_t(i)]) return a[size_t(i)] > b[size_t(i)];
    return false;
}

Release parseLatest(const String& json) {
    Release r;
    const var v = JSON::parse(json);
    const String tag = v["tag_name"].toString();
    if (tag.isEmpty()) return r;
    r.version = stripVersion(tag);
    r.page = v["html_url"].toString();
    if (const auto* assets = v["assets"].getArray())
        for (const auto& a : *assets)
            if (a["name"].toString().endsWithIgnoreCase(".exe")) {
                r.asset = a["browser_download_url"].toString();
                r.bytes = int64(a["size"]);
                break;
            }
    if (r.asset.isEmpty())  // the CI naming, if the asset list is missing
        r.asset = String(kReleasesPage) + "/download/v" + r.version + "/CorridorLeveler-" + r.version + "-Windows-Setup.exe";
    if (r.page.isEmpty()) r.page = kReleasesPage;
    return r;
}

String currentVersion() {
#ifdef CORRIDOR_VERSION_STRING
    return CORRIDOR_VERSION_STRING;
#else
    return "0.0.0";
#endif
}

// ---------------------------------------------------------------- checker
Checker::Checker() : Thread("Corridor update check") {
    statusText = "New versions are checked on GitHub when the plugin window opens. Nothing about you or your computer is sent.";
}

Checker::~Checker() {
    removeAllChangeListeners();
    stopThread(10000);
}

PropertiesFile& Checker::props() {
    if (!store) {
        PropertiesFile::Options o;
        o.applicationName = "updates";
        o.filenameSuffix = "settings";
        o.folderName = "CorridorLeveler";
        o.osxLibrarySubFolder = "Application Support";
        store = std::make_unique<PropertiesFile>(o);
    }
    return *store;
}

Release Checker::known() const { std::lock_guard<std::mutex> g(lock); return update; }
String Checker::status() const { std::lock_guard<std::mutex> g(lock); return statusText; }
String Checker::message() const { std::lock_guard<std::mutex> g(lock); return messageText; }

void Checker::check(bool manual) {
    if (isThreadRunning()) return;
    if (!manual && SystemStats::getEnvironmentVariable("CORRIDOR_NO_UPDATE_CHECK", {}).isNotEmpty()) return;
    job = manual ? 2 : 1;
    phaseNow = Phase::Checking;
    { std::lock_guard<std::mutex> g(lock); messageText = {}; }
    sendChangeMessage();
    startThread();
}

void Checker::install() {
    if (isThreadRunning() || !known().valid()) return;
    job = 3;
    phaseNow = Phase::Downloading;
    progressNow = 0.f;
    { std::lock_guard<std::mutex> g(lock); messageText = {}; }
    sendChangeMessage();
    startThread();
}

void Checker::ignoreKnown() {
    const Release r = known();
    if (r.valid()) { std::lock_guard<std::mutex> g(lock); props().setValue("updateIgnored", r.version); props().saveIfNeeded(); }
    prompt = false;
    sendChangeMessage();
}

void Checker::run() {
    const int j = job.exchange(0);
    if (j == 1 || j == 2) doCheck(j == 2);
    else if (j == 3) doInstall();
    phaseNow = Phase::Idle;
    sendChangeMessage();
}

void Checker::doCheck(bool manual) {
    int status = 0;
    Release r;
    bool reached = false;
    if (auto s = openHttps(kLatestUrl, 8000, status, this)) {
        reached = true;
        r = parseLatest(s->readEntireStreamAsString());
    } else if (status == 404) {
        reached = true;  // no release published yet
    }
    finishCheck(r, reached, manual);
}

void Checker::finishCheck(const Release& r, bool reached, bool manual) {
    const String cur = currentVersion();
    std::lock_guard<std::mutex> g(lock);
    auto& p = props();
    if (!reached) {
        statusText = "Couldn't reach GitHub. Check your connection and try again.";
        if (manual) messageText = "Couldn't check for updates.";
        else {
            Release saved;
            saved.version = p.getValue("updateFound");
            saved.page = p.getValue("updatePage");
            saved.asset = p.getValue("updateAsset");
            saved.bytes = p.getValue("updateBytes").getLargeIntValue();
            if (saved.valid() && isNewer(saved.version, cur)) { update = saved; maybePrompt(); }
        }
        return;
    }
    if (r.valid()) {
        p.setValue("updateFound", r.version);
        p.setValue("updatePage", r.page);
        p.setValue("updateAsset", r.asset);
        p.setValue("updateBytes", String(r.bytes));
    }
    p.setValue("updateCheckedAt", Time::getCurrentTime().toISO8601(true));
    p.saveIfNeeded();
    if (r.valid() && isNewer(r.version, cur)) {
        update = r;
        statusText = "Corridor Leveler " + r.version + " is available. You have " + cur + ".";
        if (manual) prompt = true;
        else maybePrompt();
    } else {
        update = {};
        statusText = "You have the newest version, " + cur + ".";
        if (manual) messageText = "Corridor Leveler is up to date.";
    }
}

void Checker::maybePrompt() {
    if (!update.valid() || promptedThisRun.load()) return;
    if (props().getValue("updateIgnored") == update.version) return;  // equality: a later version asks again
    prompt = true;
    promptedThisRun = true;
}

void Checker::doInstall() {
    const Release r = known();
    auto fail = [this](const String& m) { std::lock_guard<std::mutex> g(lock); messageText = m; };
    const File dir = File::getSpecialLocation(File::tempDirectory).getChildFile("CorridorLeveler-update");
    dir.deleteRecursively();
    dir.createDirectory();
    const File out = dir.getChildFile("CorridorLeveler-" + r.version + "-Windows-Setup.exe");
    int status = 0;
    auto s = openHttps(r.asset, 15000, status, this);
    if (!s) { fail("Couldn't download the update. Check your connection and try again."); return; }
    const int64 total = s->getTotalLength() > 0 ? s->getTotalLength() : r.bytes;
    bool ok = true;
    {
        FileOutputStream os(out);
        if (!os.openedOk()) ok = false;
        HeapBlock<char> buf(65536);
        int64 got = 0;
        while (ok && !s->isExhausted()) {
            if (threadShouldExit()) { ok = false; break; }
            const int n = s->read(buf, 65536);
            if (n < 0) { ok = false; break; }
            if (n == 0) { if (s->isError()) ok = false; break; }
            got += n;
            if (got > kMaxBytes) { ok = false; break; }
            ok = os.write(buf, size_t(n));
            if (total > 0) progressNow = jlimit(0.f, 1.f, float(double(got) / double(total)));
        }
        os.flush();
    }
    // a Windows installer starts with "MZ"
    MemoryBlock head;
    if (ok && (!out.loadFileAsData(head) || head.getSize() < 2 || static_cast<const char*>(head.getData())[0] != 'M'
               || static_cast<const char*>(head.getData())[1] != 'Z'))
        ok = false;
    if (!ok) { out.deleteFile(); fail("Couldn't download the update. Check your connection and try again."); return; }
    progressNow = 1.f;
#if JUCE_WINDOWS
    if (!out.startAsProcess()) { fail("Couldn't open the installer. Download the update from the release page instead."); return; }
    { std::lock_guard<std::mutex> g(lock); messageText = "The installer is open. When it asks, close OBS or your DAW, then reopen them."; }
#else  // Corridor Leveler ships for Windows only; other builds exist just to run the tests
    fail("Updates install on Windows.");
    return;
#endif
    prompt = false;
}

}  // namespace updates
