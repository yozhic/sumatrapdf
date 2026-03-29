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
#include "ResizeImage.h"

#include "utils/Log.h"

using Gdiplus::Bitmap;
using Gdiplus::Color;
using Gdiplus::Graphics;
using Gdiplus::Ok;
using Gdiplus::Status;

constexpr const WCHAR* kResizeImageWinClassName = L"SUMATRA_PDF_RESIZE_IMAGE";

constexpr int kMinWindowWidth = 640;
constexpr int kImagePadding = 8;
constexpr int kResizeEdgeThreshold = 2;
constexpr int kDragHandleSize = 6;
constexpr int kControlAreaDy = 100;
constexpr int kRowPadding = 6;
constexpr int kButtonPadding = 8;

struct ResizeImageWindow {
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
    int imgAreaH = 0;

    // new size in image coordinates
    int newW = 0;
    int newH = 0;

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
        BottomRight
    };
    DragEdge dragEdge = DragEdge::None;
    POINT dragStart{};
    int dragNewW = 0;
    int dragNewH = 0;

    HFONT hFont = nullptr;

    ResizeImageWindow() = default;
    ~ResizeImageWindow() {
        delete srcBitmap;
        free(filePath);
        delete btnCancel;
        delete btnSave;
    }
};

static Vec<ResizeImageWindow*> gResizeWindows;

static ResizeImageWindow* FindResizeWindowByHwnd(HWND hwnd) {
    for (auto* rw : gResizeWindows) {
        if (rw->hwnd == hwnd) {
            return rw;
        }
    }
    return nullptr;
}

// Convert display coordinates to image-scale values
static int DisplayToImageW(ResizeImageWindow* rw, int dispW) {
    if (rw->imgDisplayW <= 0) {
        return 0;
    }
    return (int)((float)dispW * rw->imgW / rw->imgDisplayW);
}

static int DisplayToImageH(ResizeImageWindow* rw, int dispH) {
    if (rw->imgDisplayH <= 0) {
        return 0;
    }
    return (int)((float)dispH * rw->imgH / rw->imgDisplayH);
}

// Convert new image size to display size
static int ImageToDisplayW(ResizeImageWindow* rw, int iw) {
    if (rw->imgW <= 0) {
        return 0;
    }
    return (int)((float)iw * rw->imgDisplayW / rw->imgW);
}

static int ImageToDisplayH(ResizeImageWindow* rw, int ih) {
    if (rw->imgH <= 0) {
        return 0;
    }
    return (int)((float)ih * rw->imgDisplayH / rw->imgH);
}

static void LayoutControls(ResizeImageWindow* rw);

static void UpdateSaveButtonText(ResizeImageWindow* rw) {
    WCHAR destW[MAX_PATH + 1]{};
    GetWindowTextW(rw->hwndDestEdit, destW, MAX_PATH);
    TempStr dest = ToUtf8Temp(destW);
    const char* text = file::Exists(dest) ? "Overwrite With Resized" : "Save Resized";
    rw->btnSave->SetText(text);
    LayoutControls(rw);
}

static TempStr FormatResizeInfoTemp(int srcW, int srcH, int newW, int newH) {
    float pctW = (srcW > 0) ? (float)newW * 100.0f / srcW : 0.0f;
    float pctH = (srcH > 0) ? (float)newH * 100.0f / srcH : 0.0f;
    return str::FormatTemp("%dx%d => %dx%d (%.2f%%,%.2f%%)", srcW, srcH, newW, newH, pctW, pctH);
}

static void UpdateInfoLabel(ResizeImageWindow* rw) {
    TempStr s = FormatResizeInfoTemp(rw->imgW, rw->imgH, rw->newW, rw->newH);
    SetWindowTextA(rw->hwndInfoLabel, s);
}

static void CalcImageLayout(ResizeImageWindow* rw) {
    Rect cRc = ClientRect(rw->hwnd);
    rw->imgAreaH = cRc.dy - kControlAreaDy;
    if (rw->imgAreaH < 10) {
        rw->imgAreaH = 10;
    }

    int availW = cRc.dx - 2 * kImagePadding;
    int availH = rw->imgAreaH - 2 * kImagePadding;
    if (availW <= 0 || availH <= 0 || rw->imgW <= 0 || rw->imgH <= 0) {
        rw->imgDisplayX = kImagePadding;
        rw->imgDisplayY = kImagePadding;
        rw->imgDisplayW = 0;
        rw->imgDisplayH = 0;
        return;
    }

    float scaleX = (float)availW / rw->imgW;
    float scaleY = (float)availH / rw->imgH;
    float scale = std::min(scaleX, scaleY);
    if (scale > 1.0f) {
        scale = 1.0f;
    }

    rw->imgDisplayW = (int)(rw->imgW * scale);
    rw->imgDisplayH = (int)(rw->imgH * scale);
    rw->imgDisplayX = kImagePadding + (availW - rw->imgDisplayW) / 2;
    rw->imgDisplayY = kImagePadding + (availH - rw->imgDisplayH) / 2;
}

