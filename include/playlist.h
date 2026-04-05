#ifndef CANARY_PLAYLIST_H
#define CANARY_PLAYLIST_H

#include <string>
#include <vector>

inline constexpr std::string_view PLAYLIST_DIR = "sdmc:/3ds/Canary3DS/Playlists/";

struct Playlist {
    std::string name;  // display name (without .m3u extension)
    std::string path;  // full path to .m3u file
    std::vector<std::string> songs;  // absolute paths to songs
};

// Load all playlists from PLAYLIST_DIR
std::vector<Playlist> loadPlaylists();

// Create an empty playlist with the given name
bool createPlaylist(const std::string& name);

// Delete a playlist by its full path
bool deletePlaylist(const std::string& playlistPath);

// Append a song (absolute path) to a playlist file
bool addSongToPlaylist(const std::string& playlistPath, const std::string& songPath);

// Remove the song at songIdx from a playlist file (rewrites the file)
bool removeSongFromPlaylist(const std::string& playlistPath, size_t songIdx);

// Parse song paths from a .m3u file (skips comment lines starting with #)
std::vector<std::string> readPlaylistSongs(const std::string& playlistPath);

#endif
