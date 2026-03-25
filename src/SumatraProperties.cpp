/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/StrFormat.h"
#include "utils/ScopedWin.h"
#include "utils/FileUtil.h"
#include "utils/WinUtil.h"

#include "wingui/UIModels.h"
#include "wingui/Layout.h"
#include "wingui/WinGui.h"

#include "Settings.h"
#include "DocProperties.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "DisplayModel.h"
#include "AppTools.h"
#include "AppColors.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "resource.h"
#include "Commands.h"
#include "HomePage.h"
#include "SumatraProperties.h"
#include "Translations.h"
#include "SumatraConfig.h"
#include "Print.h"
#include "Theme.h"
#include "DarkModeSubclass.h"

void ShowProperties(HWND parent, DocController* ctrl, bool extended);

constexpr const WCHAR* kPropertiesWinClassName = L"SUMATRA_PDF_PROPERTIES";

constexpr int kButtonAreaDy = 40;
constexpr int kButtonPadding = 8;

struct PropertiesLayout {
    HWND hwnd = nullptr;
    HWND hwndParent = nullptr;
    HWND hwndEdit = nullptr;
    Button* btnCopyToClipboard = nullptr;
    Button* btnGetFonts = nullptr;
    str::Str propsText;

    PropertiesLayout() = default;
    ~PropertiesLayout() {
        delete btnCopyToClipboard;
        delete btnGetFonts;
    }
};

static Vec<PropertiesLayout*> gPropertiesWindows;

PropertiesLayout* FindPropertyWindowByHwnd(HWND hwnd) {
    for (PropertiesLayout* pl : gPropertiesWindows) {
        if (pl->hwnd == hwnd) {
            return pl;
        }
        if (pl->hwndParent == hwnd) {
            return pl;
        }
    }
    return nullptr;
}

void DeletePropertiesWindow(HWND hwndParent) {
    PropertiesLayout* pl = FindPropertyWindowByHwnd(hwndParent);
    if (pl) {
        DestroyWindow(pl->hwnd);
    }
}

// See: http://www.verypdf.com/pdfinfoeditor/pdf-date-format.htm
// Format:  "D:YYYYMMDDHHMMSSxxxxxxx"
// Example: "D:20091222171933-05'00'"
static bool PdfDateParseA(const char* pdfDate, SYSTEMTIME* timeOut) {
    ZeroMemory(timeOut, sizeof(SYSTEMTIME));
    // "D:" at the beginning is optional
    if (str::StartsWith(pdfDate, "D:")) {
        pdfDate += 2;
    }
    return str::Parse(pdfDate,
                      "%4d%2d%2d"
                      "%2d%2d%2d",
                      &timeOut->wYear, &timeOut->wMonth, &timeOut->wDay, &timeOut->wHour, &timeOut->wMinute,
                      &timeOut->wSecond) != nullptr;
    // don't bother about the day of week, we won't display it anyway
}

// See: ISO 8601 specification
// Format:  "YYYY-MM-DDTHH:MM:SSZ"
// Example: "2011-04-19T22:10:48Z"
static bool IsoDateParse(const char* isoDate, SYSTEMTIME* timeOut) {
    ZeroMemory(timeOut, sizeof(SYSTEMTIME));
    const char* end = str::Parse(isoDate, "%4d-%2d-%2d", &timeOut->wYear, &timeOut->wMonth, &timeOut->wDay);
    if (end) { // time is optional
        str::Parse(end, "T%2d:%2d:%2dZ", &timeOut->wHour, &timeOut->wMinute, &timeOut->wSecond);
    }
    return end != nullptr;
    // don't bother about the day of week, we won't display it anyway
}

static TempStr FormatSystemTimeTemp(SYSTEMTIME& date) {
    WCHAR bufW[512]{};
    int cchBufLen = dimof(bufW);
    int ret = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &date, nullptr, bufW, cchBufLen);
    if (ret < 2) { // GetDateFormat() failed or returned an empty result
        return nullptr;
    }

    // don't add 00:00:00 for dates without time
    if (0 == date.wHour && 0 == date.wMinute && 0 == date.wSecond) {
        return ToUtf8Temp(bufW);
    }

    WCHAR* tmp = bufW + ret;
    tmp[-1] = ' ';
    ret = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &date, nullptr, tmp, cchBufLen - ret);
    if (ret < 2) { // GetTimeFormat() failed or returned an empty result
        tmp[-1] = '\0';
    }

    return ToUtf8Temp(bufW);
}

