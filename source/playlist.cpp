#include "playlist.h"

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#include "audio_decoder.h"
#include "gfx.h"
#include "image.h"

static std::vector<std::string> splitPath(const std::string &path) {
    std::vector<std::string> parts;
    size_t start = 0, end;
    while ((end = path.find('/', start)) != std::string::npos) {
        if (end > start) {
            parts.push_back(path.substr(start, end - start));
        }
        start = end + 1;
    }
    if (start < path.size()) {
        parts.push_back(path.substr(start));
    }
    return parts;
}

// "sdmc:/Music/song.opus" -> "../../../Music/song.opus" (relative to PLAYLIST_DIR)
static std::string toRelativePath(const std::string &absTarget) {
    std::vector<std::string> baseParts = splitPath(std::string(PLAYLIST_DIR));
    std::vector<std::string> targetParts = splitPath(absTarget);

    // find common prefix length
    size_t common = 0;
    while (common < baseParts.size() && common < targetParts.size() &&
           baseParts[common] == targetParts[common]) {
        common++;
    }

    std::string rel;
    // one ".." for each remaining segment in the base directory
    for (size_t i = common; i < baseParts.size(); i++) {
        rel += "../";
    }
    // then the remaining target segments
    for (size_t i = common; i < targetParts.size(); i++) {
        rel += targetParts[i];
        if (i + 1 < targetParts.size()) {
            rel += '/';
        }
    }
    return rel;
}

// "../../../Music/song.opus" -> "sdmc:/Music/song.opus" (resolved against PLAYLIST_DIR)
static std::string toAbsolutePath(const std::string &rel) {
    // already absolute
    if (rel.rfind("sdmc:/", 0) == 0) {
        return rel;
    }

    std::vector<std::string> parts = splitPath(std::string(PLAYLIST_DIR));
    for (const auto &seg : splitPath(rel)) {
        if (seg == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
        } else if (seg != ".") {
            parts.push_back(seg);
        }
    }

    std::string abs;
    for (size_t i = 0; i < parts.size(); i++) {
        abs += parts[i];
        // "sdmc:" already contains the colon; add slash after it and between all others
        if (i + 1 < parts.size()) {
            abs += '/';
        }
    }
    return abs;
}

static void ensurePlaylistDirExists() {
    std::string path(PLAYLIST_DIR);
    size_t pos = 0;

    // mkdir fails if parent directory doesn't exist
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        std::string subdir = path.substr(0, pos);
        mkdir(subdir.c_str(), 0777);
    }

    mkdir(path.c_str(), 0777);
}

static void ensurePlaylistCoverDirExists() {
    std::string path(PLAYLIST_COVER_DIR);
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        std::string subdir = path.substr(0, pos);
        mkdir(subdir.c_str(), 0777);
    }
    mkdir(path.c_str(), 0777);
}

std::string playlistCoverPath(const std::string &name) {
    return std::string(PLAYLIST_COVER_DIR) + name + ".bmp";
}

std::vector<std::string> readPlaylistSongs(const std::string &playlistPath) {
    std::vector<std::string> songs;
    std::ifstream f(playlistPath);
    if (!f.is_open()) {
        return songs;
    }
    std::string line;
    while (std::getline(f, line)) {
        // Strip trailing \r if the file has Windows line endings
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && line[0] != '#') {
            songs.push_back(toAbsolutePath(line));
        }
    }
    return songs;
}

std::vector<Playlist> loadPlaylists() {
    ensurePlaylistDirExists();
    std::vector<Playlist> playlists;

    DIR *dir = opendir(std::string(PLAYLIST_DIR).c_str());
    if (!dir) {
        logToDebugScreen("Failed to open playlist directory");
        return playlists;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string fname = ent->d_name;
        if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".m3u") {
            Playlist p;
            p.name = fname.substr(0, fname.size() - 4);
            p.path = std::string(PLAYLIST_DIR) + fname;
            p.songs = readPlaylistSongs(p.path);
            playlists.push_back(std::move(p));
        }
    }
    closedir(dir);

    std::sort(playlists.begin(), playlists.end(), [](const Playlist &a, const Playlist &b) {
        return a.name < b.name;
    });

    return playlists;
}

