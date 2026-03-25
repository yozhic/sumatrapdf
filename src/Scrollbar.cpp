/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/Dpi.h"
#include "utils/WinUtil.h"

#include "Scrollbar.h"

#define OVERLAY_SCROLLBAR_CLASS L"SUMATRA_OVERLAY_SCROLLBAR"

static bool gScrollbarClassRegistered = false;

// colors
static COLORREF kThumbColor = RGB(0x88, 0x88, 0x88);
static COLORREF kThumbHoverColor = RGB(0x66, 0x66, 0x66);
static COLORREF kArrowColor = RGB(0x66, 0x66, 0x66);
static COLORREF kTrackColor = RGB(0xF0, 0xF0, 0xF0);
static constexpr int kThumbCornerRadius = 3;
static constexpr int kMinThumbSize = 20;
static constexpr BYTE kAlphaThin = 180;
static constexpr BYTE kAlphaThick = 220;

static int ScaledWidth(OverlayScrollbar* sb, bool thick) {
    int w = thick ? sb->thickWidth : sb->thinWidth;
    return DpiScale(sb->hwndOwner, w);
}

static bool IsVert(OverlayScrollbar* sb) {
    return sb->type == ScrollbarType::Vert;
}

// Get the track rect in client coords of the scrollbar window
static Rect GetTrackRect(OverlayScrollbar* sb) {
    RECT rc;
    GetClientRect(sb->hwnd, &rc);
    int arrowSize = 0;
    if (sb->isThick) {
        arrowSize = IsVert(sb) ? (rc.right - rc.left) : (rc.bottom - rc.top);
    }
    if (IsVert(sb)) {
        return Rect(0, arrowSize, rc.right - rc.left, (rc.bottom - rc.top) - 2 * arrowSize);
    }
    return Rect(arrowSize, 0, (rc.right - rc.left) - 2 * arrowSize, rc.bottom - rc.top);
}

// Calculate thumb rect within the track
static Rect GetThumbRect(OverlayScrollbar* sb) {
    Rect track = GetTrackRect(sb);
    int range = sb->nMax - sb->nMin + 1;
    if (range <= 0 || (int)sb->nPage >= range) {
        return track; // thumb fills entire track
    }

    int trackLen = IsVert(sb) ? track.dy : track.dx;
    int thumbLen = MulDiv(trackLen, (int)sb->nPage, range);
    if (thumbLen < DpiScale(sb->hwndOwner, kMinThumbSize)) {
        thumbLen = DpiScale(sb->hwndOwner, kMinThumbSize);
    }

    int scrollableTrack = trackLen - thumbLen;
    int scrollableRange = range - (int)sb->nPage;
    int pos = sb->isDragging ? sb->nTrackPos : sb->nPos;
    int thumbOffset = 0;
    if (scrollableRange > 0) {
        thumbOffset = MulDiv(pos - sb->nMin, scrollableTrack, scrollableRange);
    }
    thumbOffset = std::clamp(thumbOffset, 0, scrollableTrack);

    if (IsVert(sb)) {
        return Rect(track.x, track.y + thumbOffset, track.dx, thumbLen);
    }
    return Rect(track.x + thumbOffset, track.y, thumbLen, track.dy);
}

// Get arrow rects (only meaningful in thick mode)
static Rect GetArrowTopRect(OverlayScrollbar* sb) {
    RECT rc;
    GetClientRect(sb->hwnd, &rc);
    int arrowSize = IsVert(sb) ? (rc.right - rc.left) : (rc.bottom - rc.top);
    if (IsVert(sb)) {
        return Rect(0, 0, rc.right - rc.left, arrowSize);
    }
    return Rect(0, 0, arrowSize, rc.bottom - rc.top);
}

static Rect GetArrowBottomRect(OverlayScrollbar* sb) {
    RECT rc;
    GetClientRect(sb->hwnd, &rc);
    int arrowSize = IsVert(sb) ? (rc.right - rc.left) : (rc.bottom - rc.top);
    if (IsVert(sb)) {
        return Rect(0, (rc.bottom - rc.top) - arrowSize, rc.right - rc.left, arrowSize);
    }
    return Rect((rc.right - rc.left) - arrowSize, 0, arrowSize, rc.bottom - rc.top);
}

static void SendScrollMsg(OverlayScrollbar* sb, UINT scrollMsg, WPARAM wp) {
    SendMessageW(sb->hwndOwner, scrollMsg, wp, 0);
}

