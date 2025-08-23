#ifndef CANARY_FILEBROWSER_H
#define CANARY_FILEBROWSER_H

#include <dirent.h>

#include <deque>
#include <string>
#include <vector>
#include "constants.h"

struct FileController {
    std::string cwd;
    std::vector<dirent> files;
    std::deque<size_t> fileHistory;
    size_t selectedFile;
    size_t playingFile;
};

extern FileController fileController;

std::vector<dirent> getFiles(const char *path);

#endif
