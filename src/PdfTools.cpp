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
#include "Translations.h"
#include "ExternalViewers.h"
#include "Flags.h"
#include "DisplayModel.h"
#include "Theme.h"

#include "DarkModeSubclass.h"

extern "C" int pdfbake_main(int argc, char** argv);
extern "C" int pdfclean_main(int argc, char** argv);
extern "C" int muconvert_main(int argc, char** argv);

// offset to align static label text with text inside an edit control
// accounts for WS_EX_CLIENTEDGE border (2px) + edit internal left margin (~2px)
constexpr int kEditTextXOffset = 4;

static int CalcDlgWidth(HFONT font, const char* path, int minW, int padding) {
    HDC hdc = GetDC(nullptr);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    TempWStr pathW = ToWStrTemp(path);
    SIZE sz{};
    GetTextExtentPoint32W(hdc, pathW, str::Leni(pathW), &sz);
    SelectObject(hdc, oldFont);
    ReleaseDC(nullptr, hdc);
    int dlgW = sz.cx + 2 * padding + 32;
    dlgW = std::max(dlgW, minW);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    dlgW = std::min(dlgW, screenW * 80 / 100);
    return dlgW;
}

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
    dlg->hFont = GetDefaultGuiFont();

    int dlgW = CalcDlgWidth(dlg->hFont, tab->filePath, kBakeDlgW, kBakeDlgPadding);

    HINSTANCE h = GetModuleHandleW(nullptr);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kPdfBakeWinClassName, L"Bake PDF",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                dlgW, kBakeDlgH, win->hwndFrame, nullptr, h, dlg);
    if (!hwnd) {
        str::Free(dlg->srcPath);
        delete dlg;
        return;
    }

    int x = kBakeDlgPadding;
    int y = kBakeDlgPadding;
    int w = dlgW - 2 * kBakeDlgPadding - 16; // account for non-client area

    // row 1: source path label (offset to align with text inside edit control)
    dlg->hwndPathLabel =
        CreateWindowExW(0, L"STATIC", ToWStrTemp(tab->filePath), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                        x + kEditTextXOffset, y, w - kEditTextXOffset, kBakeDlgRowH, hwnd, nullptr, h, nullptr);
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

// --- Extract PDF Text dialog ---

struct PdfExtractTextDialog {
    HWND hwnd = nullptr;
    HWND hwndPathLabel = nullptr;
    HWND hwndDestEdit = nullptr;
    HWND hwndBrowseBtn = nullptr;
    HWND hwndPagesLabel = nullptr;
    HWND hwndPagesEdit = nullptr;
    HWND hwndExtractBtn = nullptr;
    HWND hwndCancelBtn = nullptr;
    HFONT hFont = nullptr;
    char* srcPath = nullptr;
    MainWindow* win = nullptr;
};

constexpr int kExtractDlgW = 500;
constexpr int kExtractDlgH = 168;
constexpr int kExtractDlgPadding = 10;
constexpr int kExtractDlgRowH = 22;
constexpr int kExtractDlgRowGap = 6;

static void PdfExtractTextOnBrowse(PdfExtractTextDialog* dlg) {
    WCHAR dstFileName[MAX_PATH + 1]{};
    GetWindowTextW(dlg->hwndDestEdit, dstFileName, MAX_PATH);

    OPENFILENAME ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dlg->hwnd;
    ofn.lpstrFile = dstFileName;
    ofn.nMaxFile = dimof(dstFileName);
    ofn.lpstrFilter = L"Text Files\0*.txt\0All Files\0*.*\0";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"txt";

    if (GetSaveFileNameW(&ofn)) {
        SetWindowTextW(dlg->hwndDestEdit, dstFileName);
    }
}

static bool ExtractTextViaEngine(PdfExtractTextDialog* dlg, const char* destPath, const char* pages) {
    MainWindow* win = dlg->win;
    if (!win || !win->ctrl) {
        return false;
    }
    DisplayModel* dm = win->ctrl->AsFixed();
    if (!dm) {
        return false;
    }
    EngineBase* engine = dm->GetEngine();
    if (!engine) {
        return false;
    }
    int pageCount = engine->PageCount();
    Vec<PageRange> ranges;
    if (!ParsePageRanges(pages, ranges)) {
        return false;
    }
    str::Str text;
    for (auto& range : ranges) {
        int start = std::max(range.start, 1);
        int end = std::min(range.end, pageCount);
        for (int pageNo = start; pageNo <= end; pageNo++) {
            PageText pt = engine->ExtractPageText(pageNo);
            if (pt.text) {
                TempStr utf8 = ToUtf8Temp(pt.text);
                text.Append(utf8);
                text.AppendChar('\n');
            }
            FreePageText(&pt);
        }
    }
    return file::WriteFile(destPath, text.AsByteSlice());
}