static UINT ScrollMsgForType(OverlayScrollbar* sb) {
    return IsVert(sb) ? WM_VSCROLL : WM_HSCROLL;
}

// Update the layered window with the current appearance
static void PaintScrollbar(OverlayScrollbar* sb) {
    if (!sb->hwnd || !IsWindowVisible(sb->hwnd)) {
        return;
    }

    RECT wrc;
    GetWindowRect(sb->hwnd, &wrc);
    int w = wrc.right - wrc.left;
    int h = wrc.bottom - wrc.top;
    if (w <= 0 || h <= 0) {
        return;
    }

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP hbmpOld = (HBITMAP)SelectObject(hdcMem, hbmp);

    // Clear to fully transparent
    memset(bits, 0, w * h * 4);

    BYTE alpha = sb->isThick ? kAlphaThick : kAlphaThin;

    auto premultiply = [](COLORREF c, BYTE a) -> DWORD {
        BYTE r = (BYTE)MulDiv(GetRValue(c), a, 255);
        BYTE g = (BYTE)MulDiv(GetGValue(c), a, 255);
        BYTE b = (BYTE)MulDiv(GetBValue(c), a, 255);
        return (a << 24) | (r << 16) | (g << 8) | b;
    };

    auto fillRect = [&](Rect r, COLORREF color) {
        DWORD pixel = premultiply(color, alpha);
        DWORD* pixels = (DWORD*)bits;
        int x0 = std::max(r.x, 0);
        int y0 = std::max(r.y, 0);
        int x1 = std::min(r.x + r.dx, w);
        int y1 = std::min(r.y + r.dy, h);
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                pixels[y * w + x] = pixel;
            }
        }
    };

    auto fillRoundedRect = [&](Rect r, int radius, COLORREF color) {
        DWORD pixel = premultiply(color, alpha);
        DWORD* pixels = (DWORD*)bits;
        int x0 = std::max(r.x, 0);
        int y0 = std::max(r.y, 0);
        int x1 = std::min(r.x + r.dx, w);
        int y1 = std::min(r.y + r.dy, h);
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                // check corners
                bool inside = true;
                // top-left
                if (x < r.x + radius && y < r.y + radius) {
                    int dx2 = x - (r.x + radius);
                    int dy2 = y - (r.y + radius);
                    inside = (dx2 * dx2 + dy2 * dy2) <= (radius * radius);
                }
                // top-right
                else if (x >= r.x + r.dx - radius && y < r.y + radius) {
                    int dx2 = x - (r.x + r.dx - radius - 1);
                    int dy2 = y - (r.y + radius);
                    inside = (dx2 * dx2 + dy2 * dy2) <= (radius * radius);
                }
                // bottom-left
                else if (x < r.x + radius && y >= r.y + r.dy - radius) {
                    int dx2 = x - (r.x + radius);
                    int dy2 = y - (r.y + r.dy - radius - 1);
                    inside = (dx2 * dx2 + dy2 * dy2) <= (radius * radius);
                }
                // bottom-right
                else if (x >= r.x + r.dx - radius && y >= r.y + r.dy - radius) {
                    int dx2 = x - (r.x + r.dx - radius - 1);
                    int dy2 = y - (r.y + r.dy - radius - 1);
                    inside = (dx2 * dx2 + dy2 * dy2) <= (radius * radius);
                }
                if (inside) {
                    pixels[y * w + x] = pixel;
                }
            }
        }
    };

    // Draw track background in thick mode
    if (sb->isThick) {
        fillRect(Rect(0, 0, w, h), kTrackColor);
    }

    // Draw thumb
    Rect thumbRc = GetThumbRect(sb);
    int cornerR = DpiScale(sb->hwndOwner, kThumbCornerRadius);
    COLORREF thumbCol = sb->mouseOverThumb ? kThumbHoverColor : kThumbColor;

    if (!sb->isThick) {
        // In thin mode, draw a thin bar for the thumb
        // Center the thin bar within the window width
        int thinW = ScaledWidth(sb, false);
        if (IsVert(sb)) {
            thumbRc.x = (w - thinW) / 2;
            thumbRc.dx = thinW;
        } else {
            thumbRc.y = (h - thinW) / 2;
            thumbRc.dy = thinW;
        }
    }
    fillRoundedRect(thumbRc, cornerR, thumbCol);

    // Draw arrows in thick mode
    if (sb->isThick) {
        Rect arrowTop = GetArrowTopRect(sb);
        Rect arrowBot = GetArrowBottomRect(sb);

        // Draw triangles for arrows
        DWORD arrowPixel = premultiply(kArrowColor, alpha);
        DWORD* pixels = (DWORD*)bits;

        if (IsVert(sb)) {
            // Up arrow triangle
            int cx = arrowTop.x + arrowTop.dx / 2;
            int cy = arrowTop.y + arrowTop.dy / 2;
            int sz = arrowTop.dx / 4;
            for (int dy2 = -sz; dy2 <= sz; dy2++) {
                int halfW = sz - abs(dy2);
                int yy = cy + dy2;
                if (yy < 0 || yy >= h) {
                    continue;
                }
                for (int dx2 = -halfW; dx2 <= halfW; dx2++) {
                    int xx = cx + dx2;
                    if (xx >= 0 && xx < w) {
                        pixels[yy * w + xx] = arrowPixel;
                    }
                }
            }

            // Down arrow triangle
            cx = arrowBot.x + arrowBot.dx / 2;
            cy = arrowBot.y + arrowBot.dy / 2;
            for (int dy2 = -sz; dy2 <= sz; dy2++) {
                int halfW = abs(dy2);
                if (halfW > sz) {
                    halfW = sz;
                }
                int yy = cy + dy2;
                if (yy < 0 || yy >= h) {
                    continue;
                }
                for (int dx2 = -halfW; dx2 <= halfW; dx2++) {
                    int xx = cx + dx2;
                    if (xx >= 0 && xx < w) {
                        pixels[yy * w + xx] = arrowPixel;
                    }
                }
            }
        } else {
            // Left arrow triangle
            int cx = arrowTop.x + arrowTop.dx / 2;
            int cy = arrowTop.y + arrowTop.dy / 2;
            int sz = arrowTop.dy / 4;
            for (int dx2 = -sz; dx2 <= sz; dx2++) {
                int halfH = sz - abs(dx2);
                int xx = cx + dx2;
                if (xx < 0 || xx >= w) {
                    continue;
                }
                for (int dy2 = -halfH; dy2 <= halfH; dy2++) {
                    int yy = cy + dy2;
                    if (yy >= 0 && yy < h) {
                        pixels[yy * w + xx] = arrowPixel;
                    }
                }
            }

            // Right arrow triangle
            cx = arrowBot.x + arrowBot.dx / 2;
            cy = arrowBot.y + arrowBot.dy / 2;
            for (int dx2 = -sz; dx2 <= sz; dx2++) {
                int halfH = abs(dx2);
                if (halfH > sz) {
                    halfH = sz;
                }
                int xx = cx + dx2;
                if (xx < 0 || xx >= w) {
                    continue;
                }
                for (int dy2 = -halfH; dy2 <= halfH; dy2++) {
                    int yy = cy + dy2;
                    if (yy >= 0 && yy < h) {
                        pixels[yy * w + xx] = arrowPixel;
                    }
                }
            }
        }
    }

    // Update layered window
    POINT ptSrc = {0, 0};
    SIZE szWnd = {w, h};
    POINT ptDst = {wrc.left, wrc.top};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255; // per-pixel alpha
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(sb->hwnd, hdcScreen, &ptDst, &szWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hbmpOld);
    DeleteObject(hbmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

static void ShowScrollbarWindow(OverlayScrollbar* sb, bool thick) {
    sb->isThick = thick;
    sb->isThin = !thick;
    OverlayScrollbarUpdatePos(sb);
    ShowWindow(sb->hwnd, SW_SHOWNOACTIVATE);
    PaintScrollbar(sb);

    // Reset auto-hide timer
    KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
    if (!thick) {
        int timeout = sb->showAfterScrollMs;
        SetTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide, timeout, nullptr);
    }
}