// Convert a date in PDF or XPS format, e.g. "D:20091222171933-05'00'" to a display
// format e.g. "12/22/2009 5:19:33 PM"
// See: http://www.verypdf.com/pdfinfoeditor/pdf-date-format.htm
// The conversion happens in place
static TempStr ConvDateToDisplayTemp(const char* s, bool (*dateParseFn)(const char* date, SYSTEMTIME* timeOut)) {
    if (!s || !*s || !dateParseFn) {
        return nullptr;
    }

    SYSTEMTIME date{};
    bool ok = dateParseFn(s, &date);
    if (!ok) {
        return nullptr;
    }

    return FormatSystemTimeTemp(date);
}

// format page size according to locale (e.g. "29.7 x 21.0 cm" or "11.69 x 8.27 in")
static TempStr FormatPageSizeTemp(EngineBase* engine, int pageNo, int rotation) {
    RectF mediabox = engine->PageMediabox(pageNo);
    float zoom = 1.0f / engine->GetFileDPI();
    SizeF size = engine->Transform(mediabox, pageNo, zoom, rotation).Size();

    const char* formatName = "";
    switch (GetPaperFormatFromSizeApprox(size)) {
        case PaperFormat::A2:
            formatName = " (A2)";
            break;
        case PaperFormat::A3:
            formatName = " (A3)";
            break;
        case PaperFormat::A4:
            formatName = " (A4)";
            break;
        case PaperFormat::A5:
            formatName = " (A5)";
            break;
        case PaperFormat::A6:
            formatName = " (A6)";
            break;
        case PaperFormat::Letter:
            formatName = " (Letter)";
            break;
        case PaperFormat::Legal:
            formatName = " (Legal)";
            break;
        case PaperFormat::Tabloid:
            formatName = " (Tabloid)";
            break;
        case PaperFormat::Statement:
            formatName = " (Statement)";
            break;
    }

    bool isMetric = GetMeasurementSystem() == 0;
    double unitsPerInch = isMetric ? 2.54 : 1.0;
    const char* unit = isMetric ? "cm" : "in";

    double width = size.dx * unitsPerInch;
    double height = size.dy * unitsPerInch;
    if (((int)(width * 100)) % 100 == 99) {
        width += 0.01;
    }
    if (((int)(height * 100)) % 100 == 99) {
        height += 0.01;
    }

    char* strWidth = str::FormatFloatWithThousandSepTemp(width);
    char* strHeight = str::FormatFloatWithThousandSepTemp(height);

    return str::FormatTemp("%s x %s %s%s", strWidth, strHeight, unit, formatName);
}

// returns a list of permissions denied by this document
static TempStr FormatPermissionsTemp(DocController* ctrl) {
    if (!ctrl->AsFixed()) {
        return nullptr;
    }

    StrVec denials;

    EngineBase* engine = ctrl->AsFixed()->GetEngine();
    if (!engine->AllowsPrinting()) {
        denials.Append(_TRA("printing document"));
    }
    if (!engine->AllowsCopyingText()) {
        denials.Append(_TRA("copying text"));
    }

    return JoinTemp(&denials, ", ");
}

static void AppendProp(str::Str& out, const char* key, const char* value) {
    if (str::IsEmpty(value)) {
        return;
    }
    out.AppendFmt("%s %s\n", key, value);
}

// clang-format off
static const char* propToName[] = {
    kPropTitle, _TRN("Title:"),
    kPropSubject, _TRN("Subject:"),
    kPropAuthor, _TRN("Author:"),
    kPropCopyright, _TRN("Copyright:"),
    kPropCreatorApp, _TRN("Application:"),
    kPropPdfProducer, _TRN("PDF Producer:"),
    kPropPdfVersion, _TRN("PDF Version:"),
    nullptr,
};
// clang-format on

static void AppendPropTranslated(str::Str& out, const char* propName, const char* val) {
    const char* s = GetMatchingString(propToName, propName);
    ReportIf(!s);
    const char* trans = trans::GetTranslation(s);
    AppendProp(out, trans, val);
}

static void AppendPropTranslated(str::Str& out, DocController* ctrl, const char* propName) {
    TempStr val = ctrl->GetPropertyTemp(propName);
    AppendPropTranslated(out, propName, val);
}

