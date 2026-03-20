/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/CmdLineArgsIter.h"
#include "utils/WinUtil.h"

#include "Settings.h"
#include "DisplayMode.h"
#include "GlobalPrefs.h"
#include "Flags.h"
#include "MainWindow.h"
#include "StressTesting.h"

#include "utils/Log.h"

// @gen-start flags
// clang-format off
enum class Arg {
    Unknown = -1,
    Silent = 0, Silent2 = 1, FastInstall = 2, PrintToDefault = 3,
    PrintDialog = 4, Help = 5, Help2 = 6, Help3 = 7,
    ExitWhenDone = 8, ExitOnPrint = 9, Restrict = 10, Presentation = 11,
    FullScreen = 12, InvertColors = 13, InvertColors2 = 14, Console = 15,
    Install = 16, UnInstall = 17, WithFilter = 18, WithSearch = 19,
    WithPreview = 20, Rand = 21, Regress = 22, Extract = 23,
    Tester = 24, TestApp = 25, NewWindow = 26, Log = 27,
    CrashOnOpen = 28, ReuseInstance = 29, EscToExit = 30, ArgEnumPrinters = 31,
    ListPrinters = 32, SleepMs = 33, PrintTo = 34, PrintSettings = 35,
    InverseSearch = 36, ForwardSearch1 = 37, ForwardSearch2 = 38, NamedDest = 39,
    NamedDest2 = 40, Page = 41, View = 42, Zoom = 43,
    Scroll = 44, AppData = 45, Plugin = 46, StressTest = 47,
    N = 48, Max = 49, MaxFiles = 50, Render = 51,
    ExtractText = 52, Bench = 53, Dir = 54, InstallDir = 55,
    Lang = 56, UpdateSelfTo = 57, ArgDeleteFile = 58, BgCol = 59,
    BgCol2 = 60, FwdSearchOffset = 61, FwdSearchWidth = 62, FwdSearchColor = 63,
    FwdSearchPermanent = 64, MangaMode = 65, Search = 66, AllUsers = 67,
    AllUsers2 = 68, RunInstallNow = 69, Adobe = 70, DDE = 71,
    EngineDump = 72, SetColorRange = 73, PreviewPipe = 74, IFilterPipe = 75,
    TestPreviewPipe = 76,
};

static const char* gArgNames =
    "s\0" "silent\0" "fast-install\0" "print-to-default\0"
    "print-dialog\0" "h\0" "?\0" "help\0"
    "exit-when-done\0" "exit-on-print\0" "restrict\0" "presentation\0"
    "fullscreen\0" "invertcolors\0" "invert-colors\0" "console\0"
    "install\0" "uninstall\0" "with-filter\0" "with-search\0"
    "with-preview\0" "rand\0" "regress\0" "x\0"
    "tester\0" "testapp\0" "new-window\0" "log\0"
    "crash-on-open\0" "reuse-instance\0" "esc-to-exit\0" "enum-printers\0"
    "list-printers\0" "sleep-ms\0" "print-to\0" "print-settings\0"
    "inverse-search\0" "forward-search\0" "fwdsearch\0" "nameddest\0"
    "named-dest\0" "page\0" "view\0" "zoom\0"
    "scroll\0" "appdata\0" "plugin\0" "stress-test\0"
    "n\0" "max\0" "max-files\0" "render\0"
    "extract-text\0" "bench\0" "d\0" "install-dir\0"
    "lang\0" "update-self-to\0" "delete-file\0" "bgcolor\0"
    "bg-color\0" "fwdsearch-offset\0" "fwdsearch-width\0" "fwdsearch-color\0"
    "fwdsearch-permanent\0" "manga-mode\0" "search\0" "all-users\0"
    "allusers\0" "run-install-now\0" "a\0" "dde\0"
    "engine-dump\0" "set-color-range\0" "preview-pipe\0" "ifilter-pipe\0"
    "test-preview-pipe\0";
// clang-format on
// @gen-end flags

static void AppendPrinterAttributes(str::Str& out, DWORD attr) {
    struct {
        DWORD flag;
        const char* name;
    } flags[] = {
        {PRINTER_ATTRIBUTE_QUEUED, "QUEUED"},
        {PRINTER_ATTRIBUTE_DIRECT, "DIRECT"},
        {PRINTER_ATTRIBUTE_DEFAULT, "DEFAULT"},
        {PRINTER_ATTRIBUTE_SHARED, "SHARED"},
        {PRINTER_ATTRIBUTE_NETWORK, "NETWORK"},
        {PRINTER_ATTRIBUTE_HIDDEN, "HIDDEN"},
        {PRINTER_ATTRIBUTE_LOCAL, "LOCAL"},
        {PRINTER_ATTRIBUTE_ENABLE_DEVQ, "ENABLE_DEVQ"},
        {PRINTER_ATTRIBUTE_KEEPPRINTEDJOBS, "KEEPPRINTEDJOBS"},
        {PRINTER_ATTRIBUTE_DO_COMPLETE_FIRST, "DO_COMPLETE_FIRST"},
        {PRINTER_ATTRIBUTE_WORK_OFFLINE, "WORK_OFFLINE"},
        {PRINTER_ATTRIBUTE_ENABLE_BIDI, "ENABLE_BIDI"},
        {PRINTER_ATTRIBUTE_RAW_ONLY, "RAW_ONLY"},
        {PRINTER_ATTRIBUTE_PUBLISHED, "PUBLISHED"},
        {PRINTER_ATTRIBUTE_FAX, "FAX"},
        {PRINTER_ATTRIBUTE_TS, "TS"},
    };
    for (auto& f : flags) {
        if (attr & f.flag) {
            out.AppendFmt("\n    %s", f.name);
        }
    }
}

