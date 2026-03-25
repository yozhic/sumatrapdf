/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// factor by how large the non-maximized caption should be in relation to the tabbar
#define kCaptionTabBarDyFactor 1.25f

// gap in pixels between top of caption and tabs; this area allows dragging the window
#define kCaptionTopPadding 14

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
    Rect rect{};
    bool highlighted = false;
    bool pressed = false;
    bool inactive = false;
    bool visible = true;
    ButtonInfo() = default;
};

struct CaptionInfo {
    HWND hwndFrame = nullptr;

    ButtonInfo btn[CB_BTN_COUNT];
    COLORREF bgColor = 0;
    COLORREF textColor = 0;
    bool isMenuOpen = false;
    Rect captionRect{};

    explicit CaptionInfo(HWND hwndFrame);
    ~CaptionInfo();

    void UpdateTheme();
    void UpdateColors(bool activeWindow);
};

void CreateCaption(MainWindow* win);
LRESULT CustomCaptionFrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool* callDef, MainWindow* win);
void RelayoutCaption(MainWindow* win);
void PaintCaption(HDC hdc, MainWindow* win);
void SetCaptionButtonsRtl(CaptionInfo*, bool isRtl);
void CaptionUpdateUI(MainWindow*, CaptionInfo*);
void DeleteCaption(CaptionInfo*);
void OpenSystemMenu(MainWindow* win);