static void AppendPdfFileStructure(str::Str& out, DocController* ctrl) {
    TempStr fstruct = ctrl->GetPropertyTemp(kPropPdfFileStructure);
    if (str::IsEmpty(fstruct)) {
        bool isPDF = str::EndsWithI(ctrl->GetFilePath(), ".pdf");
        if (isPDF) {
            AppendProp(out, _TRA("Fast Web View"), _TRA("No"));
        }
        return;
    }
    StrVec parts;
    Split(&parts, fstruct, ",", true);

    StrVec props;

    const char* linearized = _TRA("No");
    if (parts.Contains("linearized")) {
        linearized = _TRA("Yes");
    }
    AppendProp(out, _TRA("Fast Web View"), linearized);

    if (parts.Contains("tagged")) {
        props.Append(_TRA("Tagged PDF"));
    }
    if (parts.Contains("PDFX")) {
        props.Append("PDF/X (ISO 15930)");
    }
    if (parts.Contains("PDFA1")) {
        props.Append("PDF/A (ISO 19005)");
    }
    if (parts.Contains("PDFE1")) {
        props.Append("PDF/E (ISO 24517)");
    }

    TempStr val = JoinTemp(&props, ", ");
    AppendProp(out, _TRA("PDF Optimizations:"), val);
}

static void GetPropsText(DocController* ctrl, str::Str& out, bool extended) {
    ReportIf(!ctrl);

    const char* path = gPluginMode ? gPluginURL : ctrl->GetFilePath();
    if (!path) {
        path = "unknown";
    }
    AppendProp(out, _TRA("File:"), path);

    DisplayModel* dm = ctrl->AsFixed();
    i64 fileSize = file::GetSize(path); // can be gPluginURL
    if (-1 == fileSize && dm) {
        EngineBase* engine = dm->GetEngine();
        ByteSlice d = engine->GetFileData();
        if (!d.empty()) {
            fileSize = d.size();
        }
        d.Free();
    }
    TempStr strTemp;
    if (-1 != fileSize) {
        strTemp = FormatFileSizeTransTemp(fileSize);
        AppendProp(out, _TRA("File Size:"), strTemp);
    }

    AppendPropTranslated(out, ctrl, kPropTitle);
    AppendPropTranslated(out, ctrl, kPropSubject);
    AppendPropTranslated(out, ctrl, kPropAuthor);
    AppendPropTranslated(out, ctrl, kPropCopyright);

    TempStr val = ctrl->GetPropertyTemp(kPropCreationDate);
    if (val && dm && kindEngineMupdf == dm->engineType) {
        strTemp = ConvDateToDisplayTemp(val, PdfDateParseA);
    } else {
        strTemp = ConvDateToDisplayTemp(val, IsoDateParse);
    }
    AppendProp(out, _TRA("Created:"), strTemp);

    val = ctrl->GetPropertyTemp(kPropModificationDate);
    if (val && dm && kindEngineMupdf == dm->engineType) {
        strTemp = ConvDateToDisplayTemp(val, PdfDateParseA);
    } else {
        strTemp = ConvDateToDisplayTemp(val, IsoDateParse);
    }
    AppendProp(out, _TRA("Modified:"), strTemp);

    AppendPropTranslated(out, ctrl, kPropCreatorApp);
    AppendPropTranslated(out, ctrl, kPropPdfProducer);
    AppendPropTranslated(out, ctrl, kPropPdfVersion);

    AppendPdfFileStructure(out, ctrl);

    strTemp = str::FormatTemp("%d", ctrl->PageCount());
    AppendProp(out, _TRA("Number of Pages:"), strTemp);

    if (dm) {
        strTemp = FormatPageSizeTemp(dm->GetEngine(), ctrl->CurrentPageNo(), dm->GetRotation());
        AppendProp(out, _TRA("Page Size:"), strTemp);
    }

    strTemp = FormatPermissionsTemp(ctrl);
    AppendProp(out, _TRA("Denied Permissions:"), strTemp);

    if (extended) {
        // Note: FontList extraction can take a while
        val = ctrl->GetPropertyTemp(kPropFontList);
        if (val) {
            out.Append("\n");
            out.Append(_TRA("Fonts:"));
            out.Append("\n");
            out.Append(val);
        }
    }
}

static void SetEditText(HWND hwndEdit, const char* text) {
    // edit control needs \r\n line endings
    str::Str crlfText;
    for (const char* s = text; *s; s++) {
        if (*s == '\n' && (s == text || *(s - 1) != '\r')) {
            crlfText.AppendChar('\r');
        }
        crlfText.AppendChar(*s);
    }
    HwndSetText(hwndEdit, crlfText.CStr());
    SendMessageW(hwndEdit, EM_SETSEL, 0, 0);
}