static void HideScrollbarWindow(OverlayScrollbar* sb) {
    sb->isThick = false;
    sb->isThin = false;
    ShowWindow(sb->hwnd, SW_HIDE);
    KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
}

static LRESULT CALLBACK WndProcOverlayScrollbar(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayScrollbar* sb = (OverlayScrollbar*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_TIMER:
            if (wp == OverlayScrollbar::kTimerAutoHide) {
                if (!sb->isDragging) {
                    HideScrollbarWindow(sb);
                }
                return 0;
            }
            break;

        case WM_MOUSEMOVE: {
            if (!sb->isThick) {
                // Mouse entered - switch to thick
                ShowScrollbarWindow(sb, true);
            }

            // Track mouse leave
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);

            if (sb->isDragging) {
                // Calculate new position based on drag
                int ptInTrack = IsVert(sb) ? my : mx;
                Rect track = GetTrackRect(sb);
                int range = sb->nMax - sb->nMin + 1;
                int thumbLen = MulDiv(IsVert(sb) ? track.dy : track.dx, (int)sb->nPage, range);
                int minThumb = DpiScale(sb->hwndOwner, kMinThumbSize);
                if (thumbLen < minThumb) {
                    thumbLen = minThumb;
                }
                int trackStart = IsVert(sb) ? track.y : track.x;
                int trackLen = IsVert(sb) ? track.dy : track.dx;
                int scrollableTrack = trackLen - thumbLen;
                int scrollableRange = range - (int)sb->nPage;

                int dragDelta = ptInTrack - sb->dragStartY;
                int newPos = sb->dragStartPos;
                if (scrollableTrack > 0 && scrollableRange > 0) {
                    newPos = sb->dragStartPos + MulDiv(dragDelta, scrollableRange, scrollableTrack);
                }
                newPos = std::clamp(newPos, sb->nMin, sb->nMax - (int)sb->nPage + 1);
                sb->nTrackPos = newPos;
                PaintScrollbar(sb);
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(SB_THUMBTRACK, newPos));
                return 0;
            }

            // Check if mouse is over thumb
            Rect thumbRc = GetThumbRect(sb);
            bool wasOver = sb->mouseOverThumb;
            sb->mouseOverThumb = thumbRc.Contains(Point(mx, my));
            if (wasOver != sb->mouseOverThumb) {
                PaintScrollbar(sb);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            if (!sb->isDragging) {
                sb->mouseOverThumb = false;
                // Switch back to thin, with auto-hide timer
                ShowScrollbarWindow(sb, false);
            }
            return 0;

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);
            SetCapture(hwnd);

            // Check if click is on arrows (thick mode only)
            if (sb->isThick) {
                Rect arrowTop = GetArrowTopRect(sb);
                Rect arrowBot = GetArrowBottomRect(sb);
                Point pt(mx, my);

                if (arrowTop.Contains(pt)) {
                    UINT code = IsVert(sb) ? SB_LINEUP : SB_LINELEFT;
                    SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                    ReleaseCapture();
                    return 0;
                }
                if (arrowBot.Contains(pt)) {
                    UINT code = IsVert(sb) ? SB_LINEDOWN : SB_LINERIGHT;
                    SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                    ReleaseCapture();
                    return 0;
                }
            }

            // Check if click is on thumb
            Rect thumbRc = GetThumbRect(sb);
            Point pt(mx, my);
            if (thumbRc.Contains(pt)) {
                sb->isDragging = true;
                sb->dragStartY = IsVert(sb) ? my : mx;
                sb->dragStartPos = sb->nPos;
                sb->nTrackPos = sb->nPos;
                return 0;
            }

            // Click in track - page up/down
            Rect track = GetTrackRect(sb);
            if (track.Contains(pt)) {
                int clickPos = IsVert(sb) ? my : mx;
                int thumbMid = IsVert(sb) ? (thumbRc.y + thumbRc.dy / 2) : (thumbRc.x + thumbRc.dx / 2);
                if (clickPos < thumbMid) {
                    UINT code = IsVert(sb) ? SB_PAGEUP : SB_PAGELEFT;
                    SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                } else {
                    UINT code = IsVert(sb) ? SB_PAGEDOWN : SB_PAGERIGHT;
                    SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                }
            }
            ReleaseCapture();
            return 0;
        }

        case WM_LBUTTONUP:
            if (sb->isDragging) {
                sb->isDragging = false;
                sb->nPos = sb->nTrackPos;
                ReleaseCapture();
                PaintScrollbar(sb);
                // Send final position
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(SB_THUMBPOSITION, sb->nPos));
            } else {
                ReleaseCapture();
            }
            return 0;

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            // Forward mouse wheel to owner
            SendMessageW(sb->hwndOwner, msg, wp, lp);
            return 0;

        case WM_NCHITTEST: {
            // Make sure we respond to mouse input
            LRESULT def = DefWindowProcW(hwnd, msg, wp, lp);
            if (def == HTNOWHERE) {
                return HTCLIENT;
            }
            return def;
        }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RegisterScrollbarClass() {
    if (gScrollbarClassRegistered) {
        return;
    }
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProcOverlayScrollbar;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.lpszClassName = OVERLAY_SCROLLBAR_CLASS;
    RegisterClassExW(&wcex);
    gScrollbarClassRegistered = true;
}

