#include "scrobbler.h"

#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>

#include "gfx.h"

namespace {
    // Tracks shorter than this are never logged (standard scrobbling rule)
    constexpr double MIN_SCROBBLE_DURATION = 30.0;
    // Counts "listened" once played past half the duration or this many seconds, whichever comes
    // first
    constexpr double MAX_SCROBBLE_THRESHOLD = 240.0;

    void ensureDir() {
        mkdir("sdmc:/3ds", 0777);
        mkdir("sdmc:/3ds/Canary", 0777);
    }

    // Tab/newline are field separators in the log
    std::string sanitize(std::string s) {
        for (char &c : s) {
            if (c == '\t' || c == '\n' || c == '\r') {
                c = ' ';
            }
        }
        return s;
    }

    std::string deriveTitle(const std::string &path) {
        std::string name = path;
        size_t sl = name.find_last_of('/');
        if (sl != std::string::npos) {
            name = name.substr(sl + 1);
        }
        size_t dot = name.rfind('.');
        if (dot != std::string::npos) {
            name = name.substr(0, dot);
        }
        return name;
    }
}  // namespace

void scrobblerInit() {
    ensureDir();
    const std::string path(SCROBBLER_LOG_PATH);

    FILE *existing = fopen(path.c_str(), "r");
    if (existing) {
        fclose(existing);
        return;
    }

    std::ofstream out(path, std::ios::out);
    if (!out.is_open()) {
        logToDebugScreen("Failed to create scrobbler log: " + path);
        return;
    }
    out << "#AUDIOSCROBBLER/1.1\n";
    out << "#TZ/UNKNOWN\n";
    out << "#CLIENT/Canary3DS v1.2.0\n";
    out << "\n";
    out << "#ARTIST\t#ALBUM\t#TITLE\t#TRACKNUM\t#LENGTH\t#RATING\t#TIMESTAMP\t#MUSICBRAINZ_"
           "TRACKID\n";
}

void scrobblerLogTrack(const std::string &path,
                       const std::string &artist,
                       const std::string &trackNumber,
                       double playedSeconds,
                       double durationSeconds,
                       time_t startUnixTime) {
    if (durationSeconds < MIN_SCROBBLE_DURATION) {
        return;
    }

    double threshold = durationSeconds / 2.0;
    if (threshold > MAX_SCROBBLE_THRESHOLD) {
        threshold = MAX_SCROBBLE_THRESHOLD;
    }
    char rating = (playedSeconds >= threshold) ? 'L' : 'S';

    std::ofstream out(std::string(SCROBBLER_LOG_PATH), std::ios::app);
    if (!out.is_open()) {
        logToDebugScreen("Failed to append to scrobbler log");
        return;
    }

    out << sanitize(artist) << '\t' << '\t'  // album unknown
        << sanitize(deriveTitle(path)) << '\t' << sanitize(trackNumber) << '\t'
        << (long) (durationSeconds + 0.5) << '\t' << rating << '\t' << (long long) startUnixTime
        << '\t'  // musicbrainz id unknown
        << '\n';
}
