/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/WinDynCalls.h"
#include "utils/WinUtil.h"

#include "wingui/UIModels.h"
#include "wingui/Layout.h"
#include "wingui/WinGui.h"

#include "Settings.h"
#include "AppColors.h"
#include "GlobalPrefs.h"
#include "ProgressUpdateUI.h"
#include "Annotation.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "Caption.h"
#include "Tabs.h"
#include "Translations.h"
#include "resource.h"
#include "Commands.h"
#include "Menu.h"
#include "Theme.h"

using Gdiplus::Color;
using Gdiplus::Graphics;
using Gdiplus::Pen;
using Gdiplus::SolidBrush;

#define UNDOCUMENTED_MENU_CLASS_NAME L"#32768"

// This will prevent menu reopening, if we click the menu button,
// while the menu is open.
#define DO_NOT_REOPEN_MENU_TIMER_ID 1
#define DO_NOT_REOPEN_MENU_DELAY_IN_MS 1

// undocumented caption buttons state
#define CBS_INACTIVE 5

// undocumented window messages
#define WM_NCUAHDRAWCAPTION 0xAE
#define WM_NCUAHDRAWFRAME 0xAF
#define WM_POPUPSYSTEMMENU 0x313

// When a top level window is maximized the window manager checks whether its client
// area covers the entire screen. If it does, the manager assumes that this is a fullscreen
// window and hides the taskbar and any topmost window. A simple workaround is to
// expand the non-client border at the expense of client area.
#define NON_CLIENT_BAND 1

// structs defined in Caption.h

static void DrawCaptionButton(HDC hdc, int button, MainWindow* win);
static HMENU GetUpdatedSystemMenu(HWND hwnd, bool changeDefaultItem);
static void MenuBarAsPopupMenu(MainWindow* win, int x, int y);
static void HandleCaptionClick(MainWindow* win, int btnIdx);

CaptionInfo::CaptionInfo(HWND frame) : hwndFrame(frame) {
    UpdateTheme();
    UpdateColors(true);
}

CaptionInfo::~CaptionInfo() {
    if (theme) {
        theme::CloseThemeData(theme);
    }
}

void CaptionInfo::UpdateTheme() {
    if (theme) {
        theme::CloseThemeData(theme);
        theme = nullptr;
    }
    if (theme::IsThemeActive()) {
        theme = theme::OpenThemeData(hwndFrame, L"WINDOW");
    }
}

void CaptionInfo::UpdateColors(bool activeWindow) {
    // Use the same background color as the tab bar.
    bgColor = ThemeControlBackgroundColor();
    textColor = ThemeWindowTextColor();
}

void SetCaptionButtonsRtl(CaptionInfo*, bool) {
    // no-op: buttons are no longer child windows
}

// TODO: could lookup MainWindow ourselves
void CaptionUpdateUI(MainWindow* win, CaptionInfo* caption) {
    caption->UpdateTheme();
    caption->UpdateColors(win->hwndFrame == GetForegroundWindow());
}

void DeleteCaption(CaptionInfo* caption) {
    delete caption;
}

void OpenSystemMenu(MainWindow* win) {
    Rect r = win->caption->btn[CB_SYSTEM_MENU].rect;
    Rect rScreen = MapRectToWindow(r, win->hwndFrame, HWND_DESKTOP);
    HMENU systemMenu = GetUpdatedSystemMenu(win->hwndFrame, false);
    uint flags = 0;
    TrackPopupMenuEx(systemMenu, flags, rScreen.x, rScreen.y + rScreen.dy, win->hwndFrame, nullptr);
}

// Returns the button index at the given client point, or -1 if none.
static int CaptionButtonAt(CaptionInfo* ci, Point pt) {
    for (int i = CB_BTN_FIRST; i < CB_BTN_COUNT; i++) {
        if (ci->btn[i].visible && ci->btn[i].rect.Contains(pt)) {
            return i;
        }
    }
    return -1;
}

