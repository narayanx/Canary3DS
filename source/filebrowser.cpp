#include "filebrowser.h"

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>

#include "gfx.h"
#include "settings.h"

FileController fileController = {.cwd = "sdmc:/Music/",
                                 .files = {},
                                 .fileHistory = {},
                                 .selectedFile = 0,
                                 .playingFile = 0,
                                 .filesShown = 0,
                                 .selectedQueueItem = 0};

namespace {
    // dirPath (trailing slash) is needed to stat() entries for date sorting.
    void sortFiles(std::vector<dirent> &files, const std::string &dirPath) {
        if (g_settings.sortBy == "Date Modified") {
            // stat() once per entry up front so the comparator is cheap; calling
            // stat() from inside the comparator would hit the SD card
            // O(n log n) times instead of O(n).
            std::vector<time_t> mtimes(files.size(), 0);
            for (size_t i = 0; i < files.size(); ++i) {
                struct stat st;
                if (stat((dirPath + files[i].d_name).c_str(), &st) == 0) {
                    mtimes[i] = st.st_mtime;
                }
            }
            std::vector<size_t> order(files.size());
            for (size_t i = 0; i < order.size(); ++i) {
                order[i] = i;
            }
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return mtimes[a] < mtimes[b];
            });
            std::vector<dirent> sorted;
            sorted.reserve(files.size());
            for (size_t i : order) {
                sorted.push_back(files[i]);
            }
            files = std::move(sorted);
        } else {
            // "Name" (and any unrecognised stored value) sorts alphabetically.
            std::stable_sort(files.begin(), files.end(), [](const dirent &a, const dirent &b) {
                return strcasecmp(a.d_name, b.d_name) < 0;
            });
        }

        if (g_settings.reverseSort) {
            std::reverse(files.begin(), files.end());
        }
    }
}  // namespace

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
    sortFiles(fileList, path);
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

    fileController.fileHistory.clear();
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

        std::vector<dirent> entries = getFiles(cur.c_str());
        int found = -1;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].d_type == DT_DIR && component == entries[i].d_name) {
                found = (int) i;
                break;
            }
        }

        if (found < 0) {
            break;
        }

        fileController.fileHistory.push_back(
            {(size_t) found,
             (size_t) found >= (size_t) MAX_FILES ? (size_t) found - MAX_FILES + 1 : 0});

        cur += component + '/';
    }
}
