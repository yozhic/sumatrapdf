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
#include "GlobalPrefs.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "resource.h"
#include "Commands.h"
#include "Translations.h"
#include "SumatraConfig.h"
#include "TabGroupsManage.h"

constexpr const WCHAR* kTabGroupsWinClassName = L"SUMATRA_PDF_TAB_GROUPS";

constexpr int kButtonAreaDy = 40;
constexpr int kButtonPadding = 8;
constexpr int kEditHeight = 24;
constexpr int kPadding = 8;

enum class TabGroupDialogMode {
    Save,
    Open,
};

struct TabGroupsDialog {
    HWND hwnd = nullptr;
    HWND hwndParent = nullptr;
    HWND hwndEdit = nullptr;
    HWND hwndListBox = nullptr;
    Button* btnOk = nullptr;
    Button* btnCancel = nullptr;
    TabGroupDialogMode mode = TabGroupDialogMode::Save;
    MainWindow* win = nullptr;
};

static Vec<TabGroupsDialog*> gTabGroupsDialogs;

static TabGroupsDialog* FindDialog(HWND hwnd) {
    for (auto* d : gTabGroupsDialogs) {
        if (d->hwnd == hwnd) {
            return d;
        }
    }
    return nullptr;
}

static void PopulateListBox(HWND hwndLb) {
    SendMessageW(hwndLb, LB_RESETCONTENT, 0, 0);
    auto* groups = gGlobalPrefs->tabGroups;
    if (!groups) {
        return;
    }
    for (auto* g : *groups) {
        auto ws = ToWStrTemp(g->name);
        SendMessageW(hwndLb, LB_ADDSTRING, 0, (LPARAM)ws);
    }
}

static void LayoutControls(TabGroupsDialog* d) {
    Rect rc = ClientRect(d->hwnd);
    int y = kPadding;
    int x = kPadding;
    int dx = rc.dx - 2 * kPadding;

    if (d->mode == TabGroupDialogMode::Save && d->hwndEdit) {
        MoveWindow(d->hwndEdit, x, y, dx, kEditHeight, TRUE);
        y += kEditHeight + kPadding;
    }

    int lbDy = rc.dy - y - kButtonAreaDy;
    if (lbDy < 20) {
        lbDy = 20;
    }
    MoveWindow(d->hwndListBox, x, y, dx, lbDy, TRUE);
    y += lbDy;

    // buttons at the bottom right
    Size okSize = d->btnOk->GetIdealSize();
    Size cancelSize = d->btnCancel->GetIdealSize();
    int btnY = rc.dy - kButtonPadding - okSize.dy;
    int btnX = rc.dx - kButtonPadding - cancelSize.dx;
    MoveWindow(d->btnCancel->hwnd, btnX, btnY, cancelSize.dx, cancelSize.dy, TRUE);
    btnX -= kButtonPadding + okSize.dx;
    MoveWindow(d->btnOk->hwnd, btnX, btnY, okSize.dx, okSize.dy, TRUE);
}

static void SaveTabGroup(TabGroupsDialog* d) {
    if (!d->hwndEdit) {
        return;
    }
    int n = GetWindowTextLengthW(d->hwndEdit);
    if (n <= 0) {
        return;
    }
    WCHAR* buf = AllocArrayTemp<WCHAR>(n + 1);
    GetWindowTextW(d->hwndEdit, buf, n + 1);
    TempStr name = ToUtf8Temp(buf);

    auto* group = AllocStruct<TabGroup>();
    group->name = str::Dup(name);
    group->tabFiles = new Vec<TabFile*>();

    for (WindowTab* tab : d->win->Tabs()) {
        if (tab->IsAboutTab()) {
            continue;
        }
        if (!tab->filePath) {
            continue;
        }
        auto* tf = AllocStruct<TabFile>();
        tf->path = str::Dup(tab->filePath);
        group->tabFiles->Append(tf);
    }

    if (!gGlobalPrefs->tabGroups) {
        gGlobalPrefs->tabGroups = new Vec<TabGroup*>();
    }
    gGlobalPrefs->tabGroups->Append(group);
    SaveSettings();
    DestroyWindow(d->hwnd);
}

static void OpenTabGroup(TabGroupsDialog* d) {
    int sel = (int)SendMessageW(d->hwndListBox, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) {
        return;
    }
    auto* groups = gGlobalPrefs->tabGroups;
    if (!groups || sel >= groups->Size()) {
        return;
    }
    TabGroup* group = groups->At(sel);
    if (!group->tabFiles || group->tabFiles->Size() == 0) {
        return;
    }

    MainWindow* newWin = CreateAndShowMainWindow(nullptr);
    if (!newWin) {
        return;
    }
    bool first = true;
    for (TabFile* tf : *group->tabFiles) {
        if (!tf->path || !*tf->path) {
            continue;
        }
        LoadArgs args(tf->path, newWin);
        // first file replaces the about tab, rest open as new tabs
        if (!first) {
            args.forceReuse = false;
        }
        LoadDocument(&args);
        first = false;
    }
    DestroyWindow(d->hwnd);
}