static void AppendPrinterStatus(str::Str& out, DWORD status) {
    struct {
        DWORD flag;
        const char* name;
    } flags[] = {
        {PRINTER_STATUS_PAUSED, "PAUSED"},
        {PRINTER_STATUS_ERROR, "ERROR"},
        {PRINTER_STATUS_PENDING_DELETION, "PENDING_DELETION"},
        {PRINTER_STATUS_PAPER_JAM, "PAPER_JAM"},
        {PRINTER_STATUS_PAPER_OUT, "PAPER_OUT"},
        {PRINTER_STATUS_MANUAL_FEED, "MANUAL_FEED"},
        {PRINTER_STATUS_PAPER_PROBLEM, "PAPER_PROBLEM"},
        {PRINTER_STATUS_OFFLINE, "OFFLINE"},
        {PRINTER_STATUS_IO_ACTIVE, "IO_ACTIVE"},
        {PRINTER_STATUS_BUSY, "BUSY"},
        {PRINTER_STATUS_PRINTING, "PRINTING"},
        {PRINTER_STATUS_OUTPUT_BIN_FULL, "OUTPUT_BIN_FULL"},
        {PRINTER_STATUS_NOT_AVAILABLE, "NOT_AVAILABLE"},
        {PRINTER_STATUS_WAITING, "WAITING"},
        {PRINTER_STATUS_PROCESSING, "PROCESSING"},
        {PRINTER_STATUS_INITIALIZING, "INITIALIZING"},
        {PRINTER_STATUS_WARMING_UP, "WARMING_UP"},
        {PRINTER_STATUS_TONER_LOW, "TONER_LOW"},
        {PRINTER_STATUS_NO_TONER, "NO_TONER"},
        {PRINTER_STATUS_PAGE_PUNT, "PAGE_PUNT"},
        {PRINTER_STATUS_USER_INTERVENTION, "USER_INTERVENTION"},
        {PRINTER_STATUS_OUT_OF_MEMORY, "OUT_OF_MEMORY"},
        {PRINTER_STATUS_DOOR_OPEN, "DOOR_OPEN"},
        {PRINTER_STATUS_SERVER_UNKNOWN, "SERVER_UNKNOWN"},
        {PRINTER_STATUS_POWER_SAVE, "POWER_SAVE"},
    };
    bool any = false;
    for (auto& f : flags) {
        if (status & f.flag) {
            out.AppendFmt("\n    %s", f.name);
            any = true;
        }
    }
    if (!any) {
        out.Append("\n    READY");
    }
}

