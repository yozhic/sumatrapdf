/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/Dpi.h"
#include "utils/WinUtil.h"

#include "OverlayScrollbar.h"
#include "Theme.h"

#define OVERLAY_SCROLLBAR_CLASS L"SUMATRA_OVERLAY_SCROLLBAR"

static bool gScrollbarClassRegistered = false;

// distance in pixels from scrollbar edge at which it transitions to thick
int gThickVisibilityDistance = 32;
bool gOverlayScrollbarSuppressThick = false;

// all live overlay scrollbars, for global mouse tracking
static Vec<OverlayScrollbar*> gAllScrollbars;
static UINT_PTR gMouseTrackTimer = 0;
static POINT gLastMousePos = {-1, -1};
static constexpr UINT_PTR kMouseTrackTimerID = 100;
static constexpr int kMouseTrackIntervalMs = 50;

// Derive scrollbar colors from current theme
static COLORREF ThemeTrackColor() {
    COLORREF bg = ThemeControlBackgroundColor();
    return bg;
}

static COLORREF ThemeThumbColor() {
    COLORREF bg = ThemeControlBackgroundColor();
    return AccentColor(bg, 100);
}

static COLORREF ThemeThumbHoverColor() {
    COLORREF bg = ThemeControlBackgroundColor();
    return AccentColor(bg, 140);
}

static COLORREF ThemeArrowColor() {
    return ThemeThumbHoverColor();
}
static constexpr int kMinThumbSize = 20;
static constexpr BYTE kAlphaThin = 180;
static constexpr BYTE kAlphaThick = 220;

static int ScaledWidth(OverlayScrollbar* sb, bool thick) {
    int w = thick ? sb->thickWidth : sb->thinWidth;
    return DpiScale(sb->hwndOwner, w);
}