static ResizeImageWindow::DragEdge HitTestResizeEdge(ResizeImageWindow* rw, int mx, int my) {
    // the "new size" rectangle, centered in the display area
    int dispNewW = ImageToDisplayW(rw, rw->newW);
    int dispNewH = ImageToDisplayH(rw, rw->newH);
    int cx = rw->imgDisplayX + rw->imgDisplayW / 2;
    int cy = rw->imgDisplayY + rw->imgDisplayH / 2;
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
        return ResizeImageWindow::DragEdge::TopLeft;
    }
    if (onRight && onTop) {
        return ResizeImageWindow::DragEdge::TopRight;
    }
    if (onLeft && onBottom) {
        return ResizeImageWindow::DragEdge::BottomLeft;
    }
    if (onRight && onBottom) {
        return ResizeImageWindow::DragEdge::BottomRight;
    }
    if (onLeft && inVertRange) {
        return ResizeImageWindow::DragEdge::Left;
    }
    if (onRight && inVertRange) {
        return ResizeImageWindow::DragEdge::Right;
    }
    if (onTop && inHorzRange) {
        return ResizeImageWindow::DragEdge::Top;
    }
    if (onBottom && inHorzRange) {
        return ResizeImageWindow::DragEdge::Bottom;
    }
    return ResizeImageWindow::DragEdge::None;
}

static HCURSOR GetCursorForEdge(ResizeImageWindow::DragEdge edge) {
    switch (edge) {
        case ResizeImageWindow::DragEdge::Left:
        case ResizeImageWindow::DragEdge::Right:
            return LoadCursor(nullptr, IDC_SIZEWE);
        case ResizeImageWindow::DragEdge::Top:
        case ResizeImageWindow::DragEdge::Bottom:
            return LoadCursor(nullptr, IDC_SIZENS);
        case ResizeImageWindow::DragEdge::TopLeft:
        case ResizeImageWindow::DragEdge::BottomRight:
            return LoadCursor(nullptr, IDC_SIZENWSE);
        case ResizeImageWindow::DragEdge::TopRight:
        case ResizeImageWindow::DragEdge::BottomLeft:
            return LoadCursor(nullptr, IDC_SIZENESW);
        default:
            return LoadCursor(nullptr, IDC_ARROW);
    }
}

static void InvalidateImageArea(ResizeImageWindow* rw) {
    RECT rc = {0, 0, 0, 0};
    GetClientRect(rw->hwnd, &rc);
    rc.bottom = rw->imgAreaH;
    InvalidateRect(rw->hwnd, &rc, FALSE);
}

