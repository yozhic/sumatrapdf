/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/ScopedWin.h"
#include "utils/WinDynCalls.h"
#include "utils/FileUtil.h"
#include "utils/WinUtil.h"
#include "utils/GdiPlusUtil.h"
#include "utils/Log.h"

#include "AppTools.h"

static bool ShouldCaptureWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) {
        return false;
    }
    if (hwnd == GetDesktopWindow()) {
        return false;
    }
    WCHAR className[256];
    if (GetClassNameW(hwnd, className, 256) > 0) {
        if (str::Eq(className, L"Progman") || str::Eq(className, L"WorkerW")) {
            return false;
        }
    }
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) {
        return false;
    }
    HWND hwndOwner = GetWindow(hwnd, GW_OWNER);
    if (hwndOwner != nullptr && !(exStyle & WS_EX_APPWINDOW)) {
        return false;
    }
    BOOL isCloaked = FALSE;
    if (SUCCEEDED(dwm::GetWindowAttribute(hwnd, DWMWA_CLOAKED, &isCloaked, sizeof(isCloaked)))) {
        if (isCloaked) {
            return false;
        }
    }
    RECT rect;
    if (!GetWindowRect(hwnd, &rect)) {
        return false;
    }
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) {
        return false;
    }
    if (w * h < 100) {
        return false;
    }
    WCHAR title[256];
    int titleLen = GetWindowTextW(hwnd, title, 256);
    if (titleLen == 0) {
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        if (!(style & WS_POPUP)) {
            return false;
        }
    }
    return true;
}

static bool SaveHBitmapAsPng(HBITMAP hBitmap, const char* filePath) {
    if (!hBitmap || !filePath) {
        return false;
    }
    Gdiplus::Bitmap gbmp(hBitmap, nullptr);
    CLSID pngEncId = GetEncoderClsid(L"image/png");
    TempWStr filePathW = ToWStrTemp(filePath);
    Gdiplus::Status status = gbmp.Save(filePathW, &pngEncId, nullptr);
    return status == Gdiplus::Ok;
}

static HBITMAP CaptureDesktop() {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
    SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    return hbm;
}

static HBITMAP CaptureWindowBmp(HWND hwnd) {
    RECT rect;
    if (!GetWindowRect(hwnd, &rect)) {
        return nullptr;
    }
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) {
        return nullptr;
    }

    HDC hdcWin = GetWindowDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcWin);
    HBITMAP hbm = CreateCompatibleBitmap(hdcWin, w, h);
    SelectObject(hdcMem, hbm);

    // Try PrintWindow with PW_RENDERFULLCONTENT (0x2) first (Windows 8.1+)
    BOOL ok = PrintWindow(hwnd, hdcMem, 0x2);
    if (!ok) {
        ok = BitBlt(hdcMem, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);
    }

    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWin);

    if (ok) {
        return hbm;
    }
    DeleteObject(hbm);
    return nullptr;
}

// Get process name (without .exe extension) for a window
static TempStr GetWindowProcessNameTemp(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return nullptr;
    }
    AutoCloseHandle hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc.IsValid()) {
        return nullptr;
    }
    WCHAR path[MAX_PATH]{};
    DWORD pathLen = MAX_PATH;
    if (!QueryFullProcessImageNameW(hProc, 0, path, &pathLen)) {
        return nullptr;
    }
    TempStr fullPath = ToUtf8Temp(path);
    TempStr baseName = path::GetBaseNameTemp(fullPath);
    // Remove .exe extension
    TempStr noExt = path::GetPathNoExtTemp(baseName);
    return noExt;
}

// Build a unique file path: dir/base.png, dir/base.1.png, dir/base.2.png, ...
static TempStr MakeUniquePathTemp(const char* dir, const char* base) {
    TempStr name = str::FormatTemp("%s.png", base);
    TempStr path = path::JoinTemp(dir, name);
    if (!file::Exists(path)) {
        return path;
    }
    for (int i = 1; i < 10000; i++) {
        name = str::FormatTemp("%s.%d.png", base, i);
        path = path::JoinTemp(dir, name);
        if (!file::Exists(path)) {
            return path;
        }
    }
    return nullptr;
}

struct ScreenshotWindowInfo {
    HWND hwnd;
};

static BOOL CALLBACK EnumScreenshotWindowsProc(HWND hwnd, LPARAM lParam) {
    Vec<ScreenshotWindowInfo>* windows = (Vec<ScreenshotWindowInfo>*)lParam;
    if (ShouldCaptureWindow(hwnd)) {
        ScreenshotWindowInfo info;
        info.hwnd = hwnd;
        windows->Append(info);
    }
    return TRUE;
}

void TakeScreenshots() {
    TempStr dataDir = GetAppDataDirTemp();
    TempStr screenshotDir = path::JoinTemp(dataDir, "Screenshots");
    dir::CreateAll(screenshotDir);

    // Capture desktop
    HBITMAP hbmDesktop = CaptureDesktop();
    if (hbmDesktop) {
        TempStr desktopPath = MakeUniquePathTemp(screenshotDir, "desktop");
        if (desktopPath) {
            SaveHBitmapAsPng(hbmDesktop, desktopPath);
            logf("Screenshot: saved desktop to '%s'\n", desktopPath);
        }
        DeleteObject(hbmDesktop);
    }

    // Capture individual windows
    Vec<ScreenshotWindowInfo> windows;
    EnumWindows(EnumScreenshotWindowsProc, (LPARAM)&windows);

    for (auto& wi : windows) {
        HBITMAP hbm = CaptureWindowBmp(wi.hwnd);
        if (!hbm) {
            continue;
        }
        TempStr procName = GetWindowProcessNameTemp(wi.hwnd);
        if (!procName) {
            procName = (TempStr) "unknown";
        }
        TempStr filePath = MakeUniquePathTemp(screenshotDir, procName);
        if (filePath) {
            SaveHBitmapAsPng(hbm, filePath);
            logf("Screenshot: saved window to '%s'\n", filePath);
        }
        DeleteObject(hbm);
    }

    // Open screenshot directory in default file manager
    LaunchFileShell(screenshotDir, nullptr, "open");
}