OverlayScrollbar* OverlayScrollbarCreate(HWND hwndOwner, ScrollbarType type) {
    RegisterScrollbarClass();

    auto* sb = new OverlayScrollbar();
    sb->hwndOwner = hwndOwner;
    sb->type = type;

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT;
    DWORD style = WS_POPUP;

    sb->hwnd = CreateWindowExW(exStyle, OVERLAY_SCROLLBAR_CLASS, nullptr, style, 0, 0, 1, 1, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(sb->hwnd, GWLP_USERDATA, (LONG_PTR)sb);

    // Remove WS_EX_TRANSPARENT so it can receive mouse input when visible
    // We'll add/remove it dynamically based on thin/thick state
    return sb;
}

void OverlayScrollbarDestroy(OverlayScrollbar* sb) {
    if (!sb) {
        return;
    }
    if (sb->hwnd) {
        KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
        DestroyWindow(sb->hwnd);
    }
    delete sb;
}

void OverlayScrollbarSetInfo(OverlayScrollbar* sb, const SCROLLINFO* si, bool redraw) {
    if (!sb) {
        return;
    }
    if (si->fMask & SIF_RANGE) {
        sb->nMin = si->nMin;
        sb->nMax = si->nMax;
    }
    if (si->fMask & SIF_PAGE) {
        sb->nPage = si->nPage;
    }
    if (si->fMask & SIF_POS) {
        sb->nPos = si->nPos;
    }

    if (redraw) {
        // Show thin scrollbar briefly after scroll info update
        ShowScrollbarWindow(sb, sb->isThick);
        if (!sb->isThick && !sb->isThin) {
            ShowScrollbarWindow(sb, false);
        } else {
            PaintScrollbar(sb);
            // Reset auto-hide timer
            KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
            if (!sb->isThick) {
                SetTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide, sb->showAfterScrollMs, nullptr);
            }
        }
    }
}