static void PaintResizeImage(ResizeImageWindow* rw, HDC hdc) {
    Rect cRc = ClientRect(rw->hwnd);

    // fill image area background
    HBRUSH bgBrush = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
    RECT rcImg = {0, 0, cRc.dx, rw->imgAreaH};
    FillRect(hdc, &rcImg, bgBrush);

    if (!rw->srcBitmap || rw->imgDisplayW <= 0 || rw->imgDisplayH <= 0) {
        return;
    }

    Graphics g(hdc);

    // draw the full image
    g.DrawImage(rw->srcBitmap, rw->imgDisplayX, rw->imgDisplayY, rw->imgDisplayW, rw->imgDisplayH);

    // draw semi-transparent overlay over the entire image
    Gdiplus::SolidBrush overlayBrush(Gdiplus::Color(128, 0, 0, 0));
    Gdiplus::Brush* pBrush = &overlayBrush;
    g.FillRectangle(pBrush, rw->imgDisplayX, rw->imgDisplayY, rw->imgDisplayW, rw->imgDisplayH);

    // draw the "new size" rectangle showing the resized portion, centered
    int dispNewW = ImageToDisplayW(rw, rw->newW);
    int dispNewH = ImageToDisplayH(rw, rw->newH);
    int cx = rw->imgDisplayX + rw->imgDisplayW / 2;
    int cy = rw->imgDisplayY + rw->imgDisplayH / 2;
    int newLeft = cx - dispNewW / 2;
    int newTop = cy - dispNewH / 2;

    // redraw the image portion at the new size area (clear overlay there)
    // clip source to full image, draw scaled into the new rect
    g.DrawImage(rw->srcBitmap, newLeft, newTop, dispNewW, dispNewH);

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

static void LayoutControls(ResizeImageWindow* rw) {
    Rect cRc = ClientRect(rw->hwnd);
    int y = rw->imgAreaH + kRowPadding;
    int x = kButtonPadding;
    int w = cRc.dx - 2 * kButtonPadding;

    // row 1: file path label
    int editBorder = GetSystemMetrics(SM_CXEDGE);
    LRESULT margins = SendMessageW(rw->hwndDestEdit, EM_GETMARGINS, 0, 0);
    int editLeftMargin = LOWORD(margins);
    int labelShift = editBorder + editLeftMargin;
    MoveWindow(rw->hwndPathLabel, x + labelShift, y, w - labelShift, 16, TRUE);
    y += 16 + kRowPadding;

    // row 2: dest edit + browse button
    int browseW = 30;
    MoveWindow(rw->hwndDestEdit, x, y, w - browseW - 4, 22, TRUE);
    MoveWindow(rw->hwndBrowseBtn, x + w - browseW, y, browseW, 22, TRUE);
    y += 22 + kRowPadding;

    // row 3: info label, cancel, save
    MoveWindow(rw->hwndInfoLabel, x, y + 4, 350, 16, TRUE);

    if (rw->btnSave && rw->btnCancel) {
        Size szSave = rw->btnSave->GetIdealSize();
        Size szCancel = rw->btnCancel->GetIdealSize();
        int bx = cRc.dx - kButtonPadding - szCancel.dx;
        rw->btnCancel->SetBounds({bx, y, szCancel.dx, szCancel.dy});
        bx -= szSave.dx + 4;
        rw->btnSave->SetBounds({bx, y, szSave.dx, szSave.dy});
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
    return L"image/png";
}

static const char* GetMatchingExt(const char* srcExt) {
    if (IsKnownImageExt(srcExt)) {
        return srcExt;
    }
    return ".png";
}

static TempStr EnsureImageExtTemp(const char* dest, const char* srcFilePath) {
    TempStr destExt = path::GetExtTemp(dest);
    if (IsKnownImageExt(destExt)) {
        return str::DupTemp(dest);
    }
    TempStr srcExt = path::GetExtTemp(srcFilePath);
    const char* ext = GetMatchingExt(srcExt);
    if (str::IsEmpty(destExt)) {
        return str::FormatTemp("%s%s", dest, ext);
    }
    int baseLen = str::Leni(dest) - str::Leni(destExt);
    TempStr base = str::DupTemp(dest, baseLen);
    return str::FormatTemp("%s%s", base, ext);
}

static void OnBrowse(ResizeImageWindow* rw) {
    WCHAR dstFileName[MAX_PATH + 1]{};
    GetWindowTextW(rw->hwndDestEdit, dstFileName, MAX_PATH);

    OPENFILENAME ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = rw->hwnd;
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
        SetWindowTextW(rw->hwndDestEdit, dstFileName);
    }
}

static void OnSave(ResizeImageWindow* rw) {
    if (!rw->srcBitmap || rw->newW <= 0 || rw->newH <= 0) {
        return;
    }

    WCHAR rawDestW[MAX_PATH + 1]{};
    GetWindowTextW(rw->hwndDestEdit, rawDestW, MAX_PATH);
    TempStr rawDest = ToUtf8Temp(rawDestW);
    if (str::IsEmpty(rawDest)) {
        return;
    }

    TempStr dest = EnsureImageExtTemp(rawDest, rw->filePath);

    // create resized bitmap
    Bitmap* resized = new Bitmap(rw->newW, rw->newH, rw->srcBitmap->GetPixelFormat());
    if (!resized) {
        MessageBoxWarning(rw->hwnd, "Failed to create resized image", "Resize Image");
        return;
    }
    Graphics g(resized);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(rw->srcBitmap, 0, 0, rw->newW, rw->newH);

    TempStr ext = path::GetExtTemp(dest);
    const WCHAR* mime = GetEncoderMimeForExt(ext);
    CLSID encoderClsid = GetEncoderClsid(mime);
    TempWStr destW = ToWStrTemp(dest);
    Status status = resized->Save(destW, &encoderClsid, nullptr);
    delete resized;

    if (status != Ok) {
        MessageBoxWarning(rw->hwnd, "Failed to save resized image", "Resize Image");
        return;
    }

    // load the saved image
    HWND hwndParent = rw->hwndParent;
    char* savedPath = str::Dup(dest);
    DestroyWindow(rw->hwnd);

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

static void OnCancel(ResizeImageWindow* rw) {
    DestroyWindow(rw->hwnd);
}

LRESULT CALLBACK WndProcResizeImage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

LRESULT CALLBACK WndProcResizeImage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ResizeImageWindow* rw;

    LRESULT res = TryReflectMessages(hwnd, msg, wp, lp);
    if (res != 0) {
        return res;
    }

    switch (msg) {
        case WM_CREATE:
            break;

        case WM_SIZE: {
            rw = FindResizeWindowByHwnd(hwnd);
            if (rw) {
                CalcImageLayout(rw);
                LayoutControls(rw);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }

        case WM_PAINT: {
            rw = FindResizeWindowByHwnd(hwnd);
            if (rw) {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                Rect cRc = ClientRect(hwnd);
                int paintH = rw->imgAreaH;
                if (paintH > cRc.dy) {
                    paintH = cRc.dy;
                }
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBmp = CreateCompatibleBitmap(hdc, cRc.dx, paintH);
                HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
                PaintResizeImage(rw, memDC);
                BitBlt(hdc, 0, 0, cRc.dx, paintH, memDC, 0, 0, SRCCOPY);
                SelectObject(memDC, oldBmp);
                DeleteObject(memBmp);
                DeleteDC(memDC);
                EndPaint(hwnd, &ps);
            }
            return 0;
        }

        case WM_ERASEBKGND: {
            rw = FindResizeWindowByHwnd(hwnd);
            if (rw) {
                HDC hdc = (HDC)wp;
                RECT crc;
                GetClientRect(hwnd, &crc);
                RECT ctrlRc = {0, rw->imgAreaH, crc.right, crc.bottom};
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
            rw = FindResizeWindowByHwnd(hwnd);
            if (!rw) {
                break;
            }
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);
            if (rw->isDragging) {
                int dx = mx - rw->dragStart.x;
                int dy = my - rw->dragStart.y;
                // convert pixel deltas to image-space deltas
                int imgDx = DisplayToImageW(rw, dx);
                int imgDy = DisplayToImageH(rw, dy);

                auto edge = rw->dragEdge;
                int nw = rw->dragNewW;
                int nh = rw->dragNewH;

                // left/right edges change width
                if (edge == ResizeImageWindow::DragEdge::Left || edge == ResizeImageWindow::DragEdge::TopLeft ||
                    edge == ResizeImageWindow::DragEdge::BottomLeft) {
                    nw = rw->dragNewW - imgDx * 2; // symmetric resize
                }
                if (edge == ResizeImageWindow::DragEdge::Right || edge == ResizeImageWindow::DragEdge::TopRight ||
                    edge == ResizeImageWindow::DragEdge::BottomRight) {
                    nw = rw->dragNewW + imgDx * 2;
                }
                // top/bottom edges change height
                if (edge == ResizeImageWindow::DragEdge::Top || edge == ResizeImageWindow::DragEdge::TopLeft ||
                    edge == ResizeImageWindow::DragEdge::TopRight) {
                    nh = rw->dragNewH - imgDy * 2;
                }
                if (edge == ResizeImageWindow::DragEdge::Bottom || edge == ResizeImageWindow::DragEdge::BottomLeft ||
                    edge == ResizeImageWindow::DragEdge::BottomRight) {
                    nh = rw->dragNewH + imgDy * 2;
                }

                if (nw < 1) {
                    nw = 1;
                }
                if (nh < 1) {
                    nh = 1;
                }

                rw->newW = nw;
                rw->newH = nh;
                UpdateInfoLabel(rw);
                InvalidateImageArea(rw);
            } else {
                auto edge = HitTestResizeEdge(rw, mx, my);
                SetCursor(GetCursorForEdge(edge));
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            rw = FindResizeWindowByHwnd(hwnd);
            if (!rw) {
                break;
            }
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);
            auto edge = HitTestResizeEdge(rw, mx, my);
            if (edge != ResizeImageWindow::DragEdge::None) {
                rw->isDragging = true;
                rw->dragEdge = edge;
                rw->dragStart = {mx, my};
                rw->dragNewW = rw->newW;
                rw->dragNewH = rw->newH;
                SetCapture(hwnd);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            rw = FindResizeWindowByHwnd(hwnd);
            if (rw && rw->isDragging) {
                rw->isDragging = false;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_SETCURSOR: {
            rw = FindResizeWindowByHwnd(hwnd);
            if (rw && LOWORD(lp) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                auto edge = HitTestResizeEdge(rw, pt.x, pt.y);
                if (edge != ResizeImageWindow::DragEdge::None) {
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
            rw = FindResizeWindowByHwnd(hwnd);
            if (!rw) {
                break;
            }
            int code = HIWORD(wp);
            if ((HWND)lp == rw->hwndBrowseBtn && code == BN_CLICKED) {
                OnBrowse(rw);
                UpdateSaveButtonText(rw);
                return 0;
            }
            if ((HWND)lp == rw->hwndDestEdit && code == EN_CHANGE) {
                UpdateSaveButtonText(rw);
                return 0;
            }
            break;
        }

        case WM_DESTROY:
            rw = FindResizeWindowByHwnd(hwnd);
            if (rw) {
                gResizeWindows.Remove(rw);
                delete rw;
            }
            break;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

void ShowResizeImageWindow(MainWindow* win) {
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

    auto* rw = new ResizeImageWindow();
    rw->filePath = str::Dup(filePath);
    rw->srcBitmap = bmp;
    rw->imgW = imgW;
    rw->imgH = imgH;
    rw->newW = imgW;
    rw->newH = imgH;

    gResizeWindows.Append(rw);

    HMODULE h = GetModuleHandleW(nullptr);
    WNDCLASSEX wcex = {};
    FillWndClassEx(wcex, kResizeImageWinClassName, WndProcResizeImage);
    wcex.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    WCHAR* iconName = MAKEINTRESOURCEW(GetAppIconID());
    wcex.hIcon = LoadIconW(h, iconName);
    RegisterClassEx(&wcex);

    int wantW = imgW + 2 * kImagePadding;
    int wantH = imgH + 2 * kImagePadding + kControlAreaDy;
    RECT rc = {0, 0, wantW, wantH};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winW = rc.right - rc.left;
    if (winW < kMinWindowWidth) {
        winW = kMinWindowWidth;
    }
    int winH = rc.bottom - rc.top;
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

    HWND hwnd = CreateWindowExW(0, kResizeImageWinClassName, L"Resize Image", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, winW, winH, nullptr, nullptr, h, nullptr);
    if (!hwnd) {
        gResizeWindows.Remove(rw);
        delete rw;
        return;
    }

    rw->hwnd = hwnd;
    rw->hwndParent = win->hwndFrame;

    rw->hFont = GetDefaultGuiFont();

    rw->hwndPathLabel =
        CreateWindowExW(0, L"STATIC", ToWStrTemp(filePath), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS, 0, 0, 0,
                        0, hwnd, nullptr, h, nullptr);
    SendMessageW(rw->hwndPathLabel, WM_SETFONT, (WPARAM)rw->hFont, TRUE);

    TempStr destPath = MakeUniqueFilePathTemp(filePath);
    rw->hwndDestEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, ToWStrTemp(destPath),
                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, nullptr, h, nullptr);
    SendMessageW(rw->hwndDestEdit, WM_SETFONT, (WPARAM)rw->hFont, TRUE);

    rw->hwndBrowseBtn = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                        nullptr, h, nullptr);
    SendMessageW(rw->hwndBrowseBtn, WM_SETFONT, (WPARAM)rw->hFont, TRUE);

    TempStr infoStr = FormatResizeInfoTemp(imgW, imgH, imgW, imgH);
    rw->hwndInfoLabel = CreateWindowExW(0, L"STATIC", ToWStrTemp(infoStr), WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0,
                                        hwnd, nullptr, h, nullptr);
    SendMessageW(rw->hwndInfoLabel, WM_SETFONT, (WPARAM)rw->hFont, TRUE);

    {
        auto* btn = new Button();
        Button::CreateArgs args;
        args.parent = hwnd;
        args.text = "Cancel";
        btn->Create(args);
        btn->onClick = MkFunc0<ResizeImageWindow>(OnCancel, rw);
        rw->btnCancel = btn;
    }
    {
        auto* btn = new Button();
        Button::CreateArgs args;
        args.parent = hwnd;
        args.text = "Save Resized";
        btn->Create(args);
        btn->onClick = MkFunc0<ResizeImageWindow>(OnSave, rw);
        rw->btnSave = btn;
    }

    CalcImageLayout(rw);
    LayoutControls(rw);
    UpdateSaveButtonText(rw);

    CenterDialog(hwnd, win->hwndFrame);
    HwndEnsureVisible(hwnd);
    if (UseDarkModeLib()) {
        DarkMode::setDarkWndSafe(hwnd);
        DarkMode::setWindowEraseBgSubclass(hwnd);
    }
    ShowWindow(hwnd, SW_SHOW);
}