static void AppendDeviceCapabilities(str::Str& out, const WCHAR* nameW, const WCHAR* portW) {
    // paper bins
    DWORD bins = DeviceCapabilitiesW(nameW, portW, DC_BINS, nullptr, nullptr);
    DWORD binNames = DeviceCapabilitiesW(nameW, portW, DC_BINNAMES, nullptr, nullptr);
    ReportIf(bins != binNames);
    if (0 == bins) {
        out.Append("  no paper bins available\n");
    } else if (bins == (DWORD)-1) {
        out.AppendFmt("  error: call to DeviceCapabilities failed with error %#x\n", GetLastError());
    } else {
        ScopedMem<WORD> binValues(AllocArray<WORD>(bins));
        DeviceCapabilitiesW(nameW, portW, DC_BINS, (WCHAR*)binValues.Get(), nullptr);
        ScopedMem<WCHAR> binNameValues(AllocArray<WCHAR>(24 * (size_t)binNames));
        DeviceCapabilitiesW(nameW, portW, DC_BINNAMES, binNameValues.Get(), nullptr);
        for (DWORD j = 0; j < bins; j++) {
            WCHAR* ws = binNameValues.Get() + 24 * (size_t)j;
            TempStr s = ToUtf8Temp(ws);
            out.AppendFmt("  bin %d: '%s' (%d)\n", (int)j, s, binValues.Get()[j]);
        }
    }

    // paper sizes
    DWORD papers = DeviceCapabilitiesW(nameW, portW, DC_PAPERS, nullptr, nullptr);
    DWORD paperNames = DeviceCapabilitiesW(nameW, portW, DC_PAPERNAMES, nullptr, nullptr);
    if (papers > 0 && papers != (DWORD)-1) {
        ScopedMem<WORD> paperValues(AllocArray<WORD>(papers));
        DeviceCapabilitiesW(nameW, portW, DC_PAPERS, (WCHAR*)paperValues.Get(), nullptr);
        // paper names are 64 WCHARs each
        ScopedMem<WCHAR> paperNameValues(AllocArray<WCHAR>(64 * (size_t)paperNames));
        DeviceCapabilitiesW(nameW, portW, DC_PAPERNAMES, paperNameValues.Get(), nullptr);
        // paper sizes in tenths of a millimeter
        ScopedMem<POINT> paperSizes(AllocArray<POINT>(papers));
        DeviceCapabilitiesW(nameW, portW, DC_PAPERSIZE, (WCHAR*)paperSizes.Get(), nullptr);
        out.Append("  paper sizes:\n");
        for (DWORD j = 0; j < papers; j++) {
            WCHAR* ws = paperNameValues.Get() + 64 * (size_t)j;
            TempStr s = ToUtf8Temp(ws);
            POINT sz = paperSizes.Get()[j];
            out.AppendFmt("    '%s' (id %d, %.1f x %.1f mm)\n", s, paperValues.Get()[j], sz.x / 10.0, sz.y / 10.0);
        }
    }

    // min/max custom paper size (dimensions packed in return value: LOWORD=width, HIWORD=height)
    DWORD minRes = DeviceCapabilitiesW(nameW, portW, DC_MINEXTENT, nullptr, nullptr);
    if (minRes != (DWORD)-1) {
        DWORD maxRes = DeviceCapabilitiesW(nameW, portW, DC_MAXEXTENT, nullptr, nullptr);
        int minW = LOWORD(minRes), minH = HIWORD(minRes);
        int maxW = LOWORD(maxRes), maxH = HIWORD(maxRes);
        out.AppendFmt("  custom paper size range: %.1f x %.1f mm to %.1f x %.1f mm\n", minW / 10.0, minH / 10.0,
                      maxW / 10.0, maxH / 10.0);
    }

    // duplex
    DWORD duplex = DeviceCapabilitiesW(nameW, portW, DC_DUPLEX, nullptr, nullptr);
    out.AppendFmt("  duplex: %s\n", duplex == 1 ? "yes" : "no");

    // color
    DWORD color = DeviceCapabilitiesW(nameW, portW, DC_COLORDEVICE, nullptr, nullptr);
    out.AppendFmt("  color: %s\n", color == 1 ? "yes" : "no");

    // copies
    DWORD copies = DeviceCapabilitiesW(nameW, portW, DC_COPIES, nullptr, nullptr);
    if (copies != (DWORD)-1) {
        out.AppendFmt("  max copies: %d\n", (int)copies);
    }

    // collate
    DWORD collate = DeviceCapabilitiesW(nameW, portW, DC_COLLATE, nullptr, nullptr);
    out.AppendFmt("  collation: %s\n", collate == 1 ? "yes" : "no");

    // orientation
    DWORD orient = DeviceCapabilitiesW(nameW, portW, DC_ORIENTATION, nullptr, nullptr);
    if (orient != (DWORD)-1 && orient != 0) {
        out.AppendFmt("  landscape rotation: %d degrees\n", (int)orient);
    }

    // resolutions
    DWORD nRes = DeviceCapabilitiesW(nameW, portW, DC_ENUMRESOLUTIONS, nullptr, nullptr);
    if (nRes > 0 && nRes != (DWORD)-1) {
        ScopedMem<LONG> resPairs(AllocArray<LONG>(2 * (size_t)nRes));
        DeviceCapabilitiesW(nameW, portW, DC_ENUMRESOLUTIONS, (WCHAR*)resPairs.Get(), nullptr);
        out.Append("  resolutions:");
        for (DWORD j = 0; j < nRes; j++) {
            LONG xDpi = resPairs.Get()[j * 2];
            LONG yDpi = resPairs.Get()[j * 2 + 1];
            out.AppendFmt(" %dx%d", (int)xDpi, (int)yDpi);
        }
        out.Append("\n");
    }

    // N-up (pages per sheet)
    DWORD nup = DeviceCapabilitiesW(nameW, portW, DC_NUP, nullptr, nullptr);
    if (nup > 0 && nup != (DWORD)-1) {
        ScopedMem<DWORD> nupValues(AllocArray<DWORD>(nup));
        DeviceCapabilitiesW(nameW, portW, DC_NUP, (WCHAR*)nupValues.Get(), nullptr);
        out.Append("  pages per sheet (N-up):");
        for (DWORD j = 0; j < nup; j++) {
            out.AppendFmt(" %d", (int)nupValues.Get()[j]);
        }
        out.Append("\n");
    }

    // media types
    DWORD nMedia = DeviceCapabilitiesW(nameW, portW, DC_MEDIATYPENAMES, nullptr, nullptr);
    if (nMedia > 0 && nMedia != (DWORD)-1) {
        // media type names are 64 WCHARs each
        ScopedMem<WCHAR> mediaNames(AllocArray<WCHAR>(64 * (size_t)nMedia));
        DeviceCapabilitiesW(nameW, portW, DC_MEDIATYPENAMES, mediaNames.Get(), nullptr);
        ScopedMem<DWORD> mediaValues(AllocArray<DWORD>(nMedia));
        DeviceCapabilitiesW(nameW, portW, DC_MEDIATYPES, (WCHAR*)mediaValues.Get(), nullptr);
        out.Append("  media types:\n");
        for (DWORD j = 0; j < nMedia; j++) {
            WCHAR* ws = mediaNames.Get() + 64 * (size_t)j;
            TempStr s = ToUtf8Temp(ws);
            out.AppendFmt("    '%s' (%d)\n", s, (int)mediaValues.Get()[j]);
        }
    }
}

