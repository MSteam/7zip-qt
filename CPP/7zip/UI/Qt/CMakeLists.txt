# ============================================================================
# 7-Zip -> Qt/Linux port : milestone C.1a
# Threaded modal-progress core (CProgressSync reused verbatim, CProgressDialog
# and CProgressThreadVirt ported to Qt), proven in isolation by a fake worker.
#
# Builds the 7zqt_progress_demo executable from:
#   * the verbatim CProgressSync extraction (ProgressSync.{h,cpp}),
#   * the ported QtProgressDialog / QtProgressThreadVirt,
#   * the demo main with a fake CProgressThreadVirt-derived worker,
# linked against the WHOLE engine archive + Qt6::Widgets.
#
# The 7-Zip TUs here need the same compile setup as the engine (they include the
# engine's "..."-relative headers): same defines, -O2 -fPIC -Wall -Wextra, NO
# -Werror. AUTOMOC handles the Q_OBJECT classes.
# ============================================================================

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)
# AUTORCC enabled globally up-front (B.6): 7zqt_browser is declared before the
# original (later) set, but now consumes fm_resources.qrc too, so AUTORCC must
# be ON when its add_executable() runs. Safe for all targets (only those that
# list a .qrc compile one).
set(CMAKE_AUTORCC ON)

set(QT_PROGRESS_SOURCES
    ProgressSync.cpp
    QtProgressDialog.cpp
    QtProgressThreadVirt.cpp
    # G.1 i18n : QtProgressDialog now uses FmLang()/QtLang_Get; this standalone
    # demo links neither the agent lib nor 7zqt's loader, so it needs its own
    # CLang loader (engine Lang.cpp, unchanged) + Qt accessors (mirrors 7zqt).
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Common/Lang.cpp
    QtLangLoader.cpp
    main_progress_demo.cpp
)

add_executable(7zqt_progress_demo ${QT_PROGRESS_SOURCES})

# Same defines as the engine, plus Z7_COM_USE_ATOMIC so this TU's view of the
# COM refcount ABI matches the engine (required once COM objects cross threads
# in C.1b; set now for consistency).
target_compile_definitions(7zqt_progress_demo PRIVATE
    NDEBUG
    _REENTRANT
    _FILE_OFFSET_BITS=64
    _LARGEFILE_SOURCE
    Z7_COM_USE_ATOMIC
)

target_compile_options(7zqt_progress_demo PRIVATE -O2 -fPIC -Wall -Wextra)

# Link the WHOLE engine archive (same rationale as 7zz: global static-ctor
# registrations must not be dropped) plus Qt Widgets.
target_link_libraries(7zqt_progress_demo PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,sevenzip_engine>"
    Qt6::Widgets
)

# ============================================================================
# C.5: the UNIFIED Qt/Linux GUI binary (7zqt) — mirrors 7zG.exe (GUI.cpp).
#
# Replaces the four ad-hoc per-flow harnesses (7zqt_extract / 7zqt_compress /
# 7zqt_hash and their main_*.cpp) with ONE binary whose entry (main_7zqt.cpp)
# ports GUI.cpp's Main2() + WinMain try/catch: it parses the REAL command line
# (CArcCmdLineParser / CArcCmdLineOptions, in the engine lib) and dispatches by
# command group to the existing Qt flows:
#   * extract / test (x/e/t) -> QtExtractGUI  (+ QtExtractCallback / dialog / prompts)
#   * update         (a/u/d) -> QtUpdateGUI   (+ QtUpdateCallback / QtCompressDialog)
#   * hash           (h)     -> QtHashGUI     (+ QtHashResultsDialog / QtHashMethodsDialog)
#   * benchmark      (b)     -> DEFERRED (prints "benchmark not implemented")
#
# main_7zqt.cpp is the SINGLE GUID-emitting TU (MyInitGuid.h + engine interface
# headers); the per-flow mains that used to each emit the GUIDs are removed.
#
# Sources = the union of all shared Qt sources (progress core + extract + compress
# + hash + their dialogs/callbacks/GUI flows) + the unified main. Same defines /
# compile options / link as 7zqt_progress_demo.
# ============================================================================
set(QT_7ZQT_SOURCES
    # --- C.1 progress core ---
    ProgressSync.cpp
    QtProgressDialog.cpp
    QtProgressThreadVirt.cpp
    # --- C.1b extract flow ---
    QtExtractCallback.cpp
    QtExtractPrompts.cpp
    QtMemDialog.cpp           # G.8a : decompression memory-limit request (CMemDialog)
    QtExtractGUI.cpp
    QtExtractSettings.cpp
    QtExtractDialog.cpp
    # --- G.3c : open-archive error-flag decoder (GetOpenArcErrorMessage port) ---
    QtOpenArcError.cpp
    # --- C.3 compress flow ---
    QtCompressSettings.cpp
    QtUpdateCallback.cpp
    QtCompressDialog.cpp
    QtCompressOptionsDialog.cpp
    QtCompressGUI.cpp
    # --- C.4 hash flow ---
    QtHashGUI.cpp
    QtHashResultsDialog.cpp
    QtHashMethodsDialog.cpp
    # --- P.2 i18n : CLang loader (engine Lang.cpp, unchanged) + Qt accessors ---
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Common/Lang.cpp
    QtLangLoader.cpp
    # --- G.10a : Benchmark (Tools->Benchmark dialog + CLI 7zqt b) ---
    fm/QtBenchmarkDialog.cpp
    # --- C.5 unified entry / dispatch (GUID home) ---
    main_7zqt.cpp
)

