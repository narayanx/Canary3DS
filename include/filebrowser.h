#pragma once

#include <deque>
#include <dirent.h>
#include <string>
#include <utility>
#include <vector>

// Maximum number of songs remembered in playback history
inline constexpr size_t MAX_HISTORY = 30;
// Maximum entries in the play queue (prevents unbounded memory use)
inline constexpr size_t MAX_QUEUE_SIZE = 500;
// If a directory contains more than this many files use paginated display
inline constexpr size_t FILE_LAZY_THRESHOLD = 200;
// Number of files revealed per page when lazy loading
inline constexpr size_t FILE_PAGE_SIZE = 50;

struct FileController {
    std::string cwd;
    std::vector<dirent> files;
    std::deque<std::pair<size_t, size_t>> fileHistory;  // { selectedFile, fileBrowserScrollOffset }
    size_t selectedFile;
    size_t playingFile;
    // Stored when playing song, so browsing elsewhere doesn't affect autoplay
    std::string playingCwd;
    std::vector<dirent> playingFiles;
    // Equals files.size() for small dirs, grows in FILE_PAGE_SIZE steps for large ones
    size_t filesShown;
    std::deque<std::string> playQueue;
    // Playback history: most recent song first
    std::deque<std::string> playHistory;
    // use int so it can go negative (negative = scrolled into history)
    int selectedQueueItem;
};

// stop saving depth after this many directories (conserve memory, TODO allow changing in settings)
inline constexpr size_t MAX_DEPTH = 20;

extern FileController fileController;

std::vector<dirent> getFiles(const char *path);

// walk start path and push history entries so going up restores the correct selection
void initFileHistory(const std::string &startPath);