static void ShowExtendedProperties(PropertiesLayout* pl) {
    if (!pl) {
        return;
    }
    // check if already showing fonts
    if (str::Find(pl->propsText.CStr(), _TRA("Fonts:"))) {
        return;
    }
    MainWindow* win = FindMainWindowByHwnd(pl->hwndParent);
    if (!win) {
        return;
    }
    DestroyWindow(pl->hwnd);
    ShowProperties(win->hwndFrame, win->ctrl, true);
}

static void CopyPropertiesToClipboard(PropertiesLayout* pl) {
    if (!pl) {
        return;
    }
    CopyTextToClipboard(pl->propsText.CStr());
}

static void LayoutButtons(PropertiesLayout* pl) {
    Rect cRc = ClientRect(pl->hwnd);
    int btnY = cRc.dy - kButtonAreaDy + kButtonPadding;

    if (pl->btnGetFonts) {
        auto sz = pl->btnGetFonts->GetIdealSize();
        Rect rc{kButtonPadding, btnY, sz.dx, sz.dy};
        pl->btnGetFonts->SetBounds(rc);
    }

    if (pl->btnCopyToClipboard) {
        auto sz = pl->btnCopyToClipboard->GetIdealSize();
        int x = cRc.dx - kButtonPadding - sz.dx;
        Rect rc{x, btnY, sz.dx, sz.dy};
        pl->btnCopyToClipboard->SetBounds(rc);
    }
}