add_executable(7zqt ${QT_7ZQT_SOURCES})

target_compile_definitions(7zqt PRIVATE
    NDEBUG
    _REENTRANT
    _FILE_OFFSET_BITS=64
    _LARGEFILE_SOURCE
    Z7_COM_USE_ATOMIC
)

target_compile_options(7zqt PRIVATE -O2 -fPIC -Wall -Wextra)

target_link_libraries(7zqt PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,sevenzip_engine>"
    Qt6::Widgets
)

# P.2 i18n deploy : stage the translation corpus next to the binary, so the
# runtime GetLangDirPrefix() (= <exe-dir>/Lang/) finds it. The build is
# configured with `cmake -S . -B build` from the repo root, so CMAKE_SOURCE_DIR
# == the repo root and ${CMAKE_SOURCE_DIR}/Lang is the user-placed *.txt corpus.
# `cmake -E copy_directory` is idempotent (it overwrites, never errors if the
# destination already exists). 7zqt and 7zqt_fm share an output directory, but
# each target gets its own rule so any future per-target output dir stays correct.
# GUARD: the corpus is an untracked, user-supplied drop-in (not committed). A fresh
# checkout WITHOUT it must still build green, so the copy is added only when the
# corpus is present at configure time (copy_directory hard-errors on a missing
# source). Re-run cmake after dropping in the corpus to activate the staging.
if(EXISTS "${CMAKE_SOURCE_DIR}/Lang")
add_custom_command(TARGET 7zqt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Lang"
        "$<TARGET_FILE_DIR:7zqt>/Lang"
    COMMENT "P.2 : staging translation corpus -> $<TARGET_FILE_DIR:7zqt>/Lang"
    VERBATIM)
endif()

# ============================================================================
# Milestone B.0 : the Agent / IFolderFolder archive-BROWSING seam on Linux.
#
# sevenzip_agent is a small static lib wrapping the toolkit-agnostic COM layer
# CPP/7zip/UI/Agent/* (CAgent, CAgentFolder, IFolderFolder) - the "archive as a
# folder" model a Qt QAbstractItemModel will sit on. It compiles the engine's
# Agent sources UNCHANGED on Linux via a force-included compat header
# (agent/AgentLinuxCompat.h) that supplies the handful of Win32-only symbols the
# Linux COM shim lacks: INVALID_FILE_ATTRIBUTES, HINSTANCE/HMODULE, UINT64.
#
# Both the BROWSE-path AND the WRITE/UPDATE-path Agent sources are built. The
# WRITE/UPDATE sources (AgentOut.cpp, ArchiveFolderOut.cpp) were coupled to the
# Win32 disk-scan model (CDirItem::Attrib/IsAltStream, FILETIME<->timespec,
# SetFileAttrib, ...); B.5 ported them to Linux with minimal _WIN32-guarded edits
# (see the B.5 note below), so CAgent/CAgentFolder have COMPLETE vtables for both
# browsing and in-place archive update. (The historical E_NOTIMPL placeholder TUs
# agent/AgentOutStubs.cpp / fm/FsFolderStubs.cpp were retired once the real
# ported sources replaced them.) agent/AgentLinuxCompat.cpp supplies the Linux
# g_hInstance + MyLoadString link definitions.
#
# Same defines / flags as the engine. NO Qt dependency (pure COM over engine).
# ============================================================================

