/* Text of the ImGui tool windows: TS(id, English, Japanese).
** An empty translation falls back to the English. A translation keeps the
** English format specifiers in the same order; a static_assert checks it. */

/* ImGui wraps lines only at ASCII spaces and after some punctuation: Japanese
** puts NB, a no-break space, where a space must not become a line end. */
#ifndef NB
#define NB "\xC2\xA0"
#endif

/* Shared by the tool windows */
TS(FIT,                       "Fit", "フィット")
TS(CANVAS_GRID,               "Grid", "グリッド")
TS(ZOOM_OUT,                  "Zoom out", "縮小")
TS(ZOOM_IN,                   "Zoom in", "拡大")
TS(FIT_TIP,                   "Fit the image to the view", "画面に合わせる")
TS(CANVAS_GRID_TIP,           "Grid (when a dot is 4 or more screen pixels)", "グリッド（1" NB "ドットが" NB "4" NB "画素以上のとき）")
TS(ZOOM_FIT_ON_TIP,           "Fit is on; selecting a zoom level turns it off", "フィット有効: 倍率を選択するとフィットを解除")
