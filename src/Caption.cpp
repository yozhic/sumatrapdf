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

#define CUSTOM_CAPTION_CLASS_NAME L"CustomCaption"
#define UNDOCUMENTED_MENU_CLASS_NAME L"#32768"

#define BTN_ID_FIRST 100

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

enum CaptionButtons {
    CB_BTN_FIRST = 0,
    CB_MINIMIZE = CB_BTN_FIRST,
    CB_MAXIMIZE,
    CB_RESTORE,
    CB_CLOSE,
    CB_MENU,
    CB_SYSTEM_MENU,
    CB_BTN_COUNT
};

struct ButtonInfo {
    HWND hwnd = nullptr;
    bool highlighted = false;
    bool inactive = false;
    // form the inner rectangle where the button image is drawn
    RECT margins{};

    ButtonInfo() = default;
};

struct CaptionInfo {
    HWND hwnd = nullptr;

    ButtonInfo btn[CB_BTN_COUNT];
    HTHEME theme = nullptr;
    COLORREF bgColor = 0;
    COLORREF textColor = 0;
    bool isMenuOpen = false;

    explicit CaptionInfo(HWND hwndCaption);
    ~CaptionInfo();

    void UpdateTheme();
    void UpdateColors(bool activeWindow);
};

static void DrawCaptionButton(DRAWITEMSTRUCT* item, MainWindow* win);
static void PaintCaptionBackground(HDC hdc, MainWindow* win, bool useDoubleBuffer);
static HMENU GetUpdatedSystemMenu(HWND hwnd, bool changeDefaultItem);
static void MenuBarAsPopupMenu(MainWindow* win, int x, int y);

CaptionInfo::CaptionInfo(HWND hwndCaption) : hwnd(hwndCaption) {
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
        theme = theme::OpenThemeData(hwnd, L"WINDOW");
    }
}

void CaptionInfo::UpdateColors(bool activeWindow) {
    // Use the same background color as the tab bar.
    bgColor = ThemeControlBackgroundColor();
    textColor = ThemeWindowTextColor();
}

// TODO: not sure if needed, those are bitmaps
void SetCaptionButtonsRtl(CaptionInfo* caption, bool isRTL) {
    for (int i = CB_BTN_FIRST; i < CB_BTN_COUNT; i++) {
        HwndSetRtl(caption->btn[i].hwnd, isRTL);
    }
}

// TODO: could lookup MainWindow ourselves
void CaptionUpdateUI(MainWindow* win, CaptionInfo* caption) {
    caption->UpdateTheme();
    caption->UpdateColors(win->hwndFrame == GetForegroundWindow());
}

void DeleteCaption(CaptionInfo* caption) {
    delete caption;
}

