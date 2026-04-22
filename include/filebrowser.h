#ifndef CANARY_FILEBROWSER_H
#define CANARY_FILEBROWSER_H

#include <deque>
#include <dirent.h>
#include <string>
#include <utility>
#include <vector>

#include "constants.h"

// Maximum number of songs remembered in playback history
inline constexpr size_t MAX_HISTORY = 30;

struct FileController {
    std::string cwd;
    std::vector<dirent> files;
    std::deque<std::pair<size_t, size_t>> fileHistory;  // { selectedFile, fileBrowserScrollOffset }
    size_t selectedFile;
    size_t playingFile;
    std::deque<std::string> playQueue;
    // Playback history: most recent song first.
    std::deque<std::string> playHistory;
    // use int so it can go negative (negative = scrolled into history)
    int selectedQueueItem;
};

extern FileController fileController;

std::vector<dirent> getFiles(const char *path);

#endif
