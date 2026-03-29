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
#include "ImageSaveCropResize.h"

#include "utils/Log.h"

using Gdiplus::Bitmap;
using Gdiplus::Color;
using Gdiplus::Graphics;
using Gdiplus::Ok;
using Gdiplus::Status;

constexpr const WCHAR* kImageEditWinClassName = L"SUMATRA_PDF_IMAGE_EDIT";

constexpr int kMinWindowWidth = 640;
constexpr int kImagePadding = 8;
constexpr int kResizeEdgeThreshold = 2;
constexpr int kDragHandleSize = 6;
constexpr int kControlAreaDy = 100;
constexpr int kRowPadding = 6;
constexpr int kButtonPadding = 8;

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
    Move // only used in crop mode
};

struct ImageEditWindow {
    ImageEditMode mode = ImageEditMode::Crop;

    HWND hwnd = nullptr;
    HWND hwndParent = nullptr;

    // child controls
    HWND hwndPathLabel = nullptr;
    HWND hwndDestEdit = nullptr;
    HWND hwndBrowseBtn = nullptr;
    HWND hwndInfoLabel = nullptr;
    Button* btnCancel = nullptr;
    Button* btnSave = nullptr;
    Button* btnSwitchMode = nullptr;

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

    // crop rectangle in image coordinates (crop mode)
    int cropX = 0;
    int cropY = 0;
    int cropW = 0;
    int cropH = 0;

    // new size in image coordinates (resize mode)
    int newW = 0;
    int newH = 0;

    // drag state
    bool isDragging = false;
    DragEdge dragEdge = DragEdge::None;
    DragEdge hoverEdge = DragEdge::None; // edge under mouse, for arrow key nudging
    POINT dragStart{};
    // crop mode drag state
    int dragCropX = 0;
    int dragCropY = 0;
    int dragCropW = 0;
    int dragCropH = 0;
    // resize mode drag state
    int dragNewW = 0;
    int dragNewH = 0;

    HFONT hFont = nullptr;

    ImageEditWindow() = default;
    ~ImageEditWindow() {
        delete srcBitmap;
        free(filePath);
        delete btnCancel;
        delete btnSave;
        delete btnSwitchMode;
    }
};

static Vec<ImageEditWindow*> gImageEditWindows;

static ImageEditWindow* FindImageEditWindowByHwnd(HWND hwnd) {
    for (auto* ew : gImageEditWindows) {
        if (ew->hwnd == hwnd) {
            return ew;
        }
    }
    return nullptr;
}

// Convert display coordinates to image coordinates (crop mode)
static int DisplayToImageX(ImageEditWindow* ew, int dx) {
    if (ew->imgDisplayW <= 0) {
        return 0;
    }
    int v = (int)((float)(dx - ew->imgDisplayX) * ew->imgW / ew->imgDisplayW);
    return std::clamp(v, 0, ew->imgW);
}

static int DisplayToImageY(ImageEditWindow* ew, int dy) {
    if (ew->imgDisplayH <= 0) {
        return 0;
    }
    int v = (int)((float)(dy - ew->imgDisplayY) * ew->imgH / ew->imgDisplayH);
    return std::clamp(v, 0, ew->imgH);
}

// Convert image coordinates to display coordinates (crop mode)
static int ImageToDisplayX(ImageEditWindow* ew, int ix) {
    if (ew->imgW <= 0) {
        return ew->imgDisplayX;
    }
    return ew->imgDisplayX + (int)((float)ix * ew->imgDisplayW / ew->imgW);
}

static int ImageToDisplayY(ImageEditWindow* ew, int iy) {
    if (ew->imgH <= 0) {
        return ew->imgDisplayY;
    }
    return ew->imgDisplayY + (int)((float)iy * ew->imgDisplayH / ew->imgH);
}

// Convert display-scale sizes to image-scale sizes (resize mode)
static int DisplayToImageW(ImageEditWindow* ew, int dispW) {
    if (ew->imgDisplayW <= 0) {
        return 0;
    }
    return (int)((float)dispW * ew->imgW / ew->imgDisplayW);
}

static int DisplayToImageH(ImageEditWindow* ew, int dispH) {
    if (ew->imgDisplayH <= 0) {
        return 0;
    }
    return (int)((float)dispH * ew->imgH / ew->imgDisplayH);
}

// Convert image size to display size (resize mode)
static int ImageToDisplayW(ImageEditWindow* ew, int iw) {
    if (ew->imgW <= 0) {
        return 0;
    }
    return (int)((float)iw * ew->imgDisplayW / ew->imgW);
}

static int ImageToDisplayH(ImageEditWindow* ew, int ih) {
    if (ew->imgH <= 0) {
        return 0;
    }
    return (int)((float)ih * ew->imgDisplayH / ew->imgH);
}

static void LayoutControls(ImageEditWindow* ew);

static void UpdateSaveButtonText(ImageEditWindow* ew) {
    WCHAR destW[MAX_PATH + 1]{};
    GetWindowTextW(ew->hwndDestEdit, destW, MAX_PATH);
    TempStr dest = ToUtf8Temp(destW);
    const char* text;
    if (ew->mode == ImageEditMode::Crop) {
        text = file::Exists(dest) ? _TRA("Overwrite With Cropped") : _TRA("Save Cropped");
    } else {
        text = file::Exists(dest) ? "Overwrite With Resized" : "Save Resized";
    }
    ew->btnSave->SetText(text);
    // re-layout since button width may have changed
    LayoutControls(ew);
}

