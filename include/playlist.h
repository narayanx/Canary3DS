#pragma once

#include <string>
#include <vector>

inline constexpr std::string_view PLAYLIST_DIR = "sdmc:/3ds/Canary/Playlists/";
inline constexpr std::string_view PLAYLIST_COVER_DIR =
    "sdmc:/3ds/Canary/Playlists/.playlist_covers/";

struct Playlist {
    std::string name;                // display name (without .m3u extension)
    std::string path;                // full path to .m3u file
    std::vector<std::string> songs;  // absolute paths to songs
};

// Load all playlists from PLAYLIST_DIR
std::vector<Playlist> loadPlaylists();

// Create an empty playlist with the given name
bool createPlaylist(const std::string &name);

// Delete a playlist by its full path
bool deletePlaylist(const std::string &playlistPath);

bool renamePlaylist(const std::string &oldPath, const std::string &newName);

bool duplicatePlaylist(const std::string &sourcePath, const std::string &newName);

bool mergePlaylist(const std::string &targetPath, const std::string &sourcePath);

bool removeDuplicateSongs(const std::string &playlistPath);

// Append a song (absolute path) to a playlist file
bool addSongToPlaylist(const std::string &playlistPath, const std::string &songPath);

// Remove the song at songIdx from a playlist file (rewrites the file)
bool removeSongFromPlaylist(const std::string &playlistPath, size_t songIdx);

// Parse song paths from a .m3u file (skips comment lines starting with #)
std::vector<std::string> readPlaylistSongs(const std::string &playlistPath);

// Returns the path to the cached cover art BMP for a playlist name.
std::string playlistCoverPath(const std::string &name);

// Extract cover art from songPath, scale to 128x128, and save as a BMP cache
// file for the given playlist name. Returns false if no cover art is found.
bool cachePlaylistCoverArt(const std::string &playlistName, const std::string &songPath);