bool createPlaylist(const std::string &name) {
    ensurePlaylistDirExists();
    std::string path = std::string(PLAYLIST_DIR) + name + ".m3u";
    std::ofstream f(path);
    if (!f.is_open()) {
        return false;
    }
    f << "#EXTM3U\n";
    return true;
}

bool deletePlaylist(const std::string &playlistPath) {
    // Derive name from path to remove the cover art cache
    std::string name;
    size_t sl = playlistPath.rfind('/');
    if (sl != std::string::npos && sl + 1 < playlistPath.size()) {
        name = playlistPath.substr(sl + 1);
    } else {
        name = playlistPath;
    }
    if (name.size() > 4 && name.substr(name.size() - 4) == ".m3u") {
        name = name.substr(0, name.size() - 4);
    }
    remove(playlistCoverPath(name).c_str());
    return remove(playlistPath.c_str()) == 0;
}

bool renamePlaylist(const std::string &oldPath, const std::string &newName) {
    std::string oldName;
    size_t sl = oldPath.rfind('/');
    oldName = (sl != std::string::npos) ? oldPath.substr(sl + 1) : oldPath;
    if (oldName.size() > 4 && oldName.substr(oldName.size() - 4) == ".m3u") {
        oldName = oldName.substr(0, oldName.size() - 4);
    }

    std::string newPath = std::string(PLAYLIST_DIR) + newName + ".m3u";
    if (rename(oldPath.c_str(), newPath.c_str()) != 0) {
        return false;
    }

    // rename cover art if it exists (best-effort)
    rename(playlistCoverPath(oldName).c_str(), playlistCoverPath(newName).c_str());
    return true;
}

bool duplicatePlaylist(const std::string &sourcePath, const std::string &newName) {
    std::vector<std::string> songs = readPlaylistSongs(sourcePath);
    if (!createPlaylist(newName)) {
        return false;
    }
    std::string newPath = std::string(PLAYLIST_DIR) + newName + ".m3u";
    for (const auto &s : songs) {
        addSongToPlaylist(newPath, s);
    }
    return true;
}

bool mergePlaylist(const std::string &targetPath, const std::string &sourcePath) {
    std::vector<std::string> songs = readPlaylistSongs(sourcePath);
    for (const auto &s : songs) {
        addSongToPlaylist(targetPath, s);
    }
    return true;
}

bool removeDuplicateSongs(const std::string &playlistPath) {
    std::vector<std::string> songs = readPlaylistSongs(playlistPath);
    std::vector<std::string> unique;
    for (const auto &s : songs) {
        bool found = false;
        for (const auto &u : unique) {
            if (u == s) {
                found = true;
                break;
            }
        }
        if (!found) {
            unique.push_back(s);
        }
    }
    if (unique.size() == songs.size()) {
        return true;
    }
    std::ofstream f(playlistPath, std::ios::trunc);
    if (!f.is_open()) {
        return false;
    }
    f << "#EXTM3U\n";
    for (const auto &s : unique) {
        f << toRelativePath(s) << "\n";
    }
    return true;
}

bool addSongToPlaylist(const std::string &playlistPath, const std::string &songPath) {
    std::ofstream f(playlistPath, std::ios::app);
    if (!f.is_open()) {
        return false;
    }
    f << toRelativePath(songPath) << "\n";
    return true;
}

bool removeSongFromPlaylist(const std::string &playlistPath, size_t songIdx) {
    std::vector<std::string> songs = readPlaylistSongs(playlistPath);
    if (songIdx >= songs.size()) {
        return false;
    }
    songs.erase(songs.begin() + songIdx);

    std::ofstream f(playlistPath, std::ios::trunc);
    if (!f.is_open()) {
        return false;
    }
    f << "#EXTM3U\n";
    for (const auto &s : songs) {
        f << toRelativePath(s) << "\n";
    }
    return true;
}

bool cachePlaylistCoverArt(const std::string &playlistName, const std::string &songPath) {
    auto dec = createDecoder(songPath);
    if (!dec) {
        return false;
    }
    if (!dec->open(songPath)) {
        return false;
    }
    const std::string &bytes = dec->getCoverArtBytes();
    bool ok = false;
    if (!bytes.empty()) {
        ensurePlaylistCoverDirExists();
        ok = saveAsBmp128(playlistCoverPath(playlistName),
                          reinterpret_cast<const unsigned char *>(bytes.data()),
                          (int) bytes.size());
    }
    dec->close();
    return ok;
}