static LRESULT CALLBACK WndProcTabGroups(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT res = TryReflectMessages(hwnd, msg, wp, lp);
    if (res) {
        return res;
    }

    TabGroupsDialog* d = FindDialog(hwnd);

    switch (msg) {
        case WM_SIZE:
            if (d) {
                LayoutControls(d);
            }
            return 0;

        case WM_COMMAND:
            if (d && HIWORD(wp) == LBN_DBLCLK && (HWND)lp == d->hwndListBox) {
                if (d->mode == TabGroupDialogMode::Open) {
                    OpenTabGroup(d);
                } else {
                    // in save mode, double-click populates edit with the group name
                    // for overwrite (but we just select the name in the edit)
                    int sel = (int)SendMessageW(d->hwndListBox, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR && d->hwndEdit) {
                        auto* groups = gGlobalPrefs->tabGroups;
                        if (groups && sel < groups->Size()) {
                            auto ws = ToWStrTemp(groups->At(sel)->name);
                            SetWindowTextW(d->hwndEdit, ws);
                            SendMessageW(d->hwndEdit, EM_SETSEL, 0, -1);
                            SetFocus(d->hwndEdit);
                        }
                    }
                }
                return 0;
            }
            break;

        case WM_CHAR:
            if (VK_ESCAPE == wp) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;

        case WM_DESTROY:
            if (d) {
                gTabGroupsDialogs.Remove(d);
                delete d->btnOk;
                delete d->btnCancel;
                delete d;
            }
            return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

static void OnCancel(TabGroupsDialog* d) {
    DestroyWindow(d->hwnd);
}

static void OnOk(TabGroupsDialog* d) {
    if (d->mode == TabGroupDialogMode::Save) {
        SaveTabGroup(d);
    } else {
        OpenTabGroup(d);
    }
}

static void ShowTabGroupsDialog(MainWindow* win, TabGroupDialogMode mode) {
    // find existing dialog for this mode and bring to front
    for (auto* d : gTabGroupsDialogs) {
        if (d->win == win && d->mode == mode) {
            BringWindowToTop(d->hwnd);
            return;
        }
    }

    HMODULE h = GetModuleHandleW(nullptr);
    WNDCLASSEX wcex = {};
    FillWndClassEx(wcex, kTabGroupsWinClassName, WndProcTabGroups);
    wcex.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    WCHAR* iconName = MAKEINTRESOURCEW(GetAppIconID());
    wcex.hIcon = LoadIconW(h, iconName);
    RegisterClassEx(&wcex);

    bool isRtl = IsUIRtl();

    const char* titleStr = (mode == TabGroupDialogMode::Save) ? _TRA("Save Tab Group") : _TRA("Restore Tab Group");
    auto title = ToWStrTemp(titleStr);

    DWORD dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    HWND hwnd = CreateWindowExW(0, kTabGroupsWinClassName, title, dwStyle, CW_USEDEFAULT, CW_USEDEFAULT, 400, 350,
                                nullptr, nullptr, h, nullptr);
    if (!hwnd) {
        return;
    }

    auto* d = new TabGroupsDialog();
    d->hwnd = hwnd;
    d->hwndParent = win->hwndFrame;
    d->mode = mode;
    d->win = win;
    gTabGroupsDialogs.Append(d);

    HwndSetRtl(hwnd, isRtl);

    HFONT hFont = GetDefaultGuiFont();

    // edit control (only in save mode)
    if (mode == TabGroupDialogMode::Save) {
        DWORD editStyle = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
        d->hwndEdit =
            CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", editStyle, 0, 0, 0, 0, hwnd, nullptr, h, nullptr);
        SendMessageW(d->hwndEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        // pre-populate with "group #<n>"
        int groupNum = 1;
        if (gGlobalPrefs->tabGroups) {
            groupNum = gGlobalPrefs->tabGroups->Size() + 1;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "group #%d", groupNum);
        auto ws = ToWStrTemp(buf);
        SetWindowTextW(d->hwndEdit, ws);
        SendMessageW(d->hwndEdit, EM_SETSEL, 0, -1);
    }

    // listbox
    DWORD lbStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | LBS_NOTIFY;
    d->hwndListBox = CreateWindowExW(0, L"LISTBOX", L"", lbStyle, 0, 0, 0, 0, hwnd, nullptr, h, nullptr);
    SendMessageW(d->hwndListBox, WM_SETFONT, (WPARAM)hFont, TRUE);
    PopulateListBox(d->hwndListBox);

    // buttons
    {
        const char* okText = (mode == TabGroupDialogMode::Save) ? _TRA("Save") : _TRA("Open");
        Button::CreateArgs args;
        args.parent = hwnd;
        args.text = okText;
        args.isRtl = isRtl;
        auto b = new Button();
        b->Create(args);
        d->btnOk = b;
        b->onClick = MkFunc0(OnOk, d);
    }
    {
        Button::CreateArgs args;
        args.parent = hwnd;
        args.text = _TRA("Cancel");
        args.isRtl = isRtl;
        auto b = new Button();
        b->Create(args);
        d->btnCancel = b;
        b->onClick = MkFunc0(OnCancel, d);
    }

    LayoutControls(d);
    CenterDialog(hwnd, win->hwndFrame);
    HwndEnsureVisible(hwnd);
    ShowWindow(hwnd, SW_SHOW);

    if (d->hwndEdit) {
        SetFocus(d->hwndEdit);
    }
}

void ShowSaveTabGroupDialog(MainWindow* win) {
    ShowTabGroupsDialog(win, TabGroupDialogMode::Save);
}

void ShowOpenTabGroupDialog(MainWindow* win) {
    ShowTabGroupsDialog(win, TabGroupDialogMode::Open);
}
