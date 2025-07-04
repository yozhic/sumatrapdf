/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/WinUtil.h"
#include "utils/ScopedWin.h"
#include "utils/Dpi.h"
#include "utils/UITask.h"

#include "wingui/UIModels.h"
#include "wingui/Layout.h"
#include "wingui/WinGui.h"

#include "Settings.h"
#include "AppSettings.h"
#include "Annotation.h"
#include "SumatraPdf.h"
#include "AppTools.h"

#include "ProgressUpdateUI.h"
#include "Notifications.h"
#include "Theme.h"

#include "utils/Log.h"

using Gdiplus::Graphics;
using Gdiplus::Pen;
using Gdiplus::SolidBrush;

Kind kNotifCursorPos = "cursorPosHelper";
Kind kNotifActionResponse = "responseToAction";
Kind kNotifPageInfo = "pageInfoHelper";
// can have multiple of those
Kind kNotifAdHoc = "notifAdHoc";

constexpr int kPadding = 6;
constexpr int kTopLeftMargin = 8;

constexpr UINT_PTR kNotifTimerTimeoutId = 1;

struct NotificationWnd : Wnd {
    NotificationWnd() = default;
    ~NotificationWnd() override;

    HWND Create(const NotificationCreateArgs&);

    void OnPaint(HDC hdc, PAINTSTRUCT* ps) override;
    void OnTimer(UINT_PTR event_id) override;
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) override;

    void UpdateMessage(const char* msg, int timeoutMs = 0, bool highlight = false);

    bool HasProgress() const {
        return progressPerc >= 0;
    }
    void Layout(const char* message);

    int timeoutMs = kNotifDefaultTimeOut; // 0 means no timeout

    bool highlight = false; // TODO: should really be a color

    NotificationWndRemoved wndRemovedCb;

    // there can only be a single notification of a given group
    Kind groupId = nullptr;

    // to reduce flicker, we might ask the window to shrink the size less often
    // (notifcation windows are only shrunken if by less than factor shrinkLimit)
    float shrinkLimit = 1.0f;

    int progressPerc = -1;

    Rect rTxt;
    Rect rClose;
    Rect rProgress;
};

Vec<NotificationWnd*> gNotifs;

static void GetForHwnd(HWND hwnd, Vec<NotificationWnd*>& v) {
    for (auto* wnd : gNotifs) {
        HWND parent = HwndGetParent(wnd->hwnd);
        if (parent == hwnd) {
            v.Append(wnd);
        }
    }
}

// notification can be removed due to a timeout or manual closing
bool IsNotificationValid(NotificationWnd* wnd) {
    bool exists = gNotifs.Contains(wnd);
    return exists;
}

static void GetForSameHwnd(NotificationWnd* wnd, Vec<NotificationWnd*>& v) {
    HWND parent = GetParent(wnd->hwnd);
    GetForHwnd(parent, v);
}

void RelayoutNotifications(HWND hwnd) {
    Vec<NotificationWnd*> wnds;
    HWND parent = HwndGetParent(hwnd);
    GetForHwnd(parent, wnds);
    if (wnds.IsEmpty()) {
        return;
    }

    auto* first = wnds[0];
    HWND hwndCanvas = GetParent(first->hwnd);
    Rect frame = ClientRect(hwndCanvas);
    int topLeftMargin = DpiScale(hwndCanvas, kTopLeftMargin);
    int dyPadding = DpiScale(hwndCanvas, kPadding);
    int y = topLeftMargin;
    for (NotificationWnd* wnd : wnds) {
        Rect rect = WindowRect(wnd->hwnd);
        rect = MapRectToWindow(rect, HWND_DESKTOP, hwndCanvas);
        if (IsUIRtl()) {
            int cxVScroll = GetSystemMetrics(SM_CXVSCROLL);
            rect.x = frame.dx - rect.dx - topLeftMargin - cxVScroll;
        } else {
            rect.x = topLeftMargin;
        }
        uint flags = SWP_NOSIZE | SWP_NOZORDER;
        SetWindowPos(wnd->hwnd, nullptr, rect.x, y, 0, 0, flags);
        y += rect.dy + dyPadding;
    }
}

static void NotifsRemoveNotification(NotificationWnd* wnd) {
    int pos = gNotifs.Remove(wnd);
    if (pos < 0) {
        return;
    }
    RelayoutNotifications(wnd->hwnd);
    delete wnd;
}

int GetWndX(NotificationWnd* wnd) {
    Rect rect = WindowRect(wnd->hwnd);
    rect = MapRectToWindow(rect, HWND_DESKTOP, GetParent(wnd->hwnd));
    return rect.x;
}