void OverlayScrollbarGetInfo(OverlayScrollbar* sb, SCROLLINFO* si) {
    if (!sb) {
        return;
    }
    if (si->fMask & SIF_RANGE) {
        si->nMin = sb->nMin;
        si->nMax = sb->nMax;
    }
    if (si->fMask & SIF_PAGE) {
        si->nPage = sb->nPage;
    }
    if (si->fMask & SIF_POS) {
        si->nPos = sb->nPos;
    }
    if (si->fMask & SIF_TRACKPOS) {
        si->nTrackPos = sb->nTrackPos;
    }
}

void OverlayScrollbarUpdatePos(OverlayScrollbar* sb) {
    if (!sb || !sb->hwnd || !sb->hwndOwner) {
        return;
    }

    RECT ownerRc;
    GetWindowRect(sb->hwndOwner, &ownerRc);

    int scrollW = ScaledWidth(sb, sb->isThick);
    int x, y, w, h;

    if (IsVert(sb)) {
        // Position on right edge of owner
        x = ownerRc.right - scrollW;
        y = ownerRc.top;
        w = scrollW;
        h = ownerRc.bottom - ownerRc.top;
    } else {
        // Position on bottom edge of owner
        x = ownerRc.left;
        y = ownerRc.bottom - scrollW;
        w = ownerRc.right - ownerRc.left;
        h = scrollW;
    }

    // When thin, we want it to pass through mouse events except over the thumb area
    // When thick, we want it to capture all mouse events
    LONG_PTR exStyle = GetWindowLongPtrW(sb->hwnd, GWL_EXSTYLE);
    if (sb->isThick || sb->isThin) {
        exStyle &= ~WS_EX_TRANSPARENT;
    } else {
        exStyle |= WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrW(sb->hwnd, GWL_EXSTYLE, exStyle);

    SetWindowPos(sb->hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

void OverlayScrollbarOnOwnerMouseMove(OverlayScrollbar* sb) {
    if (!sb || sb->isThick || sb->isDragging) {
        return;
    }
    // Show thin scrollbar when mouse moves in owner
    if (!sb->isThin) {
        ShowScrollbarWindow(sb, false);
    }
    // Reset auto-hide timer for mouse movement
    KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
    SetTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide, sb->showOnMouseMoveMs, nullptr);
}

void OverlayScrollbarShow(OverlayScrollbar* sb, bool show) {
    if (!sb) {
        return;
    }
    if (show) {
        ShowScrollbarWindow(sb, false);
    } else {
        HideScrollbarWindow(sb);
    }
}
