#include "render_frame.h"

#include <string>
#include <vector>

#include "audio_engine.h"
#include "filebrowser.h"
#include "gfx.h"
#include "image.h"

void renderFrame(TopScreenState screenState,
                 FileBrowserState &fb,
                 PlaylistState &pl,
                 InfoState &info,
                 const SettingsState &st,
                 const CtxMenu &s_ctx,
                 const CtxMenu &s_sub,
                 bool showLog,
                 float seekBarX,
                 float seekBarY,
                 float seekBarW,
                 float seekBarH) {
    consoleClear();
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(top, CLEAR_COLOR);
    C2D_SceneBegin(top);

    if (screenState == TopScreenState::FILEBROWSER) {
        printC2DText(fileController.cwd, 0);
        printFiles(fileController.files,
                   fileController.selectedFile,
                   fb.scroll,
                   fileController.filesShown,
                   1,
                   fileController.files.size());

    } else if (screenState == TopScreenState::INFO) {
        if (info.displayCover) {
            if (info.hasCover) {
                drawCoverScaled(info.image, info.subtex, 10.0f, 10.0f);
            } else {
                drawNoteCover(10.0f, 10.0f, COVER_TARGET_WIDTH, COVER_TARGET_HEIGHT, true);
            }
        }

        printNowPlayingList(fileController.playHistory,
                            fileController.playQueue,
                            info.autoplayItems,
                            fileController.selectedQueueItem,
                            info.scrollTop,
                            audioController.songPath,
                            audioController.songArtist,
                            audioController.songTrackNumber);

        {
            double dur = audioController.songDurationSeconds;
            bool showDrag = info.seekDragging || audioController.seekPending;
            double pos = (showDrag && dur > 0) ? (double) info.seekDragProgress * dur
                                               : audioController.songPositionSeconds;
            drawProgressBar(10.0f, 206.0f, 190.0f, 7.0f, (dur > 0) ? (float) (pos / dur) : 0.0f);
            drawTimeText(pos, dur, 10.0f, 217.0f, 0.44f, 0.44f);
        }

    } else if (screenState == TopScreenState::PLAYLIST_BROWSER) {
        printC2DText("Playlists  A=Open  X=Menu", 0);
        std::vector<std::string> names;
        for (const auto &p : pl.playlists) {
            names.push_back(p.name);
        }
        names.push_back("(+ Create playlist)");
        printStringList(names, pl.sel, pl.browserScroll, 1);

    } else if (screenState == TopScreenState::PLAYLIST_VIEW) {
        if (!pl.playlists.empty() && pl.sel < pl.playlists.size()) {
            const Playlist &lst = pl.playlists[pl.sel];
            std::vector<std::string> songNames;
            for (const auto &s : lst.songs) {
                size_t sl = s.find_last_of('/');
                songNames.push_back(sl != std::string::npos ? s.substr(sl + 1) : s);
            }
            printPlaylistView(lst.name,
                              songNames,
                              pl.selSong,
                              pl.viewScroll,
                              pl.inHeader,
                              pl.headerBtnSel,
                              pl.hasCover ? &pl.coverImage : nullptr);
        }
    } else if (screenState == TopScreenState::SETTINGS) {
        printSettingsMenu(SettingsState::buildRows(), st.sel);
    }

    // Context menu overlay
    if (s_sub.active) {
        printContextMenu(s_sub.labels, s_sub.idx, s_sub.scrollOffset, s_sub.x, s_sub.y);
    } else if (s_ctx.active) {
        printContextMenu(s_ctx.labels, s_ctx.idx, s_ctx.scrollOffset, s_ctx.x, s_ctx.y);
    }

    // Log overlay (drawn last so it sits on top of everything)
    if (showLog) {
        renderLogOverlay();
    }

    // Bottom screen
    C2D_TargetClear(bottom, BOTTOM_CLEAR_COLOR);
    C2D_SceneBegin(bottom);
    int activeTab = 0;
    if (screenState == TopScreenState::INFO) {
        activeTab = 1;
    } else if (screenState == TopScreenState::PLAYLIST_BROWSER ||
               screenState == TopScreenState::PLAYLIST_VIEW) {
        activeTab = 2;
    } else if (screenState == TopScreenState::SETTINGS) {
        activeTab = 3;
    }
    renderBottomScreen(audioController.songReady,
                       audioController.songPositionSeconds,
                       audioController.songDurationSeconds,
                       audioController.songPath,
                       audioController.songArtist,
                       seekBarX,
                       seekBarY,
                       seekBarW,
                       seekBarH,
                       (info.seekDragging || audioController.seekPending) ? info.seekDragProgress
                                                                          : -1.0f,
                       activeTab,
                       audioController.loopOne);
    C3D_FrameEnd(0);
}
