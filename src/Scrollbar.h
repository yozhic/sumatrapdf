/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// Overlay scrollbar - a semi-transparent top-level window that acts like
// a standard Windows scrollbar but floats over the owner window.

extern int gThickVisibilityDistance;

enum class ScrollbarType {
    Vert,
    Horz,
};

struct OverlayScrollbar {
    HWND hwnd = nullptr;      // the scrollbar top-level window
    HWND hwndOwner = nullptr; // positioned relative to this window; receives scroll messages
    ScrollbarType type = ScrollbarType::Vert;

    // scroll state (mirrors SCROLLINFO)
    int nMin = 0;
    int nMax = 0;
    uint nPage = 0;
    int nPos = 0;
    int nTrackPos = 0;

    // widths in pixels (before DPI scaling)
    int thinWidth = 4;
    int thickWidth = 16;

    // auto-hide timing (milliseconds)
    int showAfterScrollMs = 5000;    // how long to show thin bar after scroll info update
    int hideAfterMouseStopMs = 3000; // hide after mouse stops moving

    // internal state
    bool isThick = false;    // currently showing thick version
    bool isThin = false;     // currently showing thin version
    bool isDragging = false; // user is dragging the thumb
    int dragStartY = 0;      // mouse Y (or X for horz) when drag started
    int dragStartPos = 0;    // nPos when drag started
    bool mouseOverThumb = false;

    // timer IDs
    static constexpr UINT_PTR kTimerAutoHide = 1;
};

OverlayScrollbar* OverlayScrollbarCreate(HWND hwndOwner, ScrollbarType type);
void OverlayScrollbarDestroy(OverlayScrollbar* sb);

// Same API as SetScrollInfo / GetScrollInfo
void OverlayScrollbarSetInfo(OverlayScrollbar* sb, const SCROLLINFO* si, bool redraw);
void OverlayScrollbarGetInfo(OverlayScrollbar* sb, SCROLLINFO* si);

// Call when owner window moves/resizes
void OverlayScrollbarUpdatePos(OverlayScrollbar* sb);

// Show/hide
void OverlayScrollbarShow(OverlayScrollbar* sb, bool show);
