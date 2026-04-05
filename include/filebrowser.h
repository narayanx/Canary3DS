#ifndef CANARY_FILEBROWSER_H
#define CANARY_FILEBROWSER_H

#include <dirent.h>

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "constants.h"

struct FileController {
    std::string cwd;
    std::vector<dirent> files;
    std::deque<std::pair<size_t, size_t>> fileHistory;  //  { selectedFile, fileBrowserScrollOffset }
    size_t selectedFile;
    size_t playingFile;
    std::deque<std::string> playQueue;
    // use int to potentially use negative numbers for selecting song in history
    int selectedQueueItem;
};

extern FileController fileController;

std::vector<dirent> getFiles(const char* path);

#endif