static void RepaintButton(HWND hwnd, int btnIdx, MainWindow* win) {
    if (false) {
        RECT rc = ToRECT(win->caption->btn[btnIdx].rect);
        InvalidateRect(hwnd, &rc, FALSE);
        UpdateWindow(hwnd);
    } else {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

static void ClearAllHighlights(MainWindow* win) {
    CaptionInfo* ci = win->caption;
    for (int i = CB_BTN_FIRST; i < CB_BTN_COUNT; i++) {
        if (ci->btn[i].highlighted || ci->btn[i].pressed) {
            ci->btn[i].highlighted = false;
            ci->btn[i].pressed = false;
            RepaintButton(win->hwndFrame, i, win);
        }
    }
}

static void HandleCaptionClick(MainWindow* win, int btnIdx) {
    switch (btnIdx) {
        case CB_MINIMIZE:
            PostMessageW(win->hwndFrame, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            break;
        case CB_MAXIMIZE:
            PostMessageW(win->hwndFrame, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
            break;
        case CB_RESTORE:
            PostMessageW(win->hwndFrame, WM_SYSCOMMAND, SC_RESTORE, 0);
            break;
        case CB_CLOSE:
            PostMessageW(win->hwndFrame, WM_SYSCOMMAND, SC_CLOSE, 0);
            break;
        case CB_MENU:
            if (!KillTimer(win->hwndFrame, DO_NOT_REOPEN_MENU_TIMER_ID) && !win->caption->isMenuOpen) {
                Rect r = win->caption->btn[CB_MENU].rect;
                Rect rScreen = MapRectToWindow(r, win->hwndFrame, HWND_DESKTOP);
                win->caption->isMenuOpen = true;
                RepaintButton(win->hwndFrame, CB_MENU, win);
                MenuBarAsPopupMenu(win, rScreen.x, rScreen.y + rScreen.dy);
                win->caption->isMenuOpen = false;
                RepaintButton(win->hwndFrame, CB_MENU, win);
                SetTimer(win->hwndFrame, DO_NOT_REOPEN_MENU_TIMER_ID, DO_NOT_REOPEN_MENU_DELAY_IN_MS, nullptr);
            }
            HwndSetFocus(win->hwndFrame);
            break;
        case CB_SYSTEM_MENU:
            OpenSystemMenu(win);
            break;
    }
}

void CreateCaption(MainWindow* win) {
    win->caption = new CaptionInfo(win->hwndFrame);
}

void RelayoutCaption(MainWindow* win) {
    Rect rc = win->caption->captionRect;
    CaptionInfo* ci = win->caption;
    bool maximized = IsZoomed(win->hwndFrame);

    // Min/max/close buttons touch the top edge (y=0), spanning the full height
    // from top edge to caption bottom. They are square based on that height.
    int btnDy = rc.y + rc.dy;
    int btnDx = btnDy;

    ci->btn[CB_CLOSE].rect = {rc.x + rc.dx - btnDx, 0, btnDx, btnDy};
    ci->btn[CB_CLOSE].visible = true;
    rc.dx -= btnDx;

    ci->btn[CB_RESTORE].rect = {rc.x + rc.dx - btnDx, 0, btnDx, btnDy};
    ci->btn[CB_RESTORE].visible = maximized;

    ci->btn[CB_MAXIMIZE].rect = {rc.x + rc.dx - btnDx, 0, btnDx, btnDy};
    ci->btn[CB_MAXIMIZE].visible = !maximized;
    rc.dx -= btnDx;

    ci->btn[CB_MINIMIZE].rect = {rc.x + rc.dx - btnDx, 0, btnDx, btnDy};
    ci->btn[CB_MINIMIZE].visible = true;
    rc.dx -= btnDx;

    int tabHeight = GetTabbarHeight(win->hwndFrame);
    rc.y += rc.dy - tabHeight;

    ci->btn[CB_SYSTEM_MENU].rect = {rc.x, rc.y, tabHeight, tabHeight};
    ci->btn[CB_SYSTEM_MENU].visible = true;
    rc.x += tabHeight;
    rc.dx -= tabHeight;

    ci->btn[CB_MENU].rect = {rc.x, rc.y, tabHeight, tabHeight};
    ci->btn[CB_MENU].visible = true;
    rc.x += tabHeight;
    rc.dx -= tabHeight;

    DeferWinPosHelper dh;
    dh.SetWindowPos(win->tabsCtrl->hwnd, nullptr, rc.x, rc.y, rc.dx, tabHeight, SWP_NOZORDER);
    dh.End();

    UpdateTabWidth(win);

    // Invalidate button areas so they paint after the window is shown.
    // Use TRUE for erase so WM_ERASEBKGND fires and PaintCaption runs.
    for (int i = CB_BTN_FIRST; i < CB_BTN_COUNT; i++) {
        if (ci->btn[i].visible) {
            RECT r = ToRECT(ci->btn[i].rect);
            InvalidateRect(win->hwndFrame, &r, TRUE);
        }
    }
}

// Draw a single caption button at its rect position.
static void DrawCaptionButton(HDC hdc, int button, MainWindow* win) {
    ButtonInfo* bi = &win->caption->btn[button];
    if (!bi->visible) {
        return;
    }
    Rect rButton = bi->rect;
    Rect rc = rButton;

    bool isSysButton = (button == CB_MINIMIZE || button == CB_MAXIMIZE || button == CB_RESTORE || button == CB_CLOSE);

    int stateId;
    if (bi->pressed) {
        stateId = CBS_PUSHED;
    } else if (bi->highlighted) {
        stateId = CBS_HOT;
    } else if (bi->inactive) {
        stateId = CBS_INACTIVE;
    } else {
        stateId = CBS_NORMAL;
    }

    Graphics gfx(hdc);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);

    if (isSysButton) {
        // background
        COLORREF bgc = win->caption->bgColor;
        SolidBrush bgBrNormal(GdiRgbFromCOLORREF(bgc));
        gfx.FillRectangle(&bgBrNormal, rButton.x, rButton.y, rButton.dx, rButton.dy);

        bool isClose = (button == CB_CLOSE);
        bool isHot = (stateId == CBS_HOT);
        bool isPushed = (stateId == CBS_PUSHED);
        bool isInactive = (stateId == CBS_INACTIVE);

        if (isHot || isPushed) {
            Color bgCol;
            if (isClose) {
                bgCol = isPushed ? Color(200, 196, 43, 28) : Color(255, 196, 43, 28);
            } else {
                bgCol = isPushed ? Color(255, 204, 204, 204) : Color(255, 229, 229, 229);
            }
            SolidBrush bgBr(bgCol);
            gfx.FillRectangle(&bgBr, rButton.x, rButton.y, rButton.dx, rButton.dy);
        }

        Color iconCol;
        if (isInactive) {
            iconCol = Color(153, 153, 153);
        } else if (isClose && (isHot || isPushed)) {
            iconCol = Color(255, 255, 255);
        } else {
            COLORREF tc = win->caption->textColor;
            iconCol = Color(GetRValue(tc), GetGValue(tc), GetBValue(tc));
        }

        int iconSz = rc.dy * 10 / 30;
        if (iconSz < 6) {
            iconSz = 6;
        }
        iconSz = iconSz & ~1;
        int ix = rc.x + (rc.dx - iconSz) / 2;
        int iy = rc.y + (rc.dy - iconSz) / 2;

        Pen pen(iconCol, 1.0f);

        switch (button) {
            case CB_CLOSE:
                gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                gfx.DrawLine(&pen, ix, iy, ix + iconSz, iy + iconSz);
                gfx.DrawLine(&pen, ix + iconSz, iy, ix, iy + iconSz);
                break;
            case CB_MAXIMIZE:
                gfx.DrawRectangle(&pen, ix, iy, iconSz, iconSz);
                break;
            case CB_MINIMIZE: {
                int midY = iy + iconSz / 2;
                gfx.DrawLine(&pen, ix, midY, ix + iconSz, midY);
            } break;
            case CB_RESTORE: {
                int off = iconSz / 3;
                int sz = iconSz - off;
                gfx.DrawRectangle(&pen, ix, iy + off, sz, sz);
                gfx.DrawLine(&pen, ix + off, iy, ix + iconSz, iy);
                gfx.DrawLine(&pen, ix + iconSz, iy, ix + iconSz, iy + sz);
                gfx.DrawLine(&pen, ix + sz, iy + off, ix + iconSz, iy + off);
                gfx.DrawLine(&pen, ix + off, iy, ix + off, iy + off);
            } break;
        }
    } else if (button == CB_MENU) {
        // Fill button rect with caption background
        SolidBrush bgBrMenu(GdiRgbFromCOLORREF(win->caption->bgColor));
        gfx.FillRectangle(&bgBrMenu, rButton.x, rButton.y, rButton.dx, rButton.dy);

        if (win->caption->isMenuOpen) {
            stateId = CBS_PUSHED;
        }
        BYTE buttonRGB = 1;
        if (CBS_PUSHED == stateId) {
            buttonRGB = 0;
        } else if (CBS_HOT == stateId) {
            buttonRGB = 255;
        }

        if (buttonRGB != 1) {
            if (GetLightness(win->caption->textColor) > GetLightness(win->caption->bgColor)) {
                buttonRGB ^= 0xff;
            }
            BYTE buttonAlpha = BYTE((255 - abs((int)GetLightness(win->caption->bgColor) - buttonRGB)) / 2);
            SolidBrush br(Color(buttonAlpha, buttonRGB, buttonRGB, buttonRGB));
            gfx.FillRectangle(&br, rc.x, rc.y, rc.dx, rc.dy);
        }
        COLORREF c = ThemeWindowTextColor();
        u8 r, g, b;
        UnpackColor(c, r, g, b);
        float width = floor((float)rc.dy / 8.0f);
        Pen p(Color(r, g, b), width);
        rc.Inflate(-int(rc.dx * 0.2f + 0.5f), -int(rc.dy * 0.3f + 0.5f));
        for (int i = 0; i < 3; i++) {
            gfx.DrawLine(&p, rc.x, rc.y + i * rc.dy / 2, rc.x + rc.dx, rc.y + i * rc.dy / 2);
        }
    } else if (button == CB_SYSTEM_MENU) {
        // Fill button rect with caption background
        SolidBrush bgBrSys(GdiRgbFromCOLORREF(win->caption->bgColor));
        gfx.FillRectangle(&bgBrSys, rButton.x, rButton.y, rButton.dx, rButton.dy);
        int xIcon = GetSystemMetrics(SM_CXSMICON);
        int yIcon = GetSystemMetrics(SM_CYSMICON);
        HICON hIcon = (HICON)GetClassLongPtr(win->hwndFrame, GCLP_HICONSM);
        int x = rButton.x + (rButton.dx - xIcon) / 2;
        int y = rButton.y + (rButton.dy - yIcon) / 2;
        DrawIconEx(hdc, x, y, hIcon, xIcon, yIcon, 0, nullptr, DI_NORMAL);
    }
}

// Paint all caption buttons. Called from the frame's paint handler.
void PaintCaption(HDC hdc, MainWindow* win) {
    if (!win || !win->caption || !win->tabsInTitlebar) {
        return;
    }
    for (int i = CB_BTN_FIRST; i < CB_BTN_COUNT; i++) {
        DrawCaptionButton(hdc, i, win);
    }
}

// accelerator key which was pressed when invoking the "menubar",
// needs to be passed from WM_SYSCOMMAND to WM_INITMENUPOPUP
// (can be static because there can only be one menu active at a time)
static WCHAR gMenuAccelPressed = 0;

LRESULT CustomCaptionFrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool* callDef, MainWindow* win) {
    switch (msg) {
        case WM_SETTINGCHANGE:
            if (wp == SPI_SETNONCLIENTMETRICS) {
                RelayoutCaption(win);
            }
            break;

        case WM_NCPAINT:
            *callDef = false;
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            {
                // Fill the entire caption area (top padding + caption rect) with background
                HBRUSH br = CreateSolidBrush(ThemeControlBackgroundColor());
                Rect cr = win->caption->captionRect;
                RECT rcFill = {cr.x, 0, cr.x + cr.dx, cr.y + cr.dy};
                FillRect(hdc, &rcFill, br);
                DeleteObject(br);
            }
            PaintCaption(hdc, win);
            EndPaint(hwnd, &ps);
            *callDef = false;
            return 0;
        }

        case WM_NCACTIVATE:
            win->caption->UpdateColors((bool)wp);
            for (int i = CB_BTN_FIRST; i < CB_BTN_COUNT; i++) {
                win->caption->btn[i].inactive = wp == FALSE;
            }
            if (!IsIconic(hwnd)) {
                uint flags = RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN;
                RedrawWindow(hwnd, nullptr, nullptr, flags);
                *callDef = false;
                return TRUE;
            }
            break;

        case WM_TIMER:
            if (wp == DO_NOT_REOPEN_MENU_TIMER_ID) {
                KillTimer(hwnd, DO_NOT_REOPEN_MENU_TIMER_ID);
                *callDef = false;
                return 0;
            }
            break;

        case WM_THEMECHANGED:
            if (win) {
                win->caption->UpdateTheme();
            }
            break;

        case WM_NCUAHDRAWCAPTION:
        case WM_NCUAHDRAWFRAME:
            *callDef = false;
            return TRUE;

        case WM_POPUPSYSTEMMENU:
        case WM_SETCURSOR:
        case WM_SETTEXT:
        case WM_SETICON:
            if (!win->caption->theme && IsWindowVisible(hwnd)) {
                // Remove the WS_VISIBLE style to prevent DefWindowProc from drawing
                // in the caption's area when processing these mesages.
                SetWindowStyle(hwnd, WS_VISIBLE, false);
                LRESULT res = DefWindowProc(hwnd, msg, wp, lp);
                SetWindowStyle(hwnd, WS_VISIBLE, true);
                *callDef = false;
                return res;
            }
            break;

        case WM_NCCALCSIZE: {
            // Make the entire window area the client area (frameless window).
            RECT* r = wp == TRUE ? &((NCCALCSIZE_PARAMS*)lp)->rgrc[0] : (RECT*)lp;
            if (IsZoomed(hwnd)) {
                // When maximized, the window extends beyond the screen by the frame thickness.
                // Adjust the client rect inward so it fits within the visible monitor area,
                // and leave a 1px bottom band to prevent fullscreen detection hiding the taskbar.
                int frameX = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int frameY = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                r->left += frameX;
                r->top += frameY;
                r->right -= frameX;
                r->bottom -= frameY;
                r->bottom -= NON_CLIENT_BAND;
            }
            *callDef = false;
            return 0;
        }

        case WM_NCHITTEST: {
            // Hit testing for resize borders and custom caption area.
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            RECT wrc;
            GetWindowRect(hwnd, &wrc);

            if (!IsZoomed(hwnd)) {
                // The invisible shadow extends by the system frame size.
                // We add 3px beyond the visible edge for the resize hit zone.
                int frameX = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int frameY = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int borderX = frameX + 3;
                int borderY = frameY + 3;
                // Check resize borders
                bool onLeft = (x - wrc.left) < borderX;
                bool onRight = (wrc.right - x) < borderX;
                bool onTop = (y - wrc.top) < borderY;
                bool onBottom = (wrc.bottom - y) < borderY;

                if (onTop && onLeft) {
                    *callDef = false;
                    return HTTOPLEFT;
                }
                if (onTop && onRight) {
                    *callDef = false;
                    return HTTOPRIGHT;
                }
                if (onBottom && onLeft) {
                    *callDef = false;
                    return HTBOTTOMLEFT;
                }
                if (onBottom && onRight) {
                    *callDef = false;
                    return HTBOTTOMRIGHT;
                }
                if (onLeft) {
                    *callDef = false;
                    return HTLEFT;
                }
                if (onRight) {
                    *callDef = false;
                    return HTRIGHT;
                }
                if (onTop) {
                    *callDef = false;
                    return HTTOP;
                }
                if (onBottom) {
                    *callDef = false;
                    return HTBOTTOM;
                }
            }

            // Check if over a caption button
            {
                Point ptClient{x, y};
                HwndScreenToClient(hwnd, ptClient);
                int btnIdx = CaptionButtonAt(win->caption, ptClient);
                if (btnIdx >= 0) {
                    // Return HTMAXBUTTON for maximize/restore (Win11 snap layouts)
                    // Return HTCLIENT for other buttons so we get regular mouse messages
                    if (btnIdx == CB_MAXIMIZE || btnIdx == CB_RESTORE) {
                        *callDef = false;
                        return HTMAXBUTTON;
                    }
                    *callDef = false;
                    return HTCLIENT;
                }
            }

            // Check if in the caption area (above the tab bar bottom edge)
            {
                Point pt{x, y};
                Rect rClient = MapRectToWindow(ClientRect(hwnd), hwnd, HWND_DESKTOP);
                Rect rCaption = MapRectToWindow(win->caption->captionRect, hwnd, HWND_DESKTOP);
                if (rClient.Contains(pt) && pt.y < rCaption.y + rCaption.dy) {
                    *callDef = false;
                    return HTCAPTION;
                }
            }
        } break;

        case WM_NCLBUTTONDOWN:
            if (wp == HTMAXBUTTON) {
                // Suppress DefWindowProc which would start a maximize drag.
                *callDef = false;
                return 0;
            }
            break;

        case WM_NCLBUTTONUP:
            if (wp == HTMAXBUTTON) {
                WPARAM cmd = IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE;
                PostMessageW(hwnd, WM_SYSCOMMAND, cmd, 0);
                *callDef = false;
                return 0;
            }
            break;

        case WM_NCMOUSEMOVE: {
            int btnIdx = IsZoomed(hwnd) ? CB_RESTORE : CB_MAXIMIZE;
            if (wp == HTMAXBUTTON) {
                if (!win->caption->btn[btnIdx].highlighted) {
                    win->caption->btn[btnIdx].highlighted = true;
                    RepaintButton(hwnd, btnIdx, win);
                }
            } else {
                if (win->caption->btn[btnIdx].highlighted) {
                    win->caption->btn[btnIdx].highlighted = false;
                    RepaintButton(hwnd, btnIdx, win);
                }
            }
        } break;

        case WM_NCMOUSELEAVE:
            ClearAllHighlights(win);
            break;

        case WM_MOUSEMOVE: {
            // Track button hover for buttons that return HTCLIENT.
            Point ptm{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int btnIdx = CaptionButtonAt(win->caption, ptm);
            for (int i = CB_BTN_FIRST; i < CB_BTN_COUNT; i++) {
                bool shouldHighlight = (i == btnIdx);
                if (win->caption->btn[i].highlighted != shouldHighlight) {
                    win->caption->btn[i].highlighted = shouldHighlight;
                    RepaintButton(hwnd, i, win);
                }
            }
            if (btnIdx >= 0) {
                TrackMouseLeave(hwnd);
            }
        } break;

        case WM_MOUSELEAVE:
            ClearAllHighlights(win);
            break;

        case WM_LBUTTONDOWN: {
            Point ptd{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int btnIdx = CaptionButtonAt(win->caption, ptd);
            if (btnIdx >= 0) {
                win->caption->btn[btnIdx].pressed = true;
                RepaintButton(hwnd, btnIdx, win);
                SetCapture(hwnd);
                *callDef = false;
                return 0;
            }
        } break;

        case WM_LBUTTONUP: {
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            Point ptu{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int btnIdx = CaptionButtonAt(win->caption, ptu);
            // Find which button was pressed
            for (int i = CB_BTN_FIRST; i < CB_BTN_COUNT; i++) {
                if (win->caption->btn[i].pressed) {
                    win->caption->btn[i].pressed = false;
                    RepaintButton(hwnd, i, win);
                    if (i == btnIdx) {
                        HandleCaptionClick(win, i);
                    }
                }
            }
            *callDef = false;
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            Point ptdc{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int btnIdx = CaptionButtonAt(win->caption, ptdc);
            if (btnIdx == CB_SYSTEM_MENU) {
                PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
                *callDef = false;
                return 0;
            }
        } break;

        case WM_NCRBUTTONUP:
            // Prepare and show the system menu.
            if (wp == HTCAPTION) {
                HMENU menu = GetUpdatedSystemMenu(hwnd, true);
                uint flags = TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD;
                if (GetSystemMetrics(SM_MENUDROPALIGNMENT)) {
                    flags |= TPM_RIGHTALIGN;
                }
                WPARAM cmd = TrackPopupMenu(menu, flags, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, hwnd, nullptr);
                if (cmd) {
                    PostMessageW(hwnd, WM_SYSCOMMAND, cmd, 0);
                }
                *callDef = false;
                return 0;
            }
            break;

        case WM_SYSCOMMAND:
            if (wp == SC_KEYMENU) {
                // Show the "menu bar" (and the desired submenu)
                gMenuAccelPressed = (WCHAR)lp;
                if (' ' == gMenuAccelPressed) {
                    // map space to the accelerator of the Window menu
                    auto pos = str::FindChar(_TRA("&Window"), '&');
                    if (pos) {
                        char c = pos[1];
                        gMenuAccelPressed = (WCHAR)c;
                    }
                }
                HandleCaptionClick(win, CB_MENU);
                *callDef = false;
                return 0;
            }
            break;

        case WM_INITMENUPOPUP:
            if (gMenuAccelPressed) {
                // poorly documented hack: find the menu window and send it the accelerator key
                HWND hMenu = FindWindow(UNDOCUMENTED_MENU_CLASS_NAME, nullptr);
                if (hMenu) {
                    if ('a' <= gMenuAccelPressed && gMenuAccelPressed <= 'z') {
                        gMenuAccelPressed -= 'a' - 'A';
                    }
                    if ('A' <= gMenuAccelPressed && gMenuAccelPressed <= 'Z') {
                        PostMessageW(hMenu, WM_KEYDOWN, gMenuAccelPressed, 0);
                    } else {
                        PostMessageW(hMenu, WM_CHAR, gMenuAccelPressed, 0);
                    }
                }
                gMenuAccelPressed = 0;
            }
            break;

        case WM_SYSCOLORCHANGE:
            win->caption->UpdateColors(hwnd == GetForegroundWindow());
            break;
    }

    *callDef = true;
    return 0;
}

static HMENU GetUpdatedSystemMenu(HWND hwnd, bool changeDefaultItem) {
    // don't reset the system menu (in case other applications have added to it)
    HMENU menu = GetSystemMenu(hwnd, FALSE);

    // prevents drawing in the caption's area
    SetWindowStyle(hwnd, WS_VISIBLE, false);

    bool maximized = IsZoomed(hwnd);
    EnableMenuItem(menu, SC_SIZE, maximized ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(menu, SC_MOVE, maximized ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(menu, SC_MINIMIZE, MF_ENABLED);
    EnableMenuItem(menu, SC_MAXIMIZE, maximized ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(menu, SC_CLOSE, MF_ENABLED);
    EnableMenuItem(menu, SC_RESTORE, maximized ? MF_ENABLED : MF_GRAYED);
    if (changeDefaultItem) {
        SetMenuDefaultItem(menu, maximized ? SC_RESTORE : SC_MAXIMIZE, FALSE);
    } else {
        SetMenuDefaultItem(menu, SC_CLOSE, FALSE);
    }

    SetWindowStyle(hwnd, WS_VISIBLE, true);

    return menu;
}

static void MenuBarAsPopupMenu(MainWindow* win, int x, int y) {
    int count = GetMenuItemCount(win->menu);
    if (count <= 0) {
        return;
    }
    HMENU popup = CreatePopupMenu();

    MENUITEMINFO mii{};
    mii.cbSize = sizeof(MENUITEMINFO);
    mii.fMask = MIIM_SUBMENU | MIIM_STRING;
    for (int i = 0; i < count; i++) {
        mii.dwTypeData = nullptr;
        GetMenuItemInfo(win->menu, i, TRUE, &mii);
        if (!mii.hSubMenu || !mii.cch) {
            continue;
        }
        mii.cch++;
        AutoFreeWStr subMenuName(AllocArray<WCHAR>(mii.cch));
        mii.dwTypeData = subMenuName;
        GetMenuItemInfo(win->menu, i, TRUE, &mii);
        AppendMenuW(popup, MF_POPUP | MF_STRING, (UINT_PTR)mii.hSubMenu, subMenuName);
    }

    if (IsUIRtl()) {
        x += win->caption->btn[CB_MENU].rect.dx;
    }

    MarkMenuOwnerDraw(popup);
    TrackPopupMenu(popup, TPM_LEFTALIGN, x, y, 0, win->hwndFrame, nullptr);
    FreeMenuOwnerDrawInfoData(popup);

    while (count > 0) {
        --count;
        RemoveMenu(popup, count, MF_BYPOSITION);
    }
    DestroyMenu(popup);
}
