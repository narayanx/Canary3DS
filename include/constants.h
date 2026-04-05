#ifndef CANARY_CONSTANTS_H
#define CANARY_CONSTANTS_H

#include <string>

// max file name seems to be 255, file paths are concatenated filenames
inline constexpr int MAX_PATH_CHAR_LENGTH = 4096;
// max files to display at once
inline constexpr int MAX_FILES = 14;
// make sure to have trailing '/' character
inline constexpr std::string_view START_PATH = "sdmc:/Music/";
// how long to hold before auto-repeat begins (ms)
inline constexpr double REPEAT_INITIAL_DELAY_MS = 400.0;
// interval between each repeated scroll step once repeating (ms)
inline constexpr double REPEAT_INTERVAL_MS = 30.0;
// stop saving depth after this many directories (conserve memory, TODO allow changing in settings)
inline constexpr size_t MAX_DEPTH = 20;

#endif