static bool IsVert(OverlayScrollbar* sb) {
    return sb->type == OverlayScrollbar::Type::Vert;
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
        return track;
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

// Get scrollbar rect in screen coordinates (for thick size, used for proximity check)
static Rect GetScrollbarScreenRect(OverlayScrollbar* sb) {
    RECT ownerRc;
    GetWindowRect(sb->hwndOwner, &ownerRc);
    int scrollW = ScaledWidth(sb, true); // use thick width for proximity
    if (IsVert(sb)) {
        return Rect(ownerRc.right - scrollW, ownerRc.top, scrollW, ownerRc.bottom - ownerRc.top);
    }
    return Rect(ownerRc.left, ownerRc.bottom - scrollW, ownerRc.right - ownerRc.left, scrollW);
}

// Distance from point to rect edge (0 if inside)
static int DistToRect(POINT pt, Rect rc) {
    int dx = 0;
    int dy = 0;
    if (pt.x < rc.x) {
        dx = rc.x - pt.x;
    } else if (pt.x >= rc.x + rc.dx) {
        dx = pt.x - (rc.x + rc.dx - 1);
    }
    if (pt.y < rc.y) {
        dy = rc.y - pt.y;
    } else if (pt.y >= rc.y + rc.dy) {
        dy = pt.y - (rc.y + rc.dy - 1);
    }
    if (dx == 0 && dy == 0) {
        return 0;
    }
    if (dx == 0) {
        return dy;
    }
    if (dy == 0) {
        return dx;
    }
    return (int)sqrt((double)(dx * dx + dy * dy));
}

// Check if hwnd is the same as or an ancestor of child
static bool IsOrIsParentOf(HWND hwnd, HWND child) {
    while (child) {
        if (child == hwnd) {
            return true;
        }
        child = GetParent(child);
    }
    return false;
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
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP hbmpOld = (HBITMAP)SelectObject(hdcMem, hbmp);

    memset(bits, 0, (size_t)w * h * 4);

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

    if (sb->isThick) {
        fillRect(Rect(0, 0, w, h), ThemeTrackColor());
    }

    Rect thumbRc = GetThumbRect(sb);
    COLORREF thumbCol = sb->mouseOverThumb ? ThemeThumbHoverColor() : ThemeThumbColor();

    if (!sb->isThick) {
        int thinW = ScaledWidth(sb, false);
        if (IsVert(sb)) {
            thumbRc.x = (w - thinW) / 2;
            thumbRc.dx = thinW;
        } else {
            thumbRc.y = (h - thinW) / 2;
            thumbRc.dy = thinW;
        }
    }
    fillRect(thumbRc, thumbCol);

    if (sb->isThick) {
        Rect arrowTop = GetArrowTopRect(sb);
        Rect arrowBot = GetArrowBottomRect(sb);

        DWORD arrowPixel = premultiply(ThemeArrowColor(), alpha);
        DWORD* pixels = (DWORD*)bits;

        if (IsVert(sb)) {
            int cx = arrowTop.x + arrowTop.dx / 2;
            int cy = arrowTop.y + arrowTop.dy / 2;
            int sz = arrowTop.dx / 4;
            // Up-pointing triangle: point at top, base at bottom
            for (int dy2 = -sz; dy2 <= sz; dy2++) {
                int halfW = (dy2 + sz) / 2;
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

            // Down-pointing triangle: base at top, point at bottom
            cx = arrowBot.x + arrowBot.dx / 2;
            cy = arrowBot.y + arrowBot.dy / 2;
            for (int dy2 = -sz; dy2 <= sz; dy2++) {
                int halfW = (sz - dy2) / 2;
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
            int cx = arrowTop.x + arrowTop.dx / 2;
            int cy = arrowTop.y + arrowTop.dy / 2;
            int sz = arrowTop.dy / 4;
            // Left-pointing triangle: point at left, base at right
            for (int dx2 = -sz; dx2 <= sz; dx2++) {
                int halfH = (dx2 + sz) / 2;
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

            // Right-pointing triangle: base at left, point at right
            cx = arrowBot.x + arrowBot.dx / 2;
            cy = arrowBot.y + arrowBot.dy / 2;
            for (int dx2 = -sz; dx2 <= sz; dx2++) {
                int halfH = (sz - dx2) / 2;
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

    POINT ptSrc = {0, 0};
    SIZE szWnd = {w, h};
    POINT ptDst = {wrc.left, wrc.top};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(sb->hwnd, hdcScreen, &ptDst, &szWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hbmpOld);
    DeleteObject(hbmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

static void ShowScrollbarWindow(OverlayScrollbar* sb, bool thick) {
    // Don't revert to thin while user is dragging the thumb
    if (sb->isDragging && !thick) {
        return;
    }
    sb->isThick = thick;
    sb->isThin = !thick;
    OverlayScrollbarUpdatePos(sb);
    ShowWindow(sb->hwnd, SW_SHOWNOACTIVATE);
    PaintScrollbar(sb);

    KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
    if (!thick) {
        SetTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide, sb->showAfterScrollMs, nullptr);
    }
}

static void HideScrollbarWindow(OverlayScrollbar* sb) {
    // Don't hide while user is dragging the thumb
    if (sb->isDragging) {
        return;
    }
    sb->isThick = false;
    sb->isThin = false;
    sb->mouseOverThumb = false;
    ShowWindow(sb->hwnd, SW_HIDE);
    KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
}

// ---- Global mouse tracking ----

static void CALLBACK MouseTrackTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    POINT pt;
    GetCursorPos(&pt);

    bool mouseMoved = (pt.x != gLastMousePos.x || pt.y != gLastMousePos.y);
    gLastMousePos = pt;

    HWND hwndForeground = GetForegroundWindow();

    for (auto* sb : gAllScrollbars) {
        if (!sb->hwnd || !sb->hwndOwner) {
            continue;
        }

        if (!sb->enabled) {
            continue;
        }

        // Only process scrollbars whose owner is in the active window hierarchy
        bool ownerActive = IsOrIsParentOf(hwndForeground, sb->hwndOwner);
        if (!ownerActive) {
            // If we were showing, hide
            if (sb->isThick || sb->isThin) {
                if (!sb->isDragging) {
                    HideScrollbarWindow(sb);
                }
            }
            continue;
        }

        // Check if mouse is over the owner window's client area
        RECT ownerRc;
        GetWindowRect(sb->hwndOwner, &ownerRc);
        bool overOwner = PtInRect(&ownerRc, pt);

        // Check distance to scrollbar area
        Rect sbRect = GetScrollbarScreenRect(sb);
        int dist = DistToRect(pt, sbRect);
        bool closeToScrollbar = (dist <= gThickVisibilityDistance);
        bool overScrollbar = (dist == 0);

        if (sb->isDragging) {
            // Don't change state while dragging
            continue;
        }

        if (gOverlayScrollbarSuppressThick) {
            continue;
        }

        if (overScrollbar) {
            // Mouse is over the scrollbar area - show thick
            if (!sb->isThick) {
                ShowScrollbarWindow(sb, true);
            }
            // Update thumb hover state
            POINT clientPt = pt;
            ScreenToClient(sb->hwnd, &clientPt);
            Rect thumbRc = GetThumbRect(sb);
            bool wasOver = sb->mouseOverThumb;
            sb->mouseOverThumb = thumbRc.Contains(Point(clientPt.x, clientPt.y));
            if (wasOver != sb->mouseOverThumb) {
                PaintScrollbar(sb);
            }
        } else if (closeToScrollbar && overOwner) {
            // Near scrollbar and over owner - show thick
            if (!sb->isThick) {
                ShowScrollbarWindow(sb, true);
            }
        } else if (overOwner && mouseMoved) {
            // Mouse is over owner and moving, but not near scrollbar - show thin
            if (sb->isThick) {
                // Transition from thick to thin
                ShowScrollbarWindow(sb, false);
            } else if (!sb->isThin) {
                ShowScrollbarWindow(sb, false);
            }
            // Reset the auto-hide timer since mouse is moving
            KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
            SetTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide, sb->hideAfterMouseStopMs, nullptr);
        } else if (sb->isThick && !overOwner) {
            // Mouse left the owner area while thick - transition to hidden
            HideScrollbarWindow(sb);
        }
        // If mouse is over owner but not moving, the existing auto-hide timer handles it
    }
}

static void StartMouseTracking() {
    if (gMouseTrackTimer) {
        return;
    }
    gLastMousePos = {-1, -1};
    gMouseTrackTimer = SetTimer(nullptr, kMouseTrackTimerID, kMouseTrackIntervalMs, MouseTrackTimerProc);
}

static void StopMouseTracking() {
    if (gMouseTrackTimer) {
        KillTimer(nullptr, gMouseTrackTimer);
        gMouseTrackTimer = 0;
    }
}

// ---- WndProc for scrollbar window (handles clicks, drag, wheel) ----

static LRESULT CALLBACK WndProcOverlayScrollbar(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayScrollbar* sb = (OverlayScrollbar*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!sb) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    switch (msg) {
        case WM_TIMER:
            if (wp == OverlayScrollbar::kTimerAutoHide) {
                if (!sb->isDragging) {
                    HideScrollbarWindow(sb);
                }
                return 0;
            }
            if (wp == OverlayScrollbar::kTimerRepeatScroll) {
                if (sb->repeatScrollCode == 0) {
                    KillTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll);
                    return 0;
                }
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(sb->repeatScrollCode, 0));
                if (sb->repeatIsInitial) {
                    // switch from initial delay to repeat rate
                    sb->repeatIsInitial = false;
                    UINT repeatMs = 0;
                    SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &repeatMs, 0);
                    // SPI_GETKEYBOARDSPEED returns 0-31, map to ~33-500ms (same as OS key repeat)
                    repeatMs = 400 - repeatMs * 12;
                    SetTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll, repeatMs, nullptr);
                }
                return 0;
            }
            break;

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);

            if (sb->isDragging) {
                int ptInTrack = IsVert(sb) ? my : mx;
                Rect track = GetTrackRect(sb);
                int range = sb->nMax - sb->nMin + 1;
                int thumbLen = MulDiv(IsVert(sb) ? track.dy : track.dx, (int)sb->nPage, range);
                int minThumb = DpiScale(sb->hwndOwner, kMinThumbSize);
                if (thumbLen < minThumb) {
                    thumbLen = minThumb;
                }
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

            // Thumb hover is handled by the global tracker, but also handle here
            // for responsiveness when already thick
            if (sb->isThick) {
                Rect thumbRc = GetThumbRect(sb);
                bool wasOver = sb->mouseOverThumb;
                sb->mouseOverThumb = thumbRc.Contains(Point(mx, my));
                if (wasOver != sb->mouseOverThumb) {
                    PaintScrollbar(sb);
                }
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);
            SetCapture(hwnd);

            if (sb->isThick) {
                Rect arrowTop = GetArrowTopRect(sb);
                Rect arrowBot = GetArrowBottomRect(sb);
                Point pt(mx, my);

                if (arrowTop.Contains(pt)) {
                    UINT code = IsVert(sb) ? SB_LINEUP : SB_LINELEFT;
                    SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                    sb->repeatScrollCode = code;
                    sb->repeatIsInitial = true;
                    UINT delayMs = 0;
                    SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delayMs, 0);
                    delayMs = 250 + delayMs * 250; // 0-3 maps to 250-1000ms
                    SetTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll, delayMs, nullptr);
                    return 0;
                }
                if (arrowBot.Contains(pt)) {
                    UINT code = IsVert(sb) ? SB_LINEDOWN : SB_LINERIGHT;
                    SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                    sb->repeatScrollCode = code;
                    sb->repeatIsInitial = true;
                    UINT delayMs = 0;
                    SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delayMs, 0);
                    delayMs = 250 + delayMs * 250;
                    SetTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll, delayMs, nullptr);
                    return 0;
                }
            }

            // Shift+click: jump thumb center to click position
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                Rect track = GetTrackRect(sb);
                int range = sb->nMax - sb->nMin + 1;
                int trackLen = IsVert(sb) ? track.dy : track.dx;
                int thumbLen = MulDiv(trackLen, (int)sb->nPage, range);
                int minThumb = DpiScale(sb->hwndOwner, kMinThumbSize);
                if (thumbLen < minThumb) {
                    thumbLen = minThumb;
                }
                int scrollableTrack = trackLen - thumbLen;
                int scrollableRange = range - (int)sb->nPage;
                int clickInTrack = (IsVert(sb) ? my : mx) - (IsVert(sb) ? track.y : track.x);
                int thumbOffset = clickInTrack - thumbLen / 2;
                thumbOffset = std::clamp(thumbOffset, 0, scrollableTrack);
                int newPos = sb->nMin;
                if (scrollableTrack > 0 && scrollableRange > 0) {
                    newPos = sb->nMin + MulDiv(thumbOffset, scrollableRange, scrollableTrack);
                }
                sb->nTrackPos = newPos;
                PaintScrollbar(sb);
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(SB_THUMBTRACK, newPos));
                ReleaseCapture();
                return 0;
            }

            Rect thumbRc = GetThumbRect(sb);
            Point pt(mx, my);
            if (thumbRc.Contains(pt)) {
                sb->isDragging = true;
                sb->dragStartY = IsVert(sb) ? my : mx;
                sb->dragStartPos = sb->nPos;
                sb->nTrackPos = sb->nPos;
                return 0;
            }

            Rect track = GetTrackRect(sb);
            if (track.Contains(pt)) {
                int clickPos = IsVert(sb) ? my : mx;
                int thumbMid = IsVert(sb) ? (thumbRc.y + thumbRc.dy / 2) : (thumbRc.x + thumbRc.dx / 2);
                UINT code;
                if (clickPos < thumbMid) {
                    code = IsVert(sb) ? SB_PAGEUP : SB_PAGELEFT;
                } else {
                    code = IsVert(sb) ? SB_PAGEDOWN : SB_PAGERIGHT;
                }
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                sb->repeatScrollCode = code;
                sb->repeatIsInitial = true;
                UINT delayMs = 0;
                SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delayMs, 0);
                delayMs = 250 + delayMs * 250;
                SetTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll, delayMs, nullptr);
                return 0;
            }
            ReleaseCapture();
            return 0;
        }

        case WM_LBUTTONUP:
            if (sb->repeatScrollCode != 0) {
                sb->repeatScrollCode = 0;
                KillTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll);
            }
            if (sb->isDragging) {
                sb->isDragging = false;
                sb->nPos = sb->nTrackPos;
                PaintScrollbar(sb);
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(SB_THUMBPOSITION, sb->nPos));
            }
            ReleaseCapture();
            return 0;

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            SendMessageW(sb->hwndOwner, msg, wp, lp);
            return 0;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_NCHITTEST: {
            // pass through rightmost 2px of vertical scrollbar for frame resize
            if (IsVert(sb)) {
                int x = GET_X_LPARAM(lp);
                RECT rc;
                GetWindowRect(hwnd, &rc);
                if ((rc.right - x) <= 2) {
                    return HTTRANSPARENT;
                }
            }
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

OverlayScrollbar* OverlayScrollbarCreate(HWND hwndOwner, OverlayScrollbar::Type type) {
    RegisterScrollbarClass();

    auto* sb = new OverlayScrollbar();
    sb->hwndOwner = hwndOwner;
    sb->type = type;

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    DWORD style = WS_POPUP;

    sb->hwnd = CreateWindowExW(exStyle, OVERLAY_SCROLLBAR_CLASS, nullptr, style, 0, 0, 1, 1, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(sb->hwnd, GWLP_USERDATA, (LONG_PTR)sb);

    // Register for global mouse tracking
    gAllScrollbars.Append(sb);
    StartMouseTracking();

    return sb;
}

void OverlayScrollbarDestroy(OverlayScrollbar* sb) {
    if (!sb) {
        return;
    }

    // Unregister from global mouse tracking
    gAllScrollbars.Remove(sb);
    if (gAllScrollbars.IsEmpty()) {
        StopMouseTracking();
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
    bool changed = false;
    if (si->fMask & SIF_RANGE) {
        if (sb->nMin != si->nMin || sb->nMax != si->nMax) {
            changed = true;
        }
        sb->nMin = si->nMin;
        sb->nMax = si->nMax;
    }
    if (si->fMask & SIF_PAGE) {
        if (sb->nPage != si->nPage) {
            changed = true;
        }
        sb->nPage = si->nPage;
    }
    if (si->fMask & SIF_POS) {
        if (sb->nPos != si->nPos) {
            changed = true;
        }
        sb->nPos = si->nPos;
    }

    if (redraw && changed) {
        if (sb->isThick || sb->isThin) {
            PaintScrollbar(sb);
        } else {
            ShowScrollbarWindow(sb, false);
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

    // Check if the sibling scrollbar (other orientation, same owner) is thick
    bool siblingThick = false;
    for (auto* other : gAllScrollbars) {
        if (other != sb && other->hwndOwner == sb->hwndOwner && other->enabled && other->isThick) {
            siblingThick = true;
            break;
        }
    }
    int siblingInset = 0;
    if (sb->isThick && siblingThick) {
        siblingInset = scrollW;
    }

    if (IsVert(sb)) {
        x = ownerRc.right - scrollW;
        y = ownerRc.top;
        w = scrollW;
        h = ownerRc.bottom - ownerRc.top - siblingInset;
    } else {
        x = ownerRc.left;
        y = ownerRc.bottom - scrollW;
        w = ownerRc.right - ownerRc.left - siblingInset;
        h = scrollW;
    }

    LONG_PTR exStyle = GetWindowLongPtrW(sb->hwnd, GWL_EXSTYLE);
    if (sb->isThick || sb->isThin) {
        exStyle &= ~WS_EX_TRANSPARENT;
    } else {
        exStyle |= WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrW(sb->hwnd, GWL_EXSTYLE, exStyle);

    SetWindowPos(sb->hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

void OverlayScrollbarShow(OverlayScrollbar* sb, bool show) {
    if (!sb) {
        return;
    }
    sb->enabled = show;
    if (show) {
        if (!sb->isThick && !sb->isThin) {
            ShowScrollbarWindow(sb, false);
        }
    } else {
        HideScrollbarWindow(sb);
    }
}
