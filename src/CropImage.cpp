/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/ScopedWin.h"
#include "utils/FileUtil.h"
#include "utils/WinUtil.h"
#include "utils/GdiPlusUtil.h"
#include "utils/GuessFileType.h"
#include "FzImgReader.h"

#include "wingui/UIModels.h"
#include "wingui/Layout.h"
#include "wingui/WinGui.h"

#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "DisplayModel.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Commands.h"
#include "SumatraConfig.h"
#include "Theme.h"
#include "DarkModeSubclass.h"
#include "Translations.h"
#include "CropImage.h"

#include "utils/Log.h"

using Gdiplus::Bitmap;
using Gdiplus::Color;
using Gdiplus::Graphics;
using Gdiplus::Ok;
using Gdiplus::Status;

constexpr const WCHAR* kCropImageWinClassName = L"SUMATRA_PDF_CROP_IMAGE";

constexpr int kMinWindowWidth = 640;
constexpr int kImagePadding = 8;
constexpr int kResizeEdgeThreshold = 2;
constexpr int kDragHandleSize = 6;
constexpr int kControlAreaDy = 100;
constexpr int kRowPadding = 6;
constexpr int kButtonPadding = 8;

struct CropImageWindow {
    HWND hwnd = nullptr;
    HWND hwndParent = nullptr;

    // child controls
    HWND hwndPathLabel = nullptr;
    HWND hwndDestEdit = nullptr;
    HWND hwndBrowseBtn = nullptr;
    HWND hwndInfoLabel = nullptr;
    Button* btnCancel = nullptr;
    Button* btnSave = nullptr;

    // source image
    char* filePath = nullptr;
    Bitmap* srcBitmap = nullptr;
    int imgW = 0;
    int imgH = 0;

    // display
    int imgDisplayX = 0;
    int imgDisplayY = 0;
    int imgDisplayW = 0;
    int imgDisplayH = 0;
    int imgAreaH = 0; // height of the image area (window height - control area)

    // crop rectangle in image coordinates
    int cropX = 0;
    int cropY = 0;
    int cropW = 0;
    int cropH = 0;

    // drag state
    bool isDragging = false;
    enum class DragEdge {
        None,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        Move
    };
    DragEdge dragEdge = DragEdge::None;
    POINT dragStart{};
    int dragCropX = 0;
    int dragCropY = 0;
    int dragCropW = 0;
    int dragCropH = 0;

    HFONT hFont = nullptr;

    CropImageWindow() = default;
    ~CropImageWindow() {
        delete srcBitmap;
        free(filePath);
        delete btnCancel;
        delete btnSave;
    }
};

static Vec<CropImageWindow*> gCropWindows;

static CropImageWindow* FindCropWindowByHwnd(HWND hwnd) {
    for (auto* cw : gCropWindows) {
        if (cw->hwnd == hwnd) {
            return cw;
        }
    }
    return nullptr;
}

// Convert display coordinates to image coordinates
static int DisplayToImageX(CropImageWindow* cw, int dx) {
    if (cw->imgDisplayW <= 0) {
        return 0;
    }
    int v = (int)((float)(dx - cw->imgDisplayX) * cw->imgW / cw->imgDisplayW);
    return std::clamp(v, 0, cw->imgW);
}

static int DisplayToImageY(CropImageWindow* cw, int dy) {
    if (cw->imgDisplayH <= 0) {
        return 0;
    }
    int v = (int)((float)(dy - cw->imgDisplayY) * cw->imgH / cw->imgDisplayH);
    return std::clamp(v, 0, cw->imgH);
}

// Convert image coordinates to display coordinates
static int ImageToDisplayX(CropImageWindow* cw, int ix) {
    if (cw->imgW <= 0) {
        return cw->imgDisplayX;
    }
    return cw->imgDisplayX + (int)((float)ix * cw->imgDisplayW / cw->imgW);
}

static int ImageToDisplayY(CropImageWindow* cw, int iy) {
    if (cw->imgH <= 0) {
        return cw->imgDisplayY;
    }
    return cw->imgDisplayY + (int)((float)iy * cw->imgDisplayH / cw->imgH);
}

