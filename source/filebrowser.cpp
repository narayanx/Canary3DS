#include "filebrowser.h"

#include <dirent.h>
#include <string>

#include "gfx.h"

FileController fileController = {.cwd = "sdmc:/Music/",
                                 .files = {},
                                 .fileHistory = {},
                                 .selectedFile = 0,
                                 .playingFile = 0,
                                 .filesShown = 0,
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

void initFileHistory(const std::string &startPath) {
    DIR *tmp = opendir(startPath.c_str());
    if (!tmp) {
        fileController.cwd = "sdmc:/";
        fileController.fileHistory.clear();
        return;
    }
    closedir(tmp);
    fileController.cwd = startPath;

    // Ensure going up directories restores the correct selection at each level
    std::string cur = "sdmc:/";
    size_t pos = 6;  // skip "sdmc:/"
    while (pos < startPath.size()) {
        size_t slash = startPath.find('/', pos);
        if (slash == std::string::npos) {
            break;
        }
        std::string component = startPath.substr(pos, slash - pos);
        pos = slash + 1;

        DIR *d = opendir(cur.c_str());
        if (!d) {
            break;
        }
        int idx = 0, found = -1;
        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr) {
            if (ent->d_type == DT_DIR && component == ent->d_name) {
                found = idx;
            }
            ++idx;
        }
        closedir(d);

        if (found < 0) {
            break;
        }

        fileController.fileHistory.push_back(
            {(size_t) found,
             (size_t) found >= (size_t) MAX_FILES ? (size_t) found - MAX_FILES + 1 : 0});

        cur += component + '/';
    }
}
