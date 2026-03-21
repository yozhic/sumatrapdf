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
    CmdToggleToolbar = 231, CmdToggleScrollbars = 232, CmdToggleMenuBar = 233,
    CmdCopySelection = 234, CmdTranslateSelectionWithGoogle = 235, CmdTranslateSelectionWithDeepL = 236,
    CmdSearchSelectionWithGoogle = 237, CmdSearchSelectionWithBing = 238, CmdSearchSelectionWithWikipedia = 239,
    CmdSearchSelectionWithGoogleScholar = 240, CmdSelectAll = 241, CmdNewWindow = 242,
    CmdDuplicateInNewWindow = 243, CmdDuplicateInNewTab = 244, CmdCopyImage = 245,
    CmdCopyLinkTarget = 246, CmdCopyComment = 247, CmdCopyFilePath = 248,
    CmdScrollUp = 249, CmdScrollDown = 250, CmdScrollLeft = 251,
    CmdScrollRight = 252, CmdScrollLeftPage = 253, CmdScrollRightPage = 254,
    CmdScrollUpPage = 255, CmdScrollDownPage = 256, CmdScrollDownHalfPage = 257,
    CmdScrollUpHalfPage = 258, CmdGoToNextPage = 259, CmdGoToPrevPage = 260,
    CmdGoToFirstPage = 261, CmdGoToLastPage = 262, CmdGoToPage = 263,
    CmdFindFirst = 264, CmdFindNext = 265, CmdFindPrev = 266,
    CmdFindNextSel = 267, CmdFindPrevSel = 268, CmdFindToggleMatchCase = 269,
    CmdSaveAnnotations = 270, CmdSaveAnnotationsNewFile = 271, CmdEditAnnotations = 272,
    CmdDeleteAnnotation = 273, CmdZoomFitPage = 274, CmdZoomActualSize = 275,
    CmdZoomFitWidth = 276, CmdZoom6400 = 277, CmdZoom3200 = 278,
    CmdZoom1600 = 279, CmdZoom800 = 280, CmdZoom400 = 281,
    CmdZoom200 = 282, CmdZoom150 = 283, CmdZoom125 = 284,
    CmdZoom100 = 285, CmdZoom50 = 286, CmdZoom25 = 287,
    CmdZoom12_5 = 288, CmdZoom8_33 = 289, CmdZoomFitContent = 290,
    CmdZoomCustom = 291, CmdZoomIn = 292, CmdZoomOut = 293,
    CmdZoomFitWidthAndContinuous = 294, CmdZoomFitPageAndSinglePage = 295, CmdContributeTranslation = 296,
    CmdOpenWithKnownExternalViewerFirst = 297, CmdOpenWithExplorer = 298, CmdOpenWithDirectoryOpus = 299,
    CmdOpenWithTotalCommander = 300, CmdOpenWithDoubleCommander = 301, CmdOpenWithAcrobat = 302,
    CmdOpenWithFoxIt = 303, CmdOpenWithFoxItPhantom = 304, CmdOpenWithPdfXchange = 305,
    CmdOpenWithXpsViewer = 306, CmdOpenWithHtmlHelp = 307, CmdOpenWithPdfDjvuBookmarker = 308,
    CmdOpenWithKnownExternalViewerLast = 309, CmdOpenSelectedDocument = 310, CmdPinSelectedDocument = 311,
    CmdForgetSelectedDocument = 312, CmdExpandAll = 313, CmdCollapseAll = 314,
    CmdSaveEmbeddedFile = 315, CmdOpenEmbeddedPDF = 316, CmdSaveAttachment = 317,
    CmdOpenAttachment = 318, CmdOptions = 319, CmdAdvancedOptions = 320,
    CmdAdvancedSettings = 321, CmdChangeLanguage = 322, CmdCheckUpdate = 323,
    CmdHelpOpenManual = 324, CmdHelpOpenManualOnWebsite = 325, CmdHelpOpenKeyboardShortcuts = 326,
    CmdHelpVisitWebsite = 327, CmdHelpAbout = 328, CmdMoveFrameFocus = 329,
    CmdFavoriteAdd = 330, CmdFavoriteDel = 331, CmdFavoriteToggle = 332,
    CmdToggleLinks = 333, CmdToggleShowAnnotations = 334, CmdShowAnnotations = 335,
    CmdHideAnnotations = 336, CmdCreateAnnotText = 337, CmdCreateAnnotLink = 338,
    CmdCreateAnnotFreeText = 339, CmdCreateAnnotLine = 340, CmdCreateAnnotSquare = 341,
    CmdCreateAnnotCircle = 342, CmdCreateAnnotPolygon = 343, CmdCreateAnnotPolyLine = 344,
    CmdCreateAnnotHighlight = 345, CmdCreateAnnotUnderline = 346, CmdCreateAnnotSquiggly = 347,
    CmdCreateAnnotStrikeOut = 348, CmdCreateAnnotRedact = 349, CmdCreateAnnotStamp = 350,
    CmdCreateAnnotCaret = 351, CmdCreateAnnotInk = 352, CmdCreateAnnotPopup = 353,
    CmdCreateAnnotFileAttachment = 354, CmdInvertColors = 355, CmdTogglePageInfo = 356,
    CmdToggleZoom = 357, CmdNavigateBack = 358, CmdNavigateForward = 359,
    CmdToggleCursorPosition = 360, CmdOpenNextFileInFolder = 361, CmdOpenPrevFileInFolder = 362,
    CmdCommandPalette = 363, CmdShowLog = 364, CmdShowPdfInfo = 365,
    CmdClearHistory = 366, CmdReopenLastClosedFile = 367, CmdNextTab = 368,
    CmdPrevTab = 369, CmdNextTabSmart = 370, CmdPrevTabSmart = 371,
    CmdMoveTabLeft = 372, CmdMoveTabRight = 373, CmdSelectNextTheme = 374,
    CmdToggleFrequentlyRead = 375, CmdInvokeInverseSearch = 376, CmdExec = 377,
    CmdViewWithExternalViewer = 378, CmdSelectionHandler = 379, CmdSetTheme = 380,
    CmdToggleInverseSearch = 381, CmdDebugCorruptMemory = 382, CmdDebugCrashMe = 383,
    CmdDebugDownloadSymbols = 384, CmdDebugTestApp = 385, CmdDebugShowNotif = 386,
    CmdDebugStartStressTest = 387, CmdDebugTogglePredictiveRender = 388, CmdDebugToggleRtl = 389,
    CmdToggleAntiAlias = 390, CmdNone = 391,

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