NotificationWnd::~NotificationWnd() {
}

HWND NotificationWnd::Create(const NotificationCreateArgs& args) {
    highlight = args.warning;
    shrinkLimit = args.shrinkLimit;
    if (shrinkLimit < 0.2f) {
        ReportIf(shrinkLimit < 0.2f);
        shrinkLimit = 1.f;
    }
    if (args.onRemoved.IsValid()) {
        wndRemovedCb = args.onRemoved;
    } else {
        wndRemovedCb = MkFunc1Void(NotifsRemoveNotification);
    }
    timeoutMs = args.timeoutMs;

    CreateCustomArgs cargs;
    cargs.parent = args.hwndParent;
    cargs.font = args.font;
    // TODO: was this important?
    // wcex.hCursor = LoadCursor(nullptr, IDC_APPSTARTING);
    cargs.exStyle = WS_EX_TOPMOST;
    cargs.style = WS_CHILD | SS_CENTER;
    cargs.title = args.msg;
    if (cargs.font == nullptr) {
        cargs.font = GetAppBiggerFont();
    }
    cargs.pos = Rect(0, 0, 0, 0);

    CreateCustom(cargs);

    HwndSetRtl(hwnd, IsUIRtl());
    Layout(args.msg);
    ShowWindow(hwnd, SW_SHOW);

    if (timeoutMs != 0) {
        SetTimer(hwnd, kNotifTimerTimeoutId, timeoutMs, nullptr);
    }
    return hwnd;
}

// returns 0% - 100%
int CalcPerc(int current, int total) {
    ReportIf(total <= 0 || current < 0);
    ReportIf(total < current);
    if (total <= 0) {
        total = 1;
    }
    int perc = limitValue(100 * current / total, 0, 100);
    return perc;
}

constexpr int kCloseLeftMargin = 16;
constexpr int kProgressDy = 5;

void NotificationWnd::Layout(const char* message) {
    Size szText;
    {
        HDC hdc = GetDC(hwnd);
        uint fmt = DT_SINGLELINE | DT_NOPREFIX;
        szText = HdcMeasureText(hdc, message, fmt, font);
        ReleaseDC(hwnd, hdc);
    }

    int padX = DpiScale(hwnd, 12);
    int padY = DpiScale(hwnd, 8);
    int dx = padX + szText.dx + padX;
    int dy = padY + szText.dy + padY;
    rTxt = {padX, padY, szText.dx, szText.dy};
    int closeDx = DpiScale(hwnd, 16);
    int leftMargin = DpiScale(hwnd, kCloseLeftMargin - padX);
    rClose = {dx + leftMargin, padY, closeDx, closeDx + 2};

    // close button
    dx += leftMargin + closeDx + padX;
    int progressDy = DpiScale(hwnd, kProgressDy);
    rProgress = {padX, dy, szText.dx, progressDy};
    if (HasProgress()) {
        dy += padY + progressDy + padY;
    }

    Rect rCurr = WindowRect(hwnd);
    // for less flicker we don't want to shrink the window when the text shrinks
    if (dx < rCurr.dx) {
        int diff = rCurr.dx - dx;
        rClose.x += diff;
        dx = rCurr.dx;
    }
#if 0
    if (dy < rCurr.dy) {
        dy = rCurr.dy;
    }
#endif
#if 0
    if (wnd->shrinkLimit < 1.0f) {
        Rect rcOrig = ClientRect(wnd->hwnd);
        if (rMsg.dx < rcOrig.dx && rMsg.dx > rcOrig.dx * wnd->shrinkLimit) {
            rMsg.dx = rcOrig.dx;
        }
    }
#endif

    // y-center close
    rClose.y = ((dy - closeDx) / 2) + 1;

    if (dx == rCurr.dx && dy == rCurr.dy) {
        return;
    }

    // adjust the window to fit the message (only shrink the window when there's no progress bar)
    uint flags = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE;
    SetWindowPos(hwnd, nullptr, 0, 0, dx, dy, flags);

    // move the window to the right for a right-to-left layout
    if (IsUIRtl()) {
        HWND parent = GetParent(hwnd);
        Rect r = MapRectToWindow(WindowRect(hwnd), HWND_DESKTOP, parent);
        int cxVScroll = GetSystemMetrics(SM_CXVSCROLL);
        r.x = WindowRect(parent).dx - r.dx - DpiScale(hwnd, kTopLeftMargin) - cxVScroll;
        flags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOACTIVATE | SWP_DEFERERASE;
        SetWindowPos(hwnd, nullptr, r.x, r.y, 0, 0, flags);
    }
}