static void AppendDevModeInfo(str::Str& out, DEVMODEW* dm) {
    if (!dm) {
        return;
    }
    out.Append("  devmode defaults:\n");
    if (dm->dmFields & DM_ORIENTATION) {
        const char* s = dm->dmOrientation == DMORIENT_PORTRAIT ? "portrait" : "landscape";
        out.AppendFmt("    orientation: %s\n", s);
    }
    if (dm->dmFields & DM_PAPERSIZE) {
        out.AppendFmt("    paper size id: %d\n", (int)dm->dmPaperSize);
    }
    if (dm->dmFields & DM_PAPERLENGTH) {
        out.AppendFmt("    paper length: %.1f mm\n", dm->dmPaperLength / 10.0);
    }
    if (dm->dmFields & DM_PAPERWIDTH) {
        out.AppendFmt("    paper width: %.1f mm\n", dm->dmPaperWidth / 10.0);
    }
    if (dm->dmFields & DM_COPIES) {
        out.AppendFmt("    copies: %d\n", (int)dm->dmCopies);
    }
    if (dm->dmFields & DM_PRINTQUALITY) {
        out.AppendFmt("    print quality: %d dpi\n", (int)dm->dmPrintQuality);
    }
    if (dm->dmFields & DM_YRESOLUTION) {
        out.AppendFmt("    y resolution: %d dpi\n", (int)dm->dmYResolution);
    }
    if (dm->dmFields & DM_COLOR) {
        const char* s = dm->dmColor == DMCOLOR_COLOR ? "color" : "monochrome";
        out.AppendFmt("    color: %s\n", s);
    }
    if (dm->dmFields & DM_DUPLEX) {
        const char* s = "unknown";
        if (dm->dmDuplex == DMDUP_SIMPLEX) {
            s = "simplex";
        } else if (dm->dmDuplex == DMDUP_HORIZONTAL) {
            s = "horizontal";
        } else if (dm->dmDuplex == DMDUP_VERTICAL) {
            s = "vertical";
        }
        out.AppendFmt("    duplex: %s\n", s);
    }
    if (dm->dmFields & DM_COLLATE) {
        out.AppendFmt("    collate: %s\n", dm->dmCollate == DMCOLLATE_TRUE ? "yes" : "no");
    }
}

static void EnumeratePrinters() {
    str::Str out;

    gLogToConsole = true;
    RedirectIOToExistingConsole();

    PRINTER_INFO_2* info2Arr = nullptr;
    DWORD bufSize = 0;
    DWORD printersCount = 0;
    DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    BOOL ok = EnumPrintersW(flags, nullptr, 2, nullptr, 0, &bufSize, &printersCount);
    if (ok != 0 || GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        info2Arr = (PRINTER_INFO_2*)calloc(bufSize, 1);
        if (info2Arr != nullptr) {
            ok = EnumPrintersW(flags, nullptr, 2, (LPBYTE)info2Arr, bufSize, &bufSize, &printersCount);
        }
    }
    if (ok == 0 || !info2Arr) {
        out.AppendFmt("Call to EnumPrinters failed with error %#x", GetLastError());
        log(out.CStr());
        ShowTextInWindow("SumatraPDF - EnumeratePrinters", out.CStr());
        free(info2Arr);
        return;
    }
    TempStr defName = GetDefaultPrinterNameTemp();
    out.AppendFmt("Default printer: \"%s\"\n", defName);
    for (DWORD i = 0; i < printersCount; i++) {
        PRINTER_INFO_2& info = info2Arr[i];
        const WCHAR* nameW = info.pPrinterName;
        const WCHAR* portW = info.pPortName;
        DWORD attr = info.Attributes;
        TempStr name = ToUtf8Temp(nameW);
        TempStr port = ToUtf8Temp(portW);
        out.AppendFmt("Printer: \"%s\"\n", name);
        out.AppendFmt("  port: %s\n", port);

        if (info.pDriverName) {
            out.AppendFmt("  driver: %s\n", ToUtf8Temp(info.pDriverName));
        }
        if (info.pShareName && info.pShareName[0]) {
            out.AppendFmt("  share name: %s\n", ToUtf8Temp(info.pShareName));
        }
        if (info.pComment && info.pComment[0]) {
            out.AppendFmt("  comment: %s\n", ToUtf8Temp(info.pComment));
        }
        if (info.pLocation && info.pLocation[0]) {
            out.AppendFmt("  location: %s\n", ToUtf8Temp(info.pLocation));
        }
        if (info.pPrintProcessor) {
            out.AppendFmt("  print processor: %s\n", ToUtf8Temp(info.pPrintProcessor));
        }
        if (info.pDatatype) {
            out.AppendFmt("  datatype: %s\n", ToUtf8Temp(info.pDatatype));
        }

        out.AppendFmt("  queued jobs: %d\n", (int)info.cJobs);

        out.AppendFmt("  status: %#x", info.Status);
        AppendPrinterStatus(out, info.Status);
        out.Append("\n");

        out.AppendFmt("  attributes: %#x", attr);
        AppendPrinterAttributes(out, attr);
        out.Append("\n");

        AppendDevModeInfo(out, info.pDevMode);
        AppendDeviceCapabilities(out, nameW, portW);
        out.Append("\n");
    }
    log(out.CStr());
    gLogToConsole = false;
    free(info2Arr);
    ShowTextInWindow("SumatraPDF - Show Printers", out.CStr());
}

