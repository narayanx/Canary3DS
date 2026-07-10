#pragma once

#include <ctime>
#include <string>

// Audioscrobbler Portable Player Logging Specification, version 1.1
inline constexpr std::string_view SCROBBLER_LOG_PATH = "sdmc:/3ds/Canary/.scrobbler.log";

// Creates the log file and writes the header if it doesn't already exist
void scrobblerInit();

// Appends one finished track (listened or skipped) to the log
void scrobblerLogTrack(const std::string &path,
                       const std::string &artist,
                       const std::string &trackNumber,
                       double playedSeconds,
                       double durationSeconds,
                       time_t startUnixTime);