// TODO: figure out why it flickers
void NotificationWnd::OnPaint(HDC hdcIn, PAINTSTRUCT* ps) {
    Rect rc = ClientRect(hwnd);
    DoubleBuffer buffer(hwnd, rc);
    HDC hdc = buffer.GetDC();
    // HDC hdc = hdcIn;

    ScopedSelectObject fontPrev(hdc, font);

    COLORREF colBg = ThemeNotificationsBackgroundColor();
    COLORREF colBorder = MkGray(0xdd);
    COLORREF colTxt = ThemeNotificationsTextColor();
    if (highlight) {
        colBg = ThemeNotificationsHighlightColor();
        colBorder = colBg;
        colTxt = ThemeNotificationsHighlightTextColor();
    }
    // COLORREF colBg = MkRgb(0xff, 0xff, 0x5c);
    // COLORREF colBg = MkGray(0xff);

    Graphics graphics(hdc);
    SolidBrush br(GdiRgbFromCOLORREF(colBg));
    auto grc = Gdiplus::Rect(0, 0, rc.dx, rc.dy);
    graphics.FillRectangle(&br, grc);

    if (false) {
        Pen pen(GdiRgbFromCOLORREF(colBorder));
        pen.SetWidth(4);
        grc = {rc.x, rc.y, rc.dx, rc.dy};
        graphics.DrawRectangle(&pen, grc);
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, colTxt);
    char* text = HwndGetTextTemp(hwnd);
    uint format = DT_SINGLELINE | DT_NOPREFIX;
    RECT rTmp = ToRECT(rTxt);
    HdcDrawText(hdc, text, &rTmp, format);

    Point curPos = HwndGetCursorPos(hwnd);
    bool isHover = rClose.Contains(curPos);
    DrawCloseButton(hdc, rClose, isHover);
#if 0
    DrawCloseButtonArgs args;
    args.hdc = hdc;
    args.r = rClose;
    args.r.Inflate(-5, -5);
    args.isHover = isHover;
    DrawCloseButton2(args);
#endif

    if (HasProgress()) {
        rc = rProgress;
        int progressWidth = rc.dx;

        COLORREF col = ThemeNotificationsProgressColor();
        Pen pen(GdiRgbFromCOLORREF(col));
        grc = {rc.x, rc.y, rc.dx, rc.dy};
        graphics.DrawRectangle(&pen, grc);

        rc.x += 2;
        rc.dx = (progressWidth - 3) * progressPerc / 100;
        rc.y += 2;
        rc.dy -= 3;

        br.SetColor(GdiRgbFromCOLORREF(col));
        grc = {rc.x, rc.y, rc.dx, rc.dy};
        graphics.FillRectangle(&br, grc);
    }

    buffer.Flush(hdcIn);
}

void NotificationWnd::UpdateMessage(const char* msg, int timeoutMs, bool highlight) {
    HwndSetText(hwnd, msg);
    this->highlight = highlight;
    this->timeoutMs = timeoutMs;
    HwndSetRtl(hwnd, IsUIRtl());
    Layout(msg);
    HwndRepaintNow(hwnd);
    if (timeoutMs != 0) {
        SetTimer(hwnd, kNotifTimerTimeoutId, timeoutMs, nullptr);
    }
}

bool UpdateNotificationProgress(NotificationWnd* wnd, const char* msg, int perc) {
    if (!IsNotificationValid(wnd)) {
        return false;
    }
    ReportIf(perc < 0 || perc > 100);
    wnd->progressPerc = perc;
    wnd->UpdateMessage(msg);
    return true;
}

static void NotifRemove(NotificationWnd* wnd) {
    wnd->wndRemovedCb.Call(wnd);
}

static void NotifDelete(NotificationWnd* wnd) {
    delete wnd;
}

void NotificationWnd::OnTimer(UINT_PTR timerId) {
    ReportIf(kNotifTimerTimeoutId != timerId);
    // TODO a better way to delete myself
    if (wndRemovedCb.IsValid()) {
        auto fn = MkFunc0<NotificationWnd>(NotifRemove, this);
        uitask::Post(fn, "TaskNotifOnTimerRemove");
    } else {
        auto fn = MkFunc0<NotificationWnd>(NotifDelete, this);
        uitask::Post(fn, "TaskNotifOnTimerDelete");
    }
}

