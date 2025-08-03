#include "filebrowser.h"

FileController file_controller = {
    .cwd = "sdmc:/Music/",
    .files = {},
    .fileHistory = {},
    .selectedFile = 0,
    .playingFile = 0,
};

std::vector<dirent> getFiles(const char *path) {
    std::vector<dirent> file_list;
    DIR *dir = opendir(path);
    if (dir == nullptr) {
        printf("Failed to open directory: %s\n", path);
        return file_list;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        file_list.push_back(*ent);
    }

    closedir(dir);
    return file_list;
}

