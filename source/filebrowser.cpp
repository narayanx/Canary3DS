#include "filebrowser.h"

#include <algorithm>
#include <cctype>
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
    // Leading number(s) parsed from the start of a file name
    struct LeadingNumber {
        bool hasNumber = false;
        long primary = 0;  // track number, or disc number
        bool hasSecondary = false;
        long secondary = 0;  // track number when the name starts
    };

    // Caps accumulated digit runs to avoid overflow
    constexpr long MAX_PARSED_NUMBER = 1000000000L;

    // Parses a run of digits starting at a position. Returns the parsed value
    // and advances position past the digits.
    long consumeDigits(const std::string &name, size_t &pos) {
        long value = 0;
        while (pos < name.size() && std::isdigit((unsigned char) name[pos])) {
            if (value < MAX_PARSED_NUMBER) {
                value = value * 10 + (name[pos] - '0');
            }
            ++pos;
        }
        return value;
    }

    LeadingNumber parseLeadingNumber(const std::string &name) {
        LeadingNumber result;
        size_t pos = 0;
        if (pos >= name.size() || !std::isdigit((unsigned char) name[pos])) {
            return result;
        }

        result.hasNumber = true;
        result.primary = consumeDigits(name, pos);

        if (pos < name.size() && name[pos] != ' ' && !std::isdigit((unsigned char) name[pos])) {
            size_t afterDelim = pos + 1;
            if (afterDelim < name.size() && std::isdigit((unsigned char) name[afterDelim])) {
                size_t secPos = afterDelim;
                result.hasSecondary = true;
                result.secondary = consumeDigits(name, secPos);
            }
        }

        return result;
    }

    // Orders by leading track or disc-track number (numerically, so "9" sorts
    // before "10"), falling back to alphabetical order when numbers are
    // equal, missing, or tied.
    bool compareByTrackNumber(const dirent &a, const dirent &b) {
        LeadingNumber na = parseLeadingNumber(a.d_name);
        LeadingNumber nb = parseLeadingNumber(b.d_name);

        if (na.hasNumber != nb.hasNumber) {
            return na.hasNumber;  // numbered entries come first
        }
        if (na.hasNumber) {
            if (na.primary != nb.primary) {
                return na.primary < nb.primary;
            }
            long secA = na.hasSecondary ? na.secondary : -1;
            long secB = nb.hasSecondary ? nb.secondary : -1;
            if (secA != secB) {
                return secA < secB;
            }
        }
        return strcasecmp(a.d_name, b.d_name) < 0;
    }

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
        } else if (g_settings.sortBy == "Numerical") {
            std::stable_sort(files.begin(), files.end(), compareByTrackNumber);
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

void toggleSongSelection(const std::string &path) {
    auto &sel = fileController.selectedSongs;
    auto it = std::find(sel.begin(), sel.end(), path);
    if (it != sel.end()) {
        sel.erase(it);
    } else {
        sel.push_back(path);
    }
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
