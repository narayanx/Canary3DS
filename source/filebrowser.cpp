#include "filebrowser.h"

FileController fileController = {.cwd = "sdmc:/Music/",
                                 .files = {},
                                 .fileHistory = {},
                                 .selectedFile = 0,
                                 .playingFile = 0,
                                 .selectedQueueItem = 0};

std::vector<dirent> getFiles(const char *path) {
    std::vector<dirent> fileList;
    DIR *dir = opendir(path);
    if (dir == nullptr) {
        printf("Failed to open directory: %s\n", path);
        return fileList;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        fileList.push_back(*ent);
    }

    closedir(dir);
    return fileList;
}
