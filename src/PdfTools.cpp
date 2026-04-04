/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/ScopedWin.h"
#include "utils/FileUtil.h"
#include "utils/WinUtil.h"

#include "wingui/UIModels.h"
#include "wingui/Layout.h"
#include "wingui/WinGui.h"

#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "DisplayMode.h"
#include "GlobalPrefs.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "ExternalViewers.h"
#include "Theme.h"

#include "DarkModeSubclass.h"

extern "C" int pdfbake_main(int argc, char** argv);
extern "C" int fz_optind;

struct PdfBakeDialog {
    HWND hwnd = nullptr;
    HWND hwndPathLabel = nullptr;
    HWND hwndDestEdit = nullptr;
    HWND hwndBrowseBtn = nullptr;
    HWND hwndBakeBtn = nullptr;
    HWND hwndCancelBtn = nullptr;
    HFONT hFont = nullptr;
    char* srcPath = nullptr;
    MainWindow* win = nullptr;
};

constexpr int kBakeDlgW = 500;
constexpr int kBakeDlgH = 140;
constexpr int kBakeDlgPadding = 10;
constexpr int kBakeDlgRowH = 22;
constexpr int kBakeDlgRowGap = 6;

static void PdfBakeOnBrowse(PdfBakeDialog* dlg) {
    WCHAR dstFileName[MAX_PATH + 1]{};
    GetWindowTextW(dlg->hwndDestEdit, dstFileName, MAX_PATH);

    OPENFILENAME ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dlg->hwnd;
    ofn.lpstrFile = dstFileName;
    ofn.nMaxFile = dimof(dstFileName);
    ofn.lpstrFilter = L"PDF Files\0*.pdf\0All Files\0*.*\0";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"pdf";

    if (GetSaveFileNameW(&ofn)) {
        SetWindowTextW(dlg->hwndDestEdit, dstFileName);
    }
}

static void PdfBakeDoIt(PdfBakeDialog* dlg) {
    char destPath[MAX_PATH + 1]{};
    GetWindowTextA(dlg->hwndDestEdit, destPath, MAX_PATH);
    if (str::IsEmpty(destPath)) {
        return;
    }

    // build argv for pdfbake_main: "bake" input output
    char* argv[] = {(char*)"bake", dlg->srcPath, destPath};
    int argc = 3;

    // pdfbake_main uses fz_getopt which has global state, reset it
    fz_optind = 1;

    int res = pdfbake_main(argc, argv);
    if (res == 0) {
        MainWindow* win = dlg->win;
        TempStr path = str::DupTemp(destPath);
        DestroyWindow(dlg->hwnd);
        // open the baked file
        LoadArgs args(path, win);
        StartLoadDocument(&args);
    } else {
        MessageBoxWarning(dlg->hwnd, "Failed to bake PDF file.", "Bake PDF");
    }
}

static LRESULT CALLBACK PdfBakeDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PdfBakeDialog* dlg = nullptr;
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        dlg = (PdfBakeDialog*)cs->lpCreateParams;
        dlg->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dlg);
        return 0;
    }
    dlg = (PdfBakeDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!dlg) {
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    switch (msg) {
        case WM_COMMAND: {
            int code = HIWORD(wp);
            HWND ctl = (HWND)lp;
            if (ctl == dlg->hwndBrowseBtn && code == BN_CLICKED) {
                PdfBakeOnBrowse(dlg);
                return 0;
            }
            if (ctl == dlg->hwndBakeBtn && code == BN_CLICKED) {
                PdfBakeDoIt(dlg);
                return 0;
            }
            if (ctl == dlg->hwndCancelBtn && code == BN_CLICKED) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static constexpr const WCHAR* kPdfBakeWinClassName = L"SUMATRA_PDF_BAKE";
static bool gPdfBakeWinClassRegistered = false;

void ShowPdfBakeDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || !tab->filePath) {
        return;
    }
    if (!CouldBePDFDoc(tab)) {
        return;
    }

    if (!gPdfBakeWinClassRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = PdfBakeDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kPdfBakeWinClassName;
        RegisterClassExW(&wc);
        gPdfBakeWinClassRegistered = true;
    }

    PdfBakeDialog* dlg = new PdfBakeDialog();
    dlg->srcPath = str::Dup(tab->filePath);
    dlg->win = win;

    HINSTANCE h = GetModuleHandleW(nullptr);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kPdfBakeWinClassName, L"Bake PDF",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                kBakeDlgW, kBakeDlgH, win->hwndFrame, nullptr, h, dlg);
    if (!hwnd) {
        str::Free(dlg->srcPath);
        delete dlg;
        return;
    }

    dlg->hFont = GetDefaultGuiFont();

    int x = kBakeDlgPadding;
    int y = kBakeDlgPadding;
    int w = kBakeDlgW - 2 * kBakeDlgPadding - 16; // account for non-client area

    // row 1: source path label
    dlg->hwndPathLabel =
        CreateWindowExW(0, L"STATIC", ToWStrTemp(tab->filePath), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS, x,
                        y, w, kBakeDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndPathLabel, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    y += kBakeDlgRowH + kBakeDlgRowGap;

    // row 2: dest edit + browse button
    TempStr destPath = MakeUniqueFilePathTemp(tab->filePath);
    int browseW = 30;
    dlg->hwndDestEdit =
        CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, ToWStrTemp(destPath), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y,
                        w - browseW - 4, kBakeDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndDestEdit, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    dlg->hwndBrowseBtn = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x + w - browseW,
                                         y, browseW, kBakeDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndBrowseBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    y += kBakeDlgRowH + kBakeDlgRowGap;

    // row 3: Bake + Cancel buttons (right-aligned)
    int btnW = 75;
    int btnH = 24;
    int bx = x + w - btnW;
    dlg->hwndCancelBtn = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, bx, y, btnW,
                                         btnH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndCancelBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    bx -= btnW + 4;
    dlg->hwndBakeBtn = CreateWindowExW(0, L"BUTTON", L"Bake", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, bx, y, btnW,
                                       btnH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndBakeBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    CenterDialog(hwnd, win->hwndFrame);
    if (UseDarkModeLib()) {
        DarkMode::setDarkWndSafe(hwnd);
        DarkMode::setWindowEraseBgSubclass(hwnd);
    }
    ShowWindow(hwnd, SW_SHOW);
}