static void LayoutControls(CropImageWindow* cw);

static void UpdateSaveButtonText(CropImageWindow* cw) {
    WCHAR destW[MAX_PATH + 1]{};
    GetWindowTextW(cw->hwndDestEdit, destW, MAX_PATH);
    TempStr dest = ToUtf8Temp(destW);
    const char* text = file::Exists(dest) ? _TRA("Overwrite With Cropped") : _TRA("Save Cropped");
    cw->btnSave->SetText(text);
    // re-layout since button width may have changed
    LayoutControls(cw);
}

static TempStr FormatCropInfoTemp(int srcW, int srcH, int cropW, int cropH, int cropX, int cropY) {
    return str::FormatTemp("%d x %d => %d x %d @ %d , %d", srcW, srcH, cropW, cropH, cropX, cropY);
}

static void UpdateCropInfoLabel(CropImageWindow* cw) {
    TempStr s = FormatCropInfoTemp(cw->imgW, cw->imgH, cw->cropW, cw->cropH, cw->cropX, cw->cropY);
    SetWindowTextA(cw->hwndInfoLabel, s);
}

// invalidate only the image area, not the control area below
static void InvalidateImageArea(CropImageWindow* cw) {
    RECT rc = {0, 0, 0, 0};
    GetClientRect(cw->hwnd, &rc);
    rc.bottom = cw->imgAreaH;
    InvalidateRect(cw->hwnd, &rc, FALSE);
}

static void CalcImageLayout(CropImageWindow* cw) {
    Rect cRc = ClientRect(cw->hwnd);
    cw->imgAreaH = cRc.dy - kControlAreaDy;
    if (cw->imgAreaH < 10) {
        cw->imgAreaH = 10;
    }

    // fit image within image area with padding
    int availW = cRc.dx - 2 * kImagePadding;
    int availH = cw->imgAreaH - 2 * kImagePadding;
    if (availW <= 0 || availH <= 0 || cw->imgW <= 0 || cw->imgH <= 0) {
        cw->imgDisplayX = kImagePadding;
        cw->imgDisplayY = kImagePadding;
        cw->imgDisplayW = 0;
        cw->imgDisplayH = 0;
        return;
    }

    float scaleX = (float)availW / cw->imgW;
    float scaleY = (float)availH / cw->imgH;
    float scale = std::min(scaleX, scaleY);
    // don't upscale beyond 100%
    if (scale > 1.0f) {
        scale = 1.0f;
    }

    cw->imgDisplayW = (int)(cw->imgW * scale);
    cw->imgDisplayH = (int)(cw->imgH * scale);
    // center in available area
    cw->imgDisplayX = kImagePadding + (availW - cw->imgDisplayW) / 2;
    cw->imgDisplayY = kImagePadding + (availH - cw->imgDisplayH) / 2;
}

static CropImageWindow::DragEdge HitTestCropEdge(CropImageWindow* cw, int mx, int my) {
    int left = ImageToDisplayX(cw, cw->cropX);
    int right = ImageToDisplayX(cw, cw->cropX + cw->cropW);
    int top = ImageToDisplayY(cw, cw->cropY);
    int bottom = ImageToDisplayY(cw, cw->cropY + cw->cropH);

    int t = kResizeEdgeThreshold;

    bool onLeft = (mx >= left - t && mx <= left + t);
    bool onRight = (mx >= right - t && mx <= right + t);
    bool onTop = (my >= top - t && my <= top + t);
    bool onBottom = (my >= bottom - t && my <= bottom + t);

    bool inVertRange = (my >= top - t && my <= bottom + t);
    bool inHorzRange = (mx >= left - t && mx <= right + t);

    if (onLeft && onTop) {
        return CropImageWindow::DragEdge::TopLeft;
    }
    if (onRight && onTop) {
        return CropImageWindow::DragEdge::TopRight;
    }
    if (onLeft && onBottom) {
        return CropImageWindow::DragEdge::BottomLeft;
    }
    if (onRight && onBottom) {
        return CropImageWindow::DragEdge::BottomRight;
    }
    if (onLeft && inVertRange) {
        return CropImageWindow::DragEdge::Left;
    }
    if (onRight && inVertRange) {
        return CropImageWindow::DragEdge::Right;
    }
    if (onTop && inHorzRange) {
        return CropImageWindow::DragEdge::Top;
    }
    if (onBottom && inHorzRange) {
        return CropImageWindow::DragEdge::Bottom;
    }
    // inside the crop rect = move
    if (mx > left + t && mx < right - t && my > top + t && my < bottom - t) {
        return CropImageWindow::DragEdge::Move;
    }
    return CropImageWindow::DragEdge::None;
}