# The compat header is force-included into every Agent translation unit so the
# stock engine Agent sources compile unchanged.
set(AGENT_COMPAT_INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/agent/AgentLinuxCompat.h")

set(SEVENZIP_AGENT_SOURCES
    # --- engine Agent layer : BROWSE path (compiled UNCHANGED) ---
    ${CMAKE_CURRENT_SOURCE_DIR}/../Agent/Agent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Agent/AgentProxy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Agent/ArchiveFolder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Agent/ArchiveFolderOpen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Agent/UpdateCallbackAgent.cpp
    # --- B.2 : the FILESYSTEM IFolderFolder (CFSFolder), ported to Linux with
    #     minimal #ifdef _WIN32 guards (all marked "[B.2 Linux port]", Windows
    #     branch verbatim-original - same approach as the Agent.cpp B.0 edit).
    #     This is what lets QtFolderModel bind to the REAL filesystem with the
    #     same IFolderFolder seam it uses for archives. TextPairs.cpp is the
    #     portable descript.ion comment store CFSFolder's _comments uses (it
    #     compiles UNCHANGED on Linux - no guards needed). The Win32-GUI-coupled
    #     siblings AltStreamsFolder.cpp / FSDrives.cpp are NOT compiled (their
    #     symbols are unused on this port); FSFolderCopy.cpp IS compiled below
    #     (B.4a) to make the FS copy/move path real.
    ${CMAKE_CURRENT_SOURCE_DIR}/../FileManager/FSFolder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../FileManager/TextPairs.cpp
    # --- B.4a : the FILESYSTEM COPY/MOVE path (CFSFolder::CopyTo / CopyFrom /
    #     CopyFromFile + CopyFileSystemItems), ported to Linux with #ifdef _WIN32
    #     guards (all marked "[B.4 Linux port]", Windows branch verbatim-original).
    #     The Win32 CopyFileEx/MoveFileWithProgress fast path is guarded out; on
    #     Linux the engine's own portable byte-copy (CCopyStateIO::MyCopyFile) plus
    #     NDir::MyMoveFile/CreateComplexDir/DeleteFileAlways/RemoveDir are used.
    #     Compiling this here makes CFSFolder::CopyTo/CopyFrom/CopyFromFile REAL, so
    #     the B.2 E_NOTIMPL placeholder TU was retired (it is no longer in this
    #     source list, nor present in the tree).
    ${CMAKE_CURRENT_SOURCE_DIR}/../FileManager/FSFolderCopy.cpp
    # --- B.5 : the archive WRITE/UPDATE path, ported to Linux. AgentOut.cpp /
    #     ArchiveFolderOut.cpp REPLACED the now-retired E_NOTIMPL placeholder TU:
    #     they define the same IOutFolderArchive / ISetProperties /
    #     IFolderOperations vtable methods (verified 1:1 against the stub) so the
    #     CAgent / CAgentFolder vtables stay complete and BROWSING still links.
    #     AgentOut.cpp carries 4 minimal "[B.5 Linux port]" guards (alt-stream
    #     prefix-normalize, alt-folder block, di.SetAsDir, GetCurUtc_FiTime);
    #     ArchiveFolderOut.cpp carries 2 (SetFileAttrib, CDirEntry::IsDir). Every
    #     _WIN32 branch is kept byte-identical to upstream.
    #     The engine WorkDir.cpp (CWorkDirTempFile / GetWorkDir, which back the
    #     temp-file archive rewrite in CommonUpdateOperation) compiles UNCHANGED
    #     on Linux; its one otherwise-unresolved symbol NWorkDir::CInfo::Load()
    #     (defined solely in the Win32-registry ZipRegistry.cpp) is supplied by
    #     the additive agent/WorkDirSettings.cpp shim (system-temp default).
    ${CMAKE_CURRENT_SOURCE_DIR}/../Agent/AgentOut.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Agent/ArchiveFolderOut.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/WorkDir.cpp
    # --- P.2 i18n : the CLang loader (engine source, compiled UNCHANGED). It is
    #     not in sevenzip_engine's source list, so it is built here so CLang::Open
    #     / Get resolve for the un-stubbed MyLoadString. Its deps (StringToInt,
    #     UTFConvert, FileIO, MyString) are all already in sevenzip_engine.
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Common/Lang.cpp
    # --- B.0 Linux compat (additive; no engine source touched) ---
    agent/AgentLinuxCompat.cpp
    agent/WorkDirSettings.cpp
    agent/FmCompareCompat.cpp
)

add_library(sevenzip_agent STATIC ${SEVENZIP_AGENT_SOURCES})

target_compile_definitions(sevenzip_agent PRIVATE
    NDEBUG
    _REENTRANT
    _FILE_OFFSET_BITS=64
    _LARGEFILE_SOURCE
    Z7_COM_USE_ATOMIC
)

target_compile_options(sevenzip_agent PRIVATE
    -O2 -fPIC -Wall
    -include "${AGENT_COMPAT_INCLUDE}")

set_target_properties(sevenzip_agent PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(sevenzip_agent PUBLIC sevenzip_engine)

# ---- headless proof harness : 7zqt_agent_test (no Qt) ----------------------
# GUID-emitting TU (MyInitGuid.h). Opens an archive via CAgent, walks the
# IFolderFolder tree with BindToFolder, prints a recursive listing.
add_executable(7zqt_agent_test agent/agent_browse_test.cpp)

target_compile_definitions(7zqt_agent_test PRIVATE
    NDEBUG
    _REENTRANT
    _FILE_OFFSET_BITS=64
    _LARGEFILE_SOURCE
    Z7_COM_USE_ATOMIC
)

target_compile_options(7zqt_agent_test PRIVATE
    -O2 -fPIC -Wall
    -include "${AGENT_COMPAT_INCLUDE}")

# Link the agent lib plus the WHOLE engine archive (codec/format registrations
# must survive, same rationale as 7zz).
target_link_libraries(7zqt_agent_test PRIVATE
    sevenzip_agent
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,sevenzip_engine>")

# ---- B.5a headless delete proof : 7zqt_agent_delete_test (no Qt) -----------
# Byte-for-byte clone of 7zqt_agent_test. Opens an archive via CAgent, QIs the
# root CAgentFolder to IFolderOperations, Delete(rootIndex, NULL progress) -
# which drives CommonUpdateOperation's temp-file archive rewrite - then the
# caller runs `7zz l` / `7zz t` to confirm the entry is gone and the archive is
# still valid. GUID-emitting TU (MyInitGuid.h).
add_executable(7zqt_agent_delete_test agent/agent_delete_test.cpp)

target_compile_definitions(7zqt_agent_delete_test PRIVATE
    NDEBUG
    _REENTRANT
    _FILE_OFFSET_BITS=64
    _LARGEFILE_SOURCE
    Z7_COM_USE_ATOMIC
)

target_compile_options(7zqt_agent_delete_test PRIVATE
    -O2 -fPIC -Wall
    -include "${AGENT_COMPAT_INCLUDE}")

target_link_libraries(7zqt_agent_delete_test PRIVATE
    sevenzip_agent
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,sevenzip_engine>")

# ============================================================================
# Milestone B.1 : QtFolderModel (QAbstractItemModel over IFolderFolder) + a
# minimal navigable archive viewer (QtArchiveBrowser) + the 7zqt_browser binary.
#
# QtFolderModel is the faithful Qt translation of CPanel's folder binding:
#   * columns / header  <- CPanel::InitColumns (PanelItems.cpp) via
#                          GetNumberOfProperties / GetPropertyInfo,
#   * cell text         <- CPanel::SetItemText (PanelListNotify.cpp): size props
#                          grouped, everything else via the engine's canonical
#                          ConvertPropertyToString2 (PropIDUtils, in the engine),
#   * navigation        <- CPanel::OpenFolder / OpenParentFolder
#                          (PanelFolderChange.cpp): swap _folder on BindToFolder /
#                          BindToParentFolder.
#
# QtArchiveBrowser is a QMainWindow with a folders-first QSortFilterProxyModel
# (mirroring CPanel::CompareItems) over the model.
#
# main_archive_browser.cpp is the SINGLE GUID-emitting TU (MyInitGuid.h) and the
# controller: it opens the archive with CAgent (B.0 open pattern + password
# callback), binds the root IFolderFolder, and either shows the browser or runs
# the headless --dump self-check (offscreen) for diffing against `7zz l`.
#
# It includes the Agent headers, so it needs the SAME force-included compat
# header as the agent lib (AGENT_COMPAT_INCLUDE). AUTOMOC handles the Q_OBJECT
# classes (QtFolderModel / QtArchiveBrowser / QtFolderSortProxy).
# ============================================================================
set(QT_BROWSER_SOURCES
    fm/QtFolderModel.cpp
    fm/QtArchiveBrowser.cpp
    fm/FormatIcons.cpp        # B.6 : ext -> stock Archive/Icons/*.ico mapping
    fm/fm_resources.qrc       # B.6 : embeds the per-format .ico (AUTORCC, set globally)
    fm/main_archive_browser.cpp
)

add_executable(7zqt_browser ${QT_BROWSER_SOURCES})

target_compile_definitions(7zqt_browser PRIVATE
    NDEBUG
    _REENTRANT
    _FILE_OFFSET_BITS=64
    _LARGEFILE_SOURCE
    Z7_COM_USE_ATOMIC
)

# Force-include the Agent Linux compat header into every TU here (the model and
# main include Agent / IFolder headers that need the Win32-symbol shim), exactly
# as the agent lib does.
target_compile_options(7zqt_browser PRIVATE
    -O2 -fPIC -Wall
    -include "${AGENT_COMPAT_INCLUDE}")

target_link_libraries(7zqt_browser PRIVATE
    sevenzip_agent
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,sevenzip_engine>"
    Qt6::Widgets)

# ============================================================================
# Milestone B.3 : 7zqt_fm — the TWO-PANEL File Manager window (the primary FM
# entry, mirroring FM.cpp / App.cpp / CPanel). Replaces the B.2 single-panel
# main_fm_browser.cpp.
#
#   * QtFileManagerWindow (QMainWindow) : a QSplitter(Horizontal) with two
#     QtPanel widgets, the IDM_MENU menubar (resource.rc, verbatim text +
#     accelerators), the rebar's two QToolBars (App.cpp g_ArchiveButtons
#     {Add,Extract,Test} + g_StandardButtons {Copy,Move,Delete,Info}) built from
#     the ORIGINAL bitmaps, a status bar, and focused-panel tracking (App.h
#     _lastFocusedPanel / GetAnotherPanel).
#   * QtPanel : one panel (generalized QtFsBrowser) — address bar, Up, a
#     multi-select QTreeView over its own QtFolderModel + folders-first proxy,
#     and the seamless filesystem -> archive transition.
#   * FmIcons + fm_resources.qrc : the ORIGINAL 7-Zip assets (the 14 toolbar
#     bmps + FM.ico + 7zipLogo.ico) embedded BY THEIR IN-TREE PATHS via the qrc
#     (AUTORCC), with FmIcons reproducing App.cpp's magenta-mask transparency
#     (imageList.AddMasked(bmp, RGB(255,0,255))).
#   * QtFmActions : the toolbar/menu Add/Extract/Test/CRC actions, wired to the
#     Milestone-C flows by building a 7-Zip command vector and running it through
#     the REAL parser + dispatch (a lift of main_7zqt.cpp's Run7zqt). This pulls
#     in the C extract/compress/hash flow sources (the same union 7zqt builds).
#
# main_fm.cpp is the SINGLE GUID-emitting TU. AUTORCC compiles fm_resources.qrc.
# Same force-included Agent compat header / flags as 7zqt_browser; links Qt
# Widgets + the agent lib + the WHOLE engine archive.
# ============================================================================
# (CMAKE_AUTORCC is set globally near the top, before any target that uses a qrc.)

set(QT_FM_SOURCES
    # --- B.0-B.2 browse foundation (reused) ---
    fm/QtFolderModel.cpp
    fm/QtArchiveBrowser.cpp   # QtFolderSortProxy (folders-first proxy)
    fm/ArchiveOpenHelper.cpp
    # --- B.3 panel + shell + resources ---
    fm/QtPanel.cpp
    fm/FmIcons.cpp
    fm/FormatIcons.cpp        # B.6 : ext -> stock Archive/Icons/*.ico mapping
    fm/QtFileManagerWindow.cpp
    fm/QtPropertiesDialog.cpp     # B.7a : 2-col property list (CPanel::Properties mirror)
    fm/QtFmActions.cpp
    # --- B.7c : Split / Combine / Link / Diff / Favorites ---
    fm/QtSplitCombine.cpp         # CVolSeqName + Split/Combine workers (byte loops)
    fm/QtSplitDialog.cpp          # Split volume-size dialog (re-lifted ParseVolumeSizes)
    fm/QtLinkDialog.cpp           # Link dialog (symlink(2)/link(2))
    fm/QtFavorites.cpp            # 10-slot FastFolders store + [Tools] Diff command
    # --- G.10a : Tools->Benchmark dialog + CLI 7zqt b (engine Bench() driver) ---
    fm/QtBenchmarkDialog.cpp
    # --- B.8 : Options dialog + FM settings (QSettings [Options]/[FM]) + XDG trash ---
    fm/QtFmSettings.cpp           # CFmSettings subset + Editor/WorkDir/DeleteToTrash
    fm/QtOptionsDialog.cpp        # QTabWidget: Settings/Folders/Editor/Delete tabs
    # --- B.4a : the FS operations worker + IFolderOperationsExtractCallback ---
    fm/QtFsOperations.cpp
    # --- P.1 : threaded archive-open worker (responsive open of large archives) ---
    fm/QtArchiveOpenWorker.cpp
    # --- B.5b : IFolderArchiveUpdateCallback for in-place archive Delete/Add ---
    fm/QtArchiveUpdateCallback.cpp
    # --- B.4b : Qt-native drag & drop (text/uri-list) helper ---
    fm/QtFsDnd.cpp
    fm/fm_resources.qrc       # AUTORCC: embeds the ORIGINAL bmps + ico
    # --- Milestone-C operation flows (wired by QtFmActions) ---
    ProgressSync.cpp
    QtProgressDialog.cpp
    QtProgressThreadVirt.cpp
    QtExtractCallback.cpp
    QtExtractPrompts.cpp
    QtMemDialog.cpp           # G.8a : decompression memory-limit request (CMemDialog)
    QtExtractGUI.cpp
    QtExtractSettings.cpp
    QtExtractDialog.cpp
    QtOpenArcError.cpp        # G.3c : open-archive error-flag decoder
    QtCompressSettings.cpp
    QtUpdateCallback.cpp
    QtCompressDialog.cpp
    QtCompressOptionsDialog.cpp
    QtCompressGUI.cpp
    QtHashGUI.cpp
    QtHashResultsDialog.cpp
    QtHashMethodsDialog.cpp
    # --- B.3 entry (GUID home) ---
    fm/main_fm.cpp
)

add_executable(7zqt_fm ${QT_FM_SOURCES})

target_compile_definitions(7zqt_fm PRIVATE
    NDEBUG
    _REENTRANT
    _FILE_OFFSET_BITS=64
    _LARGEFILE_SOURCE
    Z7_COM_USE_ATOMIC
)

target_compile_options(7zqt_fm PRIVATE
    -O2 -fPIC -Wall
    -include "${AGENT_COMPAT_INCLUDE}")

target_link_libraries(7zqt_fm PRIVATE
    sevenzip_agent
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,sevenzip_engine>"
    Qt6::Widgets)

# P.2 i18n deploy : stage the translation corpus next to 7zqt_fm (the primary FM
# binary). Same rationale/idempotence/guard as the 7zqt rule above —
# GetLangDirPrefix() resolves <exe-dir>/Lang/, which the Options dialog's Language
# tab enumerates. Guarded so a corpus-less checkout still builds (see above).
if(EXISTS "${CMAKE_SOURCE_DIR}/Lang")
add_custom_command(TARGET 7zqt_fm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Lang"
        "$<TARGET_FILE_DIR:7zqt_fm>/Lang"
    COMMENT "P.2 : staging translation corpus -> $<TARGET_FILE_DIR:7zqt_fm>/Lang"
    VERBATIM)
endif()

# ---- B.2 single-panel browser retained (renamed 7zqt_fm_browser) -----------
# The B.2 single-panel FS browser stays buildable for regression/smoke; the
# two-panel window above is the primary 7zqt_fm. Same flags / link.
set(QT_FM_BROWSER_SOURCES
    fm/QtFolderModel.cpp
    fm/QtArchiveBrowser.cpp
    fm/QtFsBrowser.cpp
    fm/ArchiveOpenHelper.cpp
    ProgressSync.cpp          # P.1 : ArchiveOpenHelper now refs CProgressSync (open progress/cancel)
    fm/FormatIcons.cpp        # B.6 : ext -> stock Archive/Icons/*.ico mapping
    fm/fm_resources.qrc       # B.6 : embeds the per-format .ico (AUTORCC)
    fm/main_fm_browser.cpp
)

add_executable(7zqt_fm_browser ${QT_FM_BROWSER_SOURCES})

target_compile_definitions(7zqt_fm_browser PRIVATE
    NDEBUG
    _REENTRANT
    _FILE_OFFSET_BITS=64
    _LARGEFILE_SOURCE
    Z7_COM_USE_ATOMIC
)

target_compile_options(7zqt_fm_browser PRIVATE
    -O2 -fPIC -Wall
    -include "${AGENT_COMPAT_INCLUDE}")

target_link_libraries(7zqt_fm_browser PRIVATE
    sevenzip_agent
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,sevenzip_engine>"
    Qt6::Widgets)