LRESULT CALLBACK WndProcProperties(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static WNDPROC DefWndProcPropertiesEdit = nullptr;

static LRESULT CALLBACK WndProcPropertiesEdit(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (WM_CHAR == msg && VK_ESCAPE == wp) {
        DestroyWindow(GetParent(hwnd));
        return 0;
    }
    return CallWindowProc(DefWndProcPropertiesEdit, hwnd, msg, wp, lp);
}

static void PropertiesOnCommand(HWND hwnd, WPARAM wp) {
    auto cmd = LOWORD(wp);
    PropertiesLayout* pl = FindPropertyWindowByHwnd(hwnd);
    switch (cmd) {
        case CmdCopySelection:
            CopyPropertiesToClipboard(pl);
            break;

        case CmdProperties:
            // make a repeated Ctrl+D display some extended properties
            ShowExtendedProperties(pl);
            break;
    }
}

LRESULT CALLBACK WndProcProperties(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PropertiesLayout* pl;

    LRESULT res = 0;
    res = TryReflectMessages(hwnd, msg, wp, lp);
    if (res != 0) {
        return res;
    }

    switch (msg) {
        case WM_CREATE:
            break;

        case WM_SIZE:
            pl = FindPropertyWindowByHwnd(hwnd);
            if (pl && pl->hwndEdit) {
                int dx = LOWORD(lp);
                int dy = HIWORD(lp);
                int editDy = dy - kButtonAreaDy;
                MoveWindow(pl->hwndEdit, 0, 0, dx, editDy, TRUE);
                LayoutButtons(pl);
                RECT rc = {0, editDy, dx, dy};
                InvalidateRect(hwnd, &rc, TRUE);
            }
            return 0;

        case WM_CHAR:
            if (VK_ESCAPE == wp) {
                DestroyWindow(hwnd);
            }
            break;

        case WM_DESTROY:
            pl = FindPropertyWindowByHwnd(hwnd);
            ReportIf(!pl);
            gPropertiesWindows.Remove(pl);
            delete pl;
            break;

        case WM_COMMAND:
            PropertiesOnCommand(hwnd, wp);
            break;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

void ShowProperties(HWND parent, DocController* ctrl, bool extended) {
    PropertiesLayout* layoutData = FindPropertyWindowByHwnd(parent);
    if (layoutData) {
        SetActiveWindow(layoutData->hwnd);
        return;
    }

    if (!ctrl) {
        return;
    }

    layoutData = new PropertiesLayout();
    gPropertiesWindows.Append(layoutData);
    GetPropsText(ctrl, layoutData->propsText, extended);

    HMODULE h = GetModuleHandleW(nullptr);
    WNDCLASSEX wcex = {};
    FillWndClassEx(wcex, kPropertiesWinClassName, WndProcProperties);
    wcex.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    WCHAR* iconName = MAKEINTRESOURCEW(GetAppIconID());
    wcex.hIcon = LoadIconW(h, iconName);
    ReportIf(!wcex.hIcon);
    RegisterClassEx(&wcex);

    DWORD dwStyle = WS_OVERLAPPEDWINDOW;
    auto title = ToWStrTemp(_TRA("Document Properties"));
    HWND hwnd = CreateWindowExW(0, kPropertiesWinClassName, title, dwStyle, CW_USEDEFAULT, CW_USEDEFAULT, 500, 400,
                                nullptr, nullptr, h, nullptr);
    if (!hwnd) {
        gPropertiesWindows.Remove(layoutData);
        delete layoutData;
        return;
    }

    layoutData->hwnd = hwnd;
    layoutData->hwndParent = parent;

    bool isRtl = IsUIRtl();
    HwndSetRtl(hwnd, isRtl);

    // create the edit control
    Rect cRc = ClientRect(hwnd);
    int editDy = cRc.dy - kButtonAreaDy;
    DWORD editStyle =
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL;
    HWND hwndEdit =
        CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", editStyle, 0, 0, cRc.dx, editDy, hwnd, nullptr, h, nullptr);
    layoutData->hwndEdit = hwndEdit;

    if (!DefWndProcPropertiesEdit) {
        DefWndProcPropertiesEdit = (WNDPROC)GetWindowLongPtr(hwndEdit, GWLP_WNDPROC);
    }
    SetWindowLongPtr(hwndEdit, GWLP_WNDPROC, (LONG_PTR)WndProcPropertiesEdit);

    HDC hdc = GetDC(hwnd);
    HFONT font = CreateSimpleFont(hdc, "Consolas", 14);
    ReleaseDC(hwnd, hdc);
    if (font) {
        SendMessageW(hwndEdit, WM_SETFONT, (WPARAM)font, TRUE);
    }

    SetEditText(hwndEdit, layoutData->propsText.CStr());

    // estimate window width based on longest line
    {
        HDC hdcEdit = GetDC(hwndEdit);
        HGDIOBJ origFont = SelectObject(hdcEdit, font);
        int maxLineDx = 0;
        const char* text = layoutData->propsText.CStr();
        while (*text) {
            const char* nl = str::FindChar(text, '\n');
            int lineLen = nl ? (int)(nl - text) : str::Leni(text);
            SIZE sz{};
            TempWStr lineW = ToWStrTemp(text, (size_t)lineLen);
            GetTextExtentPoint32W(hdcEdit, lineW, str::Leni(lineW), &sz);
            if (sz.cx > maxLineDx) {
                maxLineDx = sz.cx;
            }
            text = nl ? nl + 1 : text + lineLen;
        }
        maxLineDx += 16;
        SelectObject(hdcEdit, origFont);
        ReleaseDC(hwndEdit, hdcEdit);

        // add padding for scrollbar, border, window frame
        int editPadding = GetSystemMetrics(SM_CXVSCROLL) + 2 * GetSystemMetrics(SM_CXEDGE) + 16;
        int frameDx = GetSystemMetrics(SM_CXFRAME) * 2;
        int wantedClientDx = maxLineDx + editPadding;
        int wantedDx = wantedClientDx + frameDx;

        // cap at 80% of screen
        Rect work = GetWorkAreaRect(WindowRect(parent), hwnd);
        int maxDx = (work.dx * 80) / 100;
        wantedDx = std::min(wantedDx, maxDx);

        Rect wRc = WindowRect(hwnd);
        MoveWindow(hwnd, wRc.x, wRc.y, wantedDx, wRc.dy, TRUE);
    }

    // create buttons
    {
        Button::CreateArgs args;
        args.parent = hwnd;
        args.text = _TRA("Copy To Clipboard");
        args.isRtl = isRtl;

        auto b = new Button();
        b->Create(args);
        layoutData->btnCopyToClipboard = b;
        b->onClick = MkFunc0(CopyPropertiesToClipboard, layoutData);
    }

    if (!extended) {
        Button::CreateArgs args;
        args.parent = hwnd;
        args.text = _TRA("Get Fonts Info");
        args.isRtl = isRtl;

        auto b = new Button();
        b->Create(args);
        layoutData->btnGetFonts = b;
        b->onClick = MkFunc0(ShowExtendedProperties, layoutData);
    }

    LayoutButtons(layoutData);

    CenterDialog(hwnd, parent);
    if (UseDarkModeLib()) {
        DarkMode::setDarkWndSafe(hwnd);
        DarkMode::setWindowEraseBgSubclass(hwnd);
    }
    ShowWindow(hwnd, SW_SHOW);
}