// parses a list of page ranges such as 1,3-5,7- (i..e all but pages 2 and 6)
// into an interable list (returns nullptr on parsing errors)
// caller must delete the result
bool ParsePageRanges(const char* ranges, Vec<PageRange>& result) {
    if (!ranges) {
        return false;
    }

    StrVec rangeList;
    Split(&rangeList, ranges, ",", true);
    SortNatural(&rangeList);

    for (char* rangeStr : rangeList) {
        int start, end;
        if (str::Parse(rangeStr, "%d-%d%$", &start, &end) && 0 < start && start <= end) {
            result.Append(PageRange{start, end});
        } else if (str::Parse(rangeStr, "%d-%$", &start) && 0 < start) {
            result.Append(PageRange{start, INT_MAX});
        } else if (str::Parse(rangeStr, "%d%$", &start) && 0 < start) {
            result.Append(PageRange{start, start});
        } else {
            return false;
        }
    }

    return result.size() > 0;
}

// a valid page range is a non-empty, comma separated list of either
// single page ("3") numbers, closed intervals "2-4" or intervals
// unlimited to the right ("5-")
bool IsValidPageRange(const char* ranges) {
    Vec<PageRange> rangeList;
    return ParsePageRanges(ranges, rangeList);
}

// <s> can be:
// * "loadonly"
// * description of page ranges e.g. "1", "1-5", "2-3,6,8-10"
bool IsBenchPagesInfo(const char* s) {
    return str::EqI(s, "loadonly") || IsValidPageRange(s);
}

// -view [continuous][singlepage|facing|bookview]
static void ParseViewMode(DisplayMode* mode, const char* s) {
    *mode = DisplayModeFromString(s, DisplayMode::Automatic);
}

static const char* zoomValues =
    "fit page\0fitpage\0fit-page\0fit width\0fitwidth\0fit-width\0fit "
    "content\0fitcontent\0fit-content\0";

// -zoom [fitwidth|fitpage|fitcontent|n]
// if a number, it's in percent e.g. 12.5 means 12.5%
// 100 means 100% i.e. actual size as e.g. given in PDF file
static void ParseZoomValue(float* zoom, const char* txtOrig) {
    auto txtDup = str::DupTemp(txtOrig);
    char* txt = str::ToLowerInPlace(txtDup);
    int zoomVal = seqstrings::StrToIdx(zoomValues, txt);
    if (zoomVal >= 0) {
        // 0-2 : fit page
        // 3-5 : fit width
        // 6-8 : fit content
        *zoom = kZoomFitContent;
        if (zoomVal <= 5) {
            *zoom = kZoomFitWidth;
        }
        if (zoomVal <= 2) {
            *zoom = kZoomFitPage;
        }
        return;
    }
    // remove trailing % in place, if exists
    if (str::EndsWith(txt, "%")) {
        txt[str::Leni(txt) - 1] = 0;
    }
    str::Parse(txt, "%f", zoom);
    // prevent really small zoom and zoom values that are not valid numbers
    // (which would be parsed as 0)
    if (*zoom < 1.f) {
        *zoom = kZoomActualSize;
    }
}

// -scroll x,y
static void ParseScrollValue(Point* scroll, const char* txt) {
    int x, y;
    if (str::Parse(txt, "%d,%d%$", &x, &y)) {
        *scroll = Point(x, y);
    }
}

static Arg GetArg(const char* s) {
    if (!CouldBeArg(s)) {
        return Arg::Unknown;
    }
    const char* arg = s + 1;
    int idx = seqstrings::StrToIdxIS(gArgNames, arg);
    if (idx < 0) {
        return Arg::Unknown;
    }
    return (Arg)idx;
}