LRESULT NotificationWnd::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (WM_SETCURSOR == msg) {
        Point pt = HwndGetCursorPos(hwnd);
        if (!pt.IsEmpty() && rClose.Contains(pt)) {
            SetCursorCached(IDC_HAND);
            return TRUE;
        }
    }

    if (WM_ERASEBKGND == msg) {
        // avoid flicker by telling we took care of erasing background
        return TRUE;
    }

    if (WM_MOUSEMOVE == msg) {
        HwndScheduleRepaint(hwnd);

        if (IsMouseOverRect(hwnd, rClose)) {
            TrackMouseLeave(hwnd);
        }
        goto DoDefault;
    }

    if (WM_MOUSELEAVE == msg) {
        HwndScheduleRepaint(hwnd);
        return 0;
    }

    if (WM_LBUTTONUP) {
        Point pt = Point(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (rClose.Contains(pt)) {
            // TODO a better way to delete myself
            if (wndRemovedCb.IsValid()) {
                auto fn = MkFunc0<NotificationWnd>(NotifRemove, this);
                uitask::Post(fn, "TaskNotifWndProcRemove");
            } else {
                auto fn = MkFunc0<NotificationWnd>(NotifDelete, this);
                uitask::Post(fn, "TaskNotifWndProcDelete");
            }
            return 0;
        }
    }

DoDefault:
    return WndProcDefault(hwnd, msg, wp, lp);
}

static int NotifsRemoveForGroup(Vec<NotificationWnd*>& wnds, Kind groupId) {
    ReportIf(groupId == nullptr);
    Vec<NotificationWnd*> toRemove;
    for (auto* wnd : wnds) {
        if (wnd->groupId == groupId) {
            toRemove.Append(wnd);
        }
    }
    for (auto* wnd : toRemove) {
        NotifsRemoveNotification(wnd);
    }
    return toRemove.Size();
}

static void NotifsAdd(Vec<NotificationWnd*>& wnds, NotificationWnd* wnd, Kind groupId) {
    bool skipRemove = (groupId == nullptr) || (groupId == kNotifAdHoc);
    if (!skipRemove) {
        NotifsRemoveForGroup(wnds, groupId);
    }
    wnd->groupId = groupId;
    gNotifs.Append(wnd);
    RelayoutNotifications(wnd->hwnd);
}

static void NotifsAdd(NotificationWnd* wnd, Kind groupId) {
    Vec<NotificationWnd*> wnds;
    GetForSameHwnd(wnd, wnds);
    NotifsAdd(wnds, wnd, groupId);
}

NotificationWnd* NotifsGetForGroup(Vec<NotificationWnd*>& wnds, Kind groupId) {
    ReportIf(!groupId);
    for (auto* wnd : wnds) {
        if (wnd->groupId == groupId) {
            return wnd;
        }
    }
    return nullptr;
}

NotificationWnd* ShowNotification(const NotificationCreateArgs& args) {
    ReportIf(!args.hwndParent);

    NotificationWnd* wnd = new NotificationWnd();
    wnd->Create(args);
    if (!wnd->hwnd) {
        delete wnd;
        return nullptr;
    }
    BringWindowToTop(wnd->hwnd);
    NotifsAdd(wnd, args.groupId);
    return wnd;
}

// show a temporary notification that will go away after a timeout
NotificationWnd* ShowTemporaryNotification(HWND hwnd, const char* msg, int timeoutMs) {
    if (timeoutMs <= 0) {
        timeoutMs = kNotifDefaultTimeOut;
    }
    NotificationCreateArgs args;
    args.hwndParent = hwnd;
    args.msg = msg;
    args.timeoutMs = timeoutMs;
    return ShowNotification(args);
}

NotificationWnd* ShowWarningNotification(HWND hwndParent, const char* msg, int timeoutMs) {
    if (timeoutMs < 0) {
        timeoutMs = kNotifDefaultTimeOut;
    }
    NotificationCreateArgs args;
    args.hwndParent = hwndParent;
    args.msg = msg;
    args.warning = true;
    args.timeoutMs = timeoutMs;
    return ShowNotification(args);
}

void NotificationUpdateMessage(NotificationWnd* wnd, const char* msg, int timeoutMs, bool highlight) {
    wnd->UpdateMessage(msg, timeoutMs, highlight);
}

void RemoveNotification(NotificationWnd* wnd) {
    NotifsRemoveNotification(wnd);
}

bool RemoveNotificationsForGroup(HWND hwnd, Kind kind) {
    Vec<NotificationWnd*> wnds;
    GetForHwnd(hwnd, wnds);
    int n = NotifsRemoveForGroup(wnds, kind);
    return n > 0;
}

NotificationWnd* GetNotificationForGroup(HWND hwnd, Kind kind) {
    Vec<NotificationWnd*> wnds;
    GetForHwnd(hwnd, wnds);
    return NotifsGetForGroup(wnds, kind);
}