static void PdfExtractTextDoIt(PdfExtractTextDialog* dlg) {
    char destPath[MAX_PATH + 1]{};
    GetWindowTextA(dlg->hwndDestEdit, destPath, MAX_PATH);
    if (str::IsEmpty(destPath)) {
        return;
    }

    char pages[256]{};
    GetWindowTextA(dlg->hwndPagesEdit, pages, dimof(pages) - 1);
    if (str::IsEmpty(pages)) {
        return;
    }

    bool ok = false;
    WindowTab* tab = dlg->win ? dlg->win->CurrentTab() : nullptr;
    bool isPdf = tab && CouldBePDFDoc(tab);
    if (isPdf) {
        // use muconvert for PDF
        char* argv[] = {(char*)"convert", (char*)"-o", destPath, dlg->srcPath, pages};
        int argc = 5;
        ok = muconvert_main(argc, argv) == 0;
    } else {
        // use engine text extraction for other formats (DjVu, etc.)
        ok = ExtractTextViaEngine(dlg, destPath, pages);
    }

    if (ok) {
        DestroyWindow(dlg->hwnd);
        OpenPathInDefaultFileManager(destPath);
    } else {
        MessageBoxWarning(dlg->hwnd, "Failed to extract text.", "Extract Text");
    }
}