static TempStr FormatCropInfoTemp(int srcW, int srcH, int cropW, int cropH, int cropX, int cropY) {
    return str::FormatTemp("%d x %d => %d x %d @ %d , %d", srcW, srcH, cropW, cropH, cropX, cropY);
}

static TempStr FormatResizeInfoTemp(int srcW, int srcH, int newW, int newH) {
    float pctW = (srcW > 0) ? (float)newW * 100.0f / srcW : 0.0f;
    float pctH = (srcH > 0) ? (float)newH * 100.0f / srcH : 0.0f;
    return str::FormatTemp("%d x %d => %d x %d (%.2f%% x %.2f%%)", srcW, srcH, newW, newH, pctW, pctH);
}

static void UpdateInfoLabel(ImageEditWindow* ew) {
    TempStr s;
    if (ew->mode == ImageEditMode::Crop) {
        s = FormatCropInfoTemp(ew->imgW, ew->imgH, ew->cropW, ew->cropH, ew->cropX, ew->cropY);
    } else {
        s = FormatResizeInfoTemp(ew->imgW, ew->imgH, ew->newW, ew->newH);
    }
    SetWindowTextA(ew->hwndInfoLabel, s);
}

// invalidate only the image area, not the control area below
static void InvalidateImageArea(ImageEditWindow* ew) {
    RECT rc = {0, 0, 0, 0};
    GetClientRect(ew->hwnd, &rc);
    rc.bottom = ew->imgAreaH;
    InvalidateRect(ew->hwnd, &rc, FALSE);
}

static void CalcImageLayout(ImageEditWindow* ew) {
    Rect cRc = ClientRect(ew->hwnd);
    ew->imgAreaH = cRc.dy - kControlAreaDy;
    if (ew->imgAreaH < 10) {
        ew->imgAreaH = 10;
    }

    // fit image within image area with padding
    int availW = cRc.dx - 2 * kImagePadding;
    int availH = ew->imgAreaH - 2 * kImagePadding;
    if (availW <= 0 || availH <= 0 || ew->imgW <= 0 || ew->imgH <= 0) {
        ew->imgDisplayX = kImagePadding;
        ew->imgDisplayY = kImagePadding;
        ew->imgDisplayW = 0;
        ew->imgDisplayH = 0;
        return;
    }

    float scaleX = (float)availW / ew->imgW;
    float scaleY = (float)availH / ew->imgH;
    float scale = std::min(scaleX, scaleY);
    // don't upscale beyond 100%
    if (scale > 1.0f) {
        scale = 1.0f;
    }

    ew->imgDisplayW = (int)(ew->imgW * scale);
    ew->imgDisplayH = (int)(ew->imgH * scale);
    // center in available area
    ew->imgDisplayX = kImagePadding + (availW - ew->imgDisplayW) / 2;
    ew->imgDisplayY = kImagePadding + (availH - ew->imgDisplayH) / 2;
}

