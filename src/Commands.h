/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// @gen-start cmd-enum
// clang-format off
enum {
    // commands are integers sent with WM_COMMAND so start them
    // at some number higher than 0
    CmdFirst = 200,
    CmdSeparator = CmdFirst,

    CmdOpenFile = 201,
    CmdClose = 202,
    CmdCloseCurrentDocument = 203,
    CmdCloseOtherTabs = 204,
    CmdCloseTabsToTheRight = 205,
    CmdCloseTabsToTheLeft = 206,
    CmdCloseAllTabs = 207,
    CmdSaveAs = 208,
    CmdPrint = 209,
    CmdShowInFolder = 210,
    CmdRenameFile = 211,
    CmdDeleteFile = 212,
    CmdExit = 213,
    CmdReloadDocument = 214,
    CmdCreateShortcutToFile = 215,
    CmdSendByEmail = 216,
    CmdProperties = 217,
    CmdSinglePageView = 218,
    CmdFacingView = 219,
    CmdBookView = 220,
    CmdToggleContinuousView = 221,
    CmdToggleMangaMode = 222,
    CmdRotateLeft = 223,
    CmdRotateRight = 224,
    CmdToggleBookmarks = 225,
    CmdToggleTableOfContents = 226,
    CmdToggleFullscreen = 227,
    CmdPresentationWhiteBackground = 228,
    CmdPresentationBlackBackground = 229,
    CmdTogglePresentationMode = 230,
    CmdToggleToolbar = 231,
    CmdToggleScrollbars = 232,
    CmdToggleOverlayScrollbar = 233,
    CmdToggleMenuBar = 234,
    CmdToggleUseTabs = 235,
    CmdToggleTabsMru = 236,
    CmdCopySelection = 237,
    CmdTranslateSelectionWithGoogle = 238,
    CmdTranslateSelectionWithDeepL = 239,
    CmdSearchSelectionWithGoogle = 240,
    CmdSearchSelectionWithBing = 241,
    CmdSearchSelectionWithWikipedia = 242,
    CmdSearchSelectionWithGoogleScholar = 243,
    CmdSelectAll = 244,
    CmdNewWindow = 245,
    CmdDuplicateInNewWindow = 246,
    CmdDuplicateInNewTab = 247,
    CmdCopyImage = 248,
    CmdCopyLinkTarget = 249,
    CmdCopyComment = 250,
    CmdCopyFilePath = 251,
    CmdScrollUp = 252,
    CmdScrollDown = 253,
    CmdScrollLeft = 254,
    CmdScrollRight = 255,
    CmdScrollLeftPage = 256,
    CmdScrollRightPage = 257,
    CmdScrollUpPage = 258,
    CmdScrollDownPage = 259,
    CmdScrollDownHalfPage = 260,
    CmdScrollUpHalfPage = 261,
    CmdGoToNextPage = 262,
    CmdGoToPrevPage = 263,
    CmdGoToFirstPage = 264,
    CmdGoToLastPage = 265,
    CmdGoToPage = 266,
    CmdFindFirst = 267,
    CmdFindNext = 268,
    CmdFindPrev = 269,
    CmdFindNextSel = 270,
    CmdFindPrevSel = 271,
    CmdFindToggleMatchCase = 272,
    CmdSaveAnnotations = 273,
    CmdSaveAnnotationsNewFile = 274,
    CmdEditAnnotations = 275,
    CmdDeleteAnnotation = 276,
    CmdZoomFitPage = 277,
    CmdZoomActualSize = 278,
    CmdZoomFitWidth = 279,
    CmdZoom6400 = 280,
    CmdZoom3200 = 281,
    CmdZoom1600 = 282,
    CmdZoom800 = 283,
    CmdZoom400 = 284,
    CmdZoom200 = 285,
    CmdZoom150 = 286,
    CmdZoom125 = 287,
    CmdZoom100 = 288,
    CmdZoom50 = 289,
    CmdZoom25 = 290,
    CmdZoom12_5 = 291,
    CmdZoom8_33 = 292,
    CmdZoomFitContent = 293,
    CmdZoomCustom = 294,
    CmdZoomIn = 295,
    CmdZoomOut = 296,
    CmdZoomFitWidthAndContinuous = 297,
    CmdZoomFitPageAndSinglePage = 298,
    CmdContributeTranslation = 299,
    CmdOpenWithKnownExternalViewerFirst = 300,
    CmdOpenWithExplorer = 301,
    CmdOpenWithDirectoryOpus = 302,
    CmdOpenWithTotalCommander = 303,
    CmdOpenWithDoubleCommander = 304,
    CmdOpenWithAcrobat = 305,
    CmdOpenWithFoxIt = 306,
    CmdOpenWithFoxItPhantom = 307,
    CmdOpenWithPdfXchange = 308,
    CmdOpenWithXpsViewer = 309,
    CmdOpenWithHtmlHelp = 310,
    CmdOpenWithPdfDjvuBookmarker = 311,
    CmdOpenWithKnownExternalViewerLast = 312,
    CmdOpenSelectedDocument = 313,
    CmdPinSelectedDocument = 314,
    CmdForgetSelectedDocument = 315,
    CmdExpandAll = 316,
    CmdCollapseAll = 317,
    CmdSaveEmbeddedFile = 318,
    CmdOpenEmbeddedPDF = 319,
    CmdSaveAttachment = 320,
    CmdOpenAttachment = 321,
    CmdOptions = 322,
    CmdAdvancedOptions = 323,
    CmdAdvancedSettings = 324,
    CmdChangeLanguage = 325,
    CmdCheckUpdate = 326,
    CmdHelpOpenManual = 327,
    CmdHelpOpenManualOnWebsite = 328,
    CmdHelpOpenKeyboardShortcuts = 329,
    CmdHelpVisitWebsite = 330,
    CmdHelpAbout = 331,
    CmdMoveFrameFocus = 332,
    CmdFavoriteAdd = 333,
    CmdFavoriteDel = 334,
    CmdFavoriteToggle = 335,
    CmdToggleLinks = 336,
    CmdToggleShowAnnotations = 337,
    CmdShowAnnotations = 338,
    CmdHideAnnotations = 339,
    CmdCreateAnnotText = 340,
    CmdCreateAnnotLink = 341,
    CmdCreateAnnotFreeText = 342,
    CmdCreateAnnotLine = 343,
    CmdCreateAnnotSquare = 344,
    CmdCreateAnnotCircle = 345,
    CmdCreateAnnotPolygon = 346,
    CmdCreateAnnotPolyLine = 347,
    CmdCreateAnnotHighlight = 348,
    CmdCreateAnnotUnderline = 349,
    CmdCreateAnnotSquiggly = 350,
    CmdCreateAnnotStrikeOut = 351,
    CmdCreateAnnotRedact = 352,
    CmdCreateAnnotStamp = 353,
    CmdCreateAnnotCaret = 354,
    CmdCreateAnnotInk = 355,
    CmdCreateAnnotPopup = 356,
    CmdCreateAnnotFileAttachment = 357,
    CmdInvertColors = 358,
    CmdTogglePageInfo = 359,
    CmdToggleZoom = 360,
    CmdNavigateBack = 361,
    CmdNavigateForward = 362,
    CmdToggleCursorPosition = 363,
    CmdOpenNextFileInFolder = 364,
    CmdOpenPrevFileInFolder = 365,
    CmdCommandPalette = 366,
    CmdShowLog = 367,
    CmdShowPdfInfo = 368,
    CmdShowErrors = 369,
    CmdClearHistory = 370,
    CmdReopenLastClosedFile = 371,
    CmdNextTab = 372,
    CmdPrevTab = 373,
    CmdNextTabSmart = 374,
    CmdPrevTabSmart = 375,
    CmdMoveTabLeft = 376,
    CmdMoveTabRight = 377,
    CmdSelectNextTheme = 378,
    CmdToggleFrequentlyRead = 379,
    CmdInvokeInverseSearch = 380,
    CmdExec = 381,
    CmdViewWithExternalViewer = 382,
    CmdSelectionHandler = 383,
    CmdSetTheme = 384,
    CmdToggleInverseSearch = 385,
    CmdDebugCorruptMemory = 386,
    CmdDebugCrashMe = 387,
    CmdDebugDownloadSymbols = 388,
    CmdDebugTestApp = 389,
    CmdDebugShowNotif = 390,
    CmdDebugStartStressTest = 391,
    CmdDebugTogglePredictiveRender = 392,
    CmdDebugToggleRtl = 393,
    CmdToggleAntiAlias = 394,
    CmdToggleSmoothScroll = 395,
    CmdToggleHideScrollbar = 396,
    CmdToggleScrollbarInSinglePage = 397,
    CmdToggleLazyLoading = 398,
    CmdListPrinters = 399,
    CmdToggleWindowsPreviewer = 400,
    CmdToggleWindowsSearchFilter = 401,
    CmdScreenshot = 402,
    CmdCropImage = 403,
    CmdResizeImage = 404,
    CmdSaveImage = 405,
    CmdPasteClipboardImage = 406,
    CmdNone = 407,