static LRESULT CALLBACK PdfExtractTextDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PdfExtractTextDialog* dlg = nullptr;
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        dlg = (PdfExtractTextDialog*)cs->lpCreateParams;
        dlg->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dlg);
        return 0;
    }
    dlg = (PdfExtractTextDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!dlg) {
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    switch (msg) {
        case WM_COMMAND: {
            int code = HIWORD(wp);
            HWND ctl = (HWND)lp;
            if (ctl == dlg->hwndBrowseBtn && code == BN_CLICKED) {
                PdfExtractTextOnBrowse(dlg);
                return 0;
            }
            if (ctl == dlg->hwndExtractBtn && code == BN_CLICKED) {
                PdfExtractTextDoIt(dlg);
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

static constexpr const WCHAR* kPdfExtractTextWinClassName = L"SUMATRA_PDF_EXTRACT_TEXT";
static bool gPdfExtractTextWinClassRegistered = false;

void ShowPdfExtractTextDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || !tab->filePath) {
        return;
    }
    if (!gPdfExtractTextWinClassRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = PdfExtractTextDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kPdfExtractTextWinClassName;
        RegisterClassExW(&wc);
        gPdfExtractTextWinClassRegistered = true;
    }

    PdfExtractTextDialog* dlg = new PdfExtractTextDialog();
    dlg->srcPath = str::Dup(tab->filePath);
    dlg->win = win;
    dlg->hFont = GetDefaultGuiFont();

    int dlgW = CalcDlgWidth(dlg->hFont, tab->filePath, kExtractDlgW, kExtractDlgPadding);

    HINSTANCE h = GetModuleHandleW(nullptr);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kPdfExtractTextWinClassName, L"Extract Text",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                dlgW, kExtractDlgH, win->hwndFrame, nullptr, h, dlg);
    if (!hwnd) {
        str::Free(dlg->srcPath);
        delete dlg;
        return;
    }

    int x = kExtractDlgPadding;
    int y = kExtractDlgPadding;
    int w = dlgW - 2 * kExtractDlgPadding - 16; // account for non-client area

    // row 1: source path label (offset to align with text inside edit control)
    dlg->hwndPathLabel =
        CreateWindowExW(0, L"STATIC", ToWStrTemp(tab->filePath), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                        x + kEditTextXOffset, y, w - kEditTextXOffset, kExtractDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndPathLabel, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    y += kExtractDlgRowH + kExtractDlgRowGap;

    // row 2: dest edit + browse button
    TempStr noExt = path::GetPathNoExtTemp(tab->filePath);
    TempStr txtPath = str::JoinTemp(noExt, ".txt");
    TempStr destPath = MakeUniqueFilePathTemp(txtPath);
    int browseW = 30;
    dlg->hwndDestEdit =
        CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, ToWStrTemp(destPath), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y,
                        w - browseW - 4, kExtractDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndDestEdit, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    dlg->hwndBrowseBtn = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x + w - browseW,
                                         y, browseW, kExtractDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndBrowseBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    y += kExtractDlgRowH + kExtractDlgRowGap;

    // row 3: "Pages:" label + pages edit
    int labelW = 42;
    dlg->hwndPagesLabel = CreateWindowExW(0, L"STATIC", L"Pages:", WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, labelW,
                                          kExtractDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndPagesLabel, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    int pageCount = win->ctrl ? win->ctrl->PageCount() : 1;
    TempStr pagesStr = str::FormatTemp("1-%d", pageCount);
    dlg->hwndPagesEdit =
        CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, ToWStrTemp(pagesStr), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                        x + labelW + 4, y, w - labelW - 4, kExtractDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndPagesEdit, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    y += kExtractDlgRowH + kExtractDlgRowGap;

    // row 4: Extract Text + Cancel buttons (right-aligned)
    int btnW = 85;
    int btnH = 24;
    int bx = x + w - btnW;
    dlg->hwndCancelBtn = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, bx, y, btnW,
                                         btnH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndCancelBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    bx -= btnW + 4;
    dlg->hwndExtractBtn = CreateWindowExW(0, L"BUTTON", L"Extract Text", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, bx,
                                          y, btnW, btnH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndExtractBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    CenterDialog(hwnd, win->hwndFrame);
    if (UseDarkModeLib()) {
        DarkMode::setDarkWndSafe(hwnd);
        DarkMode::setWindowEraseBgSubclass(hwnd);
    }
    ShowWindow(hwnd, SW_SHOW);
}

constexpr int kCompressDlgW = 500;
constexpr int kCompressDlgH = 140;
constexpr int kCompressDlgPadding = 10;
constexpr int kCompressDlgRowH = 22;
constexpr int kCompressDlgRowGap = 6;

// --- Compress PDF dialog ---

struct PdfCompressDialog {
    HWND hwnd = nullptr;
    HWND hwndPathLabel = nullptr;
    HWND hwndDestEdit = nullptr;
    HWND hwndBrowseBtn = nullptr;
    HWND hwndCompressBtn = nullptr;
    HWND hwndCancelBtn = nullptr;
    HFONT hFont = nullptr;
    char* srcPath = nullptr;
    MainWindow* win = nullptr;
};

static void PdfCompressOnBrowse(PdfCompressDialog* dlg) {
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

static void PdfCompressDoIt(PdfCompressDialog* dlg) {
    char destPath[MAX_PATH + 1]{};
    GetWindowTextA(dlg->hwndDestEdit, destPath, MAX_PATH);
    if (str::IsEmpty(destPath)) {
        return;
    }

    // equivalent of: clean -gggg -e 100 -f -i -t -Z input output
    char* argv[] = {
        (char*)"clean", (char*)"-gggg", (char*)"-e", (char*)"100", (char*)"-f",
        (char*)"-i",    (char*)"-t",    (char*)"-Z", dlg->srcPath, destPath,
    };
    int argc = 10;

    int res = pdfclean_main(argc, argv);
    if (res == 0) {
        MainWindow* win = dlg->win;
        TempStr path = str::DupTemp(destPath);
        DestroyWindow(dlg->hwnd);
        LoadArgs args(path, win);
        StartLoadDocument(&args);
    } else {
        MessageBoxWarning(dlg->hwnd, "Failed to compress PDF file.", "Compress PDF");
    }
}

static LRESULT CALLBACK PdfCompressDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PdfCompressDialog* dlg = nullptr;
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        dlg = (PdfCompressDialog*)cs->lpCreateParams;
        dlg->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dlg);
        return 0;
    }
    dlg = (PdfCompressDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!dlg) {
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    switch (msg) {
        case WM_COMMAND: {
            int code = HIWORD(wp);
            HWND ctl = (HWND)lp;
            if (ctl == dlg->hwndBrowseBtn && code == BN_CLICKED) {
                PdfCompressOnBrowse(dlg);
                return 0;
            }
            if (ctl == dlg->hwndCompressBtn && code == BN_CLICKED) {
                PdfCompressDoIt(dlg);
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

static constexpr const WCHAR* kPdfCompressWinClassName = L"SUMATRA_PDF_COMPRESS";
static bool gPdfCompressWinClassRegistered = false;

void ShowPdfCompressDialog(MainWindow* win) {
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

    if (!gPdfCompressWinClassRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = PdfCompressDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kPdfCompressWinClassName;
        RegisterClassExW(&wc);
        gPdfCompressWinClassRegistered = true;
    }

    PdfCompressDialog* dlg = new PdfCompressDialog();
    dlg->srcPath = str::Dup(tab->filePath);
    dlg->win = win;
    dlg->hFont = GetDefaultGuiFont();

    int dlgW = CalcDlgWidth(dlg->hFont, tab->filePath, kCompressDlgW, kCompressDlgPadding);

    HINSTANCE h = GetModuleHandleW(nullptr);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kPdfCompressWinClassName, L"Compress PDF",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                dlgW, kCompressDlgH, win->hwndFrame, nullptr, h, dlg);
    if (!hwnd) {
        str::Free(dlg->srcPath);
        delete dlg;
        return;
    }

    int x = kCompressDlgPadding;
    int y = kCompressDlgPadding;
    int w = dlgW - 2 * kCompressDlgPadding - 16;

    // row 1: source path label (offset to align with text inside edit control)
    dlg->hwndPathLabel =
        CreateWindowExW(0, L"STATIC", ToWStrTemp(tab->filePath), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                        x + kEditTextXOffset, y, w - kEditTextXOffset, kCompressDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndPathLabel, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    y += kCompressDlgRowH + kCompressDlgRowGap;

    // row 2: dest edit + browse button
    TempStr destPath = MakeUniqueFilePathTemp(tab->filePath);
    int browseW = 30;
    dlg->hwndDestEdit =
        CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, ToWStrTemp(destPath), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y,
                        w - browseW - 4, kCompressDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndDestEdit, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    dlg->hwndBrowseBtn = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x + w - browseW,
                                         y, browseW, kCompressDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndBrowseBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    y += kCompressDlgRowH + kCompressDlgRowGap;

    // row 3: Compress + Cancel buttons (right-aligned)
    int btnW = 85;
    int btnH = 24;
    int bx = x + w - btnW;
    dlg->hwndCancelBtn = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, bx, y, btnW,
                                         btnH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndCancelBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    bx -= btnW + 4;
    dlg->hwndCompressBtn = CreateWindowExW(0, L"BUTTON", L"Compress", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, bx, y,
                                           btnW, btnH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndCompressBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    CenterDialog(hwnd, win->hwndFrame);
    if (UseDarkModeLib()) {
        DarkMode::setDarkWndSafe(hwnd);
        DarkMode::setWindowEraseBgSubclass(hwnd);
    }
    ShowWindow(hwnd, SW_SHOW);
}

// --- Decompress PDF dialog ---

struct PdfDecompressDialog {
    HWND hwnd = nullptr;
    HWND hwndPathLabel = nullptr;
    HWND hwndDestEdit = nullptr;
    HWND hwndBrowseBtn = nullptr;
    HWND hwndDecompressBtn = nullptr;
    HWND hwndCancelBtn = nullptr;
    HFONT hFont = nullptr;
    char* srcPath = nullptr;
    MainWindow* win = nullptr;
};

static void PdfDecompressOnBrowse(PdfDecompressDialog* dlg) {
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

static void PdfDecompressDoIt(PdfDecompressDialog* dlg) {
    char destPath[MAX_PATH + 1]{};
    GetWindowTextA(dlg->hwndDestEdit, destPath, MAX_PATH);
    if (str::IsEmpty(destPath)) {
        return;
    }

    // equivalent of: clean -d input output
    char* argv[] = {(char*)"clean", (char*)"-d", dlg->srcPath, destPath};
    int argc = 4;

    int res = pdfclean_main(argc, argv);
    if (res == 0) {
        MainWindow* win = dlg->win;
        TempStr path = str::DupTemp(destPath);
        DestroyWindow(dlg->hwnd);
        LoadArgs args(path, win);
        StartLoadDocument(&args);
    } else {
        MessageBoxWarning(dlg->hwnd, "Failed to decompress PDF file.", "Decompress PDF");
    }
}

static LRESULT CALLBACK PdfDecompressDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PdfDecompressDialog* dlg = nullptr;
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        dlg = (PdfDecompressDialog*)cs->lpCreateParams;
        dlg->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dlg);
        return 0;
    }
    dlg = (PdfDecompressDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!dlg) {
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    switch (msg) {
        case WM_COMMAND: {
            int code = HIWORD(wp);
            HWND ctl = (HWND)lp;
            if (ctl == dlg->hwndBrowseBtn && code == BN_CLICKED) {
                PdfDecompressOnBrowse(dlg);
                return 0;
            }
            if (ctl == dlg->hwndDecompressBtn && code == BN_CLICKED) {
                PdfDecompressDoIt(dlg);
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

static constexpr const WCHAR* kPdfDecompressWinClassName = L"SUMATRA_PDF_DECOMPRESS";
static bool gPdfDecompressWinClassRegistered = false;

void ShowPdfDecompressDialog(MainWindow* win) {
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

    if (!gPdfDecompressWinClassRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = PdfDecompressDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kPdfDecompressWinClassName;
        RegisterClassExW(&wc);
        gPdfDecompressWinClassRegistered = true;
    }

    PdfDecompressDialog* dlg = new PdfDecompressDialog();
    dlg->srcPath = str::Dup(tab->filePath);
    dlg->win = win;
    dlg->hFont = GetDefaultGuiFont();

    int dlgW = CalcDlgWidth(dlg->hFont, tab->filePath, kCompressDlgW, kCompressDlgPadding);

    HINSTANCE h = GetModuleHandleW(nullptr);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kPdfDecompressWinClassName, L"Decompress PDF",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                dlgW, kCompressDlgH, win->hwndFrame, nullptr, h, dlg);
    if (!hwnd) {
        str::Free(dlg->srcPath);
        delete dlg;
        return;
    }

    int x = kCompressDlgPadding;
    int y = kCompressDlgPadding;
    int w = dlgW - 2 * kCompressDlgPadding - 16;

    // source path label (offset to align with text inside edit control)
    dlg->hwndPathLabel =
        CreateWindowExW(0, L"STATIC", ToWStrTemp(tab->filePath), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                        x + kEditTextXOffset, y, w - kEditTextXOffset, kCompressDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndPathLabel, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    y += kCompressDlgRowH + kCompressDlgRowGap;

    TempStr destPath = MakeUniqueFilePathTemp(tab->filePath);
    int browseW = 30;
    dlg->hwndDestEdit =
        CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, ToWStrTemp(destPath), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y,
                        w - browseW - 4, kCompressDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndDestEdit, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    dlg->hwndBrowseBtn = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x + w - browseW,
                                         y, browseW, kCompressDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndBrowseBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    y += kCompressDlgRowH + kCompressDlgRowGap;

    int btnW = 95;
    int btnH = 24;
    int bx = x + w - btnW;
    dlg->hwndCancelBtn = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, bx, y, btnW,
                                         btnH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndCancelBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    bx -= btnW + 4;
    dlg->hwndDecompressBtn = CreateWindowExW(0, L"BUTTON", L"Decompress", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, bx,
                                             y, btnW, btnH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndDecompressBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    CenterDialog(hwnd, win->hwndFrame);
    if (UseDarkModeLib()) {
        DarkMode::setDarkWndSafe(hwnd);
        DarkMode::setWindowEraseBgSubclass(hwnd);
    }
    ShowWindow(hwnd, SW_SHOW);
}

// --- Delete Pages From PDF dialog ---

struct PdfDeletePageDialog {
    HWND hwnd = nullptr;
    HWND hwndPathLabel = nullptr;
    HWND hwndDestEdit = nullptr;
    HWND hwndBrowseBtn = nullptr;
    HWND hwndPagesLabel = nullptr;
    HWND hwndPagesEdit = nullptr;
    HWND hwndTotalLabel = nullptr;
    HWND hwndDeleteBtn = nullptr; // also used as "Extract Pages" button
    HWND hwndCancelBtn = nullptr;
    HFONT hFont = nullptr;
    char* srcPath = nullptr;
    bool isExtract = false;
    MainWindow* win = nullptr;
    int pageCount = 0;
};

constexpr int kDeletePageDlgPadding = 10;
constexpr int kDeletePageDlgRowH = 22;
constexpr int kDeletePageDlgRowGap = 6;

// Parse delete page ranges like "1,3-8,13-N" where N means last page.
// Returns a sorted list of unique 1-based page numbers to delete.
// Returns false if the syntax is invalid or any page is out of range.
static bool ParseDeletePages(const char* s, int pageCount, Vec<int>& pagesToDelete) {
    if (!s || !*s) {
        return false;
    }
    StrVec parts;
    Split(&parts, s, ",", true);
    if (parts.Size() == 0) {
        return false;
    }
    for (char* part : parts) {
        str::TrimWSInPlace(part, str::TrimOpt::Both);
        if (str::IsEmpty(part)) {
            return false;
        }
        // check for range "A-B" where A/B can be a number or "N"
        char* dash = (char*)str::FindChar(part, '-');
        if (dash) {
            *dash = 0;
            char* startStr = part;
            char* endStr = dash + 1;
            str::TrimWSInPlace(startStr, str::TrimOpt::Both);
            str::TrimWSInPlace(endStr, str::TrimOpt::Both);
            if (str::IsEmpty(startStr) || str::IsEmpty(endStr)) {
                return false;
            }
            int start, end;
            if (str::EqI(startStr, "N")) {
                start = pageCount;
            } else {
                start = str::Parse(startStr, "%d%$", &start) ? start : -1;
            }
            if (str::EqI(endStr, "N")) {
                end = pageCount;
            } else {
                end = str::Parse(endStr, "%d%$", &end) ? end : -1;
            }
            if (start < 1 || start > pageCount || end < 1 || end > pageCount || start > end) {
                return false;
            }
            for (int i = start; i <= end; i++) {
                pagesToDelete.Append(i);
            }
        } else {
            // single page
            int page;
            if (str::EqI(part, "N")) {
                page = pageCount;
            } else {
                page = str::Parse(part, "%d%$", &page) ? page : -1;
            }
            if (page < 1 || page > pageCount) {
                return false;
            }
            pagesToDelete.Append(page);
        }
    }
    if (pagesToDelete.Size() == 0) {
        return false;
    }
    // sort and deduplicate
    pagesToDelete.SortTyped([](const int* a, const int* b) -> int { return *a - *b; });
    int prev = -1;
    Vec<int> unique;
    for (int p : pagesToDelete) {
        if (p != prev) {
            unique.Append(p);
            prev = p;
        }
    }
    pagesToDelete = unique;
    return true;
}

// Build the page range string of pages to KEEP (complement of pagesToDelete).
static TempStr BuildKeepPagesRange(int pageCount, const Vec<int>& pagesToDelete) {
    str::Str s;
    int delIdx = 0;
    int rangeStart = -1;
    int rangeEnd = -1;
    for (int p = 1; p <= pageCount; p++) {
        bool shouldDelete = (delIdx < pagesToDelete.Size() && pagesToDelete[delIdx] == p);
        if (shouldDelete) {
            delIdx++;
            if (rangeStart != -1) {
                if (s.Size() > 0) {
                    s.AppendChar(',');
                }
                if (rangeStart == rangeEnd) {
                    s.AppendFmt("%d", rangeStart);
                } else {
                    s.AppendFmt("%d-%d", rangeStart, rangeEnd);
                }
                rangeStart = -1;
            }
        } else {
            if (rangeStart == -1) {
                rangeStart = p;
            }
            rangeEnd = p;
        }
    }
    if (rangeStart != -1) {
        if (s.Size() > 0) {
            s.AppendChar(',');
        }
        if (rangeStart == rangeEnd) {
            s.AppendFmt("%d", rangeStart);
        } else {
            s.AppendFmt("%d-%d", rangeStart, rangeEnd);
        }
    }
    return str::DupTemp(s.Get());
}

// Format a sorted list of page numbers as a compact range string (e.g. "1-3,5,7-10").
static TempStr FormatPageRange(const Vec<int>& pages) {
    str::Str s;
    int i = 0;
    int n = pages.Size();
    while (i < n) {
        int start = pages[i];
        int end = start;
        while (i + 1 < n && pages[i + 1] == end + 1) {
            end = pages[++i];
        }
        if (s.Size() > 0) {
            s.AppendChar(',');
        }
        if (start == end) {
            s.AppendFmt("%d", start);
        } else {
            s.AppendFmt("%d-%d", start, end);
        }
        i++;
    }
    return str::DupTemp(s.Get());
}

static void PdfDeletePageUpdateButton(PdfDeletePageDialog* dlg) {
    char pages[256]{};
    GetWindowTextA(dlg->hwndPagesEdit, pages, dimof(pages) - 1);
    Vec<int> parsedPages;
    bool valid = ParseDeletePages(pages, dlg->pageCount, parsedPages);
    // for delete mode, can't delete all pages
    if (valid && !dlg->isExtract && parsedPages.Size() >= dlg->pageCount) {
        valid = false;
    }
    EnableWindow(dlg->hwndDeleteBtn, valid);
}

static void PdfDeletePageOnBrowse(PdfDeletePageDialog* dlg) {
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

static void PdfDeletePageDoIt(PdfDeletePageDialog* dlg) {
    char destPath[MAX_PATH + 1]{};
    GetWindowTextA(dlg->hwndDestEdit, destPath, MAX_PATH);
    if (str::IsEmpty(destPath)) {
        return;
    }

    char pages[256]{};
    GetWindowTextA(dlg->hwndPagesEdit, pages, dimof(pages) - 1);

    Vec<int> parsedPages;
    if (!ParseDeletePages(pages, dlg->pageCount, parsedPages)) {
        return;
    }
    if (!dlg->isExtract && parsedPages.Size() >= dlg->pageCount) {
        return;
    }

    TempStr pageRange;
    if (dlg->isExtract) {
        // for extract: pass the specified pages directly to pdfclean
        pageRange = FormatPageRange(parsedPages);
    } else {
        // for delete: pass the complement (pages to keep) to pdfclean
        pageRange = BuildKeepPagesRange(dlg->pageCount, parsedPages);
    }

    // equivalent of: clean input.pdf output.pdf <page-range>
    char* argv[] = {(char*)"clean", dlg->srcPath, destPath, pageRange};
    int argc = 4;

    int res = pdfclean_main(argc, argv);
    if (res == 0) {
        MainWindow* win = dlg->win;
        TempStr path = str::DupTemp(destPath);
        DestroyWindow(dlg->hwnd);
        LoadArgs args(path, win);
        StartLoadDocument(&args);
    } else {
        const char* msg =
            dlg->isExtract ? "Failed to extract pages from PDF file." : "Failed to delete pages from PDF file.";
        const char* title = dlg->isExtract ? "Extract Pages From PDF" : "Delete Pages From PDF";
        MessageBoxWarning(dlg->hwnd, msg, title);
    }
}

static LRESULT CALLBACK PdfDeletePageDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PdfDeletePageDialog* dlg = nullptr;
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        dlg = (PdfDeletePageDialog*)cs->lpCreateParams;
        dlg->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dlg);
        return 0;
    }
    dlg = (PdfDeletePageDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!dlg) {
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    switch (msg) {
        case WM_COMMAND: {
            int code = HIWORD(wp);
            HWND ctl = (HWND)lp;
            if (ctl == dlg->hwndBrowseBtn && code == BN_CLICKED) {
                PdfDeletePageOnBrowse(dlg);
                return 0;
            }
            if (ctl == dlg->hwndDeleteBtn && code == BN_CLICKED) {
                PdfDeletePageDoIt(dlg);
                return 0;
            }
            if (ctl == dlg->hwndCancelBtn && code == BN_CLICKED) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (ctl == dlg->hwndPagesEdit && code == EN_CHANGE) {
                PdfDeletePageUpdateButton(dlg);
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

static constexpr const WCHAR* kPdfDeletePageWinClassName = L"SUMATRA_PDF_DELETE_PAGE";
static bool gPdfDeletePageWinClassRegistered = false;

static void ShowPdfPageRangeDialog(MainWindow* win, bool isExtract) {
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

    int pageCount = win->ctrl ? win->ctrl->PageCount() : 0;
    if (pageCount < 2) {
        return;
    }

    if (!gPdfDeletePageWinClassRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = PdfDeletePageDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kPdfDeletePageWinClassName;
        RegisterClassExW(&wc);
        gPdfDeletePageWinClassRegistered = true;
    }

    PdfDeletePageDialog* dlg = new PdfDeletePageDialog();
    dlg->srcPath = str::Dup(tab->filePath);
    dlg->win = win;
    dlg->hFont = GetDefaultGuiFont();
    dlg->pageCount = pageCount;
    dlg->isExtract = isExtract;

    int dlgW = CalcDlgWidth(dlg->hFont, tab->filePath, 500, kDeletePageDlgPadding);
    int dlgH = kDeletePageDlgPadding + (kDeletePageDlgRowH + kDeletePageDlgRowGap) * 4 + 24 + 8 + kDeletePageDlgPadding;

    HINSTANCE h = GetModuleHandleW(nullptr);
    const char* title = isExtract ? _TRA("Extract Pages From PDF") : _TRA("Delete Pages From PDF");
    TempWStr ws = ToWStrTemp(title);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kPdfDeletePageWinClassName, ws,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                dlgW, dlgH, win->hwndFrame, nullptr, h, dlg);
    if (!hwnd) {
        str::Free(dlg->srcPath);
        delete dlg;
        return;
    }

    int x = kDeletePageDlgPadding;
    int y = kDeletePageDlgPadding;
    int w = dlgW - 2 * kDeletePageDlgPadding - 16;

    // row 1: source path label (offset to align with text inside edit control)
    dlg->hwndPathLabel =
        CreateWindowExW(0, L"STATIC", ToWStrTemp(tab->filePath), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                        x + kEditTextXOffset, y, w - kEditTextXOffset, kDeletePageDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndPathLabel, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    y += kDeletePageDlgRowH + kDeletePageDlgRowGap;

    // row 2: dest edit + browse button
    TempStr destPath = MakeUniqueFilePathTemp(tab->filePath);
    int browseW = 30;
    dlg->hwndDestEdit =
        CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, ToWStrTemp(destPath), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y,
                        w - browseW - 4, kDeletePageDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndDestEdit, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    dlg->hwndBrowseBtn = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x + w - browseW,
                                         y, browseW, kDeletePageDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndBrowseBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    y += kDeletePageDlgRowH + kDeletePageDlgRowGap;

    // row 3: pages label + pages edit + total pages label
    // offset static labels to align with text inside edit control (same as input path label)
    int labelW = 100;
    int labelX = x + kEditTextXOffset;
    const WCHAR* pagesLabelText = isExtract ? L"Pages To Extract:" : L"Pages To Delete:";
    dlg->hwndPagesLabel = CreateWindowExW(0, L"STATIC", pagesLabelText, WS_CHILD | WS_VISIBLE | SS_LEFT, labelX,
                                          y + kEditTextXOffset, labelW, kDeletePageDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndPagesLabel, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    TempStr totalStr = str::FormatTemp("of %d", pageCount);
    int totalW = 60;
    int editX = labelX + labelW + 8;
    int editW = x + w - editX - totalW - 4;
    int currentPage = win->ctrl ? win->ctrl->CurrentPageNo() : 1;
    TempStr pagesStr = str::FormatTemp("%d", currentPage);
    dlg->hwndPagesEdit =
        CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, ToWStrTemp(pagesStr), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, editX,
                        y, editW, kDeletePageDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndPagesEdit, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    dlg->hwndTotalLabel =
        CreateWindowExW(0, L"STATIC", ToWStrTemp(totalStr), WS_CHILD | WS_VISIBLE | SS_LEFT, editX + editW + 4,
                        y + kEditTextXOffset, totalW, kDeletePageDlgRowH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndTotalLabel, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    y += kDeletePageDlgRowH + kDeletePageDlgRowGap;

    // row 4: syntax hint (left) + action + Cancel buttons (right)
    int btnW = 85;
    int btnH = 24;

    // syntax hint label, x-aligned with pages label and baseline-aligned with button text
    HWND hwndSyntax = CreateWindowExW(0, L"STATIC", L"Syntax: 2,5-7,13-N", WS_CHILD | WS_VISIBLE | SS_LEFT, labelX,
                                      y + kEditTextXOffset, w / 2, btnH, hwnd, nullptr, h, nullptr);
    SendMessageW(hwndSyntax, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    int bx = x + w - btnW;
    dlg->hwndCancelBtn = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, bx, y, btnW,
                                         btnH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndCancelBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);
    bx -= btnW + 4;
    const WCHAR* actionBtnText = isExtract ? L"Extract Pages" : L"Delete Pages";
    dlg->hwndDeleteBtn = CreateWindowExW(0, L"BUTTON", actionBtnText, WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, bx, y,
                                         btnW, btnH, hwnd, nullptr, h, nullptr);
    SendMessageW(dlg->hwndDeleteBtn, WM_SETFONT, (WPARAM)dlg->hFont, TRUE);

    // validate initial state
    PdfDeletePageUpdateButton(dlg);

    CenterDialog(hwnd, win->hwndFrame);
    if (UseDarkModeLib()) {
        DarkMode::setDarkWndSafe(hwnd);
        DarkMode::setWindowEraseBgSubclass(hwnd);
    }
    ShowWindow(hwnd, SW_SHOW);
}

void ShowPdfDeletePageDialog(MainWindow* win) {
    ShowPdfPageRangeDialog(win, false);
}

void ShowPdfExtractPagesDialog(MainWindow* win) {
    ShowPdfPageRangeDialog(win, true);
}