// https://stackoverflow.com/questions/619158/adobe-reader-command-line-reference
// https://www.robvanderwoude.com/commandlineswitches.php#Acrobat
// with Sumatra extensions
void ParseAdobeFlags(FileArgs& i, const char* s) {
    StrVec parts;
    StrVec parts2;
    char* name;
    char* val;
    int valN;

    // tha args can be separated with `#` or `?` or `:`
    // i.e. `foo#bar` or foo&bar` or `foo:bar`
    Split(&parts, s, "&", true);
    if (parts.Size() == 1) {
        parts.Reset();
        Split(&parts, s, "#", true);
    }
    if (parts.Size() == 1) {
        parts.Reset();
        Split(&parts, s, ";", true);
    }

    for (char* part : parts) {
        parts2.Reset();
        Split(&parts2, part, "=", true);
        if (parts2.Size() != 2) {
            continue;
        }
        name = parts2[0];
        val = parts2[1];
        valN = atoi(val);

        // https://pdfobject.com/pdf/pdf_open_parameters_acro8.pdf
        if (str::EqI("name", "nameddest")) {
            i.destName = str::Dup(val);
            continue;
        }
        if (str::EqI(name, "page") && valN >= 1) {
            i.pageNumber = valN;
            continue;
        }
        // comment=
        // collab=setting
        if (str::EqI(name, "zoom")) {
            // TODO: handle zoom
            // 100 is 100%
            continue;
        }
        if (str::EqI(name, "view")) {
            // TODO: Fit FitH FitH,top FitV FitV,left
            // FitB FitBH FitBH,top FitBV, FitBV,left
            continue;
        }
        // viewrect
        // pagemode=bookmarks, thumbs, none
        // scrollbar=1|0
        if (str::EqI(name, "search")) {
            if (str::Leni(val) > 0) {
                i.search = str::Dup(val);
            }
            continue;
        }
        // toolbar=1|0
        // statusbar=1|0
        // messages=1|0
        // navpanes=1|0
        // highlight=lrt,rt,top,btm
        // fdf=URL

        // those are Sumatra additions

        if (str::EqI(name, "annotatt") && valN > 0) {
            // for annotations that are attachments this is pdf object number
            // representing the attachment
            i.annotAttObjNum = valN;
            continue;
        }

        if (str::EqI(name, "attachno") && valN > 0) {
            // this is attachment number, use PdfLoadAttachment() to load it
            i.attachmentNo = valN;
            continue;
        }
    }
}

FileArgs::~FileArgs() {
    str::FreePtr(&origPath);
    str::FreePtr(&cleanPath);
    str::FreePtr(&destName);
    str::FreePtr(&search);
}

// given file path `foo.pdf?page=4;dest=foo` etc., extract `?page=4;dest=foo`
// args into FileArgs
// returns nullptr if there are not args
FileArgs* ParseFileArgs(const char* path) {
    const char* hashPos = str::FindCharLast(path, '?');
    if (!hashPos) {
        return nullptr;
    }
    // don't mutilate long file paths that start with "\\?\"
    int off = (int)(hashPos - path);
    if (off == 2) {
        return nullptr;
    }
    FileArgs* res = new FileArgs();
    res->origPath = str::Dup(path);
    char* s = str::DupTemp(path);
    size_t n = hashPos - path;
    res->cleanPath = str::Dup(s, n);
    ParseAdobeFlags(*res, hashPos + 1);
    return res;
}