static HCURSOR GetCursorForEdge(CropImageWindow::DragEdge edge) {
    switch (edge) {
        case CropImageWindow::DragEdge::Left:
        case CropImageWindow::DragEdge::Right:
            return LoadCursor(nullptr, IDC_SIZEWE);
        case CropImageWindow::DragEdge::Top:
        case CropImageWindow::DragEdge::Bottom:
            return LoadCursor(nullptr, IDC_SIZENS);
        case CropImageWindow::DragEdge::TopLeft:
        case CropImageWindow::DragEdge::BottomRight:
            return LoadCursor(nullptr, IDC_SIZENWSE);
        case CropImageWindow::DragEdge::TopRight:
        case CropImageWindow::DragEdge::BottomLeft:
            return LoadCursor(nullptr, IDC_SIZENESW);
        case CropImageWindow::DragEdge::Move:
            return LoadCursor(nullptr, IDC_SIZEALL);
        default:
            return LoadCursor(nullptr, IDC_ARROW);
    }
}

static void PaintCropImage(CropImageWindow* cw, HDC hdc) {
    Rect cRc = ClientRect(cw->hwnd);

    // fill image area background
    HBRUSH bgBrush = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
    RECT rcImg = {0, 0, cRc.dx, cw->imgAreaH};
    FillRect(hdc, &rcImg, bgBrush);

    if (!cw->srcBitmap || cw->imgDisplayW <= 0 || cw->imgDisplayH <= 0) {
        return;
    }

    Graphics g(hdc);

    // draw the image
    g.DrawImage(cw->srcBitmap, cw->imgDisplayX, cw->imgDisplayY, cw->imgDisplayW, cw->imgDisplayH);

    // draw semi-transparent overlay over non-cropped areas
    int cropDispX = ImageToDisplayX(cw, cw->cropX);
    int cropDispY = ImageToDisplayY(cw, cw->cropY);
    int cropDispR = ImageToDisplayX(cw, cw->cropX + cw->cropW);
    int cropDispB = ImageToDisplayY(cw, cw->cropY + cw->cropH);

    int ix = cw->imgDisplayX;
    int iy = cw->imgDisplayY;
    int iw = cw->imgDisplayW;
    int ih = cw->imgDisplayH;
    int ir = ix + iw;
    int ib = iy + ih;

    // draw semi-transparent overlay using GDI+ with explicit Gdiplus::Brush pointer
    Gdiplus::SolidBrush overlayBrush(Gdiplus::Color(128, 0, 0, 0));
    Gdiplus::Brush* pBrush = &overlayBrush;

    // top strip
    if (cropDispY > iy) {
        g.FillRectangle(pBrush, ix, iy, iw, cropDispY - iy);
    }
    // bottom strip
    if (cropDispB < ib) {
        g.FillRectangle(pBrush, ix, cropDispB, iw, ib - cropDispB);
    }
    // left strip (between top and bottom crop)
    if (cropDispX > ix) {
        g.FillRectangle(pBrush, ix, cropDispY, cropDispX - ix, cropDispB - cropDispY);
    }
    // right strip
    if (cropDispR < ir) {
        g.FillRectangle(pBrush, cropDispR, cropDispY, ir - cropDispR, cropDispB - cropDispY);
    }

    // draw crop border
    Gdiplus::Pen pen(Color(255, 255, 255), 1.0f);
    pen.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawRectangle(&pen, cropDispX, cropDispY, cropDispR - cropDispX, cropDispB - cropDispY);

    // draw drag handles at corners and edge midpoints
    int hs = kDragHandleSize;
    int hh = hs / 2;
    int midX = (cropDispX + cropDispR) / 2;
    int midY = (cropDispY + cropDispB) / 2;

    Gdiplus::SolidBrush handleBrush(Color(255, 255, 255, 255));
    Gdiplus::Pen handlePen(Color(255, 0, 0, 0), 1);

    auto drawHandle = [&](int cx, int cy) {
        g.FillRectangle(&handleBrush, cx - hh, cy - hh, hs, hs);
        g.DrawRectangle(&handlePen, cx - hh, cy - hh, hs, hs);
    };

    // corners
    drawHandle(cropDispX, cropDispY);
    drawHandle(cropDispR, cropDispY);
    drawHandle(cropDispX, cropDispB);
    drawHandle(cropDispR, cropDispB);
    // edge midpoints
    drawHandle(midX, cropDispY);
    drawHandle(midX, cropDispB);
    drawHandle(cropDispX, midY);
    drawHandle(cropDispR, midY);
}

