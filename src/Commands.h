/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// @gen-start cmd-enum
// clang-format off
enum {
    // commands are integers sent with WM_COMMAND so start them
    // at some number higher than 0
    CmdFirst = 200,
    CmdSeparator = CmdFirst,

    CmdOpenFile = 201, CmdClose = 202, CmdCloseCurrentDocument = 203,
    CmdCloseOtherTabs = 204, CmdCloseTabsToTheRight = 205, CmdCloseTabsToTheLeft = 206,
    CmdCloseAllTabs = 207, CmdSaveAs = 208, CmdPrint = 209,
    CmdShowInFolder = 210, CmdRenameFile = 211, CmdDeleteFile = 212,
    CmdExit = 213, CmdReloadDocument = 214, CmdCreateShortcutToFile = 215,
    CmdSendByEmail = 216, CmdProperties = 217, CmdSinglePageView = 218,
    CmdFacingView = 219, CmdBookView = 220, CmdToggleContinuousView = 221,
    CmdToggleMangaMode = 222, CmdRotateLeft = 223, CmdRotateRight = 224,
    CmdToggleBookmarks = 225, CmdToggleTableOfContents = 226, CmdToggleFullscreen = 227,
    CmdPresentationWhiteBackground = 228, CmdPresentationBlackBackground = 229, CmdTogglePresentationMode = 230,
    CmdToggleToolbar = 231, CmdToggleScrollbars = 232, CmdToggleOverlayScrollbar = 233,
    CmdToggleMenuBar = 234, CmdCopySelection = 235, CmdTranslateSelectionWithGoogle = 236,
    CmdTranslateSelectionWithDeepL = 237, CmdSearchSelectionWithGoogle = 238, CmdSearchSelectionWithBing = 239,
    CmdSearchSelectionWithWikipedia = 240, CmdSearchSelectionWithGoogleScholar = 241, CmdSelectAll = 242,
    CmdNewWindow = 243, CmdDuplicateInNewWindow = 244, CmdDuplicateInNewTab = 245,
    CmdCopyImage = 246, CmdCopyLinkTarget = 247, CmdCopyComment = 248,
    CmdCopyFilePath = 249, CmdScrollUp = 250, CmdScrollDown = 251,
    CmdScrollLeft = 252, CmdScrollRight = 253, CmdScrollLeftPage = 254,
    CmdScrollRightPage = 255, CmdScrollUpPage = 256, CmdScrollDownPage = 257,
    CmdScrollDownHalfPage = 258, CmdScrollUpHalfPage = 259, CmdGoToNextPage = 260,
    CmdGoToPrevPage = 261, CmdGoToFirstPage = 262, CmdGoToLastPage = 263,
    CmdGoToPage = 264, CmdFindFirst = 265, CmdFindNext = 266,
    CmdFindPrev = 267, CmdFindNextSel = 268, CmdFindPrevSel = 269,
    CmdFindToggleMatchCase = 270, CmdSaveAnnotations = 271, CmdSaveAnnotationsNewFile = 272,
    CmdEditAnnotations = 273, CmdDeleteAnnotation = 274, CmdZoomFitPage = 275,
    CmdZoomActualSize = 276, CmdZoomFitWidth = 277, CmdZoom6400 = 278,
    CmdZoom3200 = 279, CmdZoom1600 = 280, CmdZoom800 = 281,
    CmdZoom400 = 282, CmdZoom200 = 283, CmdZoom150 = 284,
    CmdZoom125 = 285, CmdZoom100 = 286, CmdZoom50 = 287,
    CmdZoom25 = 288, CmdZoom12_5 = 289, CmdZoom8_33 = 290,
    CmdZoomFitContent = 291, CmdZoomCustom = 292, CmdZoomIn = 293,
    CmdZoomOut = 294, CmdZoomFitWidthAndContinuous = 295, CmdZoomFitPageAndSinglePage = 296,
    CmdContributeTranslation = 297, CmdOpenWithKnownExternalViewerFirst = 298, CmdOpenWithExplorer = 299,
    CmdOpenWithDirectoryOpus = 300, CmdOpenWithTotalCommander = 301, CmdOpenWithDoubleCommander = 302,
    CmdOpenWithAcrobat = 303, CmdOpenWithFoxIt = 304, CmdOpenWithFoxItPhantom = 305,
    CmdOpenWithPdfXchange = 306, CmdOpenWithXpsViewer = 307, CmdOpenWithHtmlHelp = 308,
    CmdOpenWithPdfDjvuBookmarker = 309, CmdOpenWithKnownExternalViewerLast = 310, CmdOpenSelectedDocument = 311,
    CmdPinSelectedDocument = 312, CmdForgetSelectedDocument = 313, CmdExpandAll = 314,
    CmdCollapseAll = 315, CmdSaveEmbeddedFile = 316, CmdOpenEmbeddedPDF = 317,
    CmdSaveAttachment = 318, CmdOpenAttachment = 319, CmdOptions = 320,
    CmdAdvancedOptions = 321, CmdAdvancedSettings = 322, CmdChangeLanguage = 323,
    CmdCheckUpdate = 324, CmdHelpOpenManual = 325, CmdHelpOpenManualOnWebsite = 326,
    CmdHelpOpenKeyboardShortcuts = 327, CmdHelpVisitWebsite = 328, CmdHelpAbout = 329,
    CmdMoveFrameFocus = 330, CmdFavoriteAdd = 331, CmdFavoriteDel = 332,
    CmdFavoriteToggle = 333, CmdToggleLinks = 334, CmdToggleShowAnnotations = 335,
    CmdShowAnnotations = 336, CmdHideAnnotations = 337, CmdCreateAnnotText = 338,
    CmdCreateAnnotLink = 339, CmdCreateAnnotFreeText = 340, CmdCreateAnnotLine = 341,
    CmdCreateAnnotSquare = 342, CmdCreateAnnotCircle = 343, CmdCreateAnnotPolygon = 344,
    CmdCreateAnnotPolyLine = 345, CmdCreateAnnotHighlight = 346, CmdCreateAnnotUnderline = 347,
    CmdCreateAnnotSquiggly = 348, CmdCreateAnnotStrikeOut = 349, CmdCreateAnnotRedact = 350,
    CmdCreateAnnotStamp = 351, CmdCreateAnnotCaret = 352, CmdCreateAnnotInk = 353,
    CmdCreateAnnotPopup = 354, CmdCreateAnnotFileAttachment = 355, CmdInvertColors = 356,
    CmdTogglePageInfo = 357, CmdToggleZoom = 358, CmdNavigateBack = 359,
    CmdNavigateForward = 360, CmdToggleCursorPosition = 361, CmdOpenNextFileInFolder = 362,
    CmdOpenPrevFileInFolder = 363, CmdCommandPalette = 364, CmdShowLog = 365,
    CmdShowPdfInfo = 366, CmdClearHistory = 367, CmdReopenLastClosedFile = 368,
    CmdNextTab = 369, CmdPrevTab = 370, CmdNextTabSmart = 371,
    CmdPrevTabSmart = 372, CmdMoveTabLeft = 373, CmdMoveTabRight = 374,
    CmdSelectNextTheme = 375, CmdToggleFrequentlyRead = 376, CmdInvokeInverseSearch = 377,
    CmdExec = 378, CmdViewWithExternalViewer = 379, CmdSelectionHandler = 380,
    CmdSetTheme = 381, CmdToggleInverseSearch = 382, CmdDebugCorruptMemory = 383,
    CmdDebugCrashMe = 384, CmdDebugDownloadSymbols = 385, CmdDebugTestApp = 386,
    CmdDebugShowNotif = 387, CmdDebugStartStressTest = 388, CmdDebugTogglePredictiveRender = 389,
    CmdDebugToggleRtl = 390, CmdToggleAntiAlias = 391, CmdNone = 392,

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