    /* range for file history */
    CmdFileHistoryFirst,
    CmdFileHistoryLast = CmdFileHistoryFirst + 32,

    /* range for favorites */
    CmdFavoriteFirst,
    CmdFavoriteLast = CmdFavoriteFirst + 256,

    CmdLast = CmdFavoriteLast,
    CmdFirstCustom = CmdLast + 100,

    // aliases, at the end to not mess ordering
    CmdViewLayoutFirst = CmdSinglePageView,
    CmdViewLayoutLast = CmdToggleMangaMode,

    CmdZoomFirst = CmdZoomFitPage,
    CmdZoomLast = CmdZoomCustom,

    CmdCreateAnnotFirst = CmdCreateAnnotText,
    CmdCreateAnnotLast = CmdCreateAnnotFileAttachment,
};
// clang-format on
// @gen-end cmd-enum

// order of CreateAnnot* must be the same as enum AnnotationType
/*
TOOD: maybe add commands for those annotations
Sound,
Movie,
Widget,
Screen,
PrinterMark,
TrapNet,
Watermark,
ThreeD,
*/

struct CommandArg {
    enum class Type : u16 {
        None,
        Bool,
        Int,
        Float,
        String,
        Color,
    };

    // arguments are a linked list for simplicity
    struct CommandArg* next = nullptr;