static void LayoutControls(CropImageWindow* cw) {
    Rect cRc = ClientRect(cw->hwnd);
    int y = cw->imgAreaH + kRowPadding;
    int x = kButtonPadding;
    int w = cRc.dx - 2 * kButtonPadding;

    // row 1: file path label, shifted right to align with edit text
    int editBorder = GetSystemMetrics(SM_CXEDGE);
    LRESULT margins = SendMessageW(cw->hwndDestEdit, EM_GETMARGINS, 0, 0);
    int editLeftMargin = LOWORD(margins);
    int labelShift = editBorder + editLeftMargin;
    MoveWindow(cw->hwndPathLabel, x + labelShift, y, w - labelShift, 16, TRUE);
    y += 16 + kRowPadding;

    // row 2: dest edit + browse button
    int browseW = 30;
    MoveWindow(cw->hwndDestEdit, x, y, w - browseW - 4, 22, TRUE);
    MoveWindow(cw->hwndBrowseBtn, x + w - browseW, y, browseW, 22, TRUE);
    y += 22 + kRowPadding;

    // row 3: src size, crop info, cancel, save
    MoveWindow(cw->hwndInfoLabel, x, y + 4, 350, 16, TRUE);

    if (cw->btnSave && cw->btnCancel) {
        Size szSave = cw->btnSave->GetIdealSize();
        Size szCancel = cw->btnCancel->GetIdealSize();
        int bx = cRc.dx - kButtonPadding - szCancel.dx;
        cw->btnCancel->SetBounds({bx, y, szCancel.dx, szCancel.dy});
        bx -= szSave.dx + 4;
        cw->btnSave->SetBounds({bx, y, szSave.dx, szSave.dy});
    }
}

static void OnBrowse(CropImageWindow* cw) {
    WCHAR dstFileName[MAX_PATH + 1]{};
    // pre-populate with current dest path
    int len = GetWindowTextW(cw->hwndDestEdit, dstFileName, MAX_PATH);
    (void)len;

    OPENFILENAME ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = cw->hwnd;
    ofn.lpstrFile = dstFileName;
    ofn.nMaxFile = dimof(dstFileName);
    ofn.lpstrFilter =
        L"All Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff;*.tif;*.webp\0"
        L"PNG Files\0*.png\0"
        L"JPEG Files\0*.jpg;*.jpeg\0"
        L"BMP Files\0*.bmp\0"
        L"All Files\0*.*\0";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetSaveFileNameW(&ofn)) {
        SetWindowTextW(cw->hwndDestEdit, dstFileName);
    }
}

static bool IsKnownImageExt(const char* ext) {
    const char* exts[] = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tiff", ".tif"};
    for (auto e : exts) {
        if (str::EqI(ext, e)) {
            return true;
        }
    }
    return false;
}