static LRESULT CALLBACK WndProcCaption(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* win = FindMainWindowByHwnd(hwnd);

    switch (msg) {
        case WM_COMMAND:
            if (win && BN_CLICKED == HIWORD(wp)) {
                WPARAM cmd;
                WORD button = LOWORD(wp) - BTN_ID_FIRST;
                switch (button) {
                    case CB_MINIMIZE:
                        cmd = SC_MINIMIZE;
                        break;
                    case CB_MAXIMIZE:
                        cmd = SC_MAXIMIZE;
                        break;
                    case CB_RESTORE:
                        cmd = SC_RESTORE;
                        break;
                    case CB_CLOSE:
                        cmd = SC_CLOSE;
                        break;
                    default:
                        cmd = 0;
                        break;
                }
                if (cmd) {
                    PostMessageW(win->hwndFrame, WM_SYSCOMMAND, cmd, 0);
                }

                if (button == CB_MENU) {
                    if (!KillTimer(hwnd, DO_NOT_REOPEN_MENU_TIMER_ID) && !win->caption->isMenuOpen) {
                        HWND hMenuButton = win->caption->btn[CB_MENU].hwnd;
                        Rect wr = WindowRect(hMenuButton);
                        win->caption->isMenuOpen = true;
                        if (!lp) {
                            // if the WM_COMMAND message was sent as a result of keyboard command
                            InvalidateRgn(hMenuButton, nullptr, FALSE);
                        }
                        MenuBarAsPopupMenu(win, wr.x, wr.y + wr.dy);
                        win->caption->isMenuOpen = false;
                        if (!lp) {
                            InvalidateRgn(hMenuButton, nullptr, FALSE);
                        }
                        SetTimer(hwnd, DO_NOT_REOPEN_MENU_TIMER_ID, DO_NOT_REOPEN_MENU_DELAY_IN_MS, nullptr);
                    }
                    HwndSetFocus(win->hwndFrame);
                }
            }
            break;

        case WM_TIMER:
            if (wp == DO_NOT_REOPEN_MENU_TIMER_ID) {
                KillTimer(hwnd, DO_NOT_REOPEN_MENU_TIMER_ID);
            }
            break;

        case WM_SIZE:
            if (win) {
                RelayoutCaption(win);
            }
            break;

        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_ERASEBKGND:
            if (win) {
                PaintCaptionBackground((HDC)wp, win, true);
            }
            return TRUE;

        case WM_DRAWITEM:
            if (win) {
                DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
                int index = dis->CtlID - BTN_ID_FIRST;
                if (CB_MENU == index && win->caption->isMenuOpen) {
                    dis->itemState |= ODS_SELECTED;
                }
                if (win->caption->btn[index].highlighted) {
                    dis->itemState |= ODS_HOTLIGHT;
                } else if (win->caption->btn[index].inactive) {
                    dis->itemState |= ODS_INACTIVE;
                }
                DrawCaptionButton(dis, win);
            }
            return TRUE;

        case WM_THEMECHANGED:
            if (win) {
                win->caption->UpdateTheme();
            }
            break;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

void OpenSystemMenu(MainWindow* win) {
    HWND hwndSysMenu = win->caption->btn[CB_SYSTEM_MENU].hwnd;
    HMENU systemMenu = GetUpdatedSystemMenu(win->hwndFrame, false);
    RECT rc;
    GetWindowRect(hwndSysMenu, &rc);

    uint flags = 0;
    TrackPopupMenuEx(systemMenu, flags, rc.left, rc.bottom, win->hwndFrame, nullptr);
}

static WNDPROC DefWndProcButton = nullptr;
static LRESULT CALLBACK WndProcButton(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* win = FindMainWindowByHwnd(hwnd);
    int index = (int)GetWindowLongPtr(hwnd, GWLP_ID) - BTN_ID_FIRST;

    switch (msg) {
        case WM_MOUSEMOVE: {
            if (CB_SYSTEM_MENU == index && (wp & MK_LBUTTON)) {
                ReleaseCapture();
                // Trigger system move, there will be no WM_LBUTTONUP event for the button
                SendMessageW(win->hwndFrame, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
                return 0;
            } else {
                Rect rc = ClientRect(hwnd);
                int x = GET_X_LPARAM(lp);
                int y = GET_Y_LPARAM(lp);
                if (!rc.Contains(Point(x, y))) {
                    ReleaseCapture();
                    return 0;
                }
                if (win) {
                    if (TrackMouseLeave(hwnd)) {
                        win->caption->btn[index].highlighted = true;
                        InvalidateRgn(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
            }
        } break;

        case WM_MOUSELEAVE:
            if (win) {
                win->caption->btn[index].highlighted = false;
                InvalidateRgn(hwnd, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_LBUTTONDOWN:
            if (CB_MENU == index) {
                PostMessageW(hwnd, WM_LBUTTONUP, 0, lp);
            }
            return CallWindowProc(DefWndProcButton, hwnd, msg, wp, lp);

        case WM_RBUTTONUP:
        case WM_LBUTTONUP:
            if (CB_SYSTEM_MENU == index) {
                // Open system menu on click if not dragged (mouse move + left click
                // will trigger system move, see MOUSEMOVE event)
                OpenSystemMenu(win);
            }
            break;

        case WM_LBUTTONDBLCLK:
            if (CB_SYSTEM_MENU == index) {
                PostMessageW(win->hwndFrame, WM_SYSCOMMAND, SC_CLOSE, 0);
            }
            break;

        case WM_KEYDOWN:
            if (CB_MENU == index && win && !win->caption->isMenuOpen &&
                (VK_RETURN == wp || VK_SPACE == wp || VK_UP == wp || VK_DOWN == wp)) {
                PostMessageW(hwnd, BM_CLICK, 0, 0);
            }
            return CallWindowProc(DefWndProcButton, hwnd, msg, wp, lp);
    }
    return CallWindowProc(DefWndProcButton, hwnd, msg, wp, lp);
}

void CreateCaption(MainWindow* win) {
    HMODULE h = GetModuleHandleW(nullptr);
    DWORD dwStyle = WS_CHILDWINDOW | WS_CLIPCHILDREN;
    HWND hwndParent = win->hwndFrame;
    win->hwndCaption =
        CreateWindow(CUSTOM_CAPTION_CLASS_NAME, L"", dwStyle, 0, 0, 0, 0, hwndParent, nullptr, h, nullptr);

    win->caption = new CaptionInfo(win->hwndCaption);

    dwStyle = WS_CHILDWINDOW | WS_VISIBLE | BS_OWNERDRAW;
    hwndParent = win->hwndCaption;
    for (UINT_PTR i = CB_BTN_FIRST; i < CB_BTN_COUNT; i++) {
        HMENU id = (HMENU)(BTN_ID_FIRST + i);
        HWND btn = CreateWindowExW(0, L"BUTTON", L"", dwStyle, 0, 0, 0, 0, hwndParent, id, h, nullptr);
        if (!DefWndProcButton) {
            DefWndProcButton = (WNDPROC)GetWindowLongPtr(btn, GWLP_WNDPROC);
        }
        SetWindowLongPtr(btn, GWLP_WNDPROC, (LONG_PTR)WndProcButton);
        win->caption->btn[i].hwnd = btn;
    }
}

void RegisterCaptionWndClass() {
    WNDCLASSEX wcex;
    FillWndClassEx(wcex, CUSTOM_CAPTION_CLASS_NAME, WndProcCaption);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassEx(&wcex);
}

void RelayoutCaption(MainWindow* win) {
    Rect rc = ClientRect(win->hwndCaption);
    CaptionInfo* ci = win->caption;
    ButtonInfo* button;
    DeferWinPosHelper dh;

    // Square buttons with height equal to caption height, flush to right edge.
    int btnSize = rc.dy;
    bool maximized = IsZoomed(win->hwndFrame);

    button = &ci->btn[CB_CLOSE];
    rc.dx -= btnSize;
    dh.SetWindowPos(button->hwnd, nullptr, rc.x + rc.dx, rc.y, btnSize, btnSize, SWP_NOZORDER | SWP_SHOWWINDOW);
    button->margins = {0, 0, 0, 0};

    button = &ci->btn[CB_RESTORE];
    rc.dx -= btnSize;
    dh.SetWindowPos(button->hwnd, nullptr, rc.x + rc.dx, rc.y, btnSize, btnSize,
                    SWP_NOZORDER | (maximized ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    button->margins = {0, 0, 0, 0};

    button = &ci->btn[CB_MAXIMIZE];
    dh.SetWindowPos(button->hwnd, nullptr, rc.x + rc.dx, rc.y, btnSize, btnSize,
                    SWP_NOZORDER | (maximized ? SWP_HIDEWINDOW : SWP_SHOWWINDOW));
    button->margins = {0, 0, 0, 0};

    button = &ci->btn[CB_MINIMIZE];
    rc.dx -= btnSize;
    dh.SetWindowPos(button->hwnd, nullptr, rc.x + rc.dx, rc.y, btnSize, btnSize, SWP_NOZORDER | SWP_SHOWWINDOW);
    button->margins = {0, 0, 0, 0};

    button = &ci->btn[CB_SYSTEM_MENU];
    int tabHeight = GetTabbarHeight(win->hwndFrame);
    rc.y += rc.dy - tabHeight;
    dh.SetWindowPos(button->hwnd, nullptr, rc.x, rc.y, tabHeight, tabHeight, SWP_NOZORDER);
    button->margins = {0, 0, 0, 0};

    rc.x += tabHeight;
    rc.dx -= tabHeight;
    button = &ci->btn[CB_MENU];
    dh.SetWindowPos(button->hwnd, nullptr, rc.x, rc.y, tabHeight, tabHeight, SWP_NOZORDER);
    button->margins = {0, 0, 0, 0};

    rc.x += tabHeight;
    rc.dx -= tabHeight;
    dh.SetWindowPos(win->tabsCtrl->hwnd, nullptr, rc.x, rc.y, rc.dx, tabHeight, SWP_NOZORDER);
    dh.End();

    UpdateTabWidth(win);
}

static void DrawCaptionButton(DRAWITEMSTRUCT* item, MainWindow* win) {
    if (!item || item->CtlType != ODT_BUTTON) {
        return;
    }

    Rect rButton = ToRect(item->rcItem);

    DoubleBuffer buffer(item->hwndItem, rButton);
    HDC memDC = buffer.GetDC();

    int button = item->CtlID - BTN_ID_FIRST;
    ButtonInfo* bi = &win->caption->btn[button];
    Rect rc(rButton);
    rc.x += bi->margins.left;
    rc.y += bi->margins.top;
    rc.dx -= bi->margins.left + bi->margins.right;
    rc.dy -= bi->margins.top + bi->margins.bottom;

    bool isSysButton = (button == CB_MINIMIZE || button == CB_MAXIMIZE || button == CB_RESTORE || button == CB_CLOSE);

    int stateId;
    if (ODS_SELECTED & item->itemState) {
        stateId = CBS_PUSHED;
    } else if (ODS_HOTLIGHT & item->itemState) {
        stateId = CBS_HOT;
    } else if (ODS_DISABLED & item->itemState) {
        stateId = CBS_DISABLED;
    } else if (ODS_INACTIVE & item->itemState) {
        stateId = CBS_INACTIVE;
    } else {
        stateId = CBS_NORMAL;
    }

    // draw system button (Win11 style)
    if (isSysButton) {
        PaintCaptionBackground(memDC, win, false);

        bool isClose = (button == CB_CLOSE);
        bool isHot = (stateId == CBS_HOT);
        bool isPushed = (stateId == CBS_PUSHED);
        bool isInactive = (stateId == CBS_INACTIVE || stateId == CBS_DISABLED);

        Graphics gfx(memDC);

        // hover/pressed background over entire button area
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

        // icon color
        Color iconCol;
        if (isInactive) {
            iconCol = Color(153, 153, 153);
        } else if (isClose && (isHot || isPushed)) {
            iconCol = Color(255, 255, 255);
        } else {
            COLORREF tc = win->caption->textColor;
            iconCol = Color(GetRValue(tc), GetGValue(tc), GetBValue(tc));
        }

        // icon rect centered within inner button area
        int iconSz = rc.dy * 10 / 30;
        if (iconSz < 6) {
            iconSz = 6;
        }
        iconSz = iconSz & ~1; // make even for symmetry
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
                gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);
                gfx.DrawRectangle(&pen, ix, iy, iconSz, iconSz);
                break;
            case CB_MINIMIZE: {
                gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);
                int midY = iy + iconSz / 2;
                gfx.DrawLine(&pen, ix, midY, ix + iconSz, midY);
            } break;
            case CB_RESTORE: {
                gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);
                int off = iconSz / 3;
                int sz = iconSz - off;
                // front rectangle (bottom-left)
                gfx.DrawRectangle(&pen, ix, iy + off, sz, sz);
                // back rectangle visible edges (top-right)
                gfx.DrawLine(&pen, ix + off, iy, ix + iconSz, iy);
                gfx.DrawLine(&pen, ix + iconSz, iy, ix + iconSz, iy + sz);
                gfx.DrawLine(&pen, ix + sz, iy + off, ix + iconSz, iy + off);
                gfx.DrawLine(&pen, ix + off, iy, ix + off, iy + off);
            } break;
        }
    }

    // draw menu's button
    if (button == CB_MENU) {
        PaintCaptionBackground(memDC, win, false);
        Graphics gfx(memDC);

        if (CBS_PUSHED != stateId && ODS_FOCUS & item->itemState) {
            stateId = CBS_HOT;
        }

        BYTE buttonRGB = 1;
        if (CBS_PUSHED == stateId) {
            buttonRGB = 0;
        } else if (CBS_HOT == stateId) {
            buttonRGB = 255;
        }

        if (buttonRGB != 1) {
            // paint the background
            if (GetLightness(win->caption->textColor) > GetLightness(win->caption->bgColor)) {
                buttonRGB ^= 0xff;
            }
            BYTE buttonAlpha = BYTE((255 - abs((int)GetLightness(win->caption->bgColor) - buttonRGB)) / 2);
            SolidBrush br(Color(buttonAlpha, buttonRGB, buttonRGB, buttonRGB));
            gfx.FillRectangle(&br, rc.x, rc.y, rc.dx, rc.dy);
        }
        // draw the three lines
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
        PaintCaptionBackground(memDC, win, false);
        int xIcon = GetSystemMetrics(SM_CXSMICON);
        int yIcon = GetSystemMetrics(SM_CYSMICON);
        HICON hIcon = (HICON)GetClassLongPtr(win->hwndFrame, GCLP_HICONSM);
        int x = (rButton.dx - xIcon) / 2;
        int y = (rButton.dy - yIcon) / 2;
        DrawIconEx(memDC, x, y, hIcon, xIcon, yIcon, 0, nullptr, DI_NORMAL);
    }

    buffer.Flush(item->hDC);
}

void PaintParentBackground(HWND hwnd, HDC hdc) {
    HWND parent = GetParent(hwnd);
    POINT pt = {0, 0};
    MapWindowPoints(hwnd, parent, &pt, 1);
    SetViewportOrgEx(hdc, -pt.x, -pt.y, &pt);
    SendMessageW(parent, WM_ERASEBKGND, (WPARAM)hdc, 0);
    SetViewportOrgEx(hdc, pt.x, pt.y, nullptr);

    // TODO: needed to force repaint of tab area after closing a window
    InvalidateRect(parent, nullptr, TRUE);
}

static void PaintCaptionBackground(HDC hdc, MainWindow* win, bool useDoubleBuffer) {
    UNREFERENCED_PARAMETER(useDoubleBuffer);
    RECT rClip;
    GetClipBox(hdc, &rClip);
    Rect rect = ToRect(rClip);

    COLORREF c = win->caption->bgColor;
    Graphics gfx(hdc);
    Color col = GdiRgbFromCOLORREF(c);
    SolidBrush br(col);
    gfx.FillRectangle(&br, rect.x, rect.y, rect.dx, rect.dy);
}

static void DrawFrame(HWND hwnd, COLORREF color, bool drawEdge = true) {
    HDC hdc = GetWindowDC(hwnd);

    RECT rWindow, rClient;
    GetWindowRect(hwnd, &rWindow);
    GetClientRect(hwnd, &rClient);
    // convert the client rectangle to window coordinates and exclude it from the clipping region
    MapWindowPoints(hwnd, HWND_DESKTOP, (LPPOINT)&rClient, 2);
    OffsetRect(&rClient, -rWindow.left, -rWindow.top);
    ExcludeClipRect(hdc, rClient.left, rClient.top, rClient.right, rClient.bottom);
    // convert the window rectangle, from screen to window coordinates, and draw the frame
    OffsetRect(&rWindow, -rWindow.left, -rWindow.top);
    HBRUSH br = CreateSolidBrush(color);
    FillRect(hdc, &rWindow, br);
    DeleteObject(br);
    if (drawEdge) {
        DrawEdge(hdc, &rWindow, EDGE_RAISED, BF_RECT | BF_FLAT);
    }

    ReleaseDC(hwnd, hdc);
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
            DrawFrame(hwnd, win->caption->bgColor);
            *callDef = false;
            return 0;

        case WM_NCACTIVATE:
            win->caption->UpdateColors((bool)wp);
            for (int i = CB_BTN_FIRST; i < CB_BTN_COUNT; i++) {
                win->caption->btn[i].inactive = wp == FALSE;
            }
            if (!IsIconic(hwnd)) {
                DrawFrame(hwnd, win->caption->bgColor);
                uint flags = RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN;
                RedrawWindow(win->hwndCaption, nullptr, nullptr, flags);
                *callDef = false;
                return TRUE;
            }
            break;

        case WM_NCUAHDRAWCAPTION:
        case WM_NCUAHDRAWFRAME:
            DrawFrame(hwnd, win->caption->bgColor);
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
                // Use system frame metrics for resize border size.
                // With WS_POPUP | WS_THICKFRAME, Windows adds an invisible border
                // (shadow area) beyond the visible edge. The resize zone must cover
                // both the invisible border and the visible edge.
                int borderX = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int borderY = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
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

            // Check if in the caption area (above the tab bar bottom edge)
            Point pt{x, y};
            Rect rClient = MapRectToWindow(ClientRect(hwnd), hwnd, HWND_DESKTOP);
            Rect rCaption = WindowRect(win->hwndCaption);
            if (rClient.Contains(pt) && pt.y < rCaption.y + rCaption.dy) {
                *callDef = false;
                return HTCAPTION;
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
                PostMessageW(win->hwndCaption, WM_COMMAND, MAKELONG(BTN_ID_FIRST + CB_MENU, BN_CLICKED), 0);
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
        x += ClientRect(win->caption->btn[CB_MENU].hwnd).dx;
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