/* parse argument list. we assume that all unrecognized arguments are file names. */
void ParseFlags(const WCHAR* cmdLine, Flags& i) {
    // logf("ParseFlags: cmdLine: '%s'\n", ToUtf8Temp(cmdLine));
    CmdLineArgsIter args(cmdLine);

    const char* param = nullptr;
    int paramInt = 0;

    for (const char* argName = args.NextArg(); argName != nullptr; argName = args.NextArg()) {
        // we register SumatraPDF with "%1" "%2" "%3" "%4"
        // for some reason that makes Directory Opus "Open With" provide the file twice
        // and gives "%3" and "%4' on cmd-line.
        // this is a hack to ignore that
        if (str::Eq(argName, "%2") || str::Eq(argName, "%3") || str::Eq(argName, "%4")) {
            logf("ParseFlags: skipping '%s'\n", argName);
            continue;
        }
        Arg arg = GetArg(argName);
        if (arg == Arg::Unknown) {
            goto CollectFile;
        }
        logf("ParseFlags: argName: '%s', arg: %d\n", argName, (int)arg);

        if (arg == Arg::Silent || arg == Arg::Silent2) {
            // silences errors happening during -print-to and -print-to-default
            i.silent = true;
            continue;
        }
        if (arg == Arg::PrintToDefault) {
            i.printerName = str::Dup(GetDefaultPrinterNameTemp());
            if (!i.printerName) {
                i.printDialog = true;
            }
            i.exitWhenDone = true;
            continue;
        }
        if (arg == Arg::PrintDialog) {
            i.printDialog = true;
            continue;
        }
        if (arg == Arg::Help || arg == Arg::Help2 || arg == Arg::Help3) {
            i.showHelp = true;
            continue;
        }
        if (arg == Arg::ExitWhenDone || arg == Arg::ExitOnPrint) {
            // only affects -print-dialog (-print-to and -print-to-default
            // always exit on print) and -stress-test (useful for profiling)
            i.exitWhenDone = true;
            continue;
        }
        if (arg == Arg::Restrict) {
            i.restrictedUse = true;
            continue;
        }
        if (arg == Arg::Presentation) {
            i.enterPresentation = true;
            continue;
        }
        if (arg == Arg::FullScreen) {
            i.enterFullScreen = true;
            continue;
        }
        if (arg == Arg::InvertColors || arg == Arg::InvertColors2) {
            // -invertcolors is for backwards compat (was used pre-1.3)
            // -invert-colors is for consistency
            // -invert-colors used to be a shortcut for -set-color-range 0xFFFFFF 0x000000
            // now it non-permanently swaps textColor and backgroundColor
            i.invertColors = true;
            continue;
        }
        if (arg == Arg::Console) {
            i.showConsole = true;
            continue;
        }
        if (arg == Arg::Install) {
            i.install = true;
            continue;
        }
        if (arg == Arg::FastInstall) {
            i.fastInstall = true;
            continue;
        }
        if (arg == Arg::UnInstall) {
            i.uninstall = true;
            continue;
        }
        if (arg == Arg::WithFilter || arg == Arg::WithSearch) {
            i.withFilter = true;
            continue;
        }
        if (arg == Arg::WithPreview) {
            i.withPreview = true;
            continue;
        }
        if (arg == Arg::Rand) {
            i.stressRandomizeFiles = true;
            continue;
        }
        if (arg == Arg::Regress) {
            i.regress = true;
            continue;
        }
        if (arg == Arg::Extract) {
            i.justExtractFiles = true;
            continue;
        }
        if (arg == Arg::Tester) {
            i.tester = true;
            continue;
        }
        if (arg == Arg::TestApp) {
            i.testApp = true;
            continue;
        }
        if (arg == Arg::NewWindow) {
            i.inNewWindow = true;
            continue;
        }
        if (arg == Arg::Log) {
            i.log = true;
            continue;
        }
        if (arg == Arg::RunInstallNow) {
            i.runInstallNow = true;
            continue;
        }
        if ((arg == Arg::AllUsers) || (arg == Arg::AllUsers2)) {
            i.allUsers = true;
            continue;
        }
        if (arg == Arg::CrashOnOpen) {
            // to make testing of crash reporting system in pre-release/release
            // builds possible
            i.crashOnOpen = true;
            continue;
        }
        if (arg == Arg::ReuseInstance) {
            // for backwards compatibility, -reuse-instance reuses whatever
            // instance has registered as DDE server
            i.reuseDdeInstance = true;
            continue;
        }
        if (arg == Arg::EscToExit) {
            i.globalPrefArgs.Append(argName);
            continue;
        }
        if ((arg == Arg::ArgEnumPrinters) || (arg == Arg::ListPrinters)) {
            EnumeratePrinters();
            /* this is for testing only, exit immediately */
            i.exitImmediately = true;
            return;
        }
        param = args.EatParam();
        // following args require at least one param
        // if no params here, assume this is a file
        if (nullptr == param) {
            // argName starts with '-' but there are no params after that and it's not
            // one of the args without params, so assume this is a file that starts with '-'
            goto CollectFile;
        }
        paramInt = atoi(param);

        if (arg == Arg::SleepMs) {
            i.sleepMs = paramInt;
            continue;
        }

        if (arg == Arg::PrintTo) {
            i.printerName = str::Dup(param);
            i.exitWhenDone = true;
            continue;
        }
        if (arg == Arg::PrintSettings) {
            // argument is a comma separated list of page ranges and
            // advanced options [even|odd], [noscale|shrink|fit] and [autorotation|portrait|landscape] and
            // disable-auto-rotation e.g. -print-settings "1-3,5,10-8,odd,fit"
            i.printSettings = str::Dup(param);
            str::RemoveCharsInPlace(i.printSettings, " ");
            str::TransCharsInPlace(i.printSettings, ";", ",");
            continue;
        }
        if (arg == Arg::InverseSearch) {
            i.inverseSearchCmdLine = str::Dup(param);
            continue;
        }
        if ((arg == Arg::ForwardSearch1 || arg == Arg::ForwardSearch2) && args.AdditionalParam(1)) {
            // -forward-search is for consistency with -inverse-search
            // -fwdsearch is for consistency with -fwdsearch-*
            i.forwardSearchOrigin = str::Dup(param);
            i.forwardSearchLine = atoi(args.EatParam());
            continue;
        }
        if (arg == Arg::NamedDest || arg == Arg::NamedDest2) {
            // -nameddest is for backwards compat (was used pre-1.3)
            // -named-dest is for consistency
            i.destName = str::Dup(param);
            continue;
        }
        if (arg == Arg::Page) {
            i.pageNumber = paramInt;
            continue;
        }
        if (arg == Arg::View) {
            ParseViewMode(&i.startView, param);
            continue;
        }
        if (arg == Arg::Zoom) {
            ParseZoomValue(&i.startZoom, param);
            continue;
        }
        if (arg == Arg::Scroll) {
            ParseScrollValue(&i.startScroll, param);
            continue;
        }
        if (arg == Arg::AppData) {
            i.appdataDir = str::Dup(param);
            continue;
        }
        if (arg == Arg::Plugin) {
            // -plugin [<URL>] <parent HWND>
            // <parent HWND> is a (numeric) window handle to
            // become the parent of a frameless SumatraPDF
            // (used e.g. for embedding it into a browser plugin)
            if (args.AdditionalParam(1) && !str::IsDigit(*param)) {
                i.pluginURL = str::Dup(param);
                i.hwndPluginParent = (HWND)(INT_PTR)atol(args.EatParam());
            } else {
                i.hwndPluginParent = (HWND)(INT_PTR)atol(param);
            }
            continue;
        }

        if (arg == Arg::StressTest) {
            // -stress-test <file or dir path> [<file filter>] [<page/file range(s)>] [<cycle
            // count>x]
            // e.g. -stress-test file.pdf 25x    for rendering file.pdf 25 times
            //      -stress-test file.pdf 1-3  render only pages 1, 2 and 3 of file.pdf
            //      -stress-test dir 301-  2x  render all files in dir twice, skipping first 300
            //      -stress-test dir *.pdf;*.xps  render all files in dir that are either PDF or XPS
            i.stressTestPath = str::Dup(param);
            const char* s = args.AdditionalParam(1);
            if (s && str::FindChar(s, '*')) {
                i.stressTestFilter = str::Dup(args.EatParam());
                s = args.AdditionalParam(1);
            }
            if (s && IsValidPageRange(s)) {
                i.stressTestRanges = str::Dup(args.EatParam());
                s = args.AdditionalParam(1);
            }
            int num;
            if (s && str::Parse(s, "%dx%$", &num) && num > 0) {
                i.stressTestCycles = num;
                args.EatParam();
            }
            continue;
        }

        if (arg == Arg::N) {
            i.stressParallelCount = paramInt;
            continue;
        }
        if (arg == Arg::Max) {
            i.stressTestMax = paramInt;
            continue;
        }
        if (arg == Arg::MaxFiles) {
            i.maxFiles = paramInt;
            continue;
        }
        if (arg == Arg::Render) {
            i.testRenderPage = true;
            i.pageNumber = paramInt;
            continue;
        }
        if (arg == Arg::ExtractText) {
            i.testExtractPage = true;
            i.pageNumber = paramInt;
            continue;
        }
        if (arg == Arg::Bench) {
            i.pathsToBenchmark.Append(param);
            const char* s = args.AdditionalParam(1);
            if (s && IsBenchPagesInfo(s)) {
                s = args.EatParam();
                i.pathsToBenchmark.Append(s);
            } else {
                // pathsToBenchmark are always in pairs
                // i.e. path + page spec
                // if page spec is missing, we do nullptr
                i.pathsToBenchmark.Append(nullptr);
            }
            i.exitImmediately = true;
            continue;
        }
        if (arg == Arg::Dir || arg == Arg::InstallDir) {
            i.installDir = str::Dup(param);
            continue;
        }
        if (arg == Arg::DDE) {
            i.dde = str::Dup(param);
            continue;
        }
        if (arg == Arg::Lang) {
            // TODO: remove the following deprecated options within
            // a release or two
            i.lang = str::Dup(param);
            continue;
        }
        if (arg == Arg::UpdateSelfTo) {
            i.updateSelfTo = str::Dup(param);
            continue;
        }
        if (arg == Arg::ArgDeleteFile) {
            i.deleteFile = str::Dup(param);
            continue;
        }
        if (arg == Arg::Search) {
            i.search = str::Dup(param);
            continue;
        }
        if (arg == Arg::Adobe) {
            FileArgs fargs;
            ParseAdobeFlags(fargs, param);
            i.search = fargs.search ? str::Dup(fargs.search) : i.search;
            i.destName = fargs.destName ? str::Dup(fargs.destName) : i.destName;
            i.pageNumber = fargs.pageNumber > 0 ? fargs.pageNumber : i.pageNumber;
            // TODO: annotAttObjNum and attachmentNo?
            continue;
        }
        if (arg == Arg::BgCol || arg == Arg::BgCol2 || arg == Arg::FwdSearchOffset || arg == Arg::FwdSearchWidth ||
            arg == Arg::FwdSearchColor || arg == Arg::FwdSearchPermanent || arg == Arg::MangaMode) {
            i.globalPrefArgs.Append(argName);
            i.globalPrefArgs.Append(param);
            continue;
        }
        if (arg == Arg::SetColorRange && args.AdditionalParam(1)) {
            i.globalPrefArgs.Append(argName);
            i.globalPrefArgs.Append(param);
            i.globalPrefArgs.Append(args.EatParam());
            continue;
        }
        if (arg == Arg::PreviewPipe) {
            i.previewPipeName = str::Dup(param);
            continue;
        }
        if (arg == Arg::IFilterPipe) {
            i.ifilterPipeName = str::Dup(param);
            continue;
        }
        if (arg == Arg::TestPreviewPipe) {
            i.testPreviewPipePath = str::Dup(param);
            continue;
        }
        // again, argName is any of the known args, so assume it's a file starting with '-'
        args.RewindParam();

    CollectFile:
        // TODO: resolve .lnk when opening file
        const char* filePath = argName;
        if (str::EndsWithI(filePath, ".lnk")) {
            filePath = ResolveLnkTemp(argName);
        }
        if (filePath) { // resolve might fail
            i.fileNames.Append(filePath);
        }
    }

    if (i.justExtractFiles) {
        // silently extract files to directory given if /d
        // or current directory if no /d given
        i.silent = true;
        if (!i.installDir) {
            i.installDir = str::Dup(".");
        }
    }
}

Flags::~Flags() {
    str::Free(printerName);
    str::Free(printSettings);
    str::Free(forwardSearchOrigin);
    str::Free(destName);
    str::Free(pluginURL);
    str::Free(appdataDir);
    str::Free(inverseSearchCmdLine);
    str::Free(stressTestPath);
    str::Free(stressTestFilter);
    str::Free(stressTestRanges);
    str::Free(lang);
    str::Free(updateSelfTo);
    str::Free(deleteFile);
    str::Free(search);
    str::Free(dde);
    str::Free(previewPipeName);
    str::Free(ifilterPipeName);
    str::Free(testPreviewPipePath);
}