    Type type = Type::None;

    // TODO: we have a fixed number of argument names
    // we could use seqstrings and use u16 for arg name id
    const char* name = nullptr;

    // TODO: could be a union
    const char* strVal = nullptr;
    bool boolVal = false;
    int intVal = 0;
    float floatVal = 0.0;
    ParsedColor colorVal;

    CommandArg() = default;
    ~CommandArg();
};

void FreeCommandArgs(CommandArg* first);

struct CustomCommand {
    // all commands are stored as linked list
    struct CustomCommand* next = nullptr;

    // the command id like CmdOpenFile
    int origId = 0;

    // for debugging, the full definition of the command
    // as given by the user
    const char* definition = nullptr;

    // optional name, if given this shows up in command palette
    const char* name = nullptr;

    // optional keyboard shortcut
    const char* key = nullptr;

    // a unique command id generated by us, starting with CmdFirstCustom
    // it identifies a command with their fixed set of arguments
    int id = 0;

    // optional
    const char* idStr = nullptr;

    CommandArg* firstArg = nullptr;
    CustomCommand() = default;
    ~CustomCommand();
};

extern CustomCommand* gFirstCustomCommand;
extern SeqStrings gCommandDescriptions;

int GetCommandIdByName(const char*);
int GetCommandIdByDesc(const char*);

CustomCommand* CreateCustomCommand(const char* definition, int origCmdId, CommandArg* args);
CustomCommand* FindCustomCommand(int cmdId);
void FreeCustomCommands();
CommandArg* NewStringArg(const char* name, const char* val);
CommandArg* NewFloatArg(const char* name, float val);
void InsertArg(CommandArg** firstPtr, CommandArg* arg);

CustomCommand* CreateCommandFromDefinition(const char* definition);
CommandArg* GetCommandArg(CustomCommand*, const char* argName);
int GetCommandIntArg(CustomCommand* cmd, const char* name, int defValue);
bool GetCommandBoolArg(CustomCommand* cmd, const char* name, bool defValue);
const char* GetCommandStringArg(CustomCommand* cmd, const char* name, const char* defValue);
void GetCommandsWithOrigId(Vec<CustomCommand*>& commands, int origId);

constexpr const char* kCmdArgColor = "color";
constexpr const char* kCmdArgBgColor = "bgcolor";
constexpr const char* kCmdArgOpacity = "opacity";
constexpr const char* kCmdArgOpenEdit = "openedit";
constexpr const char* kCmdArgTextSize = "textsize";
constexpr const char* kCmdArgBorderWidth = "borderwidth";
constexpr const char* kCmdArgInteriorColor = "interiorcolor";

constexpr const char* kCmdArgCopyToClipboard = "copytoclipboard";
constexpr const char* kCmdArgSetContent = "setcontent";
constexpr const char* kCmdArgExe = "exe";
constexpr const char* kCmdArgURL = "url";
constexpr const char* kCmdArgLevel = "level";
constexpr const char* kCmdArgFilter = "filter";
constexpr const char* kCmdArgN = "n";
constexpr const char* kCmdArgMode = "mode";
constexpr const char* kCmdArgTheme = "theme";
constexpr const char* kCmdArgCommandLine = "cmdline";
constexpr const char* kCmdArgToolbarText = "toolbartext";
constexpr const char* kCmdArgFocusEdit = "focusedit";
constexpr const char* kCmdArgFocusList = "focuslist";
