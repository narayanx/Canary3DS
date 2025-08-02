#include "filebrowser.h"

FileController file_controller = {
    .cwd = "sdmc:/Music/",
    .files = {},
    .fileHistory = {},
    .selectedFile = 0,
    .playingFile = 0,
};