// Grow the window if the new-size rectangle exceeds the image display area (resize mode only).
// Tries to grow in the direction of the drag, moving the window if needed,
// but stops at screen edges.
static void GrowWindowIfNeeded(ImageEditWindow* ew, DragEdge edge) {
    // calculate how much display space the new size needs
    int neededDispW = ImageToDisplayW(ew, ew->newW) + 2 * kImagePadding;
    int neededDispH = ImageToDisplayH(ew, ew->newH) + 2 * kImagePadding;

    Rect cRc = ClientRect(ew->hwnd);
    int availW = cRc.dx;
    int availH = ew->imgAreaH;

    int extraW = neededDispW - availW;
    int extraH = neededDispH - availH;
    if (extraW <= 0 && extraH <= 0) {
        return;
    }
    if (extraW < 0) {
        extraW = 0;
    }
    if (extraH < 0) {
        extraH = 0;
    }

    // get screen work area
    HMONITOR hMon = MonitorFromWindow(ew->hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(hMon, &mi);
    int screenL = mi.rcWork.left;
    int screenT = mi.rcWork.top;
    int screenR = mi.rcWork.right;
    int screenB = mi.rcWork.bottom;

    RECT winRc;
    GetWindowRect(ew->hwnd, &winRc);
    int winX = winRc.left;
    int winY = winRc.top;
    int winW = winRc.right - winRc.left;
    int winH = winRc.bottom - winRc.top;

    int newWinW = winW + extraW;
    int newWinH = winH + extraH;

    // don't exceed screen size
    int maxW = screenR - screenL;
    int maxH = screenB - screenT;
    if (newWinW > maxW) {
        newWinW = maxW;
    }
    if (newWinH > maxH) {
        newWinH = maxH;
    }

    int deltaW = newWinW - winW;
    int deltaH = newWinH - winH;
    if (deltaW <= 0 && deltaH <= 0) {
        return;
    }

    int newX = winX;
    int newY = winY;

    // determine grow direction based on which edge is being dragged
    bool growLeft = (edge == DragEdge::Left || edge == DragEdge::TopLeft || edge == DragEdge::BottomLeft);
    bool growUp = (edge == DragEdge::Top || edge == DragEdge::TopLeft || edge == DragEdge::TopRight);

    if (growLeft && deltaW > 0) {
        newX = winX - deltaW;
        if (newX < screenL) {
            newX = screenL;
        }
    } else if (deltaW > 0) {
        // grow right - check we don't go past screen edge
        if (newX + newWinW > screenR) {
            newX = screenR - newWinW;
            if (newX < screenL) {
                newX = screenL;
            }
        }
    }

    if (growUp && deltaH > 0) {
        newY = winY - deltaH;
        if (newY < screenT) {
            newY = screenT;
        }
    } else if (deltaH > 0) {
        // grow down
        if (newY + newWinH > screenB) {
            newY = screenB - newWinH;
            if (newY < screenT) {
                newY = screenT;
            }
        }
    }

    SetWindowPos(ew->hwnd, nullptr, newX, newY, newWinW, newWinH, SWP_NOZORDER);
    // recalc layout after window resize
    CalcImageLayout(ew);
}

static DragEdge HitTestCropEdge(ImageEditWindow* ew, int mx, int my) {
    int left = ImageToDisplayX(ew, ew->cropX);
    int right = ImageToDisplayX(ew, ew->cropX + ew->cropW);
    int top = ImageToDisplayY(ew, ew->cropY);
    int bottom = ImageToDisplayY(ew, ew->cropY + ew->cropH);

    int t = kResizeEdgeThreshold;

    bool onLeft = (mx >= left - t && mx <= left + t);
    bool onRight = (mx >= right - t && mx <= right + t);
    bool onTop = (my >= top - t && my <= top + t);
    bool onBottom = (my >= bottom - t && my <= bottom + t);

    bool inVertRange = (my >= top - t && my <= bottom + t);
    bool inHorzRange = (mx >= left - t && mx <= right + t);

    if (onLeft && onTop) {
        return DragEdge::TopLeft;
    }
    if (onRight && onTop) {
        return DragEdge::TopRight;
    }
    if (onLeft && onBottom) {
        return DragEdge::BottomLeft;
    }
    if (onRight && onBottom) {
        return DragEdge::BottomRight;
    }
    if (onLeft && inVertRange) {
        return DragEdge::Left;
    }
    if (onRight && inVertRange) {
        return DragEdge::Right;
    }
    if (onTop && inHorzRange) {
        return DragEdge::Top;
    }
    if (onBottom && inHorzRange) {
        return DragEdge::Bottom;
    }
    // inside the crop rect = move
    if (mx > left + t && mx < right - t && my > top + t && my < bottom - t) {
        return DragEdge::Move;
    }
    return DragEdge::None;
}

static DragEdge HitTestResizeEdge(ImageEditWindow* ew, int mx, int my) {
    // the "new size" rectangle, centered in the display area
    int dispNewW = ImageToDisplayW(ew, ew->newW);
    int dispNewH = ImageToDisplayH(ew, ew->newH);
    int cx = ew->imgDisplayX + ew->imgDisplayW / 2;
    int cy = ew->imgDisplayY + ew->imgDisplayH / 2;
    int left = cx - dispNewW / 2;
    int right = left + dispNewW;
    int top = cy - dispNewH / 2;
    int bottom = top + dispNewH;

    int t = kResizeEdgeThreshold;

    bool onLeft = (mx >= left - t && mx <= left + t);
    bool onRight = (mx >= right - t && mx <= right + t);
    bool onTop = (my >= top - t && my <= top + t);
    bool onBottom = (my >= bottom - t && my <= bottom + t);

    bool inVertRange = (my >= top - t && my <= bottom + t);
    bool inHorzRange = (mx >= left - t && mx <= right + t);

    if (onLeft && onTop) {
        return DragEdge::TopLeft;
    }
    if (onRight && onTop) {
        return DragEdge::TopRight;
    }
    if (onLeft && onBottom) {
        return DragEdge::BottomLeft;
    }
    if (onRight && onBottom) {
        return DragEdge::BottomRight;
    }
    if (onLeft && inVertRange) {
        return DragEdge::Left;
    }
    if (onRight && inVertRange) {
        return DragEdge::Right;
    }
    if (onTop && inHorzRange) {
        return DragEdge::Top;
    }
    if (onBottom && inHorzRange) {
        return DragEdge::Bottom;
    }
    return DragEdge::None;
}

static HCURSOR GetCursorForEdge(DragEdge edge) {
    switch (edge) {
        case DragEdge::Left:
        case DragEdge::Right:
            return LoadCursor(nullptr, IDC_SIZEWE);
        case DragEdge::Top:
        case DragEdge::Bottom:
            return LoadCursor(nullptr, IDC_SIZENS);
        case DragEdge::TopLeft:
        case DragEdge::BottomRight:
            return LoadCursor(nullptr, IDC_SIZENWSE);
        case DragEdge::TopRight:
        case DragEdge::BottomLeft:
            return LoadCursor(nullptr, IDC_SIZENESW);
        case DragEdge::Move:
            return LoadCursor(nullptr, IDC_SIZEALL);
        default:
            return LoadCursor(nullptr, IDC_ARROW);
    }
}

static void PaintCropImage(ImageEditWindow* ew, HDC hdc) {
    Rect cRc = ClientRect(ew->hwnd);

    // fill image area background
    HBRUSH bgBrush = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
    RECT rcImg = {0, 0, cRc.dx, ew->imgAreaH};
    FillRect(hdc, &rcImg, bgBrush);

    if (!ew->srcBitmap || ew->imgDisplayW <= 0 || ew->imgDisplayH <= 0) {
        return;
    }

    Graphics g(hdc);

    // draw the image
    g.DrawImage(ew->srcBitmap, ew->imgDisplayX, ew->imgDisplayY, ew->imgDisplayW, ew->imgDisplayH);

    // draw semi-transparent overlay over non-cropped areas
    int cropDispX = ImageToDisplayX(ew, ew->cropX);
    int cropDispY = ImageToDisplayY(ew, ew->cropY);
    int cropDispR = ImageToDisplayX(ew, ew->cropX + ew->cropW);
    int cropDispB = ImageToDisplayY(ew, ew->cropY + ew->cropH);

    int ix = ew->imgDisplayX;
    int iy = ew->imgDisplayY;
    int iw = ew->imgDisplayW;
    int ih = ew->imgDisplayH;
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

static void PaintResizeImage(ImageEditWindow* ew, HDC hdc) {
    Rect cRc = ClientRect(ew->hwnd);

    // fill image area background
    HBRUSH bgBrush = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
    RECT rcImg = {0, 0, cRc.dx, ew->imgAreaH};
    FillRect(hdc, &rcImg, bgBrush);

    if (!ew->srcBitmap || ew->imgDisplayW <= 0 || ew->imgDisplayH <= 0) {
        return;
    }

    Graphics g(hdc);

    // draw the full image
    g.DrawImage(ew->srcBitmap, ew->imgDisplayX, ew->imgDisplayY, ew->imgDisplayW, ew->imgDisplayH);

    // draw semi-transparent overlay over the entire image
    Gdiplus::SolidBrush overlayBrush(Gdiplus::Color(128, 0, 0, 0));
    Gdiplus::Brush* pBrush = &overlayBrush;
    g.FillRectangle(pBrush, ew->imgDisplayX, ew->imgDisplayY, ew->imgDisplayW, ew->imgDisplayH);

    // draw the "new size" rectangle showing the resized portion, centered
    int dispNewW = ImageToDisplayW(ew, ew->newW);
    int dispNewH = ImageToDisplayH(ew, ew->newH);
    int cx = ew->imgDisplayX + ew->imgDisplayW / 2;
    int cy = ew->imgDisplayY + ew->imgDisplayH / 2;
    int newLeft = cx - dispNewW / 2;
    int newTop = cy - dispNewH / 2;

    // redraw the image portion at the new size area (clear overlay there)
    // clip source to full image, draw scaled into the new rect
    g.DrawImage(ew->srcBitmap, newLeft, newTop, dispNewW, dispNewH);

    // draw border around the new size rectangle
    Gdiplus::Pen pen(Color(255, 255, 255), 1.0f);
    pen.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawRectangle(&pen, newLeft, newTop, dispNewW, dispNewH);

    // draw drag handles
    int hs = kDragHandleSize;
    int hh = hs / 2;
    int midX = newLeft + dispNewW / 2;
    int midY = newTop + dispNewH / 2;
    int right = newLeft + dispNewW;
    int bottom = newTop + dispNewH;

    Gdiplus::SolidBrush handleBrush(Color(255, 255, 255, 255));
    Gdiplus::Pen handlePen(Color(255, 0, 0, 0), 1);

    auto drawHandle = [&](int hx, int hy) {
        g.FillRectangle(&handleBrush, hx - hh, hy - hh, hs, hs);
        g.DrawRectangle(&handlePen, hx - hh, hy - hh, hs, hs);
    };

    drawHandle(newLeft, newTop);
    drawHandle(right, newTop);
    drawHandle(newLeft, bottom);
    drawHandle(right, bottom);
    drawHandle(midX, newTop);
    drawHandle(midX, bottom);
    drawHandle(newLeft, midY);
    drawHandle(right, midY);
}

static void LayoutControls(ImageEditWindow* ew) {
    Rect cRc = ClientRect(ew->hwnd);
    int y = ew->imgAreaH + kRowPadding;
    int x = kButtonPadding;
    int w = cRc.dx - 2 * kButtonPadding;

    // row 1: file path label, shifted right to align with edit text
    int editBorder = GetSystemMetrics(SM_CXEDGE);
    LRESULT margins = SendMessageW(ew->hwndDestEdit, EM_GETMARGINS, 0, 0);
    int editLeftMargin = LOWORD(margins);
    int labelShift = editBorder + editLeftMargin;
    MoveWindow(ew->hwndPathLabel, x + labelShift, y, w - labelShift, 16, TRUE);
    y += 16 + kRowPadding;

    // row 2: dest edit + browse button
    int browseW = 30;
    MoveWindow(ew->hwndDestEdit, x, y, w - browseW - 4, 22, TRUE);
    MoveWindow(ew->hwndBrowseBtn, x + w - browseW, y, browseW, 22, TRUE);
    y += 22 + kRowPadding;

    // row 3: info label, cancel, save
    MoveWindow(ew->hwndInfoLabel, x, y + 4, 350, 16, TRUE);

    if (ew->btnSave && ew->btnCancel) {
        Size szSave = ew->btnSave->GetIdealSize();
        Size szCancel = ew->btnCancel->GetIdealSize();
        int bx = cRc.dx - kButtonPadding - szCancel.dx;
        ew->btnCancel->SetBounds({bx, y, szCancel.dx, szCancel.dy});
        bx -= szSave.dx + 4;
        ew->btnSave->SetBounds({bx, y, szSave.dx, szSave.dy});
        if (ew->btnSwitchMode) {
            Size szSwitch = ew->btnSwitchMode->GetIdealSize();
            bx -= szSwitch.dx + 4;
            ew->btnSwitchMode->SetBounds({bx, y, szSwitch.dx, szSwitch.dy});
        }
    }
}

static void OnBrowse(ImageEditWindow* ew) {
    WCHAR dstFileName[MAX_PATH + 1]{};
    // pre-populate with current dest path
    int len = GetWindowTextW(ew->hwndDestEdit, dstFileName, MAX_PATH);
    (void)len;

    OPENFILENAME ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ew->hwnd;
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
        SetWindowTextW(ew->hwndDestEdit, dstFileName);
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

static void OnSave(ImageEditWindow* ew) {
    if (ew->mode == ImageEditMode::Crop) {
        if (!ew->srcBitmap || ew->cropW <= 0 || ew->cropH <= 0) {
            return;
        }
    } else {
        if (!ew->srcBitmap || ew->newW <= 0 || ew->newH <= 0) {
            return;
        }
    }

    WCHAR rawDestW[MAX_PATH + 1]{};
    GetWindowTextW(ew->hwndDestEdit, rawDestW, MAX_PATH);
    TempStr rawDest = ToUtf8Temp(rawDestW);
    if (str::IsEmpty(rawDest)) {
        return;
    }

    // ensure the destination has a valid image extension
    TempStr dest = EnsureImageExtTemp(rawDest, ew->filePath);

    Bitmap* result = nullptr;
    if (ew->mode == ImageEditMode::Crop) {
        // create cropped bitmap
        Gdiplus::Rect srcRect(ew->cropX, ew->cropY, ew->cropW, ew->cropH);
        result = ew->srcBitmap->Clone(srcRect, ew->srcBitmap->GetPixelFormat());
        if (!result) {
            MessageBoxWarning(ew->hwnd, "Failed to create cropped image", "Crop Image");
            return;
        }
    } else {
        // create resized bitmap
        result = new Bitmap(ew->newW, ew->newH, ew->srcBitmap->GetPixelFormat());
        if (!result) {
            MessageBoxWarning(ew->hwnd, "Failed to create resized image", "Resize Image");
            return;
        }
        Graphics g(result);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(ew->srcBitmap, 0, 0, ew->newW, ew->newH);
    }

    TempStr ext = path::GetExtTemp(dest);
    const WCHAR* mime = GetEncoderMimeForExt(ext);
    CLSID encoderClsid = GetEncoderClsid(mime);
    TempWStr destW = ToWStrTemp(dest);
    Status status = result->Save(destW, &encoderClsid, nullptr);
    delete result;

    if (status != Ok) {
        const char* errMsg =
            (ew->mode == ImageEditMode::Crop) ? "Failed to save cropped image" : "Failed to save resized image";
        const char* title = (ew->mode == ImageEditMode::Crop) ? _TRA("Crop Image") : "Resize Image";
        MessageBoxWarning(ew->hwnd, errMsg, title);
        return;
    }

    // load the saved image
    HWND hwndParent = ew->hwndParent;
    char* savedPath = str::Dup(dest);
    DestroyWindow(ew->hwnd);

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

static void OnCancel(ImageEditWindow* ew) {
    DestroyWindow(ew->hwnd);
}

static void UpdateInfoLabel(ImageEditWindow* ew);

static void ReplaceSrcBitmap(ImageEditWindow* ew, Bitmap* newBmp) {
    delete ew->srcBitmap;
    ew->srcBitmap = newBmp;
    ew->imgW = (int)newBmp->GetWidth();
    ew->imgH = (int)newBmp->GetHeight();
    CalcImageLayout(ew);
}

static void OnSwitchMode(ImageEditWindow* ew) {
    if (ew->mode == ImageEditMode::Crop) {
        // create new bitmap from the cropped region
        if (ew->cropW > 0 && ew->cropH > 0 &&
            (ew->cropX != 0 || ew->cropY != 0 || ew->cropW != ew->imgW || ew->cropH != ew->imgH)) {
            Gdiplus::Rect srcRect(ew->cropX, ew->cropY, ew->cropW, ew->cropH);
            Bitmap* cropped = ew->srcBitmap->Clone(srcRect, ew->srcBitmap->GetPixelFormat());
            if (cropped) {
                ReplaceSrcBitmap(ew, cropped);
            }
        }
        // switch to resize mode with new size = current image size
        ew->mode = ImageEditMode::Resize;
        ew->newW = ew->imgW;
        ew->newH = ew->imgH;
        SetWindowTextW(ew->hwnd, L"Resize Image");
        ew->btnSwitchMode->SetText("Crop Resized");
    } else {
        // create new bitmap at the resized dimensions
        if (ew->newW > 0 && ew->newH > 0 && (ew->newW != ew->imgW || ew->newH != ew->imgH)) {
            Bitmap* resized = new Bitmap(ew->newW, ew->newH, ew->srcBitmap->GetPixelFormat());
            if (resized) {
                Graphics g(resized);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.DrawImage(ew->srcBitmap, 0, 0, ew->newW, ew->newH);
                ReplaceSrcBitmap(ew, resized);
            }
        }
        // switch to crop mode with crop = full image
        ew->mode = ImageEditMode::Crop;
        ew->cropX = 0;
        ew->cropY = 0;
        ew->cropW = ew->imgW;
        ew->cropH = ew->imgH;
        SetWindowTextW(ew->hwnd, L"Crop Image");
        ew->btnSwitchMode->SetText("Resize Cropped");
    }
    UpdateSaveButtonText(ew);
    UpdateInfoLabel(ew);
    InvalidateImageArea(ew);
}

LRESULT CALLBACK WndProcImageEdit(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

LRESULT CALLBACK WndProcImageEdit(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ImageEditWindow* ew;

    LRESULT res = TryReflectMessages(hwnd, msg, wp, lp);
    if (res != 0) {
        return res;
    }

    switch (msg) {
        case WM_CREATE:
            break;

        case WM_SIZE: {
            ew = FindImageEditWindowByHwnd(hwnd);
            if (ew) {
                CalcImageLayout(ew);
                LayoutControls(ew);
                if (ew->mode == ImageEditMode::Crop) {
                    InvalidateImageArea(ew);
                } else {
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            return 0;
        }

        case WM_PAINT: {
            ew = FindImageEditWindowByHwnd(hwnd);
            if (ew) {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                // double-buffer only the image area to avoid flicker
                Rect cRc = ClientRect(hwnd);
                int paintH = ew->imgAreaH;
                if (paintH > cRc.dy) {
                    paintH = cRc.dy;
                }
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBmp = CreateCompatibleBitmap(hdc, cRc.dx, paintH);
                HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
                if (ew->mode == ImageEditMode::Crop) {
                    PaintCropImage(ew, memDC);
                } else {
                    PaintResizeImage(ew, memDC);
                }
                BitBlt(hdc, 0, 0, cRc.dx, paintH, memDC, 0, 0, SRCCOPY);
                SelectObject(memDC, oldBmp);
                DeleteObject(memBmp);
                DeleteDC(memDC);
                EndPaint(hwnd, &ps);
            }
            return 0;
        }

        case WM_ERASEBKGND: {
            ew = FindImageEditWindowByHwnd(hwnd);
            if (ew) {
                // paint control area background, skip image area (double-buffered)
                HDC hdc = (HDC)wp;
                RECT crc;
                GetClientRect(hwnd, &crc);
                RECT ctrlRc = {0, ew->imgAreaH, crc.right, crc.bottom};
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
            ew = FindImageEditWindowByHwnd(hwnd);
            if (!ew) {
                break;
            }
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);
            if (ew->isDragging) {
                if (ew->mode == ImageEditMode::Crop) {
                    int imgDx = DisplayToImageX(ew, mx) - DisplayToImageX(ew, ew->dragStart.x);
                    int imgDy = DisplayToImageY(ew, my) - DisplayToImageY(ew, ew->dragStart.y);

                    auto edge = ew->dragEdge;
                    int nx = ew->dragCropX;
                    int ny = ew->dragCropY;
                    int nw = ew->dragCropW;
                    int nh = ew->dragCropH;

                    if (edge == DragEdge::Left || edge == DragEdge::TopLeft || edge == DragEdge::BottomLeft) {
                        nx = ew->dragCropX + imgDx;
                        nw = ew->dragCropW - imgDx;
                    }
                    if (edge == DragEdge::Right || edge == DragEdge::TopRight || edge == DragEdge::BottomRight) {
                        nw = ew->dragCropW + imgDx;
                    }
                    if (edge == DragEdge::Top || edge == DragEdge::TopLeft || edge == DragEdge::TopRight) {
                        ny = ew->dragCropY + imgDy;
                        nh = ew->dragCropH - imgDy;
                    }
                    if (edge == DragEdge::Bottom || edge == DragEdge::BottomLeft || edge == DragEdge::BottomRight) {
                        nh = ew->dragCropH + imgDy;
                    }
                    if (edge == DragEdge::Move) {
                        nx = ew->dragCropX + imgDx;
                        ny = ew->dragCropY + imgDy;
                        // clamp to image bounds
                        if (nx < 0) {
                            nx = 0;
                        }
                        if (ny < 0) {
                            ny = 0;
                        }
                        if (nx + nw > ew->imgW) {
                            nx = ew->imgW - nw;
                        }
                        if (ny + nh > ew->imgH) {
                            ny = ew->imgH - nh;
                        }
                    }

                    // enforce minimum size and bounds
                    if (nw < 1) {
                        nw = 1;
                        nx = ew->cropX;
                    }
                    if (nh < 1) {
                        nh = 1;
                        ny = ew->cropY;
                    }
                    if (nx < 0) {
                        nw += nx;
                        nx = 0;
                    }
                    if (ny < 0) {
                        nh += ny;
                        ny = 0;
                    }
                    if (nx + nw > ew->imgW) {
                        nw = ew->imgW - nx;
                    }
                    if (ny + nh > ew->imgH) {
                        nh = ew->imgH - ny;
                    }

                    ew->cropX = nx;
                    ew->cropY = ny;
                    ew->cropW = nw;
                    ew->cropH = nh;
                    UpdateInfoLabel(ew);
                    InvalidateImageArea(ew);
                } else {
                    // resize mode
                    int dx = mx - ew->dragStart.x;
                    int dy = my - ew->dragStart.y;
                    // convert pixel deltas to image-space deltas
                    int imgDx = DisplayToImageW(ew, dx);
                    int imgDy = DisplayToImageH(ew, dy);

                    auto edge = ew->dragEdge;
                    int nw = ew->dragNewW;
                    int nh = ew->dragNewH;

                    // left/right edges change width
                    if (edge == DragEdge::Left || edge == DragEdge::TopLeft || edge == DragEdge::BottomLeft) {
                        nw = ew->dragNewW - imgDx * 2; // symmetric resize
                    }
                    if (edge == DragEdge::Right || edge == DragEdge::TopRight || edge == DragEdge::BottomRight) {
                        nw = ew->dragNewW + imgDx * 2;
                    }
                    // top/bottom edges change height
                    if (edge == DragEdge::Top || edge == DragEdge::TopLeft || edge == DragEdge::TopRight) {
                        nh = ew->dragNewH - imgDy * 2;
                    }
                    if (edge == DragEdge::Bottom || edge == DragEdge::BottomLeft || edge == DragEdge::BottomRight) {
                        nh = ew->dragNewH + imgDy * 2;
                    }

                    if (nw < 1) {
                        nw = 1;
                    }
                    if (nh < 1) {
                        nh = 1;
                    }

                    ew->newW = nw;
                    ew->newH = nh;
                    GrowWindowIfNeeded(ew, edge);
                    UpdateInfoLabel(ew);
                    InvalidateImageArea(ew);
                }
            } else {
                DragEdge edge;
                if (ew->mode == ImageEditMode::Crop) {
                    edge = HitTestCropEdge(ew, mx, my);
                } else {
                    edge = HitTestResizeEdge(ew, mx, my);
                }
                ew->hoverEdge = edge;
                SetCursor(GetCursorForEdge(edge));
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            ew = FindImageEditWindowByHwnd(hwnd);
            if (!ew) {
                break;
            }
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);
            DragEdge edge;
            if (ew->mode == ImageEditMode::Crop) {
                edge = HitTestCropEdge(ew, mx, my);
            } else {
                edge = HitTestResizeEdge(ew, mx, my);
            }
            if (edge != DragEdge::None) {
                ew->isDragging = true;
                ew->dragEdge = edge;
                ew->dragStart = {mx, my};
                if (ew->mode == ImageEditMode::Crop) {
                    ew->dragCropX = ew->cropX;
                    ew->dragCropY = ew->cropY;
                    ew->dragCropW = ew->cropW;
                    ew->dragCropH = ew->cropH;
                } else {
                    ew->dragNewW = ew->newW;
                    ew->dragNewH = ew->newH;
                }
                SetCapture(hwnd);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            ew = FindImageEditWindowByHwnd(hwnd);
            if (ew && ew->isDragging) {
                ew->isDragging = false;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_SETCURSOR: {
            ew = FindImageEditWindowByHwnd(hwnd);
            if (ew && LOWORD(lp) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                DragEdge edge;
                if (ew->mode == ImageEditMode::Crop) {
                    edge = HitTestCropEdge(ew, pt.x, pt.y);
                } else {
                    edge = HitTestResizeEdge(ew, pt.x, pt.y);
                }
                if (edge != DragEdge::None) {
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

        case WM_KEYDOWN: {
            ew = FindImageEditWindowByHwnd(hwnd);
            if (!ew || ew->mode != ImageEditMode::Crop) {
                break;
            }
            // use the latched hover edge (set on mouse move, persists through arrow nudges)
            auto edge = ew->hoverEdge;
            if (edge == DragEdge::None) {
                break;
            }
            int dx = 0, dy = 0;
            if (wp == VK_LEFT) {
                dx = -1;
            } else if (wp == VK_RIGHT) {
                dx = 1;
            } else if (wp == VK_UP) {
                dy = -1;
            } else if (wp == VK_DOWN) {
                dy = 1;
            } else {
                break;
            }
            // apply nudge based on edge
            if (dx != 0 && (edge == DragEdge::Left || edge == DragEdge::TopLeft || edge == DragEdge::BottomLeft)) {
                ew->cropX += dx;
                ew->cropW -= dx;
            }
            if (dx != 0 && (edge == DragEdge::Right || edge == DragEdge::TopRight || edge == DragEdge::BottomRight)) {
                ew->cropW += dx;
            }
            if (dy != 0 && (edge == DragEdge::Top || edge == DragEdge::TopLeft || edge == DragEdge::TopRight)) {
                ew->cropY += dy;
                ew->cropH -= dy;
            }
            if (dy != 0 &&
                (edge == DragEdge::Bottom || edge == DragEdge::BottomLeft || edge == DragEdge::BottomRight)) {
                ew->cropH += dy;
            }
            // clamp
            if (ew->cropX < 0) {
                ew->cropW += ew->cropX;
                ew->cropX = 0;
            }
            if (ew->cropY < 0) {
                ew->cropH += ew->cropY;
                ew->cropY = 0;
            }
            if (ew->cropW < 1) {
                ew->cropW = 1;
            }
            if (ew->cropH < 1) {
                ew->cropH = 1;
            }
            if (ew->cropX + ew->cropW > ew->imgW) {
                ew->cropW = ew->imgW - ew->cropX;
            }
            if (ew->cropY + ew->cropH > ew->imgH) {
                ew->cropH = ew->imgH - ew->cropY;
            }
            UpdateInfoLabel(ew);
            InvalidateImageArea(ew);
            return 0;
        }

        case WM_COMMAND: {
            ew = FindImageEditWindowByHwnd(hwnd);
            if (!ew) {
                break;
            }
            int code = HIWORD(wp);
            // browse button
            if ((HWND)lp == ew->hwndBrowseBtn && code == BN_CLICKED) {
                OnBrowse(ew);
                UpdateSaveButtonText(ew);
                return 0;
            }
            // dest edit changed
            if ((HWND)lp == ew->hwndDestEdit && code == EN_CHANGE) {
                UpdateSaveButtonText(ew);
                return 0;
            }
            break;
        }

        case WM_DESTROY:
            ew = FindImageEditWindowByHwnd(hwnd);
            if (ew) {
                gImageEditWindows.Remove(ew);
                delete ew;
            }
            break;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

void ShowImageEditWindow(MainWindow* win, ImageEditMode mode) {
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

    auto* ew = new ImageEditWindow();
    ew->mode = mode;
    ew->filePath = str::Dup(filePath);
    ew->srcBitmap = bmp;
    ew->imgW = imgW;
    ew->imgH = imgH;

    if (mode == ImageEditMode::Crop) {
        // initial crop = full image
        ew->cropX = 0;
        ew->cropY = 0;
        ew->cropW = imgW;
        ew->cropH = imgH;
    } else {
        ew->newW = imgW;
        ew->newH = imgH;
    }

    gImageEditWindows.Append(ew);

    HMODULE h = GetModuleHandleW(nullptr);
    WNDCLASSEX wcex = {};
    FillWndClassEx(wcex, kImageEditWinClassName, WndProcImageEdit);
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

    const WCHAR* title = (mode == ImageEditMode::Crop) ? L"Crop Image" : L"Resize Image";
    HWND hwnd = CreateWindowExW(0, kImageEditWinClassName, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                CW_USEDEFAULT, winW, winH, nullptr, nullptr, h, nullptr);
    if (!hwnd) {
        gImageEditWindows.Remove(ew);
        delete ew;
        return;
    }

    ew->hwnd = hwnd;
    ew->hwndParent = win->hwndFrame;

    // create font
    ew->hFont = GetDefaultGuiFont();

    // create child controls
    // row 1: file path label (read-only)
    ew->hwndPathLabel =
        CreateWindowExW(0, L"STATIC", ToWStrTemp(filePath), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS, 0, 0, 0,
                        0, hwnd, nullptr, h, nullptr);
    SendMessageW(ew->hwndPathLabel, WM_SETFONT, (WPARAM)ew->hFont, TRUE);

    // row 2: dest edit + browse
    TempStr destPath = MakeUniqueFilePathTemp(filePath);
    ew->hwndDestEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, ToWStrTemp(destPath),
                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, nullptr, h, nullptr);
    SendMessageW(ew->hwndDestEdit, WM_SETFONT, (WPARAM)ew->hFont, TRUE);

    ew->hwndBrowseBtn = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                        nullptr, h, nullptr);
    SendMessageW(ew->hwndBrowseBtn, WM_SETFONT, (WPARAM)ew->hFont, TRUE);

    // row 3: info label
    TempStr infoStr;
    if (mode == ImageEditMode::Crop) {
        infoStr = FormatCropInfoTemp(imgW, imgH, imgW, imgH, 0, 0);
    } else {
        infoStr = FormatResizeInfoTemp(imgW, imgH, imgW, imgH);
    }
    ew->hwndInfoLabel = CreateWindowExW(0, L"STATIC", ToWStrTemp(infoStr), WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0,
                                        hwnd, nullptr, h, nullptr);
    SendMessageW(ew->hwndInfoLabel, WM_SETFONT, (WPARAM)ew->hFont, TRUE);

    // buttons
    {
        auto* btn = new Button();
        Button::CreateArgs args;
        args.parent = hwnd;
        args.text = "Cancel";
        btn->Create(args);
        btn->onClick = MkFunc0<ImageEditWindow>(OnCancel, ew);
        ew->btnCancel = btn;
    }
    {
        auto* btn = new Button();
        Button::CreateArgs args;
        args.parent = hwnd;
        args.text = (mode == ImageEditMode::Crop) ? "Save Cropped" : "Save Resized";
        btn->Create(args);
        btn->onClick = MkFunc0<ImageEditWindow>(OnSave, ew);
        ew->btnSave = btn;
    }
    {
        auto* btn = new Button();
        Button::CreateArgs args;
        args.parent = hwnd;
        args.text = (mode == ImageEditMode::Crop) ? "Resize Cropped" : "Crop Resized";
        btn->Create(args);
        btn->onClick = MkFunc0<ImageEditWindow>(OnSwitchMode, ew);
        ew->btnSwitchMode = btn;
    }

    CalcImageLayout(ew);
    LayoutControls(ew);
    UpdateSaveButtonText(ew);

    CenterDialog(hwnd, win->hwndFrame);
    HwndEnsureVisible(hwnd);
    if (UseDarkModeLib()) {
        DarkMode::setDarkWndSafe(hwnd);
        DarkMode::setWindowEraseBgSubclass(hwnd);
    }
    ShowWindow(hwnd, SW_SHOW);
}
