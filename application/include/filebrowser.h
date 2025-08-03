#ifndef CANARY_FILEBROWSER_H
#define CANARY_FILEBROWSER_H

#include <dirent.h>

#include <deque>
#include <string>
#include <vector>

// max file name seems to be 255, file paths are concatenated filenames
inline constexpr int MAX_PATH_CHAR_LENGTH = 4096;
// max files to display at once TODO change back to 14 once I'm done debugging
inline constexpr int MAX_FILES = 12;
// make sure to have trailing '/' character
inline constexpr std::string_view START_PATH = "sdmc:/Music/";
// delay before auto repeat of input starts
inline constexpr double REPEAT_DELAY_MS = 175.0;
// stop saving depth after this many directories (conserve memory, TODO allow changing in settings)
inline constexpr size_t MAX_DEPTH = 20;

struct FileController {
    std::string cwd;
    std::vector<dirent> files;
    std::deque<size_t> fileHistory;
    size_t selectedFile;
    size_t playingFile;
};

extern FileController file_controller;

#endif