static const WCHAR* GetEncoderMimeForExt(const char* ext) {
    if (str::EqI(ext, ".png")) {
        return L"image/png";
    }
    if (str::EqI(ext, ".jpg") || str::EqI(ext, ".jpeg")) {
        return L"image/jpeg";
    }
    if (str::EqI(ext, ".bmp")) {
        return L"image/bmp";
    }
    if (str::EqI(ext, ".gif")) {
        return L"image/gif";
    }
    if (str::EqI(ext, ".tiff") || str::EqI(ext, ".tif")) {
        return L"image/tiff";
    }
    // default to PNG
    return L"image/png";
}

// Get the file extension that matches the source image format.
// Returns the source extension if it's a known image format, otherwise ".png".
static const char* GetMatchingExt(const char* srcExt) {
    if (IsKnownImageExt(srcExt)) {
        return srcExt;
    }
    return ".png";
}

// Ensure dest path has an extension matching a supported image format.
// If the extension is unknown, replace it (or append) with one derived from the source file.
static TempStr EnsureImageExtTemp(const char* dest, const char* srcFilePath) {
    TempStr destExt = path::GetExtTemp(dest);
    if (IsKnownImageExt(destExt)) {
        return str::DupTemp(dest);
    }
    // use source file's extension if it's known, otherwise default to .png
    TempStr srcExt = path::GetExtTemp(srcFilePath);
    const char* ext = GetMatchingExt(srcExt);
    if (str::IsEmpty(destExt)) {
        // no extension at all - append
        return str::FormatTemp("%s%s", dest, ext);
    }
    // has an unrecognized extension - replace it
    int baseLen = str::Leni(dest) - str::Leni(destExt);
    TempStr base = str::DupTemp(dest, baseLen);
    return str::FormatTemp("%s%s", base, ext);
}

static void OnSave(CropImageWindow* cw) {
    if (!cw->srcBitmap || cw->cropW <= 0 || cw->cropH <= 0) {
        return;
    }

    WCHAR rawDestW[MAX_PATH + 1]{};
    GetWindowTextW(cw->hwndDestEdit, rawDestW, MAX_PATH);
    TempStr rawDest = ToUtf8Temp(rawDestW);
    if (str::IsEmpty(rawDest)) {
        return;
    }

    // ensure the destination has a valid image extension
    TempStr dest = EnsureImageExtTemp(rawDest, cw->filePath);

    // create cropped bitmap
    Gdiplus::Rect srcRect(cw->cropX, cw->cropY, cw->cropW, cw->cropH);
    Bitmap* cropped = cw->srcBitmap->Clone(srcRect, cw->srcBitmap->GetPixelFormat());
    if (!cropped) {
        MessageBoxWarning(cw->hwnd, "Failed to create cropped image", "Crop Image");
        return;
    }

    TempStr ext = path::GetExtTemp(dest);
    const WCHAR* mime = GetEncoderMimeForExt(ext);
    CLSID encoderClsid = GetEncoderClsid(mime);
    TempWStr destW = ToWStrTemp(dest);
    Status status = cropped->Save(destW, &encoderClsid, nullptr);
    delete cropped;

    if (status != Ok) {
        MessageBoxWarning(cw->hwnd, "Failed to save cropped image", _TRA("Crop Image"));
        return;
    }

    // load the saved image
    HWND hwndParent = cw->hwndParent;
    char* savedPath = str::Dup(dest);
    DestroyWindow(cw->hwnd);

    MainWindow* win = FindMainWindowByHwnd(hwndParent);
    if (!win && !gWindows.IsEmpty()) {
        win = gWindows.at(0);
    }
    if (win) {
        LoadArgs args(savedPath, win);
        StartLoadDocument(&args);
    }
    free(savedPath);
}

static void OnCancel(CropImageWindow* cw) {
    DestroyWindow(cw->hwnd);
}

LRESULT CALLBACK WndProcCropImage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

LRESULT CALLBACK WndProcCropImage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CropImageWindow* cw;

    LRESULT res = TryReflectMessages(hwnd, msg, wp, lp);
    if (res != 0) {
        return res;
    }

    switch (msg) {
        case WM_CREATE:
            break;

        case WM_SIZE: {
            cw = FindCropWindowByHwnd(hwnd);
            if (cw) {
                CalcImageLayout(cw);
                LayoutControls(cw);
                InvalidateImageArea(cw);
            }
            return 0;
        }

        case WM_PAINT: {
            cw = FindCropWindowByHwnd(hwnd);
            if (cw) {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                // double-buffer only the image area to avoid flicker
                Rect cRc = ClientRect(hwnd);
                int paintH = cw->imgAreaH;
                if (paintH > cRc.dy) {
                    paintH = cRc.dy;
                }
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBmp = CreateCompatibleBitmap(hdc, cRc.dx, paintH);
                HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
                PaintCropImage(cw, memDC);
                BitBlt(hdc, 0, 0, cRc.dx, paintH, memDC, 0, 0, SRCCOPY);
                SelectObject(memDC, oldBmp);
                DeleteObject(memBmp);
                DeleteDC(memDC);
                EndPaint(hwnd, &ps);
            }
            return 0;
        }

        case WM_ERASEBKGND: {
            cw = FindCropWindowByHwnd(hwnd);
            if (cw) {
                // paint control area background, skip image area (double-buffered)
                HDC hdc = (HDC)wp;
                RECT crc;
                GetClientRect(hwnd, &crc);
                RECT ctrlRc = {0, cw->imgAreaH, crc.right, crc.bottom};
                FillRect(hdc, &ctrlRc, GetSysColorBrush(COLOR_BTNFACE));
            }
            return 1;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wp;
            SetBkMode(hdcStatic, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }

        case WM_MOUSEMOVE: {
            cw = FindCropWindowByHwnd(hwnd);
            if (!cw) {
                break;
            }
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);
            if (cw->isDragging) {
                int dx = mx - cw->dragStart.x;
                int dy = my - cw->dragStart.y;
                int imgDx = DisplayToImageX(cw, mx) - DisplayToImageX(cw, cw->dragStart.x);
                int imgDy = DisplayToImageY(cw, my) - DisplayToImageY(cw, cw->dragStart.y);

                auto edge = cw->dragEdge;
                int nx = cw->dragCropX;
                int ny = cw->dragCropY;
                int nw = cw->dragCropW;
                int nh = cw->dragCropH;

                if (edge == CropImageWindow::DragEdge::Left || edge == CropImageWindow::DragEdge::TopLeft ||
                    edge == CropImageWindow::DragEdge::BottomLeft) {
                    nx = cw->dragCropX + imgDx;
                    nw = cw->dragCropW - imgDx;
                }
                if (edge == CropImageWindow::DragEdge::Right || edge == CropImageWindow::DragEdge::TopRight ||
                    edge == CropImageWindow::DragEdge::BottomRight) {
                    nw = cw->dragCropW + imgDx;
                }
                if (edge == CropImageWindow::DragEdge::Top || edge == CropImageWindow::DragEdge::TopLeft ||
                    edge == CropImageWindow::DragEdge::TopRight) {
                    ny = cw->dragCropY + imgDy;
                    nh = cw->dragCropH - imgDy;
                }
                if (edge == CropImageWindow::DragEdge::Bottom || edge == CropImageWindow::DragEdge::BottomLeft ||
                    edge == CropImageWindow::DragEdge::BottomRight) {
                    nh = cw->dragCropH + imgDy;
                }
                if (edge == CropImageWindow::DragEdge::Move) {
                    nx = cw->dragCropX + imgDx;
                    ny = cw->dragCropY + imgDy;
                    // clamp to image bounds
                    if (nx < 0) {
                        nx = 0;
                    }
                    if (ny < 0) {
                        ny = 0;
                    }
                    if (nx + nw > cw->imgW) {
                        nx = cw->imgW - nw;
                    }
                    if (ny + nh > cw->imgH) {
                        ny = cw->imgH - nh;
                    }
                }

                // enforce minimum size and bounds
                if (nw < 1) {
                    nw = 1;
                    nx = cw->cropX;
                }
                if (nh < 1) {
                    nh = 1;
                    ny = cw->cropY;
                }
                if (nx < 0) {
                    nw += nx;
                    nx = 0;
                }
                if (ny < 0) {
                    nh += ny;
                    ny = 0;
                }
                if (nx + nw > cw->imgW) {
                    nw = cw->imgW - nx;
                }
                if (ny + nh > cw->imgH) {
                    nh = cw->imgH - ny;
                }

                cw->cropX = nx;
                cw->cropY = ny;
                cw->cropW = nw;
                cw->cropH = nh;
                UpdateCropInfoLabel(cw);
                InvalidateImageArea(cw);
            } else {
                auto edge = HitTestCropEdge(cw, mx, my);
                SetCursor(GetCursorForEdge(edge));
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            cw = FindCropWindowByHwnd(hwnd);
            if (!cw) {
                break;
            }
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);
            auto edge = HitTestCropEdge(cw, mx, my);
            if (edge != CropImageWindow::DragEdge::None) {
                cw->isDragging = true;
                cw->dragEdge = edge;
                cw->dragStart = {mx, my};
                cw->dragCropX = cw->cropX;
                cw->dragCropY = cw->cropY;
                cw->dragCropW = cw->cropW;
                cw->dragCropH = cw->cropH;
                SetCapture(hwnd);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            cw = FindCropWindowByHwnd(hwnd);
            if (cw && cw->isDragging) {
                cw->isDragging = false;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_SETCURSOR: {
            cw = FindCropWindowByHwnd(hwnd);
            if (cw && LOWORD(lp) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                auto edge = HitTestCropEdge(cw, pt.x, pt.y);
                if (edge != CropImageWindow::DragEdge::None) {
                    SetCursor(GetCursorForEdge(edge));
                    return TRUE;
                }
            }
            return DefWindowProc(hwnd, msg, wp, lp);
        }

        case WM_CHAR:
            if (VK_ESCAPE == wp) {
                DestroyWindow(hwnd);
            }
            break;

        case WM_COMMAND: {
            cw = FindCropWindowByHwnd(hwnd);
            if (!cw) {
                break;
            }
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            // browse button
            if ((HWND)lp == cw->hwndBrowseBtn && code == BN_CLICKED) {
                OnBrowse(cw);
                UpdateSaveButtonText(cw);
                return 0;
            }
            // dest edit changed
            if ((HWND)lp == cw->hwndDestEdit && code == EN_CHANGE) {
                UpdateSaveButtonText(cw);
                return 0;
            }
            break;
        }

        case WM_DESTROY:
            cw = FindCropWindowByHwnd(hwnd);
            if (cw) {
                gCropWindows.Remove(cw);
                delete cw;
            }
            break;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

void ShowCropImageWindow(MainWindow* win) {
    if (!win) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || !tab->filePath) {
        return;
    }
    Kind engineType = tab->GetEngineType();
    if (engineType != kindEngineImage) {
        return;
    }
    EngineBase* engine = tab->GetEngine();
    if (!engine) {
        return;
    }

    // load the image
    const char* filePath = tab->filePath;
    ByteSlice data = file::ReadFile(filePath);
    if (data.empty()) {
        return;
    }
    Bitmap* bmp = BitmapFromData(data);
    data.Free();
    if (!bmp) {
        return;
    }

    int imgW = (int)bmp->GetWidth();
    int imgH = (int)bmp->GetHeight();
    if (imgW <= 0 || imgH <= 0) {
        delete bmp;
        return;
    }

    auto* cw = new CropImageWindow();
    cw->filePath = str::Dup(filePath);
    cw->srcBitmap = bmp;
    cw->imgW = imgW;
    cw->imgH = imgH;
    // initial crop = full image
    cw->cropX = 0;
    cw->cropY = 0;
    cw->cropW = imgW;
    cw->cropH = imgH;

    gCropWindows.Append(cw);

    HMODULE h = GetModuleHandleW(nullptr);
    WNDCLASSEX wcex = {};
    FillWndClassEx(wcex, kCropImageWinClassName, WndProcCropImage);
    wcex.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    WCHAR* iconName = MAKEINTRESOURCEW(GetAppIconID());
    wcex.hIcon = LoadIconW(h, iconName);
    RegisterClassEx(&wcex);

    // calculate window size: image at 100% + padding + control area, clamped to screen
    int wantW = imgW + 2 * kImagePadding;
    int wantH = imgH + 2 * kImagePadding + kControlAreaDy;
    // add window chrome
    RECT rc = {0, 0, wantW, wantH};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winW = rc.right - rc.left;
    if (winW < kMinWindowWidth) {
        winW = kMinWindowWidth;
    }
    int winH = rc.bottom - rc.top;
    // clamp to screen
    HMONITOR hMon = MonitorFromWindow(win->hwndFrame, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(hMon, &mi);
    int screenW = mi.rcWork.right - mi.rcWork.left;
    int screenH = mi.rcWork.bottom - mi.rcWork.top;
    if (winW > screenW) {
        winW = screenW;
    }
    if (winH > screenH) {
        winH = screenH;
    }

    HWND hwnd = CreateWindowExW(0, kCropImageWinClassName, L"Crop Image", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, winW, winH, nullptr, nullptr, h, nullptr);
    if (!hwnd) {
        gCropWindows.Remove(cw);
        delete cw;
        return;
    }

    cw->hwnd = hwnd;
    cw->hwndParent = win->hwndFrame;

    // create font
    cw->hFont = GetDefaultGuiFont();

    // create child controls
    // row 1: file path label (read-only)
    cw->hwndPathLabel =
        CreateWindowExW(0, L"STATIC", ToWStrTemp(filePath), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS, 0, 0, 0,
                        0, hwnd, nullptr, h, nullptr);
    SendMessageW(cw->hwndPathLabel, WM_SETFONT, (WPARAM)cw->hFont, TRUE);

    // row 2: dest edit + browse
    TempStr destPath = MakeUniqueFilePathTemp(filePath);
    cw->hwndDestEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, ToWStrTemp(destPath),
                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, nullptr, h, nullptr);
    SendMessageW(cw->hwndDestEdit, WM_SETFONT, (WPARAM)cw->hFont, TRUE);

    cw->hwndBrowseBtn = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                        nullptr, h, nullptr);
    SendMessageW(cw->hwndBrowseBtn, WM_SETFONT, (WPARAM)cw->hFont, TRUE);

    // row 3: src size label, crop info label
    TempStr infoStr = FormatCropInfoTemp(imgW, imgH, imgW, imgH, 0, 0);
    cw->hwndInfoLabel = CreateWindowExW(0, L"STATIC", ToWStrTemp(infoStr), WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0,
                                        hwnd, nullptr, h, nullptr);
    SendMessageW(cw->hwndInfoLabel, WM_SETFONT, (WPARAM)cw->hFont, TRUE);

    // buttons
    {
        auto* btn = new Button();
        Button::CreateArgs args;
        args.parent = hwnd;
        args.text = "Cancel";
        btn->Create(args);
        btn->onClick = MkFunc0<CropImageWindow>(OnCancel, cw);
        cw->btnCancel = btn;
    }
    {
        auto* btn = new Button();
        Button::CreateArgs args;
        args.parent = hwnd;
        args.text = "Save Cropped";
        btn->Create(args);
        btn->onClick = MkFunc0<CropImageWindow>(OnSave, cw);
        cw->btnSave = btn;
    }

    CalcImageLayout(cw);
    LayoutControls(cw);
    UpdateSaveButtonText(cw);

    CenterDialog(hwnd, win->hwndFrame);
    HwndEnsureVisible(hwnd);
    if (UseDarkModeLib()) {
        DarkMode::setDarkWndSafe(hwnd);
        DarkMode::setWindowEraseBgSubclass(hwnd);
    }
    ShowWindow(hwnd, SW_SHOW);
}
