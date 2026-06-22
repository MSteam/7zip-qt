// main_fm.cpp
// ----------------------------------------------------------------------------
// Milestone B.3 : entry point for the 7zqt_fm target — the TWO-PANEL File
// Manager window (QtFileManagerWindow), the Qt analogue of FM.cpp's main window.
// It replaces the B.2 single-panel main_fm_browser.cpp as the primary FM entry.
//
//   7zqt_fm [<left-dir>] [<right-dir>] [--selftest] [--add|--extract|--test|--hash[=M]]
//
//   * <left-dir> / <right-dir> : starting directories for the two panels
//     (default: the current working directory for both).
//   * --selftest : headless (offscreen) resource + structure self-check:
//       - load every toolbar bitmap via FmIcons and assert non-null + that the
//         magenta key colour was made transparent (mirror AddMasked),
//       - assert FM.ico / 7zipLogo.ico load non-null,
//       - build the two-panel window offscreen and assert two panels + toolbars.
//   * --add / --extract / --test / --hash[=METHOD] : drive a wired operation on
//     the focused (left) panel's selection programmatically (the offscreen
//     scripted operation path), so Extract/Add/CRC can be proven end-to-end.
//
// This is the SINGLE GUID-emitting TU for the executable (MyInitGuid.h + the
// engine interface headers), exactly like main_7zqt.cpp / main_fm_browser.cpp.
// ----------------------------------------------------------------------------

#include "../../FileManager/StdAfx.h"

#include "../agent/AgentLinuxCompat.h"

// The one place GUIDs are instantiated for the whole executable.
#include "../../../../Common/MyInitGuid.h"

// Engine interface GUIDs the WHOLE_ARCHIVE engine references (same set the
// other GUID-emitting mains pull in): ICoder/ICrypto, IArchive, IFolder*.
#include "../../../ICoder.h"
#include "../../../IPassword.h"
#include "../../../IProgress.h"
#include "../../../Archive/IArchive.h"
#include "../../Common/IFileExtractCallback.h"
#include "../../Agent/Agent.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>           // G.5f : Edit -> Copy read-back
#include <QtWidgets/QTreeView>
#include <QtWidgets/QListView>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QHeaderView>      // G.4b : assert the view's hidden sections
#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QItemSelectionModel>
#include <QtCore/QTimer>
#include <QtCore/QMimeData>
#include <QtCore/QUrl>
#include <QtCore/QTemporaryDir>   // B.6 live-model icon test
#include <QtCore/QThread>         // P.1 : prove the archive open ran off the GUI thread
#include <QtCore/QElapsedTimer>   // G.4e : Auto Refresh pump-until-count deadline
#include <QtCore/QEventLoop>      // G.4e : processEvents flags for the inotify pump
#include <QtCore/QFile>
#include <QtCore/QDir>            // B.8 : trash-dir existence checks
#include <QtCore/QFileInfo>       // B.8 : gone-from-dir / .trashinfo checks
#include <QtCore/QSettings>       // B.8 : --opt-set/--opt-get round-trip
#include <QtCore/QStandardPaths>  // B.8 : XDG data dir for the trash location
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtGui/QIcon>            // icon-theme fallback (self-contained / AppImage runs)
#include <QtGui/QPalette>         // P.3 : --dark forced-Fusion dark QPalette
#include <QtGui/QColor>

#include "../../../../Common/MyString.h"
#include "../../../../Common/StringConvert.h"
#include "../../../../Windows/Defs.h"          // P.3 : VARIANT_BOOLToBool (dark-probe)
#include "../../../../Windows/PropVariant.h"   // P.3 : NCOM::CPropVariant (dark-probe)
#include "../../../../Windows/FileDir.h"
#include "../../../../Windows/FileName.h"
#include "../../../../Windows/FileFind.h"    // G.6c : verify a dropped file landed in a subfolder

#include "../../../PropID.h"                   // P.3 : kpidIsDeleted (dark-probe)

#include "../../FileManager/FSFolder.h"

#include "FmIcons.h"
#include "FormatIcons.h"   // B.6 : ext -> stock per-format archive icon
#include "QtPanel.h"
#include "QtArchiveBrowser.h"  // G.4b : QtFolderSortProxy::sortKey() in the column dump
#include "QtFileManagerWindow.h"
#include "QtBenchmarkDialog.h"  // G.10a : RunBenchmarkConsole (headless --benchmark)
#include "../../../MyVersion.h"  // G.10b : MY_VERSION_NUMBERS / MY_DATE (--about assertions)
#include "QtFsDnd.h"
#include "QtFmSettings.h"  // B.8 : Options persistence round-trip
#include "QtOptionsDialog.h" // B.8 : --screenshot of the Options dialog
#include "../QtExtractSettings.h"  // G.9a : NExtractQt::Read_LimitGB / Save_LimitGB (--opt-set/get MemLimitGB)
#include "../QtExtractCallback.h"  // G.8a : --mem-qi-probe (IArchiveRequestMemoryUseCallback QI/answer)
#include "../QtProgressDialog.h"   // G.8b/G.8e : message-list copy/select-all + '\n'-split self-check
#include "../../../../Common/StringToInt.h"  // G.9a : ConvertStringToUInt32 (MemLimitGB parse)
#include "../QtLang.h"      // P.2 : QtLang_LoadFile + FmLang (headless --lang-dump)
#include "../../FileManager/resource.h"  // P.2 : IDM_*/IDS_* ids for the dump

using namespace NWindows;

// main_7zqt.cpp normally owns this; in 7zqt_fm it lives here (consulted by
// QtFmActions.cpp's RunFmCommand, set from options.YesToAll during parsing).
bool g_DisableUserQuestions = false;

// B.7a : the text for --arc-comment-set (the entry name comes via --arc-name's
// selection arg; the comment body is carried separately so it may contain any
// characters). Consumed by RunScriptedArc's CommentSet branch.
const char *g_CommentText = nullptr;

// G.2d : the hash method for --arc-hash=<entry>[,<method>] (default CRC32).
// Consumed by RunScriptedArc's ArcHash branch.
const char *g_ArcHashMethod = nullptr;

#ifdef Z7_EXTERNAL_CODECS
extern const CExternalCodecs *g_ExternalCodecs_Ptr;
const CExternalCodecs *g_ExternalCodecs_Ptr;
#endif

static const char *OemUtf8(const UString &s)
{
  static AString buf;
  buf = GetOemString(s);
  return (const char *)buf;
}

// --- build a CFSFolder rooted at a directory (same as main_fm_browser.cpp) ----
static HRESULT BindFsFolder(const FString &dirPath, CMyComPtr<IFolderFolder> &folder)
{
  FString path = dirPath;
  NFile::NName::NormalizeDirPathPrefix(path);
  NFsFolder::CFSFolder *fsSpec = new NFsFolder::CFSFolder;
  CMyComPtr<IFolderFolder> f = fsSpec;
  RINOK(fsSpec->Init(path))
  folder = f;
  return S_OK;
}

// === headless resource + structure self-check ===============================
static int RunSelfTest()
{
  int rc = 0;
  printf("---- B.3 resource self-check (FmIcons over ORIGINAL bitmaps) ----\n");

  struct { FmIcons::Button b; const char *name; } buttons[] = {
    { FmIcons::Button::Add,     "Add" },
    { FmIcons::Button::Extract, "Extract" },
    { FmIcons::Button::Test,    "Test" },
    { FmIcons::Button::Copy,    "Copy" },
    { FmIcons::Button::Move,    "Move" },
    { FmIcons::Button::Delete,  "Delete" },
    { FmIcons::Button::Info,    "Info" },
  };

  for (auto &bi : buttons)
    for (int large = 1; large >= 0; --large)
    {
      const QString path = FmIcons::bitmapResourcePath(bi.b, large != 0);
      QImage raw(path);
      if (raw.isNull())
      {
        printf("  FAIL: %s (%s) MISSING/UNUSABLE: %s\n", bi.name,
            large ? "large" : "small", path.toUtf8().constData());
        rc = 1;
        continue;
      }
      // Count magenta pixels in the raw bmp, and assert the mask removes them.
      int rawMagenta = 0;
      {
        const QImage rgb = raw.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < rgb.height(); ++y)
          for (int x = 0; x < rgb.width(); ++x)
          {
            const QColor c = rgb.pixelColor(x, y);
            if (c.red() == 255 && c.green() == 0 && c.blue() == 255)
              rawMagenta++;
          }
      }
      const QImage masked = FmIcons::applyMagentaMask(raw);
      int maskedOpaqueMagenta = 0, transparentPx = 0;
      for (int y = 0; y < masked.height(); ++y)
        for (int x = 0; x < masked.width(); ++x)
        {
          const QColor c = masked.pixelColor(x, y);
          if (c.alpha() == 0) transparentPx++;
          else if (c.red() == 255 && c.green() == 0 && c.blue() == 255)
            maskedOpaqueMagenta++;
        }
      const QIcon icon = FmIcons::toolbarIcon(bi.b, large != 0);
      const bool ok = !icon.isNull() && rawMagenta > 0
          && maskedOpaqueMagenta == 0 && transparentPx >= rawMagenta;
      printf("  %s %-8s %s : %dx%d  rawMagenta=%d  masked->transparent=%d  opaqueMagentaLeft=%d  iconNull=%d  [%s]\n",
          large ? "L" : "s", bi.name, ok ? "OK " : "BAD",
          raw.width(), raw.height(), rawMagenta, transparentPx,
          maskedOpaqueMagenta, icon.isNull(),
          ok ? "PASS" : "FAIL");
      if (!ok) rc = 1;
    }

  // Window icon + about logo.
  const QIcon win = FmIcons::windowIcon();
  const QIcon logo = FmIcons::aboutLogo();
  printf("  FM.ico window icon: null=%d sizes=%lld  [%s]\n",
      win.isNull(), (long long)win.availableSizes().size(),
      win.isNull() ? "FAIL" : "PASS");
  printf("  7zipLogo.ico about logo: null=%d  [%s]\n",
      logo.isNull(), logo.isNull() ? "FAIL" : "PASS");
  if (win.isNull() || logo.isNull()) rc = 1;

  // Two-panel window structure (offscreen).
  FString cwd; NFile::NDir::GetCurrentDir(cwd);
  const UString cwdU = fs2us(cwd);
  QtFileManagerWindow win2(cwdU, cwdU, /*headless*/ true);
  const bool struct_ok = win2.leftPanel() && win2.rightPanel()
      && win2.otherPanel() == win2.rightPanel();
  printf("  two-panel structure: left=%p right=%p other(left)==right -> %s\n",
      (void *)win2.leftPanel(), (void *)win2.rightPanel(),
      struct_ok ? "PASS" : "FAIL");
  if (!struct_ok) rc = 1;

  // === B.6 : per-format archive file icons =================================
  // (a) Direct table assertion — no model/engine; proves embed + table +
  //     distinctness with zero filesystem setup.
  printf("---- B.6 format icons (ext -> stock Archive/Icons) ----\n");
  {
    struct { const char *ext; bool archive; } cases[] = {
      {"7z",true},{"zip",true},{"rar",true},{"tar",true},{"gz",true},
      {"xz",true},{"bz2",true},{"zst",true},{"tgz",true},{"001",true},
      {"lha",true},{"vhdx",true},{"7Z",true},   // case-insensitive / multi-ext
      {"txt",false},{"png",false},{"exe",false},{"",false}
    };
    for (auto &c : cases)
    {
      const QIcon ic = FormatIcons::iconForExtension(QString::fromLatin1(c.ext));
      const bool ok = (c.archive == !ic.isNull());
      printf("  .%-6s archive=%d iconNull=%d [%s]\n",
          c.ext, c.archive, ic.isNull(), ok ? "PASS" : "FAIL");
      if (!ok) rc = 1;
    }
    // distinctness: 7z vs zip must be DIFFERENT icons.
    const QPixmap p7 = FormatIcons::iconForExtension(QStringLiteral("7z")).pixmap(32, 32);
    const QPixmap pz = FormatIcons::iconForExtension(QStringLiteral("zip")).pixmap(32, 32);
    const bool distinct = !p7.toImage().isNull() && p7.toImage() != pz.toImage();
    printf("  7z.ico != zip.ico : %s\n", distinct ? "PASS" : "FAIL");
    if (!distinct) rc = 1;
  }

  // (b) Live-model assertion through the REAL DecorationRole seam: a temp dir
  //     with foo.7z/.zip/.rar/.tar/.txt, bound via CFSFolder, read each row's
  //     DecorationRole and assert the archive rows are non-null + distinct from
  //     the generic .txt row's icon.
  printf("---- B.6 live model (QtFolderModel::data DecorationRole) ----\n");
  {
    QTemporaryDir tmp;
    bool live_ok = tmp.isValid();
    if (!live_ok)
      printf("  FAIL: could not create temp dir\n");
    const char *files[] = { "foo.7z", "foo.zip", "foo.rar", "foo.tar", "foo.txt" };
    for (const char *fn : files)
    {
      QFile f(tmp.path() + QLatin1Char('/') + QLatin1String(fn));
      if (!f.open(QIODevice::WriteOnly)) { live_ok = false; }
      f.close();
    }
    CMyComPtr<IFolderFolder> folder;
    const FString dir = us2fs(GetUnicodeString(tmp.path().toUtf8().constData()));
    if (live_ok && BindFsFolder(dir, folder) == S_OK)
    {
      QtFolderModel model;
      model.setRootFolder(folder);
      // Find the kpidName column (column 0 in practice).
      const int nameCol = 0;
      // Map filename -> its DecorationRole QIcon by scanning rows.
      QImage txtImg;
      int found = 0;
      const int rows = model.rowCount(QModelIndex());
      // First pass: locate the .txt icon (generic fallback).
      for (int r = 0; r < rows; ++r)
      {
        const QString nm = model.data(model.index(r, nameCol), Qt::DisplayRole).toString();
        if (nm == QLatin1String("foo.txt"))
        {
          const QIcon ic = model.data(model.index(r, nameCol), Qt::DecorationRole).value<QIcon>();
          txtImg = ic.pixmap(32, 32).toImage();
        }
      }
      for (int r = 0; r < rows; ++r)
      {
        const QString nm = model.data(model.index(r, nameCol), Qt::DisplayRole).toString();
        bool archive = nm.endsWith(QLatin1String(".7z")) || nm.endsWith(QLatin1String(".zip"))
                    || nm.endsWith(QLatin1String(".rar")) || nm.endsWith(QLatin1String(".tar"));
        if (!archive && nm != QLatin1String("foo.txt"))
          continue;
        found++;
        const QIcon ic = model.data(model.index(r, nameCol), Qt::DecorationRole).value<QIcon>();
        const QImage img = ic.pixmap(32, 32).toImage();
        bool ok;
        if (archive)
          // The per-format icon must be present AND visibly different from the
          // generic .txt row's decoration. (The generic .txt icon is the theme
          // fallback QIcon::fromTheme("text-x-generic"), which can be null in a
          // headless env with no icon theme — that is NOT a B.6 failure, so we
          // only require the archive icon itself to be non-null + distinct.)
          ok = !ic.isNull() && !img.isNull() && img != txtImg;
        else
          // .txt: just report; non-null only when a theme is installed.
          ok = true;
        printf("  %-8s archive=%d iconNull=%d distinctFromTxt=%d [%s]\n",
            nm.toUtf8().constData(), archive, ic.isNull(),
            (archive ? (img != txtImg) : 0), ok ? "PASS" : "FAIL");
        if (!ok) live_ok = false;
      }
      if (found < 5) { printf("  FAIL: expected 5 rows, found %d\n", found); live_ok = false; }
    }
    else if (live_ok)
    {
      printf("  FAIL: BindFsFolder failed\n");
      live_ok = false;
    }
    if (!live_ok) rc = 1;
  }

  // === G.9d : ShowRealFileIcons gate (per-format icon -> generic when OFF) =====
  // Same live-model seam as B.6, but toggling QtFolderModel::setShowRealIcons. With
  // the gate ON (default) an archive file keeps its per-format icon (non-null,
  // distinct from the generic .txt row); with it OFF the archive file's decoration
  // must EQUAL the generic .txt decoration (the per-format lookup is suppressed).
  printf("---- G.9d ShowRealFileIcons gate (model DecorationRole) ----\n");
  {
    QTemporaryDir tmp;
    bool g9d_ok = tmp.isValid();
    if (!g9d_ok)
      printf("  FAIL: could not create temp dir\n");
    const char *files9d[] = { "foo.7z", "foo.txt" };
    for (const char *fn : files9d)
    {
      QFile f(tmp.path() + QLatin1Char('/') + QLatin1String(fn));
      if (!f.open(QIODevice::WriteOnly)) g9d_ok = false;
      f.close();
    }
    CMyComPtr<IFolderFolder> folder;
    const FString dir = us2fs(GetUnicodeString(tmp.path().toUtf8().constData()));
    if (g9d_ok && BindFsFolder(dir, folder) == S_OK)
    {
      QtFolderModel model;
      model.setRootFolder(folder);
      const int nameCol = 0;
      const int rows = model.rowCount(QModelIndex());
      int arcRow = -1, txtRow = -1;
      for (int r = 0; r < rows; ++r)
      {
        const QString nm = model.data(model.index(r, nameCol), Qt::DisplayRole).toString();
        if (nm == QLatin1String("foo.7z"))  arcRow = r;
        if (nm == QLatin1String("foo.txt")) txtRow = r;
      }
      if (arcRow < 0 || txtRow < 0)
      {
        printf("  FAIL: rows not found (arc=%d txt=%d)\n", arcRow, txtRow);
        g9d_ok = false;
      }
      else
      {
        auto iconImg = [&](int r) {
          const QIcon ic = model.data(model.index(r, nameCol), Qt::DecorationRole).value<QIcon>();
          return ic.pixmap(32, 32).toImage();
        };
        // ON (default): archive icon non-null + distinct from the generic .txt icon.
        const QImage arcOn = iconImg(arcRow);
        const QImage txtOn = iconImg(txtRow);
        const bool onOk = !arcOn.isNull() && arcOn != txtOn;
        printf("  ShowRealFileIcons ON : arcNull=%d arc!=txt=%d [%s]\n",
            arcOn.isNull(), arcOn != txtOn, onOk ? "PASS" : "FAIL");
        if (!onOk) g9d_ok = false;

        // OFF: the archive row's decoration must equal the generic .txt decoration
        // (the per-format icon is suppressed -> both are the generic file icon).
        model.setShowRealIcons(false);
        const QImage arcOff = iconImg(arcRow);
        const QImage txtOff = iconImg(txtRow);
        const bool offOk = (arcOff == txtOff);
        printf("  ShowRealFileIcons OFF: arc==txt(generic)=%d [%s]\n",
            arcOff == txtOff, offOk ? "PASS" : "FAIL");
        if (!offOk) g9d_ok = false;

        // Toggle back ON restores the per-format icon (live setter repaints).
        model.setShowRealIcons(true);
        const QImage arcBack = iconImg(arcRow);
        const bool backOk = (arcBack == arcOn);
        printf("  ShowRealFileIcons ON-again restores per-format=%d [%s]\n",
            arcBack == arcOn, backOk ? "PASS" : "FAIL");
        if (!backOk) g9d_ok = false;
      }
    }
    else if (g9d_ok)
    {
      printf("  FAIL: BindFsFolder failed\n");
      g9d_ok = false;
    }
    if (!g9d_ok) rc = 1;
  }

  // === G.8b/G.8e : progress message-list '\n'-split + numbering + clipboard ===
  printf("---- G.8e message '\\n'-split + numbering (QtProgressDialog) ----\n");
  {
    QtProgressDialog dlg;

    // Message 1 : a multi-line message. Per AddMessage, the FIRST physical line is
    // numbered "1", the two continuation lines are UN-numbered (blank number col).
    dlg.addMessage_ForTest(UString("alpha\nbeta\ngamma"));
    // Message 2 : single line -> one numbered row "2".
    dlg.addMessage_ForTest(UString("delta"));

    // Expect 4 displayed rows total: alpha(1), beta, gamma, delta(2).
    const int rows = dlg.messageRowCount();
    const bool countOk = (rows == 4);
    printf("  row count = %d (expect 4) [%s]\n", rows, countOk ? "PASS" : "FAIL");
    if (!countOk) rc = 1;

    // Raw strings (what CopyToClipboard uses) must be the per-line split, NOT the
    // collapsed original, and carry NO number column.
    struct { int row; const char *raw; } rawCases[] = {
      {0, "alpha"}, {1, "beta"}, {2, "gamma"}, {3, "delta"}
    };
    for (auto &c : rawCases)
    {
      const UString got = dlg.messageRawText(c.row);
      const bool ok = (got == UString(c.raw));
      printf("  raw[%d] = \"%s\" (expect \"%s\") [%s]\n", c.row,
          UnicodeStringToMultiByte(got, CP_UTF8).Ptr(), c.raw, ok ? "PASS" : "FAIL");
      if (!ok) rc = 1;
    }

    // Numbering: row 0 displays "1", rows 1-2 have NO leading digit, row 3 "2".
    auto firstNonSpaceIsDigit = [](const UString &s) -> bool {
      for (unsigned i = 0; i < s.Len(); i++)
      {
        const wchar_t ch = s[i];
        if (ch == L' ') continue;
        return (ch >= L'0' && ch <= L'9');
      }
      return false; // all-blank number field
    };
    const bool num0 = firstNonSpaceIsDigit(dlg.messageRowText(0));    // numbered
    const bool num1 = firstNonSpaceIsDigit(dlg.messageRowText(1));    // NOT numbered
    const bool num2 = firstNonSpaceIsDigit(dlg.messageRowText(2));    // NOT numbered
    const bool num3 = firstNonSpaceIsDigit(dlg.messageRowText(3));    // numbered
    const bool numberingOk = num0 && !num1 && !num2 && num3;
    printf("  numbering: row0=%d row1=%d row2=%d row3=%d (expect 1 0 0 1) [%s]\n",
        num0, num1, num2, num3, numberingOk ? "PASS" : "FAIL");
    if (!numberingOk) rc = 1;

    // Clipboard: with NOTHING selected, CopyToClipboard copies ALL raw rows, one
    // per line. (G.8b copy-selected-or-all.)
    dlg.copyMessagesToClipboard_ForTest();
    {
      const QString all = QApplication::clipboard()->text();
      const bool ok = (all == QStringLiteral("alpha\nbeta\ngamma\ndelta\n"));
      printf("  clipboard ALL = %s [%s]\n",
          ok ? "alpha/beta/gamma/delta" : all.toUtf8().constData(),
          ok ? "PASS" : "FAIL");
      if (!ok) rc = 1;
    }

    // Clipboard: select rows 1 and 3 -> copy only "beta" + "delta".
    dlg.selectMessageRow(1);
    dlg.selectMessageRow(3);
    dlg.copyMessagesToClipboard_ForTest();
    {
      const QString sel = QApplication::clipboard()->text();
      const bool ok = (sel == QStringLiteral("beta\ndelta\n"));
      printf("  clipboard SELECTED(1,3) = %s [%s]\n",
          ok ? "beta/delta" : sel.toUtf8().constData(),
          ok ? "PASS" : "FAIL");
      if (!ok) rc = 1;
    }
  }

  printf("---- self-check %s ----\n", rc == 0 ? "PASSED" : "FAILED");
  return rc;
}

// === scripted operation path (offscreen) ====================================
// Select files in the left panel and run a wired operation, proving the C-flow
// (and B.4a FS-op) wiring end-to-end without a display.
enum class ScriptOp { None, Add, Extract, Test, Hash,
                      Copy, Move, Delete, Rename, Mkdir, Mkfile,
                      // B.5b : in-place archive ops driven through the REAL FM op
                      // path (callback + worker + refresh).
                      ArcDelete, ArcAdd,
                      // B.5c : archive drag-OUT (extract-to-temp) source path.
                      ArcDragOut,
                      // G.2d : in-archive CRC/checksum of a selected entry.
                      ArcHash,
                      // B.7a : item actions (Properties / Comment / Open-View-Edit).
                      Props, CommentGet, CommentSet, OpenView, OpenEdit, OpenOut,
                      // G.5a : multi-select Properties aggregate (all items selected).
                      PropsMulti,
                      // G.5b : Properties dialog interactivity reachability (copy + viewer).
                      PropsInteract,
                      // B.7b : view modes / arrange-by / select-by-mask & by-type.
                      ViewMode, Arrange, SelectMask, SelectType,
                      // B.7c : Split / Combine / Link / Diff / Favorites.
                      Split, Combine, Link, Diff, FavAdd, FavGo,
                      // B.8 : Options persistence round-trip + trash/permanent delete.
                      OptSet, OptGet, DeleteTrash, DeletePerm,
                      // P.1 : open an archive through the THREADED worker path and
                      // dump the root listing (+ prove the open ran off the GUI thread).
                      OpenArchive,
                      // P.3 : assert the deleted-in-archive ForegroundRole mapping
                      // (no spurious red on normal rows; gate drops all reds).
                      DarkProbe,
                      // G.2b : enter an archive INSIDE the open archive seamlessly
                      // as a nested sub-folder, dump its listing, then Up out.
                      Nested,
                      // G.4i/G.4j : enter a named subfolder, read the status-bar
                      // focused-item fields, then go Up and assert the just-left
                      // subfolder is re-focused + selected (Up-focus restore).
                      StatusUp,
                      // G.4k : dual-pane keyboard cross-panel commands (tab /
                      // path-focus / same-folder / sub-folder mirror).
                      Key,
                      // G.4l : inline (in-place) rename via the model's setData()
                      // path — the SAME commit an F2 / slow-double-click edit makes.
                      // FS panel; arg == "<entry>,<newName>".
                      InlineRename,
                      // G.4l : inline rename INSIDE an open updatable archive.
                      // arg == "<entry>,<newName>" (via RunScriptedArc).
                      ArcInlineRename,
                      // G.4m : arrange an OPEN archive by a sort key (name|type|
                      // date|size) and print the resulting proxy order — the path
                      // that exercises by-VALUE date/numeric sorting on an archive
                      // whose Modified column doesn't format monotonically.
                      ArcArrange,
                      // G.4b : header column chooser. ColList dumps every column's
                      // (propID, name, visible, hidden-in-header) state; ColHide
                      // toggles a column by propID-or-name and re-dumps, proving the
                      // FS default-hide set, the Name lock, and the sort-column reset.
                      ColList, ColHide,
                      // G.4a : per-list-type view-settings persistence round-trip.
                      // ViewPersistSet (run 1) mutates a panel's column width / hide /
                      // sort / view-mode and navigates, persisting all to QSettings;
                      // ViewPersistGet (run 2, fresh process, same HOME) reads the
                      // panel back and dumps the restored mode / path / header layout.
                      ViewPersistSet, ViewPersistGet,
                      // G.4c : Flat View (recursive flat listing). Toggle flat mode
                      // on the opened folder (FS dir, or --arc-name=<leaf> archive)
                      // and print the flat listing as FLAT: <prefix><name> lines —
                      // proving all descendants (deep files) appear with their path
                      // prefixes, vs the non-flat listing showing only the top level.
                      FlatView,
                      // G.4f : ShowDots ".." pseudo-row. Enable ShowDots on the FS
                      // panel (optionally after opening --arc-name=<leaf>), then prove
                      // the row<->realIndex mapping is not broken: ".." at row 0,
                      // correct name/props/open/delete/rename/hash on the REAL items,
                      // ".." excluded from selection counts, and no ".." at FS root.
                      ShowDots,
                      // G.4d : Folders History (Alt+F12) + address-bar dropdown.
                      // Navigate the focused panel through a few subdirs (recording
                      // each into the app-level history), then dump the recorded
                      // history (most-recent-first) + the address-bar ancestor list,
                      // and drive the picker's "navigate to a recorded entry" outcome.
                      HistDump,
                      // G.4e : Auto Refresh (FS directory-change watcher). Enable
                      // auto-refresh on the LEFT FS panel, print the listing count,
                      // create+delete a file in that dir, pump the event loop so the
                      // QFileSystemWatcher delivers directoryChanged, and print the
                      // re-listed count — proving the panel re-lists on a disk change.
                      AutoRefresh,
                      // G.4g : one/two-panel toggle (F9 / IDM_VIEW_TWO_PANELS).
                      // Toggle the LEFT-focused window to single-panel and assert:
                      // only the focused panel is visible, otherPanel() stays valid
                      // (hidden, not destroyed), a Copy still lands in the hidden
                      // panel's dir, and NumPanels==1 is persisted; then prove a
                      // fresh process restores the single-panel layout.
                      OnePanel,
                      // G.4h : Open Root Folder ('\' / IDM_OPEN_ROOT_FOLDER).
                      // Navigate the focused (left) panel to the filesystem root "/"
                      // (CRootFolder "Computer" -> CFSFolder InitToRoot). With
                      // --arc-name=<leaf> the panel first opens that archive, then
                      // Open Root Folder must exit to "/" (release the archive).
                      OpenRoot,
                      // G.5f : Edit -> Copy item names to clipboard. Select the
                      // LEFT panel's items and dump the CRLF-joined clipboard text
                      // (EDIT_COPY_CLIPBOARD marker) + a QClipboard read-back.
                      EditCopy,
                      // LINK-TARGET : the QtLinkDialog current-target read-back for an
                      // EXISTING symlink (arg == the LEFT item name). Prints
                      // LINK_TARGET: <name> -> <target>.
                      LinkTarget,
                      // G.10a : Tools->Benchmark. Drives the engine Bench() (the same
                      // driver the dialog uses) and prints the textual report — proving
                      // the menu path produces real speed/rating numbers, not the
                      // "not implemented" stub. The dialog itself is GUI-only.
                      Benchmark,
                      // G.10b : Help->About. Print the REAL version/date + homepage the
                      // About dialog surfaces (QtFileManagerWindow::aboutText), proving
                      // the dialog shows MY_VERSION_CPU / MY_DATE, not a hardcoded blurb.
                      About,
                      // G.10c : Tools->Delete Temporary Files (IDM_TEMP_DIR). Mint a
                      // drag temp dir, then run deleteTempFiles() and prove the FM's own
                      // temp working dirs are purged (count removed > 0, dir gone).
                      DeleteTempFiles };

// For the C flows (Add/Extract/Test/Hash) a single dir is enough; for the B.4a FS
// ops, left == source dir, right == destination dir. `arg` carries the new name
// for rename/mkdir/mkfile.
static int RunScriptedOp(const UString &leftDir, const UString &rightDir,
    ScriptOp op, const char *hashMethod, const char *arg)
{
  FString fdir = us2fs(leftDir);
  CMyComPtr<IFolderFolder> root;
  if (BindFsFolder(fdir, root) != S_OK || !root)
  {
    fprintf(stderr, "scripted-op: cannot bind FS folder %s\n", OemUtf8(leftDir));
    return 2;
  }

  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *panel = win.leftPanel();

  const UString argU = arg ? GetUnicodeString(arg) : UString();

  // G.5a multi-select Properties on FS rows: select EVERY row in <left-dir> and
  // dump the aggregate summary (PanelMenu.cpp:260-295).
  if (op == ScriptOp::PropsMulti)
  {
    win.focusPanelForTest(panel);
    QtFolderModel *m = panel->model();
    const int rowCount = m->rowCount(QModelIndex());
    QVector<int> rows;
    for (int r = 0; r < rowCount; r++)
    {
      // Skip the synthetic ".." row (modelRowToRealIndex < 0) — not a real item.
      if (m->modelRowToRealIndex(r) >= 0)
        rows.append(r);
    }
    win.selectRowsForTest(panel, rows);
    printf("---- props-multi (%d rows selected) ----\n", (int)rows.size());
    win.doProperties();
    printf("props-multi done\n");
    return 0;
  }

  // B.7a item actions on a FS row: select the row named `arg` (or the first row),
  // not the whole selection, so Properties/Open target one item like the GUI.
  if (op == ScriptOp::Props || op == ScriptOp::OpenView
      || op == ScriptOp::OpenEdit || op == ScriptOp::OpenOut
      || op == ScriptOp::PropsInteract)   // G.5b
  {
    win.focusPanelForTest(panel);
    QtFolderModel *m = panel->model();
    int row = 0;
    if (arg && arg[0])
    {
      // G.4f : rowForName returns the correct MODEL row in either ShowDots mode.
      row = m->rowForName(argU);
      if (row < 0)
      {
        fprintf(stderr, "scripted-op: row %s not found\n", arg);
        return 2;
      }
    }
    win.selectRowForTest(panel, row);
    switch (op)
    {
      case ScriptOp::Props:    win.doProperties(); break;
      case ScriptOp::PropsInteract: win.doPropertiesInteractTest(/*valueViewerRow*/ 0); break; // G.5b
      case ScriptOp::OpenView: win.doOpen((int)QtFileManagerWindow::View); break;
      case ScriptOp::OpenEdit: win.doOpen((int)QtFileManagerWindow::Edit); break;
      case ScriptOp::OpenOut:  win.doOpen((int)QtFileManagerWindow::OpenOutside); break;
      default: break;
    }
    return 0;
  }

  // Select ALL rows in the left panel (the operation acts on the selection).
  QTreeView *view = panel->findChild<QTreeView *>();
  if (view) view->selectAll();

  switch (op)
  {
    case ScriptOp::Add:     win.doAdd();     break;
    case ScriptOp::Extract: win.doExtract(); break;
    case ScriptOp::Test:    win.doTest();    break;
    case ScriptOp::Hash:    win.doHash(hashMethod); break;
    // B.4a : left panel is focused (source); the OTHER (right) panel is the dest.
    case ScriptOp::Copy:    win.doCopyMove(false); break;
    case ScriptOp::Move:    win.doCopyMove(true);  break;
    case ScriptOp::Delete:  win.doDelete();        break;
    case ScriptOp::Rename:  win.doRename(argU);    break;
    case ScriptOp::Mkdir:   win.doCreateFolder(argU); break;
    case ScriptOp::Mkfile:  win.doCreateFile(argU);   break;
    default: break;
  }
  return 0;
}

// G.4l : exercise the INLINE rename — the model's flags()+setData() path, which is
// exactly what a list-view F2 / slow-double-click label edit commits (the proxy
// view's delegate forwards setData to the source model on the mapped index). Drives
// `entry` -> `newName` on the FS (`left`) panel. Prints the editable-flag state, the
// setData result, and the post-rename row presence so the harness can verify the
// disk file moved AND that an invalid name ("." / ".." / a name with '/') is
// rejected (setData -> false, row unchanged).
static int RunInlineRename(QtFileManagerWindow &win, QtPanel *panel,
    const char *entry, const char *newName)
{
  win.focusPanelForTest(panel);
  QtFolderModel *m = panel->model();

  const UString entryU = entry ? GetUnicodeString(entry) : UString();
  const UString newU   = newName ? GetUnicodeString(newName) : UString();

  // Find the source MODEL row named `entry`. G.4f : rowForName() returns the correct
  // MODEL row whether or not ShowDots shifts the rows by the ".." entry.
  const int row = m->rowForName(entryU);
  if (row < 0)
  {
    fprintf(stderr, "inline-rename: entry %s not found\n", entry ? entry : "");
    return 2;
  }

  // The Name column index (kpidName), and the editable-flag gate (flags()).
  const int nameCol = m->columnForPropID(kpidName);
  const QModelIndex nameIdx = m->index(row, nameCol, QModelIndex());
  const bool editable =
      (m->flags(nameIdx) & Qt::ItemIsEditable) != Qt::ItemFlags();
  printf("inline-rename: writable=%d nameEditable=%d entry=\"%s\" -> \"%s\"\n",
      m->isFolderWritable() ? 1 : 0, editable ? 1 : 0,
      entry ? entry : "", newName ? newName : "");

  // Commit the edit exactly as the view's delegate does: setData(nameIdx, newName,
  // EditRole) on the model. (The proxy in the live GUI just maps the index first;
  // the source-model call is identical.)
  const QString newQ = QString::fromWCharArray(newU.Ptr(), (int)newU.Len());
  const bool ok = m->setData(nameIdx, newQ, Qt::EditRole);
  printf("inline-rename: setData=%d\n", ok ? 1 : 0);

  // Report whether the OLD name is gone and the NEW name is present after refresh.
  m = panel->model();   // re-read: refresh() may have re-bound (archive)
  bool oldGone = true, newPresent = false;
  UInt32 nj = 0;
  if (panel->currentFolder())
    panel->currentFolder()->GetNumberOfItems(&nj);
  // G.4f : iterate ENGINE indices (itemNameByRealIndex) so the scan is correct
  // whether or not ShowDots adds the ".." MODEL row.
  for (UInt32 i = 0; i < nj; i++)
  {
    const UString nm = m->itemNameByRealIndex((int)i);
    if (nm == entryU) oldGone = false;
    if (nm == newU)   newPresent = true;
  }
  printf("inline-rename: oldGone=%d newPresent=%d\n",
      oldGone ? 1 : 0, newPresent ? 1 : 0);
  return 0;
}

// G.4l : FS-panel inline rename. Binds <left-dir>, then drives RunInlineRename on
// the left (FS) panel. `arg1` = entry, `arg2` = newName.
static int RunScriptedInlineRename(const UString &leftDir, const UString &rightDir,
    const char *arg1, const char *arg2)
{
  FString fdir = us2fs(leftDir);
  CMyComPtr<IFolderFolder> root;
  if (BindFsFolder(fdir, root) != S_OK || !root)
  {
    fprintf(stderr, "inline-rename: cannot bind FS folder %s\n", OemUtf8(leftDir));
    return 2;
  }
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  return RunInlineRename(win, win.leftPanel(), arg1, arg2);
}

// === B.7b : scripted view-mode / arrange-by / select-by-mask & by-type =======
// Pure GUI-state assertions on the LEFT FS panel (no engine op): bind, drive the
// op, print machine-checkable lines the harness asserts.
//   --view-mode=large|small|list|details : assert the active view widget class.
//   --arrange=name|type|date|size|unsorted : print the post-sort proxy order.
//   --select-mask=PATTERN : (de)select by mask; print the selected-row names.
//   --select-type : focus row 0, select-by-type; print the selected-row names.
static int RunScriptedView(const UString &leftDir, const UString &rightDir,
    ScriptOp op, const char *arg)
{
  FString fdir = us2fs(leftDir);
  CMyComPtr<IFolderFolder> root;
  if (BindFsFolder(fdir, root) != S_OK || !root)
  {
    fprintf(stderr, "scripted-view: cannot bind FS folder %s\n", OemUtf8(leftDir));
    return 2;
  }

  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *panel = win.leftPanel();
  win.focusPanelForTest(panel);
  QtFolderModel *m = panel->model();
  QSortFilterProxyModel *proxy =
      qobject_cast<QSortFilterProxyModel *>(panel->view()->model());

  if (op == ScriptOp::ViewMode)
  {
    int mode = 3; const char *name = "details";
    if (arg)
    {
      if      (strcmp(arg, "large")   == 0) { mode = 0; name = "large"; }
      else if (strcmp(arg, "small")   == 0) { mode = 1; name = "small"; }
      else if (strcmp(arg, "list")    == 0) { mode = 2; name = "list"; }
      else if (strcmp(arg, "details") == 0) { mode = 3; name = "details"; }
    }
    panel->setViewMode(mode);
    QAbstractItemView *av = panel->activeView();
    const bool isList = qobject_cast<QListView *>(av) != nullptr;
    const bool isTree = qobject_cast<QTreeView *>(av) != nullptr;
    printf("VIEW_MODE: %s active=%s isList=%d isTree=%d iconW=%d\n",
        name, av->metaObject()->className(),
        isList ? 1 : 0, isTree ? 1 : 0, av->iconSize().width());
    // Prove the shared selection model still works after the switch: select all,
    // then read back via selectedSourceRows().
    av->selectAll();
    printf("VIEW_MODE: selectedRows=%d\n", (int)panel->selectedSourceRows().size());
    return 0;
  }

  if (op == ScriptOp::Arrange)
  {
    // Accept a comma-separated list so the toggle rule can be demonstrated in one
    // process (e.g. --arrange=name,name : first toggles to desc, second back to
    // asc). The LAST key's resulting order is printed.
    const char *name = "name";
    const AString list = arg ? AString(arg) : AString("name");
    const int len = (int)list.Len();
    int start = 0;
    for (int i = 0; i <= len; i++)
    {
      if (i < len && list[(unsigned)i] != ',')
        continue;
      const AString tok = list.Mid((unsigned)start, (unsigned)(i - start));
      start = i + 1;
      if (tok.IsEmpty())
        continue;
      int propID = kpidName; bool unsorted = false;
      if      (tok == "name")     { propID = kpidName; name = "name"; }
      else if (tok == "type")     { propID = kpidExtension; name = "type"; }
      else if (tok == "date")     { propID = kpidMTime; name = "date"; }
      else if (tok == "size")     { propID = kpidSize; name = "size"; }
      else if (tok == "unsorted") { propID = kpidNoProperty; unsorted = true; name = "unsorted"; }
      win.onArrange(propID, unsorted);
    }
    printf("ARRANGE: key=%s\n", name);
    const int n = proxy ? proxy->rowCount() : 0;
    for (int i = 0; i < n; i++)
    {
      const QModelIndex p = proxy->index(i, 0);
      const int srcRow = proxy->mapToSource(p).row();
      printf("ARRANGE_ROW: %s\n", OemUtf8(m->itemName(srcRow)));
    }
    return 0;
  }

  if (op == ScriptOp::SelectMask)
  {
    const UString mask = arg ? GetUnicodeString(arg) : UString(L"*");
    win.doSelectByMask(true, mask);
    printf("SELECT_MASK: mask=%s\n", arg ? arg : "*");
    const QVector<int> rows = panel->selectedSourceRows();
    for (int r : rows)
      printf("SELECTED: %s\n", OemUtf8(m->itemName(r)));
    printf("SELECT_MASK: count=%d\n", (int)rows.size());
    return 0;
  }

  if (op == ScriptOp::SelectType)
  {
    // Focus the row named `arg` (or row 0) so SelectByType derives its type.
    int row = 0;
    if (arg && arg[0])
    {
      // G.4f : rowForName returns the correct MODEL row in either ShowDots mode.
      row = m->rowForName(GetUnicodeString(arg));
      if (row < 0)
      {
        fprintf(stderr, "scripted-view: row %s not found\n", arg);
        return 2;
      }
    }
    win.selectRowForTest(panel, row);
    printf("SELECT_TYPE: focus=%s\n", OemUtf8(m->itemName(row)));
    panel->selectByType(true);
    const QVector<int> rows = panel->selectedSourceRows();
    for (int r : rows)
      printf("SELECTED: %s\n", OemUtf8(m->itemName(r)));
    printf("SELECT_TYPE: count=%d\n", (int)rows.size());
    return 0;
  }

  return 0;
}

// === G.4b : scripted header column chooser =====================================
// Dump every column's (propID, name, model-visible, header-hidden) state on the
// given (already-bound) panel; for ColHide, toggle the column named/numbered `arg`
// first (the Qt analogue of clicking its checkbox in ShowColumnsContextMenu), then
// re-dump. Proves: FS folders default-hide the trimmed set, an archive shows its
// set, the Name column cannot be hidden, hiding the active sort column resets the
// sort to Name, and the view's setSectionHidden tracks the model flag.
static int RunScriptedColumns(QtFileManagerWindow &win, QtPanel *panel,
    ScriptOp op, const char *arg)
{
  QtFolderModel *m = panel->model();
  QHeaderView *hdr = panel->view()->header();
  QtFolderSortProxy *proxy =
      qobject_cast<QtFolderSortProxy *>(panel->view()->model());

  auto dump = [&](const char *tag)
  {
    printf("%s: isFs=%d sortIndicatorCol=%d proxySortKey=%u\n",
        tag, m->isFsFolder() ? 1 : 0,
        hdr ? hdr->sortIndicatorSection() : -1,
        proxy ? (unsigned)proxy->sortKey() : 0u);
    const int n = m->columnCount(QModelIndex());
    for (int c = 0; c < n; c++)
    {
      const PROPID pid = m->columnPropID(c);
      const bool vis = m->isColumnVisible(c);
      const bool hidden = (hdr && c < hdr->count()) ? hdr->isSectionHidden(c) : true;
      printf("COL: col=%d propid=%u name=%s visible=%d headerHidden=%d\n",
          c, (unsigned)pid,
          m->columnName(c).toUtf8().constData(), vis ? 1 : 0, hidden ? 1 : 0);
    }
  };

  // G.4b : an optional ",sort" suffix on the ColHide arg pre-ARRANGES the panel by
  // the target column (so it becomes the active sort column) BEFORE hiding it, to
  // prove the de-select-active-sort-column -> reset-to-Name rule. Strip it here.
  AString argStr = arg ? AString(arg) : AString();
  bool preSort = false;
  {
    const char *suffix = ",sort";
    const unsigned sl = (unsigned)strlen(suffix);
    if (argStr.Len() >= sl &&
        strcmp(argStr.Ptr(argStr.Len() - sl), suffix) == 0)
    {
      preSort = true;
      argStr.DeleteFrom(argStr.Len() - sl);
    }
  }
  const char *colArg = argStr.Ptr();

  dump("COL_STATE_BEFORE");

  if (op == ScriptOp::ColHide && colArg && colArg[0])
  {
    // Resolve `arg` to a column PROPID: an integer is the PROPID directly, else a
    // case-insensitive match on the column header NAME.
    PROPID target = 0;
    bool found = false;
    char *endp = nullptr;
    const long asNum = strtol(colArg, &endp, 10);
    if (endp && *endp == 0 && endp != colArg)
    {
      target = (PROPID)asNum;
      found = (m->columnForPropID(target) >= 0);
    }
    if (!found)
    {
      const QString want = QString::fromUtf8(colArg).trimmed();
      const int n = m->columnCount(QModelIndex());
      for (int c = 0; c < n; c++)
        if (m->columnName(c).compare(want, Qt::CaseInsensitive) == 0)
        {
          target = m->columnPropID(c);
          found = true;
          break;
        }
    }
    if (!found)
    {
      fprintf(stderr, "col-hide: column %s not found\n", colArg);
      return 2;
    }

    // Optional: make `target` the active sort column first (the ",sort" suffix),
    // so hiding it exercises the reset-sort-to-Name rule.
    if (preSort)
    {
      win.onArrange((int)target, false);
      printf("COL_PRESORT: propid=%u sortKeyBefore=%u\n",
          (unsigned)target, (unsigned)target);
    }

    const bool wasVisible = m->isColumnVisibleByPropID(target);
    // Toggle (a hide if currently shown; a show if currently hidden) through the
    // panel's PROPID setter — the exact path the header context menu drives,
    // including the sort-column reset.
    panel->setColumnVisible(target, !wasVisible);
    printf("COL_TOGGLE: propid=%u was=%d now=%d locked=%d\n",
        (unsigned)target, wasVisible ? 1 : 0,
        m->isColumnVisibleByPropID(target) ? 1 : 0,
        (target == kpidName) ? 1 : 0);
  }

  dump("COL_STATE_AFTER");
  return 0;
}

// === G.4c : scripted Flat View (recursive flat listing) =======================
// Dump the bound panel's listing BEFORE (non-flat: only the current folder's direct
// children) and AFTER toggling Flat View (flat: ALL descendants, each printed as
// FLAT: <prefix><name> using its kpidPrefix path column). Proves a flat folder
// enumerates deep files with their subfolder prefixes, the kpidPrefix column becomes
// visible, and the non-flat listing is just the top level. Works for an FS dir or an
// open archive (the caller binds the panel; the engine folder — CFSFolder or
// CAgentFolder — supplies the recursion via IFolderSetFlatMode).
static int RunScriptedFlat(QtFileManagerWindow &win, QtPanel *panel)
{
  win.focusPanelForTest(panel);
  QtFolderModel *m = panel->model();

  // The kpidPrefix column value of a row (the item's relative subfolder path), via
  // the model's raw GetProperty seam. Empty for a top-level item / non-flat.
  auto prefixOf = [&](int row) -> UString
  {
    NWindows::NCOM::CPropVariant prop;
    if (m->getRawProperty(row, kpidPrefix, prop) == S_OK && prop.vt == VT_BSTR)
      return UString(prop.bstrVal);
    return UString();
  };

  // A full listing dump under `tag`, with each row's prefix+name and dir flag.
  auto dump = [&](const char *tag)
  {
    UInt32 n = 0;
    if (panel->currentFolder())
      panel->currentFolder()->GetNumberOfItems(&n);
    printf("%s: flat=%d supported=%d count=%u prefixColVisible=%d\n",
        tag, m->isFlatMode() ? 1 : 0, m->flatModeSupported() ? 1 : 0,
        (unsigned)n, m->isColumnVisibleByPropID(kpidPrefix) ? 1 : 0);
    for (UInt32 i = 0; i < n; i++)
    {
      // Compose prefix+name into ONE UString so we call OemUtf8 (static buffer) once.
      UString line = prefixOf((int)i);
      line += m->itemName((int)i);
      printf("%s: %s dir=%d\n",
          (m->isFlatMode() ? "FLAT" : "NONFLAT"),
          OemUtf8(line), m->isFolder((int)i) ? 1 : 0);
    }
  };

  dump("FLAT_STATE_BEFORE");

  if (!panel->flatModeSupported())
  {
    printf("FLAT_VIEW: not supported on this folder (menu would be grayed)\n");
    return 0;
  }

  // CPanel::ChangeFlatMode (the View -> Flat View click).
  panel->toggleFlatMode();
  printf("FLAT_VIEW: toggled -> flat=%d\n", panel->flatMode() ? 1 : 0);

  dump("FLAT_STATE_AFTER");

  // Toggle back off, proving the non-flat listing is restored (and the prefix column
  // disappears again).
  panel->toggleFlatMode();
  dump("FLAT_STATE_OFF");
  return 0;
}

// === G.4f : scripted ShowDots ".." pseudo-row + index-mapping proof ===========
// Enable ShowDots on the LEFT panel (an FS dir, or first open --arc-name=<leaf>) and
// prove the row<->realIndex mapping is intact: (1) ".." is at model row 0 AND proxy
// row 0; (2) selecting a real file returns the CORRECT name/props (no off-by-one);
// (3) double-clicking ".." goes Up; (4) ".." is excluded from selection counts;
// (5) a real-item delete/rename/hash targets the right realIndex; (6) no ".." at the
// FS filesystem root. `arg` names a real file to probe; `archiveName` (optional)
// opens an archive first so the in-archive ".." (exit) path is exercised too.
static int RunScriptedShowDots(const UString &leftDir, const UString &rightDir,
    const char *arg, const char *archiveName)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *panel = win.leftPanel();
  win.focusPanelForTest(panel);
  QtFolderModel *m = panel->model();
  QSortFilterProxyModel *proxy =
      qobject_cast<QSortFilterProxyModel *>(panel->view()->model());

  // Optionally open an archive first (so ".." inside an archive exits via the stack).
  if (archiveName && archiveName[0])
  {
    const UString arcU = GetUnicodeString(archiveName);
    UInt32 n = 0;
    if (panel->currentFolder())
      panel->currentFolder()->GetNumberOfItems(&n);
    bool opened = false;
    for (UInt32 i = 0; i < n; i++)
      if (m->itemName((int)i) == arcU)
      {
        m->enterItem((int)i);
        opened = panel->isInArchive();
        break;
      }
    if (!opened)
    {
      fprintf(stderr, "scripted-showdots: could not open archive %s\n", archiveName);
      return 2;
    }
    printf("SHOWDOTS: opened archive %s inArchive=%d\n",
        archiveName, panel->isInArchive() ? 1 : 0);
  }

  // The REAL engine item count (excludes ".."), captured BEFORE enabling ShowDots.
  const int realCountBefore = m->realItemCount();

  // (a) enable ShowDots LIVE via the panel's applySettings (the same path the Options
  // dialog drives). The ".." row appears at model row 0.
  QtFmSettings::CInfo s; s.Load();
  s.ShowDots = true;
  win.applySettingsToPanels(s);

  // (1) ".." is row 0 (model AND proxy), rowCount == realCount + 1, realItemCount
  // unchanged. modelRowToRealIndex(0) is the parent sentinel; row 1 -> realIndex 0.
  const bool dotsActive = m->showDotsActive();
  const bool row0IsParent = m->isParentRow(0);
  const QString row0Name = m->index(0, 0, QModelIndex()).data(Qt::DisplayRole).toString();
  const int rc = m->rowCount(QModelIndex());
  const int realCount = m->realItemCount();
  const int map0 = m->modelRowToRealIndex(0);
  const int map1 = m->modelRowToRealIndex(1);
  // Proxy row 0 must be the ".." source row (it always sorts first).
  int proxyRow0Src = -1;
  if (proxy && proxy->rowCount() > 0)
    proxyRow0Src = proxy->mapToSource(proxy->index(0, 0)).row();
  printf("SHOWDOTS: active=%d row0IsParent=%d row0Name=\"%s\" rowCount=%d realCount=%d(was %d) map[0]=%d map[1]=%d proxyTopSrc=%d\n",
      dotsActive ? 1 : 0, row0IsParent ? 1 : 0, row0Name.toUtf8().constData(),
      rc, realCount, realCountBefore, map0, map1, proxyRow0Src);

  // (7) FS filesystem ROOT (no parent): ShowDots is ON but no ".." may appear. The
  // gate (!IsRootFolder()) suppresses it; the mapping stays the identity. Assert and
  // STOP (the file/up probes need a real ".." row).
  if (!dotsActive)
  {
    printf("SHOWDOTS: CHECK no-dotdot-at-root=%s (active=%d row0=\"%s\" rowCount=%d realCount=%d map[0]=%d)\n",
        (!row0IsParent && row0Name != QStringLiteral("..")
         && rc == realCount && map0 == 0) ? "PASS" : "FAIL",
        dotsActive ? 1 : 0, row0Name.toUtf8().constData(), rc, realCount, map0);
    return 0;
  }

  printf("SHOWDOTS: CHECK row0-is-dotdot=%s\n",
      (dotsActive && row0IsParent && row0Name == QStringLiteral("..")
       && rc == realCount + 1 && realCount == realCountBefore
       && map0 == QtFolderModel::kParentRow && map1 == 0
       && proxyRow0Src == 0) ? "PASS" : "FAIL");

  // (2)+(5) select a REAL file by name and prove no off-by-one: the selection's row
  // maps to the file's realIndex, and itemName/getRawProperty(kpidName) at that row
  // both return the SAME requested name (not the neighbour).
  if (arg && arg[0])
  {
    const UString want = GetUnicodeString(arg);
    const int modelRow = m->rowForName(want);   // returns the SHIFTED model row
    if (modelRow < 0)
    {
      fprintf(stderr, "scripted-showdots: file %s not found\n", arg);
      return 2;
    }
    win.selectRowForTest(panel, modelRow);
    const QVector<int> sel = panel->selectedSourceRows();
    const int selRow = sel.isEmpty() ? -1 : sel.first();
    const int selReal = (selRow >= 0) ? m->modelRowToRealIndex(selRow) : -2;
    const UString nameAtRow = m->itemName(modelRow);
    NWindows::NCOM::CPropVariant prop;
    m->getRawProperty(modelRow, kpidName, prop);
    const UString rawName = (prop.vt == VT_BSTR) ? UString(prop.bstrVal) : UString();
    // The expected realIndex = the engine position of `want` in the engine listing.
    int wantReal = -1;
    for (int k = 0; k < realCount; k++)
      if (m->itemNameByRealIndex(k) == want) { wantReal = k; break; }
    printf("SHOWDOTS: probe \"%s\": modelRow=%d selRow=%d selReal=%d wantReal=%d nameAtRow=\"%s\" rawName=\"%s\"\n",
        arg, modelRow, selRow, selReal, wantReal,
        OemUtf8(nameAtRow), OemUtf8(rawName));
    printf("SHOWDOTS: CHECK file-no-offbyone=%s\n",
        (modelRow == m->realIndexToModelRow(wantReal)
         && selRow == modelRow && selReal == wantReal
         && nameAtRow == want && rawName == want
         && wantReal >= 0) ? "PASS" : "FAIL");

    // (6) ".." excluded from the selection count: exactly ONE real item is selected
    // (the file), never the ".." row.
    bool dotsInSel = false;
    for (int r : sel) if (m->isParentRow(r)) dotsInSel = true;
    printf("SHOWDOTS: CHECK dotdot-excluded-from-selection=%s (selCount=%d)\n",
        (!dotsInSel && sel.size() == 1) ? "PASS" : "FAIL", (int)sel.size());
  }

  // (3) double-click ".." goes Up. Capture the path, activate the parent row via the
  // model's enterItem (the SAME call onDoubleClicked makes), and confirm the folder
  // changed (or we exited the archive).
  {
    const UString before = m->currentPath();
    const bool wasInArc = panel->isInArchive();
    m->enterItem(0);   // model row 0 == ".." -> parentActivated -> onUp
    // Re-read the model: refresh()/rebind may have swapped the pointer (archive exit).
    m = panel->model();
    const UString after = m->currentPath();
    const bool nowInArc = panel->isInArchive();
    // OemUtf8 returns a STATIC buffer, so print each path in its own call (a single
    // printf with two OemUtf8 args would clobber the first).
    printf("SHOWDOTS: dotdot-up before=\"%s\"(inArc=%d)", OemUtf8(before), wasInArc ? 1 : 0);
    printf(" after=\"%s\"(inArc=%d)\n", OemUtf8(after), nowInArc ? 1 : 0);
    printf("SHOWDOTS: CHECK dotdot-activates-up=%s\n",
        (after != before || nowInArc != wasInArc) ? "PASS" : "FAIL");
  }

  return 0;
}

// === G.4a : scripted per-list-type view-settings persistence ==================
// Two-phase round-trip (run twice with the SAME HOME so QSettings is shared):
//   --view-persist-set=<directives>  (run 1) : on the LEFT FS panel, apply column
//     resize / hide / arrange-by / view-mode changes and (optionally) navigate, all
//     persisted to QSettings via the QtPanel save hooks. Directives are comma-
//     separated: w=<col>:<px> (resize section), hide=<propid> (hide a column),
//     sort=<key> (arrange-by name|type|date|size), mode=<0..3> (view mode),
//     nav=<subdir> (navigate the LEFT panel into <leftDir>/<subdir>).
//   --view-persist-get               (run 2, fresh process) : rebuild the window
//     (which restores from QSettings on construction) and DUMP the restored view
//     mode, the restored LEFT panel path, and the LEFT FS type's header layout
//     (per-column width + hidden + the sort indicator) read back through the panel.
// The LEFT panel index is 0; the type key for an FS folder is "fs".
static int RunScriptedViewPersist(const UString &leftDir, const UString &rightDir,
    ScriptOp op, const char *arg)
{
  // Phase GET first: a fresh window restores from QSettings in its constructor.
  if (op == ScriptOp::ViewPersistGet)
  {
    QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
    QtPanel *panel = win.leftPanel();
    win.focusPanelForTest(panel);
    QHeaderView *hdr = panel->view()->header();

    printf("VP_GET: viewMode=%d\n", panel->viewMode());
    printf("VP_GET: leftPath=%s\n", OemUtf8(panel->currentPath()));
    QtFolderModel *m = panel->model();
    const int n = m->columnCount(QModelIndex());
    for (int c = 0; c < n; c++)
    {
      const int w = (hdr && c < hdr->count()) ? hdr->sectionSize(c) : -1;
      const bool hidden = (hdr && c < hdr->count()) ? hdr->isSectionHidden(c) : true;
      printf("VP_COL: col=%d propid=%u width=%d hidden=%d\n",
          c, (unsigned)m->columnPropID(c), w, hidden ? 1 : 0);
    }
    printf("VP_GET: sortIndicatorCol=%d sortAsc=%d\n",
        hdr ? hdr->sortIndicatorSection() : -1,
        (hdr && hdr->sortIndicatorOrder() == Qt::AscendingOrder) ? 1 : 0);
    return 0;
  }

  // Phase SET : apply the directives and let the QtPanel save hooks persist them.
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *panel = win.leftPanel();
  win.focusPanelForTest(panel);
  QtFolderModel *m = panel->model();
  QHeaderView *hdr = panel->view()->header();

  const AString list = arg ? AString(arg) : AString();
  const int len = (int)list.Len();
  int start = 0;
  for (int i = 0; i <= len; i++)
  {
    if (i < len && list[(unsigned)i] != ',')
      continue;
    const AString tok = list.Mid((unsigned)start, (unsigned)(i - start));
    start = i + 1;
    if (tok.IsEmpty())
      continue;
    const int eq = tok.Find('=');
    if (eq < 0)
      continue;
    const AString k = tok.Mid(0, (unsigned)eq);
    const AString v = tok.Mid((unsigned)(eq + 1), tok.Len() - (unsigned)(eq + 1));

    if (k == "w")   // w=<col>:<px>  -> resize a header section
    {
      const int colon = v.Find(':');
      if (colon >= 0 && hdr)
      {
        const int col = (int)atoi(v.Mid(0, (unsigned)colon).Ptr());
        const int px  = (int)atoi(v.Ptr((unsigned)(colon + 1)));
        hdr->resizeSection(col, px);
        printf("VP_SET: resize col=%d px=%d\n", col, px);
      }
    }
    else if (k == "hide")   // hide=<propid>
    {
      const PROPID pid = (PROPID)atoi(v.Ptr());
      panel->setColumnVisible(pid, false);
      printf("VP_SET: hide propid=%u\n", (unsigned)pid);
    }
    else if (k == "sort")   // sort=name|type|date|size
    {
      int propID = kpidName; bool unsorted = false;
      if      (v == "name") propID = kpidName;
      else if (v == "type") propID = kpidExtension;
      else if (v == "date") propID = kpidMTime;
      else if (v == "size") propID = kpidSize;
      win.onArrange(propID, unsorted);
      printf("VP_SET: sort=%s\n", v.Ptr());
    }
    else if (k == "mode")   // mode=0..3
    {
      const int mode = (int)atoi(v.Ptr());
      panel->setViewMode(mode);
      printf("VP_SET: mode=%d\n", mode);
    }
    else if (k == "nav")    // nav=<subdir> (relative to leftDir)
    {
      UString sub = leftDir;
      if (!sub.IsEmpty() && sub.Back() != L'/')
        sub += L'/';
      sub += GetUnicodeString(v.Ptr());
      const bool ok = panel->navigateToFsPath(sub);
      printf("VP_SET: nav=%s ok=%d -> %s\n", v.Ptr(), ok ? 1 : 0,
          OemUtf8(panel->currentFsDirPath()));
    }
  }

  // Force the debounced header-state save to flush NOW (the test process exits
  // before the 400ms timer would fire). saveViewSettingsNow is reachable via the
  // public scheduleSave— but to flush synchronously we drive QtFmSettings directly
  // with the panel's live header state + visibility set + view mode + path, exactly
  // what saveViewSettingsNow writes. (m is used to read the type key shape.)
  {
    const UString typeKey = QtFmSettings::ListTypeKey(m->folderTypeId());
    if (hdr)
      QtFmSettings::SaveListViewState(typeKey, hdr->saveState());
    QtFmSettings::SaveColumnVisible(typeKey, panel->visibleColumnPropIDs());
    QtFmSettings::SaveListMode(0, panel->viewMode());
    if (!panel->isInArchive() && !panel->currentFsDirPath().IsEmpty())
      QtFmSettings::SavePanelPath(0, panel->currentFsDirPath());
    printf("VP_SET: flushed typeKey=%s\n", OemUtf8(typeKey));
  }
  return 0;
}

// === G.4i / G.4j : scripted status-bar fields + Up-focus restore =============
// Drives the LEFT FS panel: select the first item and dump the status bar's
// focused-item size/date (G.4i); then enter the subfolder named `arg`, go Up, and
// assert the just-left subfolder is the current+selected row (G.4j Up-focus
// restore). Prints machine-checkable STATUS:/UPFOCUS: lines.
static int RunScriptedStatusUp(const UString &leftDir, const UString &rightDir,
    const char *arg, const char *archiveName)
{
  FString fdir = us2fs(leftDir);
  CMyComPtr<IFolderFolder> root;
  if (BindFsFolder(fdir, root) != S_OK || !root)
  {
    fprintf(stderr, "scripted-statusup: cannot bind FS folder %s\n", OemUtf8(leftDir));
    return 2;
  }

  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *panel = win.leftPanel();
  win.focusPanelForTest(panel);
  QtFolderModel *m = panel->model();

  // Optionally open an archive first (in the LEFT panel) so the Up-focus test
  // exercises the IN-ARCHIVE sub-folder Up path (goParent + parent-frame pop).
  if (archiveName && archiveName[0])
  {
    const UString arcU = GetUnicodeString(archiveName);
    // G.4f : rowForName returns the correct MODEL row in either ShowDots mode.
    bool opened = false;
    {
      const int arcRow = m->rowForName(arcU);
      if (arcRow >= 0)
      {
        m->enterItem(arcRow);
        opened = panel->isInArchive();
      }
    }
    if (!opened)
    {
      fprintf(stderr, "scripted-statusup: could not open archive %s\n", archiveName);
      return 2;
    }
    printf("STATUS: opened archive %s inArchive=%d\n",
        archiveName, panel->isInArchive() ? 1 : 0);
  }

  // --- G.4i : status bar focused-item fields (select the named row, or row 0) ---
  int frow = 0;
  if (arg && arg[0])
  {
    frow = m->rowForName(GetUnicodeString(arg));
    if (frow < 0)
    {
      fprintf(stderr, "scripted-statusup: row %s not found\n", arg);
      return 2;
    }
  }
  win.selectRowForTest(panel, frow);
  win.refreshStatusBarForTest();
  printf("STATUS: focused=%s size=\"%s\" date=\"%s\"\n",
      OemUtf8(m->itemName(frow)),
      win.statusFocSizeText().toUtf8().constData(),
      win.statusFocDateText().toUtf8().constData());
  printf("STATUS: sel=\"%s\"\n", win.statusSelText().toUtf8().constData());

  // --- G.4j : Up-focus restore. Enter the named subfolder, then Up. ---
  if (arg && arg[0])
  {
    const UString sub = GetUnicodeString(arg);
    const int subRow = m->rowForName(sub);
    if (subRow >= 0 && m->isFolder(subRow))
    {
      if (m->enterItem(subRow))
      {
        printf("UPFOCUS: entered=%s now=%s\n",
            OemUtf8(sub), OemUtf8(m->currentPath()));
        panel->onUp();   // the REAL Up path (captures leaf, restores focus)
        const int frow2 = panel->focusedSourceRow();
        const UString focusedName = (frow2 >= 0) ? m->itemName(frow2) : UString();
        const QVector<int> selRows = panel->selectedSourceRows();
        const bool selMatch = (selRows.size() == 1
            && m->itemName(selRows.first()) == sub);
        printf("UPFOCUS: afterUp focused=%s selectedMatch=%d  -> %s\n",
            OemUtf8(focusedName), selMatch ? 1 : 0,
            (focusedName == sub && selMatch) ? "PASS" : "FAIL");
      }
      else
        printf("UPFOCUS: could not enter %s\n", OemUtf8(sub));
    }
    else
      printf("UPFOCUS: %s is not a subfolder (skip Up test)\n", OemUtf8(sub));
  }
  return 0;
}

// === G.4k : scripted dual-pane keyboard cross-panel commands (offscreen) ======
// Live keyboard input can't be synthesized reliably under the offscreen platform
// (same limitation noted for the context menu), so this drives the PUBLIC shell
// commands the panel-local accelerators emit into — proving the cross-panel wiring
// (focus the other panel / focus a path field / mirror the same-or-sub folder).
// `arg` (optional) names a subfolder in leftDir for the Alt+Up/Alt+Right mirror.
static int RunScriptedKey(const UString &leftDir, const UString &rightDir,
    const char *arg)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *left = win.leftPanel();
  QtPanel *right = win.rightPanel();
  win.focusPanelForTest(left);

  // Tab -> OnTab : focus moves to the OTHER panel.
  win.focusOtherPanel();
  printf("KEY: tab focusedIsRight=%d -> %s\n",
      win.focusedPanel() == right ? 1 : 0,
      win.focusedPanel() == right ? "PASS" : "FAIL");
  // Tab again returns to the left panel.
  win.focusOtherPanel();
  printf("KEY: tab-back focusedIsLeft=%d -> %s\n",
      win.focusedPanel() == left ? 1 : 0,
      win.focusedPanel() == left ? "PASS" : "FAIL");

  // Alt+F1 / Alt+F2 -> SetFocusToPath(0 / 1) : focus left / right path field.
  win.setFocusToPath(1);
  printf("KEY: altF2 focusedIsRight=%d -> %s\n",
      win.focusedPanel() == right ? 1 : 0,
      win.focusedPanel() == right ? "PASS" : "FAIL");
  win.setFocusToPath(0);
  printf("KEY: altF1 focusedIsLeft=%d -> %s\n",
      win.focusedPanel() == left ? 1 : 0,
      win.focusedPanel() == left ? "PASS" : "FAIL");

  // Alt+Up -> OnSetSameFolder : the OTHER (right) panel opens the focused (left)
  // panel's SAME folder. Drive from the left panel.
  win.focusPanelForTest(left);
  win.onSetSameFolder();
  {
    const UString lp = left->currentPath();
    const UString rp = right->currentPath();
    printf("KEY: altUp left=%s right=%s same=%d -> %s\n",
        OemUtf8(lp), OemUtf8(rp), lp == rp ? 1 : 0,
        lp == rp ? "PASS" : "FAIL");
  }

  // Alt+Right -> OnSetSubFolder : focus a subfolder named `arg` in the LEFT panel,
  // then mirror it INTO the right panel (which should now show leftDir/arg).
  if (arg && arg[0])
  {
    QtFolderModel *m = left->model();
    const UString sub = GetUnicodeString(arg);
    const int subRow = m->rowForName(sub);
    if (subRow >= 0 && m->isFolder(subRow))
    {
      win.focusPanelForTest(left);
      win.selectRowForTest(left, subRow);   // sets the focused/current row
      // Confirm the panel reports the focused sub-folder the window will read.
      const UString reported = left->focusedSubFolderName();
      win.onSetSubFolder();
      const UString rp = right->currentPath();
      // Expected: leftDir (current left path) + sub + separator.
      UString expect = left->currentFsDirPath();   // trailing separator
      expect += sub;
      // currentPath() carries a trailing separator for a directory; compare leaf.
      const bool match = (rp.Find(expect) == 0);
      printf("KEY: altRight reportedSub=%s right=%s entered=%d -> %s\n",
          OemUtf8(reported), OemUtf8(rp), match ? 1 : 0,
          (reported == sub && match) ? "PASS" : "FAIL");
    }
    else
      printf("KEY: altRight %s is not a subfolder (skip)\n", OemUtf8(sub));
  }
  return 0;
}

// === G.4d : scripted Folders History + address-bar ancestors (offscreen) =====
// `arg` = comma-separated subdir names to descend through under <left-dir>, e.g.
// "a,b". For each, navigate the focused (left) panel INTO it (navigateToFsPath, the
// BindToPathAndRefresh analogue, which fires pathChanged -> recordFolderHistory).
// Then dump:
//   HIST: the recorded history (most-recent-first), proving each visited folder was
//         recorded and deduped/most-recent-first.
//   ANCESTOR: the address-bar dropdown's ancestor breadcrumb (root-first) for the
//         current path, proving the indented ancestor walk.
// Finally drive historyNavigateForTest(1) (the picker's "navigate to a recorded
// entry" outcome) and confirm the panel moved there.
static int RunScriptedHistory(const UString &leftDir, const UString &rightDir,
    const char *arg)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *left = win.leftPanel();
  win.focusPanelForTest(left);

  // Build the start path with a trailing separator for clean joins.
  UString base = leftDir;
  if (!base.IsEmpty() && !IS_PATH_SEPAR(base.Back()))
    base.Add_PathSepar();
  // The window's initial bind already recorded the start dir; re-bind it to be sure
  // the start is the head of the history regardless of ctor timing.
  left->navigateToFsPath(base);

  // Descend through each named subdir, recording it.
  AString a(arg ? arg : "");
  int start = 0;
  const int len = (int)a.Len();
  UString cur = base;
  for (int j = 0; j <= len; j++)
  {
    if (j < len && a[(unsigned)j] != ',')
      continue;
    AString tok = a.Mid((unsigned)start, (unsigned)(j - start));
    start = j + 1;
    if (tok.IsEmpty())
      continue;
    cur += GetUnicodeString(tok);
    cur.Add_PathSepar();
    const bool ok = left->navigateToFsPath(cur);
    printf("HIST: navigate \"%s\" ok=%d\n", OemUtf8(cur), ok ? 1 : 0);
  }

  // Dump the recorded history (most-recent-first).
  const UStringVector hist = win.folderHistoryForTest();
  printf("HIST: count=%d\n", (int)hist.Size());
  FOR_VECTOR (i, hist)
    printf("HIST_ENTRY: [%u] %s\n", i, OemUtf8(hist[i]));

  // Dump the address-bar dropdown ancestor breadcrumb (root-first) for the current
  // path. Indent is implicit in the order; we print depth so the breadcrumb is clear.
  const UStringVector anc = left->addressAncestors();
  printf("ANCESTOR: count=%d for %s\n", (int)anc.Size(), OemUtf8(left->currentPath()));
  FOR_VECTOR (i, anc)
    printf("ANCESTOR_ENTRY: depth=%u %s\n", i, OemUtf8(anc[i]));

  // Drive the picker's navigate-to-entry outcome: entry [1] is the second-most-recent
  // folder (the parent of the deepest, in a simple a->b descent). Navigate there.
  if (hist.Size() >= 2)
  {
    const UString target = hist[1];
    const bool ok = win.historyNavigateForTest(1);
    const UString now = left->currentPath();
    const bool atTarget = (now == target);
    printf("HIST: pickerNavigate to [1]=%s ok=%d now=%s -> %s\n",
        OemUtf8(target), ok ? 1 : 0, OemUtf8(now),
        (ok && atTarget) ? "PASS" : "FAIL");
  }
  return 0;
}

// === G.5f : scripted Edit -> Copy (item names -> clipboard) ==================
// Select the LEFT panel's items (or a single named one), run onEditCopy via the menu
// action target, and read the clipboard back. Prints the EDIT_COPY_CLIPBOARD marker
// (headless path) AND a QClipboard read-back so the harness can assert both.
static int RunScriptedEditCopy(const UString &leftDir, const UString &rightDir,
    const char *arg)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *left = win.leftPanel();
  win.focusPanelForTest(left);
  QtFolderModel *m = left->model();

  if (arg && arg[0])
  {
    const int r = m->rowForName(GetUnicodeString(arg));
    if (r < 0)
    {
      fprintf(stderr, "edit-copy: row %s not found\n", arg);
      return 2;
    }
    win.selectRowForTest(left, r);
  }
  else
  {
    // Select EVERY loaded row explicitly (selectAll on an offscreen view does not
    // reliably populate the selection model), so the multi-name CRLF join is exercised.
    const int n = m->rowCount();
    QVector<int> all;
    all.reserve(n);
    for (int r = 0; r < n; r++)
      if (!m->isParentRow(r))
        all.push_back(r);
    win.selectRowsForTest(left, all);
  }

  // Run the SAME core the Edit -> Copy menu slot invokes (editCopySelectedNames).
  win.editCopySelectedNames();

  // QClipboard read-back (proves the names actually reached the system clipboard).
  QClipboard *cb = QApplication::clipboard();
  if (cb)
  {
    const QByteArray u8 = cb->text().toUtf8();
    printf("EDIT_COPY_READBACK: %s\n", u8.constData());
    fflush(stdout);
  }
  return 0;
}

// === LINK-TARGET : scripted current-target read-back for an existing symlink =====
static int RunScriptedLinkTarget(const UString &leftDir, const UString &rightDir,
    const char *arg)
{
  if (!arg || !arg[0])
  {
    fprintf(stderr, "link-target: expected an item name\n");
    return 2;
  }
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *left = win.leftPanel();
  win.focusPanelForTest(left);
  const UString name = GetUnicodeString(arg);
  const UString target = win.linkTargetForTest(left, name);
  printf("LINK_TARGET: %s -> %s\n", arg, OemUtf8(target));
  fflush(stdout);
  return target.IsEmpty() ? 1 : 0;
}

// === G.4h : scripted Open Root Folder ('\') ==================================
// Mirror of CPanel::OpenRootFolder -> SetToRootFolder (CRootFolder, whose sole
// non-Windows item "Computer" binds a CFSFolder rooted at "/"). Two modes:
//   FS:      start in leftDir, then Open Root Folder -> the focused panel is at "/".
//   archive: with --arc-name=<leaf>, first open that archive in the left panel
//            (we are inArchive), then Open Root Folder -> we exit to "/" (the
//            archive is released, navigateToFsPath drops _inArchive/_parentStack).
static int RunScriptedOpenRoot(const UString &leftDir, const UString &rightDir,
    const char *archiveName)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *left = win.leftPanel();
  win.focusPanelForTest(left);

  if (archiveName && archiveName[0])
  {
    // Open the archive first so Open Root Folder must EXIT it to reach "/".
    const UString arcU = GetUnicodeString(archiveName);
    QtFolderModel *lm = left->model();
    const int arcRow = lm->rowForName(arcU);
    if (arcRow < 0)
    {
      fprintf(stderr, "open-root: archive %s not found in %s\n",
          archiveName, OemUtf8(leftDir));
      return 2;
    }
    lm->enterItem(arcRow);
    if (!left->isInArchive())
    {
      fprintf(stderr, "open-root: could not open archive %s\n", archiveName);
      return 2;
    }
    printf("OPENROOT: opened archive %s inArchive=%d at %s\n",
        archiveName, left->isInArchive() ? 1 : 0, OemUtf8(left->currentPath()));
  }

  printf("OPENROOT: before path=%s inArchive=%d\n",
      OemUtf8(left->currentPath()), left->isInArchive() ? 1 : 0);

  // The action under test: CPanel::OpenRootFolder -> "/".
  win.openRootFolder();

  const UString now = left->currentPath();
  const bool inArc = left->isInArchive();
  // kpidPath of a CFSFolder rooted at "/" is "/". Pass iff we are at "/" and the
  // archive (if any) was released.
  const bool atRoot = (now == UString(WSTRING_PATH_SEPARATOR));
  printf("OPENROOT: after path=%s inArchive=%d -> %s\n",
      OemUtf8(now), inArc ? 1 : 0,
      (atRoot && !inArc) ? "PASS" : "FAIL");
  return (atRoot && !inArc) ? 0 : 1;
}

// === G.4g : scripted one/two-panel toggle + persistence =======================
// Mirror of CApp::SwitchOnOffOnePanel (App.cpp:360) + the AppState NumPanels
// persistence (CApp::Save/Read). Two phases share one HOME:
//   "set" (run 1): build the LEFT-focused window (two-panel), toggle to single-
//     panel, and assert (a) only the focused (left) panel is shown and the other
//     (right) panel is HIDDEN — not destroyed; (b) otherPanel() still returns that
//     hidden right panel and resolves its last FS dir (== rightDir); (c) a real
//     Copy of a probe file still lands in that hidden panel's dir (doCopyMove reads
//     otherPanel()->currentFsDirPath()); (d) NumPanels==1 was persisted.
//   "get" (run 2, fresh process, same HOME): build a fresh window and assert the
//     persisted single-panel layout is restored (right panel hidden at startup).
static int RunScriptedOnePanel(const UString &leftDir, const UString &rightDir,
    const char *phase)
{
  const bool getPhase = (phase && strcmp(phase, "get") == 0);

  if (getPhase)
  {
    // Run 2 : a fresh window must restore the persisted single-panel layout.
    const int persisted = QtFmSettings::ReadNumPanels(2);
    QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
    const bool leftShown  = !win.leftPanel()->isHidden();
    const bool rightShown = !win.rightPanel()->isHidden();
    const bool single = win.isSinglePanel();
    printf("ONE_PANEL_GET: persistedNumPanels=%d isSinglePanel=%d "
        "leftShown=%d rightShown=%d\n",
        persisted, single ? 1 : 0, leftShown ? 1 : 0, rightShown ? 1 : 0);
    // Faithful restore: NumPanels==1, single-panel mode, left visible, right hidden.
    const bool pass = (persisted == 1) && single && leftShown && !rightShown;
    printf("ONE_PANEL_GET: restore -> %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
  }

  // Run 1 : start in two-panel, drop a probe file in the LEFT dir to copy across.
  UString lbase = leftDir;
  if (!lbase.IsEmpty() && !IS_PATH_SEPAR(lbase.Back()))
    lbase.Add_PathSepar();
  UString rbase = rightDir;
  if (!rbase.IsEmpty() && !IS_PATH_SEPAR(rbase.Back()))
    rbase.Add_PathSepar();
  const UString probeName = L"__one_panel_probe__.txt";
  const UString srcProbe = lbase + probeName;
  const UString dstProbe = rbase + probeName;
  const QString srcProbeQ = QString::fromWCharArray(srcProbe.Ptr(), (int)srcProbe.Len());
  const QString dstProbeQ = QString::fromWCharArray(dstProbe.Ptr(), (int)dstProbe.Len());
  QFile::remove(dstProbeQ);   // clean any stale copy from a prior run
  {
    QFile f(srcProbeQ);
    if (!f.open(QIODevice::WriteOnly))
    {
      fprintf(stderr, "scripted-one-panel: cannot create probe file %s\n",
          OemUtf8(srcProbe));
      return 2;
    }
    f.write("one-panel");
    f.close();
  }

  // Force two-panel as the starting state (a prior run may have persisted 1), so
  // this phase always exercises the 2 -> 1 toggle deterministically.
  QtFmSettings::SaveNumPanels(2);

  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  win.focusPanelForTest(win.leftPanel());   // left is the focused/source panel

  printf("ONE_PANEL: startSinglePanel=%d leftHidden=%d rightHidden=%d\n",
      win.isSinglePanel() ? 1 : 0,
      win.leftPanel()->isHidden() ? 1 : 0, win.rightPanel()->isHidden() ? 1 : 0);

  // The hidden-panel destination BEFORE the toggle (otherPanel() == right panel).
  QtPanel *otherBefore = win.otherPanel();
  const UString destBefore = otherBefore ? otherBefore->currentFsDirPath() : UString();

  // Toggle to single-panel (F9 / IDM_VIEW_TWO_PANELS -> SwitchOnOffOnePanel).
  win.switchOnOffOnePanel();

  // (a) Only the focused (left) panel is shown; the right panel is HIDDEN.
  const bool single = win.isSinglePanel();
  const bool leftShown  = !win.leftPanel()->isHidden();
  const bool rightHidden = win.rightPanel()->isHidden();
  printf("ONE_PANEL: afterToggle isSinglePanel=%d leftShown=%d rightHidden=%d\n",
      single ? 1 : 0, leftShown ? 1 : 0, rightHidden ? 1 : 0);

  // (b) otherPanel() still returns the (hidden) right panel and resolves its dir.
  QtPanel *otherAfter = win.otherPanel();
  const bool otherSame = (otherAfter == otherBefore) && (otherAfter == win.rightPanel());
  const UString destAfter = otherAfter ? otherAfter->currentFsDirPath() : UString();
  const bool destResolves = !destAfter.IsEmpty()
      && destAfter.IsEqualTo_NoCase(destBefore);
  printf("ONE_PANEL: otherPanelValid=%d destResolves=%d dest=%s\n",
      otherSame ? 1 : 0, destResolves ? 1 : 0, OemUtf8(destAfter));

  // (c) A real Copy in single-panel mode must still land in the hidden panel's dir.
  const int probeRow = win.leftPanel()->model()->rowForName(probeName);
  win.selectRowForTest(win.leftPanel(), probeRow);
  win.doCopyMove(/*moveMode*/ false);
  const bool copiedToOther = QFile::exists(dstProbeQ);
  printf("ONE_PANEL: copyLandedInHiddenPanelDir=%d (%s)\n",
      copiedToOther ? 1 : 0, OemUtf8(dstProbe));

  // (d) NumPanels==1 must be persisted (CApp::Save).
  const int persisted = QtFmSettings::ReadNumPanels(2);
  printf("ONE_PANEL: persistedNumPanels=%d\n", persisted);

  // Clean up the probe files (leave the persisted NumPanels==1 for --one-panel-get).
  QFile::remove(srcProbeQ);
  QFile::remove(dstProbeQ);

  const bool pass = single && leftShown && rightHidden
      && otherSame && destResolves && copiedToOther && (persisted == 1);
  printf("ONE_PANEL: result -> %s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// === G.4e : scripted Auto Refresh (FS directory-change watcher) ===============
// Mirror of CPanel::AutoRefresh_Mode + OnTimer (PanelItems.cpp:1435), proven via
// the QFileSystemWatcher analogue: enable Auto Refresh on the LEFT FS panel, print
// the listing count, then create (and later delete) a file in the watched directory
// and pump the event loop so the watcher delivers directoryChanged -> the panel
// re-lists. Prints the BEFORE / AFTER-create / AFTER-delete real listing counts; the
// AFTER-create count must be BEFORE+1 and the AFTER-delete count back to BEFORE,
// proving the panel re-listed to the new on-disk state without a manual Refresh.
static int RunScriptedAutoRefresh(const UString &leftDir, const UString &rightDir)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *left = win.leftPanel();
  win.focusPanelForTest(left);

  QtFolderModel *m = left->model();
  if (!m)
  {
    fprintf(stderr, "scripted-auto-refresh: no left model\n");
    return 2;
  }

  // Pump the Qt event loop for up to `ms` milliseconds OR until the model's real
  // listing count reaches `wantCount` — whichever comes first. inotify delivers the
  // directoryChanged signal through the event loop, so a bare grab won't see it.
  auto pumpUntilCount = [&](int wantCount, int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms && m->realItemCount() != wantCount)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  };

  // Build the watched directory path with a trailing separator for clean joins.
  UString base = leftDir;
  if (!base.IsEmpty() && !IS_PATH_SEPAR(base.Back()))
    base.Add_PathSepar();

  const int before = m->realItemCount();
  printf("AUTO_REFRESH: enabled before-count=%d dir=%s\n", before, OemUtf8(base));

  // Turn Auto Refresh ON — points the watcher at the current FS directory.
  left->setAutoRefresh(true);
  printf("AUTO_REFRESH: isAutoRefresh=%d\n", left->isAutoRefresh() ? 1 : 0);

  // Create a NEW file in the watched directory (on disk, behind the panel's back).
  const UString newPath = base + L"__auto_refresh_probe__.txt";
  const QString newPathQ = QString::fromWCharArray(newPath.Ptr(), (int)newPath.Len());
  {
    QFile f(newPathQ);
    if (!f.open(QIODevice::WriteOnly))
    {
      fprintf(stderr, "scripted-auto-refresh: cannot create probe file %s\n",
          OemUtf8(newPath));
      return 2;
    }
    f.write("x");
    f.close();
  }
  // Let the watcher fire + the panel re-list (expect before+1).
  pumpUntilCount(before + 1, 4000);
  const int afterCreate = m->realItemCount();
  printf("AUTO_REFRESH: after-create count=%d (want %d) -> %s\n",
      afterCreate, before + 1, afterCreate == before + 1 ? "PASS" : "FAIL");

  // Delete the probe file; the panel must re-list back to the original count.
  QFile::remove(newPathQ);
  pumpUntilCount(before, 4000);
  const int afterDelete = m->realItemCount();
  printf("AUTO_REFRESH: after-delete count=%d (want %d) -> %s\n",
      afterDelete, before, afterDelete == before ? "PASS" : "FAIL");

  // Turn it OFF and confirm the flag drops (drops the watcher too).
  left->setAutoRefresh(false);
  printf("AUTO_REFRESH: disabled isAutoRefresh=%d\n", left->isAutoRefresh() ? 1 : 0);

  return (afterCreate == before + 1 && afterDelete == before) ? 0 : 1;
}

// === B.5b : scripted in-archive Delete / Add (offscreen) =====================
// Opens an archive in the LEFT panel via the seamless FS->archive path (the same
// enterItem -> tryOpenAsArchive the GUI uses), then drives the REAL FM op:
//   ArcDelete : select the entry named `arg` and call win.doDelete()
//               (archive Delete via QtFsDeleteWorker + QtArchiveUpdateCallback).
//   ArcAdd    : add the RIGHT panel's selected FS files into the open archive via
//               win.doCopyMove(false) (focused=RIGHT FS source, other=LEFT archive).
//
// `leftDir`     : the directory CONTAINING the archive (LEFT panel start).
// `archiveName` : the archive's leaf name within leftDir.
// `rightDir`    : for ArcAdd, the FS dir whose selection is added.
// `arg`         : ArcDelete -> entry name to delete.
static int RunScriptedArc(const UString &leftDir, const UString &rightDir,
    const char *archiveName, ScriptOp op, const char *arg)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *left = win.leftPanel();
  QtPanel *right = win.rightPanel();

  // Open the archive in the LEFT panel: find its row, enterItem (-> leafActivated
  // -> tryOpenAsArchive seamless open).
  const UString arcU = GetUnicodeString(archiveName);
  QtFolderModel *lm = left->model();
  // G.4f : find by name via rowForName() (returns the correct MODEL row whether or
  // not ShowDots shifts the rows by the ".." entry), then enterItem() that MODEL row.
  // A raw [0,GetNumberOfItems()) loop with itemName((int)i) would mis-bound / shift
  // when ShowDots is on (the harness used to assume model row == engine index).
  bool opened = false;
  {
    const int arcRow = lm->rowForName(arcU);
    if (arcRow >= 0)
    {
      lm->enterItem(arcRow);
      opened = left->isInArchive();
    }
  }
  if (!opened)
  {
    fprintf(stderr, "scripted-arc: could not open archive %s in %s\n",
        archiveName, OemUtf8(leftDir));
    return 2;
  }
  printf("opened archive %s : inArchive=%d updatable=%d\n",
      archiveName, left->isInArchive() ? 1 : 0,
      left->isUpdatableArchive() ? 1 : 0);

  // G.4b : header column chooser on the OPEN archive (an archive folder shows its
  // full column set; nothing is default-hidden, and the Name lock / sort-reset
  // rules still apply).
  if (op == ScriptOp::ColList || op == ScriptOp::ColHide)
  {
    win.focusPanelForTest(left);
    return RunScriptedColumns(win, left, op, arg);
  }

  // G.4c : Flat View on the OPEN archive (CAgentFolder::SetFlatMode recurses the
  // proxy tree; each descendant reports its subfolder path via kpidPrefix).
  if (op == ScriptOp::FlatView)
    return RunScriptedFlat(win, left);

  if (op == ScriptOp::ArcDelete)
  {
    // Focus the LEFT (archive) panel and select ONLY the named entry.
    QTreeView *lv = left->findChild<QTreeView *>();
    if (!lv) { fprintf(stderr, "scripted-arc: no left view\n"); return 2; }
    win.focusPanelForTest(left);
    const UString entryU = arg ? GetUnicodeString(arg) : UString();
    int row = -1;
    UInt32 ni = 0;
    if (left->currentFolder())
      left->currentFolder()->GetNumberOfItems(&ni);
    for (UInt32 i = 0; i < ni; i++)
      // G.4f : iterate ENGINE indices (itemNameByRealIndex), store the MODEL row.
      if (lm->itemNameByRealIndex((int)i) == entryU) { row = lm->realIndexToModelRow((int)i); break; }
    if (row < 0)
    {
      fprintf(stderr, "scripted-arc: entry %s not found in archive\n", arg);
      return 2;
    }
    win.selectRowForTest(left, row);
    printf("---- arc-delete entry \"%s\" (row %d) ----\n", arg, row);
    win.doDelete();
    printf("arc-delete done\n");
    return 0;
  }

  if (op == ScriptOp::ArcInlineRename)
  {
    // G.4l : inline rename INSIDE the open updatable archive, via the model's
    // setData() (the F2 / slow-double-click commit). `arg` = "<entry>,<newName>";
    // after setData the panel's itemRenamed slot re-binds from the reopened agent
    // root (Close()+ReOpen() the engine did inside Rename()).
    QByteArray ab(arg ? arg : "");
    const int comma = ab.indexOf(',');
    QByteArray e = comma >= 0 ? ab.left(comma) : ab;
    QByteArray nn = comma >= 0 ? ab.mid(comma + 1) : QByteArray();
    printf("---- arc-inline-rename ----\n");
    return RunInlineRename(win, left, e.constData(), nn.constData());
  }

  if (op == ScriptOp::ArcAdd)
  {
    // RIGHT panel is the FS source; select all, focus it, then Copy To -> the
    // other (LEFT, archive) panel triggers the add-into-archive path.
    QTreeView *rv = right->findChild<QTreeView *>();
    if (rv) rv->selectAll();
    win.focusPanelForTest(right);
    printf("---- arc-add (right FS selection -> archive) ----\n");
    win.doCopyMove(false);
    printf("arc-add done\n");
    return 0;
  }

  if (op == ScriptOp::ArcDragOut)
  {
    // B.5c : drag-OUT (extract-to-temp). Select the named entry in the LEFT
    // (archive) panel (or ALL if no name), focus it, then exercise the new
    // drag-source path (buildDragMimeData -> extractSelectionToTempMime) and
    // print each resulting temp file:// URI for the harness to verify.
    QTreeView *lv = left->findChild<QTreeView *>();
    if (!lv) { fprintf(stderr, "scripted-arc: no left view\n"); return 2; }
    if (arg && arg[0])
    {
      const UString entryU = GetUnicodeString(arg);
      int row = -1;
      UInt32 ni = 0;
      if (left->currentFolder())
        left->currentFolder()->GetNumberOfItems(&ni);
      for (UInt32 i = 0; i < ni; i++)
        // G.4f : iterate ENGINE indices (itemNameByRealIndex), store the MODEL row.
      if (lm->itemNameByRealIndex((int)i) == entryU) { row = lm->realIndexToModelRow((int)i); break; }
      if (row < 0)
      {
        fprintf(stderr, "scripted-arc: entry %s not found in archive\n", arg);
        return 2;
      }
      win.focusPanelForTest(left);
      win.selectRowForTest(left, row);
    }
    else
    {
      win.focusPanelForTest(left);
      lv->selectAll();
    }
    printf("---- arc-dragout (extract-to-temp) ----\n");
    QMimeData *mime = left->buildDragMimeData();   // B.5c : now extracts to temp
    if (!mime) { printf("  FAIL: no mime\n"); return 1; }
    const QList<QUrl> urls = mime->urls();
    printf("  hasUrls=%d count=%lld\n",
        mime->hasUrls() ? 1 : 0, (long long)urls.size());
    for (const QUrl &u : urls)
      printf("    TEMP_URI: %s  local=%s\n",
          u.toString().toUtf8().constData(),
          u.toLocalFile().toUtf8().constData());
    delete mime;
    printf("arc-dragout done\n");
    return 0;
  }

  if (op == ScriptOp::ArcHash)
  {
    // G.2d : in-archive CRC/checksum. Select the named entry in the LEFT (archive)
    // panel (or ALL if no name), focus it, then run the in-archive hash via the
    // real doHash() path (which dispatches to doHashInArchive when inArchive). In
    // headless mode doHashInArchive prints "HASH: <name> = <value>" lines, so the
    // harness can compare the digest against `7zz h -scrc<method> <extracted>`.
    QTreeView *lv = left->findChild<QTreeView *>();
    if (!lv) { fprintf(stderr, "scripted-arc: no left view\n"); return 2; }
    win.focusPanelForTest(left);
    if (arg && arg[0])
    {
      const UString entryU = GetUnicodeString(arg);
      int row = -1;
      UInt32 ni = 0;
      if (left->currentFolder())
        left->currentFolder()->GetNumberOfItems(&ni);
      for (UInt32 i = 0; i < ni; i++)
        // G.4f : iterate ENGINE indices (itemNameByRealIndex), store the MODEL row.
      if (lm->itemNameByRealIndex((int)i) == entryU) { row = lm->realIndexToModelRow((int)i); break; }
      if (row < 0)
      {
        fprintf(stderr, "scripted-arc: entry %s not found in archive\n", arg);
        return 2;
      }
      win.selectRowForTest(left, row);
    }
    else
    {
      lv->selectAll();
    }
    const char *method = g_ArcHashMethod ? g_ArcHashMethod : "CRC32";
    printf("---- arc-hash entry \"%s\" method \"%s\" ----\n",
        arg ? arg : "(all)", method);
    win.doHash(method);
    printf("arc-hash done\n");
    return 0;
  }

  if (op == ScriptOp::ArcArrange)
  {
    // G.4m : arrange the OPEN archive by a sort key and dump the proxy order. This
    // is the path that proves Date/numeric columns sort by VALUE (not by the
    // formatted/locale Modified text). `arg` = "name|type|date|size" (default date).
    win.focusPanelForTest(left);
    int propID = kpidMTime; const char *name = "date";
    if (arg && arg[0])
    {
      if      (strcmp(arg, "name") == 0) { propID = kpidName;      name = "name"; }
      else if (strcmp(arg, "type") == 0) { propID = kpidExtension; name = "type"; }
      else if (strcmp(arg, "date") == 0) { propID = kpidMTime;     name = "date"; }
      else if (strcmp(arg, "size") == 0) { propID = kpidSize;      name = "size"; }
    }
    win.onArrange(propID, /*unsorted*/ false);
    QSortFilterProxyModel *proxy =
        qobject_cast<QSortFilterProxyModel *>(left->view()->model());
    printf("ARC_ARRANGE: key=%s\n", name);
    const int rows = proxy ? proxy->rowCount() : 0;
    for (int i = 0; i < rows; i++)
    {
      const QModelIndex p = proxy->index(i, 0);
      const int srcRow = proxy->mapToSource(p).row();
      // Print the entry name + its raw mtime text so the harness can confirm the
      // chronological order against `7zz l -slt`.
      printf("ARC_ARRANGE_ROW: %s  mtime=%s\n",
          OemUtf8(lm->itemName(srcRow)),
          lm->focusedMTimeString(srcRow).toUtf8().constData());
    }
    printf("arc-arrange done\n");
    return 0;
  }

  // === G.5a : multi-select Properties aggregate on the OPEN archive ===========
  // Select EVERY entry in the open archive and dump the aggregate summary
  // (count + total/packed Size + Files/Folders) — PanelMenu.cpp:260-295.
  if (op == ScriptOp::PropsMulti)
  {
    win.focusPanelForTest(left);
    UInt32 ni = 0;
    if (left->currentFolder())
      left->currentFolder()->GetNumberOfItems(&ni);
    QVector<int> rows;
    for (UInt32 i = 0; i < ni; i++)
      rows.append(lm->realIndexToModelRow((int)i));
    win.selectRowsForTest(left, rows);
    printf("---- arc-props-multi (%d entries selected) ----\n", (int)rows.size());
    win.doProperties();   // headless -> dumps the aggregate PROP rows
    printf("arc-props-multi done\n");
    return 0;
  }

  // === B.7a : Properties / Comment / View on an OPEN archive entry ============
  if (op == ScriptOp::Props || op == ScriptOp::CommentGet
      || op == ScriptOp::CommentSet || op == ScriptOp::OpenView
      || op == ScriptOp::OpenEdit
      || op == ScriptOp::PropsInteract)   // G.5b
  {
    // Select the named entry in the LEFT (archive) panel.
    win.focusPanelForTest(left);
    const UString entryU = arg ? GetUnicodeString(arg) : UString();
    int row = -1;
    UInt32 ni = 0;
    if (left->currentFolder())
      left->currentFolder()->GetNumberOfItems(&ni);
    for (UInt32 i = 0; i < ni; i++)
      // G.4f : iterate ENGINE indices (itemNameByRealIndex), store the MODEL row.
      if (lm->itemNameByRealIndex((int)i) == entryU) { row = lm->realIndexToModelRow((int)i); break; }
    if (row < 0)
    {
      fprintf(stderr, "scripted-arc: entry %s not found in archive\n", arg);
      return 2;
    }
    win.selectRowForTest(left, row);

    if (op == ScriptOp::Props)
    {
      printf("---- arc-props entry \"%s\" (row %d) ----\n", arg, row);
      win.doProperties();   // headless -> dumps PROP: name = value
      printf("arc-props done\n");
      return 0;
    }
    if (op == ScriptOp::PropsInteract)   // G.5b
    {
      printf("---- arc-props-interact entry \"%s\" (row %d) ----\n", arg, row);
      win.doPropertiesInteractTest(/*valueViewerRow*/ 0);
      printf("arc-props-interact done\n");
      return 0;
    }
    if (op == ScriptOp::CommentGet)
    {
      printf("---- arc-comment-get entry \"%s\" (row %d) ----\n", arg, row);
      win.doComment(UString(), /*commentGet*/ true);   // prints COMMENT: ...
      return 0;
    }
    if (op == ScriptOp::CommentSet)
    {
      // arg is "<entry>|<text>" : entry already selected above from <entry>; the
      // text is passed separately (see main()'s parse).
      const UString text = g_CommentText ? GetUnicodeString(g_CommentText) : UString();
      printf("---- arc-comment-set entry \"%s\" = \"%s\" ----\n",
          arg, g_CommentText ? g_CommentText : "");
      win.doComment(text);
      printf("arc-comment-set done\n");
      return 0;
    }
    // G.2a : --arc-edit=<entry> drives the FULL Edit write-back into the archive.
    // The GUI QFileSystemWatcher can't be triggered without a real editor, so the
    // headless hook extracts <entry>, OVERWRITES the temp (env SEVENZQT_EDIT_CONTENT,
    // default "EDITED-BY-G2A\n"), then runs the SAME CopyFromFile path the watcher
    // would. A test can then `7zz x`/`7zz t` the archive to confirm the new bytes.
    if (op == ScriptOp::OpenEdit)
    {
      printf("---- arc-edit entry \"%s\" (row %d) ----\n", arg, row);
      const bool ok = win.arcEditWriteBackForTest(row);
      printf("arc-edit done ok=%d\n", ok ? 1 : 0);
      return ok ? 0 : 1;
    }
    // OpenView on an archive entry -> extract-to-temp + read-only open.
    printf("---- arc-view entry \"%s\" (row %d) ----\n", arg, row);
    win.doOpen((int)QtFileManagerWindow::View);
    printf("arc-open done\n");
    return 0;
  }
  return 1;
}

// === G.2b : scripted nested-archive entry self-check (offscreen) =============
// Opens <leftDir>/<outerLeaf> in the LEFT panel (the seamless FS->archive open),
// finds <innerEntry> (an archive INSIDE the outer archive), and enters it through
// the REAL onLeafActivated -> tryOpenNestedArchive path (model->enterItem on a leaf
// emits leafActivated). It then:
//   (a) dumps the NESTED listing as "NESTED: <name> <size>" lines so the harness
//       can diff against `7zz l <innerArchive>`;
//   (b) calls onUp() to pop back to the OUTER archive and prints "NESTED-UP: ok"
//       (re-listing the OUTER contents as "OUTER: <name>") proving the return.
// SEVENZQT_TEST_PASSWORD drives an encrypted nesting (QtPasswordPrompt reads it).
static int RunScriptedNested(const UString &leftDir, const UString &rightDir,
    const char *outerLeaf, const char *innerEntry)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *left = win.leftPanel();
  QtFolderModel *lm = left->model();

  // (1) Open the OUTER archive (FS -> archive seamless entry).
  const UString outerU = GetUnicodeString(outerLeaf);
  UInt32 n = 0;
  if (left->currentFolder())
    left->currentFolder()->GetNumberOfItems(&n);
  bool opened = false;
  for (UInt32 i = 0; i < n; i++)
    if (lm->itemName((int)i) == outerU)
    {
      lm->enterItem((int)i);          // -> leafActivated -> tryOpenAsArchive
      opened = left->isInArchive();
      break;
    }
  if (!opened)
  {
    fprintf(stderr, "arc-nested: could not open outer archive %s in %s\n",
        outerLeaf, OemUtf8(leftDir));
    return 2;
  }

  // (2) Find <innerEntry> in the OUTER listing and enter it as a NESTED sub-folder.
  const UString innerU = GetUnicodeString(innerEntry);
  UInt32 no = 0;
  if (left->currentFolder())
    left->currentFolder()->GetNumberOfItems(&no);
  bool entered = false;
  for (UInt32 i = 0; i < no; i++)
    if (lm->itemName((int)i) == innerU)
    {
      // enterItem on a LEAF emits leafActivated -> onLeafActivated, whose _inArchive
      // branch calls tryOpenNestedArchive (G.2b). A successful nested open swaps the
      // model to the nested root.
      lm->enterItem((int)i);
      entered = left->isInArchive();   // still in an archive (now the NESTED one)
      break;
    }
  if (!entered)
  {
    fprintf(stderr, "arc-nested: inner entry %s not found / not entered in %s\n",
        innerEntry, outerLeaf);
    return 3;
  }

  // (3) Dump the NESTED listing (name + size) for the diff against `7zz l`.
  UInt32 ni = 0;
  if (left->currentFolder())
    left->currentFolder()->GetNumberOfItems(&ni);
  for (UInt32 i = 0; i < ni; i++)
  {
    UInt64 size = 0;
    {
      NCOM::CPropVariant pv;
      if (left->currentFolder()->GetProperty(i, kpidSize, &pv) == S_OK
          && pv.vt == VT_UI8)
        size = pv.uhVal.QuadPart;
    }
    printf("NESTED: %s %llu\n", OemUtf8(lm->itemName((int)i)),
        (unsigned long long)size);
  }
  printf("NESTED-COUNT: %u updatable=%d\n", ni,
      left->isUpdatableArchive() ? 1 : 0);

  // (4) Up out of the nested archive -> back to the OUTER archive. Prove it by
  // re-listing the OUTER contents (which must again contain <innerEntry>).
  left->onUp();
  bool backInOuter = left->isInArchive();
  UInt32 nb = 0;
  if (left->currentFolder())
    left->currentFolder()->GetNumberOfItems(&nb);
  bool sawInner = false;
  for (UInt32 i = 0; i < nb; i++)
  {
    const UString nm = lm->itemName((int)i);
    if (nm == innerU)
      sawInner = true;
    printf("OUTER: %s\n", OemUtf8(nm));
  }
  printf("NESTED-UP: %s (inArchive=%d sawInner=%d)\n",
      (backInOuter && sawInner) ? "ok" : "FAIL",
      backInOuter ? 1 : 0, sawInner ? 1 : 0);
  return (backInOuter && sawInner) ? 0 : 4;
}

// === P.1 : scripted threaded archive-open self-check (offscreen) =============
// Opens <leftDir>/<archiveName> in the LEFT panel THROUGH THE WORKER PATH (the
// panel's force-thread hook makes the open run on a QtProgressThreadVirt worker
// regardless of file size), then:
//   (a) dumps the root listing (ENTRY: <name>) so the harness can diff it
//       against `7zz l <arc>` (root-level names);
//   (b) prints GUI_THREAD / and the worker prints OPEN_THREAD (stderr) so the
//       harness can assert the open ran on a DIFFERENT QThread than the GUI.
static int RunScriptedOpenArchive(const UString &leftDir, const UString &rightDir,
    const char *archiveName)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *left = win.leftPanel();

  // Force the threaded open path even for a tiny fixture (so the off-GUI-thread
  // open is actually exercised). In the GUI build this stays off (size gates).
  left->setForceThreadOpen(true);

  printf("GUI_THREAD gui=%p\n", (void *)QThread::currentThread());

  const UString arcU = GetUnicodeString(archiveName);
  QtFolderModel *lm = left->model();
  UInt32 n = 0;
  if (left->currentFolder())
    left->currentFolder()->GetNumberOfItems(&n);
  bool opened = false;
  for (UInt32 i = 0; i < n; i++)
  {
    if (lm->itemName((int)i) == arcU)
    {
      lm->enterItem((int)i);       // -> leafActivated -> tryOpenAsArchive (worker)
      opened = left->isInArchive();
      break;
    }
  }
  if (!opened)
  {
    fprintf(stderr, "open-archive: could not open archive %s in %s\n",
        archiveName, OemUtf8(leftDir));
    return 2;
  }

  UInt32 ni = 0;
  if (left->currentFolder())
    left->currentFolder()->GetNumberOfItems(&ni);
  for (UInt32 i = 0; i < ni; i++)
    printf("ENTRY: %s\n", OemUtf8(lm->itemName((int)i)));
  printf("open-archive done count=%u updatable=%d\n",
      ni, left->isUpdatableArchive() ? 1 : 0);
  return 0;
}

// === P.3 : deleted-in-archive ForegroundRole self-check (offscreen) ==========
// Opens <leftDir>/<archiveName> in the LEFT panel, then for every row asserts the
// deleted-red mapping is faithful to CPanel::OnCustomDraw's deleted-item branch:
//   (a) row-for-row, data(ForegroundRole).isValid() == (kpidIsDeleted==true).
//       A normal 7z/zip row has kpidIsDeleted=false -> NO ForegroundRole (no
//       spurious red; the QPalette drives the text color = dark-safe). Any row
//       with kpidIsDeleted=true -> a red brush (deletedTextColor()).
//   (b) the _markDeletedItems-equivalent gate: toggling it OFF must drop EVERY
//       red row (proves the gate without needing a real kpidIsDeleted disk image,
//       which no `7zz a` can produce); toggling it back ON restores the mapping.
// Also prints the resolved deletedTextColor() so the harness can confirm it is a
// clearly-red hue and that it tracks the active (light/dark) palette.
static int RunScriptedDarkProbe(const UString &leftDir, const UString &rightDir,
    const char *archiveName)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *left = win.leftPanel();

  const UString arcU = GetUnicodeString(archiveName);
  QtFolderModel *lm = left->model();
  // G.4f : find by name via rowForName() (returns the correct MODEL row whether or
  // not ShowDots shifts the rows by the ".." entry), then enterItem() that MODEL row.
  // A raw [0,GetNumberOfItems()) loop with itemName((int)i) would mis-bound / shift
  // when ShowDots is on (the harness used to assume model row == engine index).
  bool opened = false;
  {
    const int arcRow = lm->rowForName(arcU);
    if (arcRow >= 0)
    {
      lm->enterItem(arcRow);
      opened = left->isInArchive();
    }
  }
  if (!opened)
  {
    fprintf(stderr, "dark-probe: could not open archive %s in %s\n",
        archiveName, OemUtf8(leftDir));
    return 2;
  }

  const QColor dc = lm->deletedTextColor();
  printf("DARK_COLOR: deletedTextColor=#%02X%02X%02X (r=%d g=%d b=%d)\n",
      dc.red(), dc.green(), dc.blue(), dc.red(), dc.green(), dc.blue());

  UInt32 ni = 0;
  if (left->currentFolder())
    left->currentFolder()->GetNumberOfItems(&ni);

  // Name column index (kpidName), the column the FM paints with the row color.
  const int nameCol = lm->columnForPropID(kpidName) < 0 ? 0
      : lm->columnForPropID(kpidName);

  int reds = 0, normals = 0, mismatches = 0;
  for (UInt32 i = 0; i < ni; i++)
  {
    const QModelIndex ix = lm->index((int)i, nameCol);
    const QVariant fg = lm->data(ix, Qt::ForegroundRole);
    bool del = false;
    {
      NCOM::CPropVariant pv;
      if (left->currentFolder()->GetProperty(i, kpidIsDeleted, &pv) == S_OK
          && pv.vt == VT_BOOL)
        del = VARIANT_BOOLToBool(pv.boolVal);
    }
    const bool hasFg = fg.isValid();
    if (del != hasFg) mismatches++;     // mapping must agree row-for-row
    if (hasFg) reds++; else normals++;
    printf("DARK_ROW: %s deleted=%d fgSet=%d\n",
        OemUtf8(lm->itemName((int)i)), del ? 1 : 0, hasFg ? 1 : 0);
  }
  printf("DARK_PROBE: rows=%u reds=%d normals=%d mismatches=%d gate=%d [%s]\n",
      ni, reds, normals, mismatches, lm->markDeletedItems() ? 1 : 0,
      mismatches == 0 ? "PASS" : "FAIL");

  // Gate self-assert (no disk image needed): with the gate OFF, EVERY row's
  // ForegroundRole must become invalid; restoring it must restore the mapping.
  lm->setMarkDeletedItems(false);
  int redsOff = 0;
  for (UInt32 i = 0; i < ni; i++)
    if (lm->data(lm->index((int)i, nameCol), Qt::ForegroundRole).isValid())
      redsOff++;
  lm->setMarkDeletedItems(true);
  int redsOn = 0;
  for (UInt32 i = 0; i < ni; i++)
    if (lm->data(lm->index((int)i, nameCol), Qt::ForegroundRole).isValid())
      redsOn++;
  const bool gatePass = (redsOff == 0) && (redsOn == reds);
  printf("DARK_GATE: redsOff=%d redsOn=%d (baseline=%d) [%s]\n",
      redsOff, redsOn, reds, gatePass ? "PASS" : "FAIL");

  return (mismatches == 0 && gatePass) ? 0 : 1;
}

// === B.4b : scripted drag & drop self-check (offscreen, no live mouse) =======
// Proves the drag-source mime AND the drop->operation wiring:
//   mode "copy" / "move" : INTERNAL drop — select the LEFT panel's items, build
//       the panel's own drag mime, and drop it onto the RIGHT panel; assert the
//       move-vs-copy outcome on disk.
//   mode "ext"           : EXTERNAL drop — a uri-list of a file in `extPath`
//       (an unrelated dir, NOT a panel selection) dropped onto the RIGHT panel.
// Also dumps the drag-source mime urls() so the "drag OUT to another app" path
// (text/uri-list with real file paths) is visibly confirmed.
static int RunScriptedDrop(const UString &leftDir, const UString &rightDir,
    const char *mode, const char *extPath)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *left = win.leftPanel();
  QtPanel *right = win.rightPanel();

  // === G.6c : drop ONTO an FS subfolder row -> the file lands INSIDE that subfolder.
  // RIGHT holds at least one sub-folder; drop the LEFT panel's selection onto that
  // sub-folder row (the no-mouse hit-test via scriptedDrop's targetSubFolderRow) and
  // assert the dropped file appears in RIGHT/<subfolder>/, NOT in RIGHT's current dir.
  if (strcmp(mode, "subfolder") == 0)
  {
    printf("---- G.6c drop-onto-subfolder ----\n");
    QtFolderModel *rm = right->model();
    // Find the first FS sub-folder row in the RIGHT panel (the drop target).
    int folderRow = -1;
    UString folderName;
    UInt32 rn = 0;
    if (right->currentFolder()) right->currentFolder()->GetNumberOfItems(&rn);
    for (UInt32 i = 0; i < rn; i++)
      if (rm->isFolder((int)i)) { folderRow = (int)i; folderName = rm->itemName((int)i); break; }
    if (folderRow < 0)
    {
      fprintf(stderr, "drop-subfolder: RIGHT panel has no sub-folder to target\n");
      return 2;
    }
    // Drop the LEFT panel's whole selection onto that sub-folder row.
    if (QTreeView *lv = left->findChild<QTreeView *>())
      lv->selectAll();
    const UStringVector lp = left->selectedFullPaths();
    if (lp.Size() == 0)
    {
      fprintf(stderr, "drop-subfolder: LEFT panel selection is empty\n");
      return 2;
    }
    printf("  target subfolder = \"%s\" (row %d)\n",
        OemUtf8(folderName), folderRow);
    const bool handled = win.scriptedDrop(right, lp, Qt::CopyAction, folderRow);
    printf("  drop handled = %d\n", handled ? 1 : 0);

    // Verify : each dropped leaf now exists in RIGHT/<subfolder>/, NOT in RIGHT's dir.
    FString subDir = us2fs(rightDir);
    NFile::NName::NormalizeDirPathPrefix(subDir);
    subDir += us2fs(folderName);
    NFile::NName::NormalizeDirPathPrefix(subDir);
    FString curDir = us2fs(rightDir); NFile::NName::NormalizeDirPathPrefix(curDir);
    bool allInside = true, anyInCurrent = false;
    FOR_VECTOR (i, lp)
    {
      // leaf = last path component of the source full path.
      UString leaf = lp[i];
      const int slash = leaf.ReverseFind_PathSepar();
      if (slash >= 0) leaf.DeleteFrontal((unsigned)(slash + 1));
      NFile::NFind::CFileInfo fi;
      if (!fi.Find(subDir + us2fs(leaf))) allInside = false;
      NFile::NFind::CFileInfo fc;
      if (fc.Find(curDir + us2fs(leaf))) anyInCurrent = true;
      printf("  DROP_SUBFOLDER_LANDED: %s\n", OemUtf8(leaf));
    }
    const bool ok = handled && allInside && !anyInCurrent;
    printf("  landed-inside=%d any-in-current=%d [%s]\n",
        allInside ? 1 : 0, anyInCurrent ? 1 : 0, ok ? "PASS" : "FAIL");
    printf("---- drop-onto-subfolder %s ----\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
  }

  // === G.6d : RIGHT-button-drag menu — drop the LEFT selection onto RIGHT and FORCE
  // the chosen menu command. extPath = "copy" | "move" | "add". The live QMenu::exec
  // cannot run headlessly, so scriptedRightDrop substitutes the user's pick. Verify
  // the chosen action's effect: MOVE = source removed + dest gained; COPY = both
  // present; ADD = a new archive created in RIGHT from the dropped item.
  if (strcmp(mode, "rmenu") == 0)
  {
    printf("---- G.6d right-button-drag menu (cmd=%s) ----\n", extPath ? extPath : "");
    if (!extPath)
    {
      fprintf(stderr, "drop-rmenu: --drop-rmenu=copy|move|add required\n");
      return 2;
    }
    QtFileManagerWindow::DragMenuCmd cmd = QtFileManagerWindow::DragMenuCmd::None;
    if (strcmp(extPath, "copy") == 0) cmd = QtFileManagerWindow::DragMenuCmd::Copy;
    else if (strcmp(extPath, "move") == 0) cmd = QtFileManagerWindow::DragMenuCmd::Move;
    else if (strcmp(extPath, "add") == 0) cmd = QtFileManagerWindow::DragMenuCmd::AddToArc;
    else { fprintf(stderr, "drop-rmenu: cmd must be copy|move|add\n"); return 2; }

    // Drop the LEFT panel's whole selection onto the RIGHT FS panel. For the ADD
    // case select only ONE item so the archive name is the single-item leaf (the
    // multi-item folder-leaf fallback name is identical to compressFilesCore but is
    // sensitive to a '.' in the dest-dir leaf, which a mktemp TMPDIR can introduce).
    if (cmd == QtFileManagerWindow::DragMenuCmd::AddToArc)
      left->selectSourceRowForTest(0);
    else if (QTreeView *lv = left->findChild<QTreeView *>())
      lv->selectAll();
    const UStringVector lp = left->selectedFullPaths();
    if (lp.Size() == 0)
    {
      fprintf(stderr, "drop-rmenu: LEFT panel selection is empty\n");
      return 2;
    }
    // Capture each leaf name to verify the source/dest after the op.
    UStringVector leaves;
    FOR_VECTOR (i, lp)
    {
      UString leaf = lp[i];
      const int slash = leaf.ReverseFind_PathSepar();
      if (slash >= 0) leaf.DeleteFrontal((unsigned)(slash + 1));
      leaves.Add(leaf);
    }

    const bool handled = win.scriptedRightDrop(right, lp, cmd);
    printf("  right-drag menu dispatch handled = %d\n", handled ? 1 : 0);

    FString rdir = us2fs(rightDir); NFile::NName::NormalizeDirPathPrefix(rdir);
    FString ldir = us2fs(leftDir);  NFile::NName::NormalizeDirPathPrefix(ldir);

    bool ok = handled;
    if (cmd == QtFileManagerWindow::DragMenuCmd::AddToArc)
    {
      // Add to archive : the archive name mirrors compressFilesCore's nameHint rule —
      // a SINGLE item -> its leaf with one trailing extension stripped; MULTIPLE items
      // -> the dest folder leaf. Sources are kept on disk (compress is COPY semantics).
      UString base;
      if (leaves.Size() == 1)
      {
        base = leaves.Front();
        const int dotPos = base.Find(L'.');
        if (dotPos > 0 && base.Find(L'.', (unsigned)dotPos + 1) < 0)
          base.DeleteFrom((unsigned)dotPos);
      }
      else
      {
        // dest folder leaf (rightDir's last component, separators stripped).
        UString d = rightDir;
        while (d.Len() > 1 && (d.Back() == L'/' || d.Back() == L'\\'))
          d.DeleteBack();
        const int slash = d.ReverseFind_PathSepar();
        base = (slash >= 0) ? UString(d.Ptr(slash + 1)) : d;
      }
      const FString expectArc = rdir + us2fs(base) + FString(".7z");
      NFile::NFind::CFileInfo fi;
      const bool arcCreated = fi.Find(expectArc) && !fi.IsDir();
      NFile::NFind::CFileInfo sfi;
      const bool srcKept = sfi.Find(us2fs(lp.Front()));
      printf("  ADD: archive=%s created=%d  source-kept=%d\n",
          OemUtf8(fs2us(expectArc)), arcCreated ? 1 : 0, srcKept ? 1 : 0);
      ok = ok && arcCreated && srcKept;
    }
    else
    {
      // Copy/Move here : verify dest gained every leaf; on MOVE the source is gone,
      // on COPY the source is kept.
      const bool moveMode = (cmd == QtFileManagerWindow::DragMenuCmd::Move);
      bool allInDest = true, allSrcGone = true, allSrcKept = true;
      FOR_VECTOR (i, leaves)
      {
        NFile::NFind::CFileInfo dfi;
        if (!dfi.Find(rdir + us2fs(leaves[i]))) allInDest = false;
        NFile::NFind::CFileInfo sfi;
        const bool srcThere = sfi.Find(ldir + us2fs(leaves[i]));
        if (srcThere) allSrcGone = false; else allSrcKept = false;
        printf("  %s: leaf=%s in-dest=%d src-present=%d\n",
            moveMode ? "MOVE" : "COPY", OemUtf8(leaves[i]),
            (dfi.Find(rdir + us2fs(leaves[i]))) ? 1 : 0, srcThere ? 1 : 0);
      }
      if (moveMode)
        ok = ok && allInDest && allSrcGone;       // source removed + dest gained
      else
        ok = ok && allInDest && allSrcKept;       // both present
    }
    printf("---- right-button-drag menu (%s) %s ----\n",
        extPath, ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
  }

  // === G.6a : drop ONTO an updatable archive -> the confirmation path is taken
  // (headless auto-yes prints the marker) and the entry is added. RIGHT is opened
  // into the first archive it holds; the LEFT panel selection is dropped onto it.
  if (strcmp(mode, "arc-confirm") == 0)
  {
    printf("---- G.6a drop-onto-archive confirmation ----\n");
    QtFolderModel *rm = right->model();
    bool opened = false;
    UInt32 n = 0;
    if (right->currentFolder()) right->currentFolder()->GetNumberOfItems(&n);
    for (UInt32 i = 0; i < n; i++)
    {
      const UString nm = rm->itemName((int)i);
      if (nm.Len() >= 3 && (nm.Find(L".7z") >= 0 || nm.Find(L".zip") >= 0))
      {
        rm->enterItem((int)i);
        opened = right->isInArchive();
        break;
      }
    }
    if (!opened || !right->isUpdatableArchive())
    {
      fprintf(stderr, "drop-arc-confirm: RIGHT did not open an UPDATABLE archive "
          "(opened=%d updatable=%d)\n", opened ? 1 : 0, right->isUpdatableArchive() ? 1 : 0);
      return 2;
    }
    if (QTreeView *lv = left->findChild<QTreeView *>())
      lv->selectAll();
    const UStringVector lp = left->selectedFullPaths();
    if (lp.Size() == 0) { fprintf(stderr, "drop-arc-confirm: empty LEFT selection\n"); return 2; }
    // The marker DROP_ARC_CONFIRM_TITLE/MSG is printed by dropOnto's archive branch
    // (headless), proving the confirmation path was taken before the add ran.
    const bool handled = win.scriptedDrop(right, lp, Qt::CopyAction);
    printf("  drop handled = %d (added into archive after confirm)\n", handled ? 1 : 0);
    printf("---- drop-onto-archive confirmation %s ----\n", handled ? "PASSED" : "FAILED");
    return handled ? 0 : 1;
  }

  // === G.6e : a FAILED drop reports the HRESULT (DROP_ERROR marker) and does NOT
  // KillSelection. Force a failure by dropping a NON-EXISTENT source path onto the
  // RIGHT FS panel (the external path binds CFSFolders that can't resolve it).
  if (strcmp(mode, "fail") == 0)
  {
    printf("---- G.6e failed-drop error + no-kill-selection ----\n");
    // A bogus path under leftDir that does not exist on disk.
    UStringVector bogus;
    UString p = leftDir; NFile::NName::NormalizeDirPathPrefix(p);
    p += L"__no_such_file_for_drop_fail__.bin";
    bogus.Add(p);
    const bool handled = win.scriptedDrop(right, bogus, Qt::CopyAction);
    // handled==false (no items moved) AND a DROP_ERROR marker should have printed.
    printf("  drop handled = %d (expected 0; DROP_ERROR marker above on real error)\n",
        handled ? 1 : 0);
    printf("---- failed-drop %s ----\n", !handled ? "PASSED" : "FAILED");
    return !handled ? 0 : 1;
  }

  // === G.6b : compress-on-drop. EXTERNAL file paths (from an unrelated dir, NOT a
  // panel selection) dropped onto the RIGHT FS panel must launch the Add-to-Archive
  // flow (createNewArchive), creating a NEW archive in the RIGHT dir from the dropped
  // files. Headless RunFmCommand auto-runs the compress (DisableUserQuestions). We
  // assert the new <name>.7z appears in the RIGHT dir; the harness then runs
  // `7zz l <arc>` to confirm it contains the dropped leaves (path printed below).
  if (strcmp(mode, "compress") == 0)
  {
    printf("---- G.6b compress-on-drop (external -> Add to Archive) ----\n");
    if (!extPath)
    {
      fprintf(stderr, "compress-on-drop: --drop-ext=<file> required (the dropped file)\n");
      return 2;
    }
    UStringVector paths;
    paths.Add(GetUnicodeString(extPath));
    // The leaf of the dropped file -> the expected default archive base name
    // (CreateArchiveName: single item's leaf with one trailing extension stripped).
    UString leaf = paths.Front();
    {
      const int slash = leaf.ReverseFind_PathSepar();
      if (slash >= 0) leaf.DeleteFrontal((unsigned)(slash + 1));
    }
    UString base = leaf;
    {
      const int dotPos = base.Find(L'.');
      if (dotPos > 0 && base.Find(L'.', (unsigned)dotPos + 1) < 0)
        base.DeleteFrom((unsigned)dotPos);
    }
    FString rdir = us2fs(rightDir);
    NFile::NName::NormalizeDirPathPrefix(rdir);
    const FString expectArc = rdir + us2fs(base) + FString(".7z");
    printf("  dropped leaf = \"%s\"\n", OemUtf8(leaf));
    printf("  expected archive = %s\n", OemUtf8(fs2us(expectArc)));

    // External-style drop (paths are NOT a panel selection -> dropOnto's external
    // branch -> compressDroppedFiles). headless -> RunFmCommand actually compresses.
    const bool handled = win.scriptedDrop(right, paths, Qt::CopyAction);
    printf("  drop handled = %d\n", handled ? 1 : 0);

    NFile::NFind::CFileInfo fi;
    const bool arcCreated = fi.Find(expectArc) && !fi.IsDir();
    printf("  COMPRESS_DROP_ARCHIVE: %s\n", OemUtf8(fs2us(expectArc)));
    printf("  archive created = %d  size = %llu\n",
        arcCreated ? 1 : 0, (unsigned long long)(arcCreated ? fi.Size : 0));

    // The source file is left in place (compress is COPY semantics).
    NFile::NFind::CFileInfo srcFi;
    const bool srcKept = srcFi.Find(us2fs(paths.Front()));
    printf("  source kept on disk = %d\n", srcKept ? 1 : 0);

    const bool ok = handled && arcCreated && srcKept;
    printf("---- compress-on-drop %s ----\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
  }

  // --- archive guard : open the RIGHT panel into an archive, then assert that
  //     (a) B.5c : dragging FROM it now EXTRACTS-to-temp (non-null mime with
  //         file:// temp URIs — drag-out is no longer deferred), and
  //     (b) dropping ONTO a read-only archive is rejected (no crash). ----------
  if (strcmp(mode, "archive") == 0)
  {
    printf("---- B.4b archive-panel guard ----\n");
    // Enter the first archive in the RIGHT panel (double-click == enterItem ->
    // leafActivated -> seamless FS->archive open).
    QtFolderModel *rm = right->model();
    bool opened = false;
    UInt32 n = 0;
    if (right->currentFolder())
      right->currentFolder()->GetNumberOfItems(&n);
    for (UInt32 i = 0; i < n; i++)
    {
      const UString nm = rm->itemName((int)i);
      if (nm.Len() >= 3 && (nm.Find(L".7z") >= 0 || nm.Find(L".zip") >= 0))
      {
        rm->enterItem((int)i);   // emits leafActivated -> tryOpenAsArchive
        opened = right->isInArchive();
        break;
      }
    }
    printf("  right panel opened-as-archive = %d (inArchive=%d)\n",
        opened ? 1 : 0, right->isInArchive() ? 1 : 0);

    // (a) B.5c : drag FROM the archive panel now EXTRACTS-to-temp -> a non-null
    //     mime carrying file:// temp URIs (extraction happened eagerly).
    if (QTreeView *rv = right->findChild<QTreeView *>())
      rv->selectAll();
    QMimeData *m = right->buildDragMimeData();
    const bool dragOutExtracted = (m != nullptr) && m->hasUrls() && !m->urls().isEmpty();
    delete m;
    printf("  drag-from-archive extracted-to-temp = %d  [%s]\n",
        dragOutExtracted ? 1 : 0, dragOutExtracted ? "PASS" : "FAIL");

    // (b) drop ONTO the archive panel : no crash. Outcome depends on whether the
    //     opened archive is UPDATABLE (B.5b): an updatable archive ACCEPTS the
    //     drop (add-into-archive); a read-only archive REJECTS it. Either is a
    //     PASS as long as it matches acceptsDrop() and does not crash.
    UStringVector paths;
    paths.Add(leftDir + UString(L"")); // any FS path; here just the left dir
    if (QTreeView *lv = left->findChild<QTreeView *>())
      lv->selectAll();
    const UStringVector lp = left->selectedFullPaths();
    const bool dropHandled = win.scriptedDrop(right,
        lp.Size() ? lp : paths, Qt::CopyAction);
    const bool dropExpected = right->isUpdatableArchive(); // B.5b add-on-drop
    const bool dropOk = (dropHandled == dropExpected);
    printf("  drop-onto-archive handled=%d updatable=%d [%s]\n",
        dropHandled ? 1 : 0, dropExpected ? 1 : 0, dropOk ? "PASS" : "FAIL");

    const bool ok = opened && dragOutExtracted && dropOk;
    printf("---- archive guard %s ----\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
  }

  // Select all rows in the LEFT panel (the drag source).
  if (QTreeView *lv = left->findChild<QTreeView *>())
    lv->selectAll();

  // --- drag-source mime check : the panel must offer file:// urls -----------
  {
    QMimeData *mime = left->buildDragMimeData();
    printf("---- B.4b drag-source mime (left panel selection) ----\n");
    if (!mime)
    {
      printf("  FAIL: buildDragMimeData() returned null (no selection / archive)\n");
    }
    else
    {
      const QList<QUrl> urls = mime->urls();
      printf("  hasUrls=%d  count=%lld\n", mime->hasUrls() ? 1 : 0,
          (long long)urls.size());
      for (const QUrl &u : urls)
        printf("    uri: %s  (localFile=%s)\n",
            u.toString().toUtf8().constData(),
            u.toLocalFile().toUtf8().constData());
      delete mime;
    }
  }

  // --- the drop ------------------------------------------------------------
  printf("---- B.4b scripted drop (mode=%s) ----\n", mode);
  bool handled = false;
  if (strcmp(mode, "ext") == 0)
  {
    if (!extPath)
    {
      fprintf(stderr, "scripted-drop ext: --drop-ext=<file> required\n");
      return 2;
    }
    UStringVector paths;
    paths.Add(GetUnicodeString(extPath));
    // External-style: these paths are NOT the panel's selection, so dropOnto
    // takes the temporary-CFSFolder path. Default action -> same-volume rule.
    handled = win.scriptedDrop(right, paths, Qt::CopyAction);
  }
  else
  {
    // Internal: use the LEFT panel's own selection paths as the drop payload.
    const UStringVector paths = left->selectedFullPaths();
    Qt::DropAction act = Qt::CopyAction;
    if (strcmp(mode, "move") == 0) act = Qt::MoveAction;
    // "default" : Qt::IgnoreAction makes MapDropAction fall back to PanelDrag's
    // same-volume rule (MOVE within a volume, COPY across) — proves that path.
    else if (strcmp(mode, "default") == 0) act = Qt::IgnoreAction;
    const int selBefore = left->selectedSourceRows().size();
    handled = win.scriptedDrop(right, paths, act);
    // G.6e : a completed INTERNAL drag clears the SOURCE selection (KillSelection,
    // PanelDrag.cpp:1800-1801). After a handled drop the LEFT selection is empty.
    const int selAfter = left->selectedSourceRows().size();
    printf("  KILL_SELECTION: before=%d after=%d [%s]\n",
        selBefore, selAfter,
        (handled && selAfter == 0) ? "PASS" : (handled ? "FAIL" : "n/a"));
  }
  printf("  drop handled=%d\n", handled ? 1 : 0);
  return handled ? 0 : 1;
}

// === B.7c : scripted Split / Combine / Link / Diff / Favorites ===============
// LEFT holds the source file/dir; RIGHT is the destination dir (so split volumes
// / combine output land in RIGHT, matching the live other-panel-dest behavior).
//   --split=<file>,<size>      : Split LEFT/<file> at <size> into RIGHT.
//   --combine=<file.001>       : Combine the LEFT/<file.001> part series into RIGHT.
//   --link=<target>,<link>[,sym|hard] : create RIGHT/<link> pointing at <target>.
//   --diff                     : print the resolved diff command + two paths.
//   --fav-add                  : store LEFT's dir in the first free Favorites slot.
//   --fav-go=<n>               : navigate LEFT to Favorites slot <n>, print its dir.
//
//   arg1/arg2 carry the comma-split components (file/size or target/link/kind).
static int RunScriptedSplitCombine(const UString &leftDir, const UString &rightDir,
    ScriptOp op, const char *arg1, const char *arg2, const char *arg3)
{
  FString fdir = us2fs(leftDir);
  CMyComPtr<IFolderFolder> root;
  if (BindFsFolder(fdir, root) != S_OK || !root)
  {
    fprintf(stderr, "scripted-b7c: cannot bind FS folder %s\n", OemUtf8(leftDir));
    return 2;
  }

  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *panel = win.leftPanel();
  win.focusPanelForTest(panel);
  QtFolderModel *m = panel->model();

  // Select the LEFT row named `arg1` (split/combine/link source item).
  auto selectByName = [&](const char *name) -> bool {
    if (!name || !name[0])
      return true;
    const UString want = GetUnicodeString(name);
    // G.4f : rowForName returns the correct MODEL row in either ShowDots mode.
    const int r = m->rowForName(want);
    if (r >= 0) { win.selectRowForTest(panel, r); return true; }
    fprintf(stderr, "scripted-b7c: row %s not found\n", name);
    return false;
  };

  switch (op)
  {
    case ScriptOp::Split:
      if (!selectByName(arg1)) return 2;
      win.doSplit(arg2 ? GetUnicodeString(arg2) : UString());
      return 0;
    case ScriptOp::Combine:
      if (!selectByName(arg1)) return 2;
      win.doCombine();
      return 0;
    case ScriptOp::Link:
    {
      // arg1 = target (to), arg2 = link path (from), arg3 = "sym"|"hard".
      // The link is created in RIGHT (dest dir): if `from` is not absolute it is
      // resolved against the focused panel's dir — here we point the focused panel
      // at RIGHT first so the relative `from` lands there.
      panel->navigateToFsPath(rightDir);
      win.focusPanelForTest(panel);
      const UString toU = arg1 ? GetUnicodeString(arg1) : UString();
      const UString fromU = arg2 ? GetUnicodeString(arg2) : UString();
      int kind = 0; // symbolic
      if (arg3 && strcmp(arg3, "hard") == 0) kind = 1;
      win.doLink(fromU, toU, kind);
      return 0;
    }
    case ScriptOp::Diff:
    {
      // Select up to two rows by the comma-split arg1,arg2 names; else the focused
      // single item is diffed against the other panel's same-named file.
      if (arg1 && arg1[0] && arg2 && arg2[0])
      {
        // Two named rows in the focused panel: select both source rows explicitly
        // (selectRowForTest is single-row, so we drive the selection model here).
        const UString n1 = GetUnicodeString(arg1);
        const UString n2 = GetUnicodeString(arg2);
        UInt32 ni = 0;
        if (panel->currentFolder())
          panel->currentFolder()->GetNumberOfItems(&ni);
        QTreeView *view = panel->view();
        view->clearSelection();
        QSortFilterProxyModel *proxy =
            qobject_cast<QSortFilterProxyModel *>(view->model());
        for (UInt32 i = 0; i < ni; i++)
        {
          const UString nm = m->itemName((int)i);
          if (nm == n1 || nm == n2)
          {
            const QModelIndex src = m->index((int)i, 0);
            const QModelIndex p = proxy ? proxy->mapFromSource(src) : src;
            view->selectionModel()->select(p,
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
          }
        }
      }
      else if (!selectByName(arg1))
        return 2;
      win.doDiff();
      return 0;
    }
    case ScriptOp::FavAdd:
      win.favAdd();
      printf("FAV_ADD: %s\n", OemUtf8(panel->currentFsDirPath()));
      return 0;
    case ScriptOp::FavGo:
    {
      const int slot = arg1 ? atoi(arg1) : 0;
      win.favGo(slot);
      printf("FAV_GO: slot=%d dir=%s\n", slot, OemUtf8(panel->currentFsDirPath()));
      return 0;
    }
    default:
      return 0;
  }
}

// === B.8 : Options persistence + trash/permanent delete (offscreen) ==========

// --opt-set=key=val / --opt-get=key : round-trip a typed [Options] value through
// QtFmSettings::CInfo (so the harness can prove persistence to 7-Zip.ini). The
// key set mirrors the dialog's bool fields + WorkDir/Editor/Diff.
static int RunScriptedOptSet(const char *arg)   // arg == "key=val"
{
  const char *eq = arg ? strchr(arg, '=') : nullptr;
  if (!eq)
  {
    fprintf(stderr, "opt-set: expected key=val\n");
    return 2;
  }
  const AString key(AString(arg).Mid(0, (unsigned)(eq - arg)));
  const AString val(eq + 1);
  const bool b = (val == "1" || val == "true" || val == "on");

  QtFmSettings::CInfo s;
  s.Load();
  if      (key == "ShowGrid")             s.ShowGrid = b;
  else if (key == "FullRow")              s.FullRow = b;
  else if (key == "AlternatingColors")    s.AlternatingColors = b;
  else if (key == "SingleClick")          s.SingleClick = b;
  else if (key == "ShowDots")             s.ShowDots = b;
  else if (key == "ShowRealFileIcons")    s.ShowRealFileIcons = b;   // G.9d
  else if (key == "DeleteToTrash")        s.DeleteToTrash = b;
  else if (key == "WorkDirUseSystemTemp") { s.WorkDirUseSystemTemp = b; s.WorkDirMode = b ? 0 : 2; }
  // G.9c : the three-way work-dir mode (0=System, 1=Current, 2=Specified).
  else if (key == "WorkDirMode")
  {
    int m = 0;
    if (val == "1" || val == "current")        m = 1;
    else if (val == "2" || val == "specified") m = 2;
    else                                        m = 0;   // 0/system/other
    s.WorkDirMode = m;
    s.WorkDirUseSystemTemp = (m == 0);
  }
  else if (key == "WorkDirPath")          s.WorkDirPath = GetUnicodeString(val.Ptr());
  else if (key == "Editor")               s.EditorPath = GetUnicodeString(val.Ptr());
  else if (key == "DiffCommand")          s.DiffCommand = GetUnicodeString(val.Ptr());
  else if (key == "Lang")                 s.LangName = GetUnicodeString(val.Ptr());   // P.2
  // G.9a : the extract-memory GB limit lives in the [Extraction] store (NExtractQt),
  // not in QtFmSettings::CInfo. Set/Save it directly. "none"/"off"/"-1"/"0" => no
  // configured limit ((UInt32)-1); any positive integer => that many GB.
  else if (key == "MemLimitGB")
  {
    UInt32 gb = (UInt32)(Int32)-1;
    if (!(val == "none" || val == "off" || val == "-1" || val == "0" || val.IsEmpty()))
    {
      const char *end;
      const UInt32 v = ConvertStringToUInt32(val.Ptr(), &end);
      if (*end == 0)
        gb = v;
    }
    NExtractQt::Save_LimitGB(gb);
    printf("OPT_SET: %s=%s\n", key.Ptr(), val.Ptr());
    return 0;
  }
  else { fprintf(stderr, "opt-set: unknown key %s\n", key.Ptr()); return 2; }
  s.Save();
  printf("OPT_SET: %s=%s\n", key.Ptr(), val.Ptr());
  return 0;
}

static int RunScriptedOptGet(const char *key)
{
  QtFmSettings::CInfo s;
  s.Load();
  QString out;
  const AString k(key ? key : "");
  if      (k == "ShowGrid")             out = s.ShowGrid ? "1" : "0";
  else if (k == "FullRow")              out = s.FullRow ? "1" : "0";
  else if (k == "AlternatingColors")    out = s.AlternatingColors ? "1" : "0";
  else if (k == "SingleClick")          out = s.SingleClick ? "1" : "0";
  else if (k == "ShowDots")             out = s.ShowDots ? "1" : "0";
  else if (k == "ShowRealFileIcons")    out = s.ShowRealFileIcons ? "1" : "0";   // G.9d
  else if (k == "DeleteToTrash")        out = s.DeleteToTrash ? "1" : "0";
  else if (k == "WorkDirUseSystemTemp") out = s.WorkDirUseSystemTemp ? "1" : "0";
  else if (k == "WorkDirMode")          out = QString::number(s.WorkDirMode);     // G.9c
  else if (k == "WorkDirPath")          out = QString::fromWCharArray(s.WorkDirPath.Ptr(), (int)s.WorkDirPath.Len());
  else if (k == "Editor")               out = QString::fromWCharArray(s.EditorPath.Ptr(), (int)s.EditorPath.Len());
  else if (k == "DiffCommand")          out = QString::fromWCharArray(s.DiffCommand.Ptr(), (int)s.DiffCommand.Len());
  else if (k == "Lang")                 out = QString::fromWCharArray(s.LangName.Ptr(), (int)s.LangName.Len());  // P.2
  // G.9a : MemLimitGB from the [Extraction] store. (UInt32)-1 / 0 (no limit) is
  // reported as "none"; any positive limit as its decimal GB value.
  else if (k == "MemLimitGB")
  {
    const UInt32 gb = NExtractQt::Read_LimitGB();
    out = (gb == 0 || gb == (UInt32)(Int32)-1) ? QStringLiteral("none")
                                               : QString::number((uint)gb);
  }
  else { fprintf(stderr, "opt-get: unknown key %s\n", k.Ptr()); return 2; }
  printf("OPT: %s=%s\n", k.Ptr(), out.toUtf8().constData());
  return 0;
}

// G.9c/G.9e : --workdir-probe. Prove the PERSISTED work-dir setting is honored at
// STARTUP. We replicate exactly what QtFileManagerWindow's ctor does on launch:
// load [Options] WorkDir*, then Qt_SetWorkDir(path, mode) into the agent overlay.
// Then we ask the agent for the EFFECTIVE NWorkDir::NMode the engine would use
// (Qt_GetEffectiveWorkDirMode -> CInfo::Load) — without opening Options. The probe
// is settings-only (no window), matching how the startup hook runs before any UI.
extern "C" void Qt_SetWorkDir(const wchar_t *path, int mode);
extern "C" int  Qt_GetEffectiveWorkDirMode();

static int RunWorkDirProbe()
{
  QtFmSettings::CInfo s;
  s.Load();
  // The startup hook (window ctor): push the persisted overlay into the agent.
  Qt_SetWorkDir(s.WorkDirPath.Ptr(), s.WorkDirMode);
  const int effective = Qt_GetEffectiveWorkDirMode();
  // Report the persisted mode/path and the engine's effective mode after Load().
  const QString path =
      QString::fromWCharArray(s.WorkDirPath.Ptr(), (int)s.WorkDirPath.Len());
  printf("WORKDIR_PROBE: persistedMode=%d effectiveMode=%d path=%s\n",
      s.WorkDirMode, effective, path.toUtf8().constData());
  return 0;
}

// G.9b : --lang-retranslate=<txt>. Build a headless FM window (menu in the current
// language = English), read a stable menu label, load the given Lang/*.txt, run the
// live retranslate (the doOptions() langChanged path) and read the SAME label again.
// Prints BEFORE/AFTER so a test can assert the already-built menu text changed in
// place without a restart. Returns 0 if AFTER differs from BEFORE (retranslate took).
static int RunLangRetranslateProbe(const UString &leftDir, const UString &rightDir,
    const char *txtPath)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  // File->Open (IDM_OPEN, langID 540 — present in essentially every community txt),
  // located by POSITION (File menu = 0, Open = action 0) so the lookup survives the
  // text changing under the language switch.
  const QString before = win.menuActionTextForTest(/*menu*/ 0, /*action*/ 0);
  const QString tbBefore = win.archiveToolBarAddTextForTest();  // Add button

  const UString p = GetUnicodeString(txtPath);
  const bool loaded = QtLang_LoadFile(us2fs(p));
  if (!loaded)
    fprintf(stderr, "lang-retranslate: failed to load \"%s\" "
        "(bad signature/id-0/missing)\n", txtPath);
  // The doOptions() langChanged branch: re-resolve every live label through the
  // just-loaded table (rebuilds the already-built menu in place).
  win.retranslateUiForTest();
  const QString after = win.menuActionTextForTest(0, 0);
  const QString tbAfter = win.archiveToolBarAddTextForTest();

  printf("LANG_RETRANSLATE: loaded=%d\n", loaded ? 1 : 0);
  printf("LANG_RETRANSLATE: File->Open before=\"%s\"\n", before.toUtf8().constData());
  printf("LANG_RETRANSLATE: File->Open after =\"%s\"\n", after.toUtf8().constData());
  printf("LANG_RETRANSLATE: toolbar Add before=\"%s\"\n", tbBefore.toUtf8().constData());
  printf("LANG_RETRANSLATE: toolbar Add after =\"%s\"\n", tbAfter.toUtf8().constData());
  const bool menuChanged = loaded && !after.isEmpty() && after != before;
  const bool tbChanged   = loaded && !tbAfter.isEmpty() && tbAfter != tbBefore;
  printf("LANG_RETRANSLATE: menuChanged=%d toolbarChanged=%d [%s]\n",
      menuChanged ? 1 : 0, tbChanged ? 1 : 0,
      (menuChanged && tbChanged) ? "PASS" : "FAIL");
  return (menuChanged && tbChanged) ? 0 : 1;
}

// G.8a : --mem-qi-probe. Prove the Qt extract callback now implements
// IArchiveRequestMemoryUseCallback (so the engine's RAR5 mem-limit gate can reach
// it) AND that RequestMemoryUse answers per the original logic. This mirrors,
// without a RAR archive, exactly what the engine does:
//   (1) QueryInterface(IID_IArchiveRequestMemoryUseCallback, ...)   [Rar5Handler.cpp:2980]
//   (2) requestMem->RequestMemoryUse(flags, ..., required, &allowed, &answer)
// We drive two scenarios against the saved GB limit (NExtractQt::Read_LimitGB):
//   A) required <= allowed  -> expect k_Allow
//   B) required >  allowed  -> headless (DisableUserQuestions=false here, but no
//      ProgressDialog so the prompt branch is gated by a null ProgressDialog ->
//      treated as the non-interactive default) -> the answer carries Limit_Exceeded
//      / SkipArc-expected per the flags, never crashing. The decisive assertion is
//      (1): QI must return S_OK + non-null (it would FAIL before this task).
static int RunMemQiProbe()
{
  CMyComPtr<IFolderArchiveExtractCallback> cb = new QtExtractCallback;

  // (1) the engine's QI (Rar5Handler.cpp:2980). Before G.8a this returned E_NOINTERFACE.
  CMyComPtr<IArchiveRequestMemoryUseCallback> requestMem;
  const HRESULT qi = cb->QueryInterface(
      IID_IArchiveRequestMemoryUseCallback, (void **)&requestMem);
  if (qi != S_OK || !requestMem)
  {
    printf("MEM_QI: FAIL qi=0x%x ptr=%p\n", (unsigned)qi, (void *)requestMem);
    return 1;
  }
  printf("MEM_QI: OK (IArchiveRequestMemoryUseCallback reachable)\n");

  const UInt32 savedGB = NExtractQt::Read_LimitGB();
  printf("MEM_QI: savedGB=%u\n", (unsigned)savedGB);

  // (2A) required <= default allowed -> k_Allow. Use a default allowed of 4GB
  // (the engine's k_MemLimit default class) and a 1GB requirement.
  {
    UInt64 allowed = (UInt64)4 << 30;
    UInt32 answer = NRequestMemoryAnswerFlags::k_Allow; // engine's pre-set default
    const HRESULT r = requestMem->RequestMemoryUse(
        /*flags*/ 0, /*indexType*/ 0 /*kNoIndex*/, /*index*/ 0, /*path*/ NULL,
        /*required*/ (UInt64)1 << 30, &allowed, &answer);
    const bool allow = (answer & NRequestMemoryAnswerFlags::k_Allow) != 0;
    printf("MEM_QI: A required=1GB allowed=4GB -> hr=0x%x answer=0x%x allow=%d\n",
        (unsigned)r, (unsigned)answer, allow ? 1 : 0);
    if (r != S_OK || !allow)
      return 1;
  }

  // (2B) required > allowed, SkipArc expected. With no ProgressDialog/MemPrompt
  // wired the interactive branch is not taken; the answer must carry
  // Limit_Exceeded. k_NoErrorMessage is set so the report path (which would touch
  // the unwired ProgressDialog->Sync) is skipped — in the real flow ProgressDialog
  // is always wired, so this is a probe-only accommodation, not a behavior change.
  {
    UInt64 allowed = (UInt64)1 << 30;
    UInt32 answer = NRequestMemoryAnswerFlags::k_Allow;
    const HRESULT r = requestMem->RequestMemoryUse(
        NRequestMemoryUseFlags::k_DefaultLimit_Exceeded
      | NRequestMemoryUseFlags::k_SkipArc_IsExpected
      | NRequestMemoryUseFlags::k_NoErrorMessage,
        0, 0, NULL,
        /*required*/ (UInt64)8 << 30, &allowed, &answer);
    const bool exceeded = (answer & NRequestMemoryAnswerFlags::k_Limit_Exceeded) != 0;
    printf("MEM_QI: B required=8GB allowed=1GB -> hr=0x%x answer=0x%x limitExceeded=%d\n",
        (unsigned)r, (unsigned)answer, exceeded ? 1 : 0);
    if (r != S_OK || !exceeded)
      return 1;
  }

  printf("MEM_QI: PASS\n");
  return 0;
}

// G.10a : headless Tools->Benchmark. Mirrors RunFmBenchmark's headless branch
// (RunBenchmarkConsole == BenchCon.cpp) so the FM menu path is regression-testable
// off-display. `propsArg` is an optional comma list of benchmark props ("mt2,d1m");
// each token splits on '=' into a CProperty (Name/Value), exactly as the -m parser
// feeds Benchmark(). One pass keeps the self-test short.
static int RunBenchmarkProbe(const char *propsArg)
{
  CObjectVector<CProperty> props;
  if (propsArg && *propsArg)
  {
    AString all(propsArg);
    for (;;)
    {
      const int comma = all.Find(',');
      AString tok = (comma >= 0) ? all.Left((unsigned)comma) : all;
      if (!tok.IsEmpty())
      {
        CProperty p;
        const int eq = tok.Find('=');
        if (eq >= 0)
        {
          p.Name = GetUnicodeString(tok.Left((unsigned)eq));
          p.Value = GetUnicodeString(tok.Ptr((unsigned)eq + 1));
        }
        else
          p.Name = GetUnicodeString(tok);
        props.Add(p);
      }
      if (comma < 0)
        break;
      all.DeleteFrontal((unsigned)comma + 1);
    }
  }
  printf("BENCHMARK: start (props=%s)\n", (propsArg && *propsArg) ? propsArg : "(default)");
  const HRESULT res = RunBenchmarkConsole(props, 1, stdout);
  fflush(stdout);
  printf("BENCHMARK: done hr=0x%x\n", (unsigned)res);
  return (res == S_OK) ? 0 : 1;
}

// G.10b : headless Help->About. Build a headless window and dump the SAME version /
// date / homepage the About dialog surfaces (aboutText mirrors AboutDialog.cpp's
// IDT_ABOUT_VERSION / IDT_ABOUT_DATE + kHomePageURL). Proves the dialog shows the
// real MY_VERSION_CPU / MY_DATE, not the old hardcoded "milestone B.3" blurb.
static int RunAboutProbe(const UString &leftDir, const UString &rightDir)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  const QString txt = win.aboutText();
  const QByteArray u8 = txt.toUtf8();
  printf("ABOUT:\n%s\n", u8.constData());
  // Assert the version line carries the real numeric version (MY_VERSION_NUMBERS).
  const bool hasVer = txt.contains(QString::fromLatin1(MY_VERSION_NUMBERS));
  const bool hasDate = txt.contains(QString::fromLatin1(MY_DATE));
  const bool hasUrl = txt.contains(QtFileManagerWindow::aboutHomepageUrl());
  printf("ABOUT: hasVersion=%d hasDate=%d hasHomepage=%d [%s]\n",
      hasVer ? 1 : 0, hasDate ? 1 : 0, hasUrl ? 1 : 0,
      (hasVer && hasDate && hasUrl) ? "PASS" : "FAIL");
  return (hasVer && hasDate && hasUrl) ? 0 : 1;
}

// G.10c : headless Tools->Delete Temporary Files. Mint one drag temp dir (the same
// createDragTempDir() a drag-OUT mints), then run deleteTempFiles(confirm=false)
// and prove the FM's own temp working dir is purged (count removed == 1, dir gone).
static int RunDeleteTempProbe(const UString &leftDir, const UString &rightDir)
{
  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  const UString dir = win.mintTempDirForTest();
  const FString fdir = us2fs(dir);
  const bool existedBefore = !dir.IsEmpty() && NFile::NFind::DoesDirExist(fdir);
  printf("DELETE_TEMP: minted=\"%s\" existedBefore=%d\n",
      OemUtf8(dir), existedBefore ? 1 : 0);
  const int removed = win.deleteTempFiles(/*confirm*/ false);
  const bool goneAfter = dir.IsEmpty() || !NFile::NFind::DoesDirExist(fdir);
  printf("DELETE_TEMP: removed=%d goneAfter=%d [%s]\n",
      removed, goneAfter ? 1 : 0,
      (existedBefore && removed == 1 && goneAfter) ? "PASS" : "FAIL");
  return (existedBefore && removed == 1 && goneAfter) ? 0 : 1;
}

// The freedesktop "home" trash dir ($XDG_DATA_HOME/Trash, else ~/.local/share/
// Trash). QFile::moveToTrash writes here; the harness points XDG_DATA_HOME / HOME
// at a temp dir so the real trash is never touched.
static QString TrashDir()
{
  QString dataHome = QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME"));
  if (dataHome.isEmpty())
    dataHome = QDir::homePath() + QStringLiteral("/.local/share");
  return dataHome + QStringLiteral("/Trash");
}

// --delete-trash=<name> : select LEFT/<name>, force trash mode, doDelete(false);
// assert the file LEFT the dir AND landed in Trash/files/<name> with a matching
// Trash/info/<name>.trashinfo. --delete-perm=<name> : doDelete(true); assert
// gone AND NOT in Trash/files.
static int RunScriptedDelete(const UString &leftDir, const UString &rightDir,
    ScriptOp op, const char *name)
{
  if (!name || !name[0])
  {
    fprintf(stderr, "scripted-delete: name required\n");
    return 2;
  }
  const UString nameU = GetUnicodeString(name);

  // Force the trash setting deterministically for the test (the dialog default
  // is already true, but a prior --opt-set could have flipped it).
  {
    QtFmSettings::CInfo s; s.Load();
    s.DeleteToTrash = (op == ScriptOp::DeleteTrash);
    s.Save();
  }

  QtFileManagerWindow win(leftDir, rightDir, /*headless*/ true);
  QtPanel *panel = win.leftPanel();
  win.focusPanelForTest(panel);

  // Locate the MODEL row named <name>. G.4f : rowForName returns the correct MODEL
  // row in either ShowDots mode.
  QtFolderModel *m = panel->model();
  const int row = m->rowForName(nameU);
  if (row < 0)
  {
    fprintf(stderr, "scripted-delete: row %s not found\n", name);
    return 2;
  }
  win.selectRowForTest(panel, row);

  // Absolute source path BEFORE the op.
  const UString src = panel->currentFsDirPath() + nameU;
  const QString qsrc = QString::fromWCharArray(src.Ptr(), (int)src.Len());

  win.doDelete(op == ScriptOp::DeletePerm /*permanentOverride*/);

  const bool goneFromDir = !QFileInfo::exists(qsrc);
  const QString filesDir = TrashDir() + QStringLiteral("/files");
  const QString infoDir  = TrashDir() + QStringLiteral("/info");
  const QString qname    = QString::fromWCharArray(nameU.Ptr(), (int)nameU.Len());
  const bool inTrashFiles = QFileInfo::exists(filesDir + QStringLiteral("/") + qname);
  const bool hasTrashInfo = QFileInfo::exists(
      infoDir + QStringLiteral("/") + qname + QStringLiteral(".trashinfo"));

  if (op == ScriptOp::DeleteTrash)
  {
    const bool pass = goneFromDir && inTrashFiles && hasTrashInfo;
    printf("DELETE_TRASH: %s gone=%d inTrash=%d info=%d -> %s\n",
        name, goneFromDir ? 1 : 0, inTrashFiles ? 1 : 0, hasTrashInfo ? 1 : 0,
        pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
  }
  // DeletePerm
  const bool pass = goneFromDir && !inTrashFiles;
  printf("DELETE_PERM: %s gone=%d inTrash=%d -> %s\n",
      name, goneFromDir ? 1 : 0, inTrashFiles ? 1 : 0, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// === P.2 : headless language-switch dump ====================================
// Prints "ID<id>=<FmLang(id, english)>" for a representative set of converted
// ids (menu / toolbar / status / dialog). With a loaded --lang txt the lines show
// the translation; without it (or a bad file) they show the inline English
// fallback — the harness asserts the switch by diffing these two runs.
static int RunLangDump()
{
  struct E { unsigned id; const char *eng; const char *tag; };
  static const E table[] = {
    { IDM_FILE,               "&File",                 "MENU_FILE" },
    { IDM_OPEN,               "&Open",                 "MENU_OPEN" },
    { IDM_OPTIONS,            "&Options...",           "MENU_OPTIONS" },
    { IDM_ABOUT,              "&About 7-Zip...",       "MENU_ABOUT" },
    { IDS_ADD,                "Add",                   "BTN_ADD" },
    { IDS_EXTRACT,            "Extract",               "BTN_EXTRACT" },
    { IDS_TEST,               "Test",                  "BTN_TEST" },
    { IDS_OPTIONS,            "Options",               "DLG_OPTIONS" },
    { IDS_N_SELECTED_ITEMS,   "{0} object(s) selected","STATUS_SELECTED" },
    { 401,                    "OK",                    "BTN_OK" },
    { 402,                    "Cancel",                "BTN_CANCEL" },
  };
  for (const E &e : table)
  {
    const QString s = FmLang(e.id, QString::fromUtf8(e.eng));
    printf("ID%u %s=%s\n", e.id, e.tag, s.toUtf8().constData());
  }
  printf("LANG_LOADED=%d\n", QtLang_IsLoaded() ? 1 : 0);
  return 0;
}

int main(int argc, char **argv)
{
  const char *leftDir = nullptr;
  const char *rightDir = nullptr;
  bool selftest = false;
  ScriptOp op = ScriptOp::None;
  const char *hashMethod = nullptr;
  const char *screenshotPath = nullptr;
  const char *optShotPath = nullptr;  // B.8 : grab the Options dialog to a PNG
  const char *opArg = nullptr;   // new name for rename/mkdir/mkfile
  const char *dropMode = nullptr; // "copy" / "move" / "ext"  (B.4b)
  const char *dropExt = nullptr;  // external source file for --drop-ext
  const char *arcName = nullptr;  // B.5b : archive leaf name in <left-dir>
  bool darkMode = false;          // P.3 : force a Fusion dark QPalette (screenshot)
  const char *langFile = nullptr; // P.2 : explicit Lang/*.txt to load (test hook)
  bool langDump = false;          // P.2 : headless dump of converted strings
  const char *langRetranslate = nullptr; // G.9b : live retranslate probe (txt path)
  bool workdirProbe = false;      // G.9c/G.9e : work-dir overlay startup-honoring probe
  bool memQiProbe = false;        // G.8a : IArchiveRequestMemoryUseCallback QI/answer probe
  // B.7c : comma-split components for split/combine/link/diff/fav.
  AString b7cArg1, b7cArg2, b7cArg3;

  for (int i = 1; i < argc; i++)
  {
    const char *a = argv[i];
    if (strcmp(a, "--selftest") == 0) selftest = true;
    else if (strcmp(a, "--add") == 0) op = ScriptOp::Add;
    else if (strcmp(a, "--extract") == 0) op = ScriptOp::Extract;
    else if (strcmp(a, "--test") == 0) op = ScriptOp::Test;
    else if (strcmp(a, "--hash") == 0) { op = ScriptOp::Hash; hashMethod = "*"; }
    else if (strncmp(a, "--hash=", 7) == 0) { op = ScriptOp::Hash; hashMethod = a + 7; }
    // --- B.4a FS operations ---
    else if (strcmp(a, "--copy") == 0) op = ScriptOp::Copy;
    else if (strcmp(a, "--move") == 0) op = ScriptOp::Move;
    else if (strcmp(a, "--delete") == 0) op = ScriptOp::Delete;
    else if (strncmp(a, "--rename=", 9) == 0) { op = ScriptOp::Rename; opArg = a + 9; }
    else if (strncmp(a, "--mkdir=", 8) == 0) { op = ScriptOp::Mkdir; opArg = a + 8; }
    else if (strncmp(a, "--mkfile=", 9) == 0) { op = ScriptOp::Mkfile; opArg = a + 9; }
    // G.4l : inline (in-place) rename via the model's setData() path.
    //   --inline-rename=<entry>,<newName>     : FS panel (<left-dir>).
    //   --arc-inline-rename=<entry>,<newName> : inside --arc-name=<leaf> archive.
    else if (strncmp(a, "--inline-rename=", 16) == 0) { op = ScriptOp::InlineRename; opArg = a + 16; }
    else if (strncmp(a, "--arc-inline-rename=", 20) == 0) { op = ScriptOp::ArcInlineRename; opArg = a + 20; }
    // --- B.5b in-archive ops : <left-dir> holds <archive>; --arc-name=<leaf> ---
    else if (strncmp(a, "--arc-name=", 11) == 0) arcName = a + 11;
    else if (strncmp(a, "--arc-delete=", 13) == 0) { op = ScriptOp::ArcDelete; opArg = a + 13; }
    else if (strcmp(a, "--arc-add") == 0) op = ScriptOp::ArcAdd;
    // G.4m : arrange an OPEN archive (--arc-name=<leaf>) by a sort key, dump order.
    else if (strncmp(a, "--arc-arrange=", 14) == 0) { op = ScriptOp::ArcArrange; opArg = a + 14; }
    else if (strcmp(a, "--arc-arrange") == 0) op = ScriptOp::ArcArrange;
    // P.1 : open an archive through the THREADED worker path + dump root listing.
    else if (strncmp(a, "--open-archive=", 15) == 0) { op = ScriptOp::OpenArchive; arcName = a + 15; }
    // P.3 : assert the deleted-in-archive ForegroundRole mapping for <archive>.
    else if (strncmp(a, "--dark-probe=", 13) == 0) { op = ScriptOp::DarkProbe; arcName = a + 13; }
    // P.3 : render the FM under a forced Fusion dark QPalette (dark-theme shot).
    else if (strcmp(a, "--dark") == 0) darkMode = true;
    // B.5c : archive drag-OUT (extract-to-temp). Optional =entry selects one item.
    else if (strcmp(a, "--arc-dragout") == 0) op = ScriptOp::ArcDragOut;
    else if (strncmp(a, "--arc-dragout=", 14) == 0) { op = ScriptOp::ArcDragOut; opArg = a + 14; }
    // G.2b : enter <innerEntry> (an archive INSIDE --arc-name=<outerLeaf>) as a
    // nested sub-folder; dump its listing, then Up out to prove the return.
    else if (strncmp(a, "--arc-nested=", 13) == 0) { op = ScriptOp::Nested; opArg = a + 13; }
    // G.2d : in-archive CRC/checksum of a selected entry.
    //   --arc-hash=<entry>[,<method>]  (method default CRC32). The entry is split
    //   off into a static buffer (opArg); the method into g_ArcHashMethod.
    else if (strncmp(a, "--arc-hash=", 11) == 0)
    {
      op = ScriptOp::ArcHash;
      static AString s_arcHashEntry;
      s_arcHashEntry = a + 11;
      const int comma = s_arcHashEntry.Find(',');
      if (comma >= 0)
      {
        static AString s_arcHashMethod;
        s_arcHashMethod = s_arcHashEntry.Ptr((unsigned)comma + 1);
        s_arcHashEntry.DeleteFrom((unsigned)comma);
        g_ArcHashMethod = s_arcHashMethod.Ptr();
      }
      opArg = s_arcHashEntry.Ptr();
    }
    // --- B.7a item actions (FS : <left-dir> row) ---
    else if (strcmp(a, "--props") == 0) op = ScriptOp::Props;
    else if (strncmp(a, "--props=", 8) == 0) { op = ScriptOp::Props; opArg = a + 8; }
    // G.5a : multi-select Properties aggregate. FS: select ALL rows in <left-dir>.
    // Archive: with --arc-name=<leaf>, select ALL entries in the open archive.
    else if (strcmp(a, "--props-multi") == 0) op = ScriptOp::PropsMulti;
    // G.5b : drive the Properties dialog interactivity (Ctrl+C copy + value viewer)
    // over the row named <name> (FS in <left-dir>, or archive with --arc-name).
    else if (strncmp(a, "--props-interact=", 17) == 0)
      { op = ScriptOp::PropsInteract; opArg = a + 17; }
    else if (strncmp(a, "--view=", 7) == 0) { op = ScriptOp::OpenView; opArg = a + 7; }
    else if (strncmp(a, "--edit=", 7) == 0) { op = ScriptOp::OpenEdit; opArg = a + 7; }
    else if (strncmp(a, "--open=", 7) == 0) { op = ScriptOp::OpenOut; opArg = a + 7; }
    // --- B.7a item actions (ARCHIVE : needs --arc-name=<leaf>; entry == arg) ---
    else if (strncmp(a, "--arc-props=", 12) == 0) { op = ScriptOp::Props; opArg = a + 12; }
    else if (strncmp(a, "--arc-comment-get=", 18) == 0) { op = ScriptOp::CommentGet; opArg = a + 18; }
    else if (strncmp(a, "--arc-comment-set=", 18) == 0) { op = ScriptOp::CommentSet; opArg = a + 18; }
    else if (strncmp(a, "--comment-text=", 15) == 0) g_CommentText = a + 15;
    else if (strncmp(a, "--arc-view=", 11) == 0) { op = ScriptOp::OpenView; opArg = a + 11; }
    else if (strncmp(a, "--arc-edit=", 11) == 0) { op = ScriptOp::OpenEdit; opArg = a + 11; }
    // --- B.7b view modes / arrange-by / select-by-mask & by-type ---
    else if (strncmp(a, "--view-mode=", 12) == 0) { op = ScriptOp::ViewMode; opArg = a + 12; }
    else if (strncmp(a, "--arrange=", 10) == 0) { op = ScriptOp::Arrange; opArg = a + 10; }
    else if (strncmp(a, "--select-mask=", 14) == 0) { op = ScriptOp::SelectMask; opArg = a + 14; }
    else if (strcmp(a, "--select-type") == 0) op = ScriptOp::SelectType;
    else if (strncmp(a, "--select-type=", 14) == 0) { op = ScriptOp::SelectType; opArg = a + 14; }
    // G.4b : header column chooser. --col-list dumps the column visibility state;
    // --col-hide=<propid-or-name> toggles a column and re-dumps. With --arc-name
    // the dump/toggle is on the OPEN archive's columns (else the FS panel's).
    else if (strcmp(a, "--col-list") == 0) op = ScriptOp::ColList;
    else if (strncmp(a, "--col-hide=", 11) == 0) { op = ScriptOp::ColHide; opArg = a + 11; }
    // G.4c : Flat View. Toggle flat mode on the opened folder and dump the flat
    // listing (FLAT: <prefix><name>). With --arc-name=<leaf> the open archive is
    // flattened; without it, the LEFT FS dir.
    else if (strcmp(a, "--flat-view") == 0) op = ScriptOp::FlatView;
    // G.4f : ShowDots ".." pseudo-row + index-mapping proof. --show-dots[=<file>]
    // enables ShowDots on the LEFT FS panel and probes the named real file (or none);
    // with --arc-name=<leaf> it opens the archive first so the in-archive ".." (exit)
    // is exercised too.
    else if (strcmp(a, "--show-dots") == 0) op = ScriptOp::ShowDots;
    else if (strncmp(a, "--show-dots=", 12) == 0) { op = ScriptOp::ShowDots; opArg = a + 12; }
    // G.4a : view-settings persistence round-trip (run --view-persist-set= then,
    // in a fresh process with the same HOME, --view-persist-get).
    else if (strncmp(a, "--view-persist-set=", 19) == 0) { op = ScriptOp::ViewPersistSet; opArg = a + 19; }
    else if (strcmp(a, "--view-persist-get") == 0) op = ScriptOp::ViewPersistGet;
    // G.4i/G.4j : status-bar focused-item fields + Up-focus restore (FS panel).
    else if (strcmp(a, "--status-up") == 0) op = ScriptOp::StatusUp;
    else if (strncmp(a, "--status-up=", 12) == 0) { op = ScriptOp::StatusUp; opArg = a + 12; }
    // G.4k : dual-pane keyboard cross-panel commands. --key alone runs the
    // tab/path-focus/same-folder checks; --key=<subfolder> also runs the
    // Alt+Right sub-folder mirror against that named left-panel subfolder.
    else if (strcmp(a, "--key") == 0) op = ScriptOp::Key;
    else if (strncmp(a, "--key=", 6) == 0) { op = ScriptOp::Key; opArg = a + 6; }
    // G.4d : Folders History + address-bar ancestors. --hist-dump=<sub1,sub2,...>
    // descends the left FS panel through the named subdirs (recording each), then
    // dumps the recorded history + the address-bar ancestor breadcrumb + the picker
    // navigate outcome. --hist-dump alone just dumps the start dir's history/ancestors.
    else if (strcmp(a, "--hist-dump") == 0) op = ScriptOp::HistDump;
    else if (strncmp(a, "--hist-dump=", 12) == 0) { op = ScriptOp::HistDump; opArg = a + 12; }
    // G.4e : Auto Refresh (FS directory-change watcher) self-check on the LEFT FS dir.
    else if (strcmp(a, "--auto-refresh") == 0) op = ScriptOp::AutoRefresh;
    // G.4g : one/two-panel toggle. --one-panel (run 1) toggles to single-panel,
    // proves otherPanel() validity + the copy dest + NumPanels persistence;
    // --one-panel-get (run 2, fresh process, same HOME) asserts the restore.
    else if (strcmp(a, "--one-panel") == 0) { op = ScriptOp::OnePanel; opArg = "set"; }
    else if (strcmp(a, "--one-panel-get") == 0) { op = ScriptOp::OnePanel; opArg = "get"; }
    // G.4h : Open Root Folder ('\'). --open-root navigates the focused (left) panel
    // to "/"; add --arc-name=<leaf> to first open that archive and prove Open Root
    // Folder exits it to "/".
    else if (strcmp(a, "--open-root") == 0) op = ScriptOp::OpenRoot;
    // G.5f : Edit -> Copy item names to clipboard. --edit-copy selects ALL left items;
    // --edit-copy=<name> selects the single named one.
    else if (strcmp(a, "--edit-copy") == 0) op = ScriptOp::EditCopy;
    else if (strncmp(a, "--edit-copy=", 12) == 0) { op = ScriptOp::EditCopy; opArg = a + 12; }
    // LINK-TARGET : --link-target=<name> reads back an EXISTING symlink's current target.
    else if (strncmp(a, "--link-target=", 14) == 0) { op = ScriptOp::LinkTarget; opArg = a + 14; }
    // --- B.7c Split / Combine / Link / Diff / Favorites ---
    // Split the comma-separated payload into b7cArg1/2/3.
    else if (strncmp(a, "--split=", 8) == 0 || strncmp(a, "--combine=", 10) == 0
          || strncmp(a, "--link=", 7) == 0 || strncmp(a, "--fav-go=", 9) == 0
          || strncmp(a, "--diff=", 7) == 0)
    {
      const char *eq = strchr(a, '=');
      AString payload(eq + 1);
      if      (strncmp(a, "--split=", 8) == 0)   op = ScriptOp::Split;
      else if (strncmp(a, "--combine=", 10) == 0) op = ScriptOp::Combine;
      else if (strncmp(a, "--link=", 7) == 0)    op = ScriptOp::Link;
      else if (strncmp(a, "--fav-go=", 9) == 0)  op = ScriptOp::FavGo;
      else                                       op = ScriptOp::Diff;
      // split into up to 3 comma-separated tokens.
      int tok = 0, start = 0;
      const int len = (int)payload.Len();
      for (int j = 0; j <= len; j++)
      {
        if (j < len && payload[(unsigned)j] != ',')
          continue;
        AString t = payload.Mid((unsigned)start, (unsigned)(j - start));
        start = j + 1;
        if      (tok == 0) b7cArg1 = t;
        else if (tok == 1) b7cArg2 = t;
        else if (tok == 2) b7cArg3 = t;
        tok++;
      }
    }
    else if (strcmp(a, "--diff") == 0) op = ScriptOp::Diff;
    else if (strcmp(a, "--fav-add") == 0) op = ScriptOp::FavAdd;
    // --- B.8 Options persistence + trash/permanent delete ---
    else if (strncmp(a, "--opt-set=", 10) == 0) { op = ScriptOp::OptSet; opArg = a + 10; }
    else if (strncmp(a, "--opt-get=", 10) == 0) { op = ScriptOp::OptGet; opArg = a + 10; }
    else if (strncmp(a, "--delete-trash=", 15) == 0) { op = ScriptOp::DeleteTrash; opArg = a + 15; }
    else if (strncmp(a, "--delete-perm=", 14) == 0) { op = ScriptOp::DeletePerm; opArg = a + 14; }
    // --- B.4b drag & drop (scripted) ---
    else if (strcmp(a, "--drop-copy") == 0) dropMode = "copy";
    else if (strcmp(a, "--drop-move") == 0) dropMode = "move";
    else if (strcmp(a, "--drop-default") == 0) dropMode = "default";
    else if (strcmp(a, "--drop-archive") == 0) dropMode = "archive";
    else if (strncmp(a, "--drop-ext=", 11) == 0) { dropMode = "ext"; dropExt = a + 11; }
    // --- G.6 drop-path correctness (subfolder target / archive confirm / failure) ---
    else if (strcmp(a, "--drop-subfolder") == 0) dropMode = "subfolder"; // G.6c
    else if (strcmp(a, "--drop-arc-confirm") == 0) dropMode = "arc-confirm"; // G.6a
    else if (strcmp(a, "--drop-fail") == 0) dropMode = "fail";    // G.6e error + KillSelection
    // G.6b : compress-on-drop — EXTERNAL <file> dropped onto the RIGHT FS panel ->
    // Add-to-Archive (createNewArchive). Reuses dropExt as the dropped file path.
    else if (strncmp(a, "--drop-compress=", 16) == 0) { dropMode = "compress"; dropExt = a + 16; }
    // G.6d : right-button-drag menu — drop the LEFT selection onto RIGHT and FORCE the
    // chosen menu command (copy|move|add). copy/move = FS Copy/Move here; add = Add to
    // archive (compress-on-drop). Proves the menu-action dispatch headlessly.
    else if (strncmp(a, "--drop-rmenu=", 13) == 0) { dropMode = "rmenu"; dropExt = a + 13; }
    else if (strncmp(a, "--screenshot=", 13) == 0) screenshotPath = a + 13;
    else if (strncmp(a, "--options-screenshot=", 21) == 0) optShotPath = a + 21;
    // --- P.2 i18n : explicit txt load + headless converted-string dump ---
    else if (strncmp(a, "--lang=", 7) == 0) langFile = a + 7;
    else if (strcmp(a, "--lang-dump") == 0) langDump = true;
    // --- G.9b : live menu/toolbar retranslate probe (read a menu label, load the
    //     given txt, retranslate, read it again -> prove it changed in place). ---
    else if (strncmp(a, "--lang-retranslate=", 19) == 0) langRetranslate = a + 19;
    // --- G.9c/G.9e : work-dir overlay probe. Pushes the PERSISTED WorkDir settings
    //     into the agent overlay exactly as the window ctor does at startup, then
    //     reports the engine's EFFECTIVE NWorkDir::NMode (proves startup honoring). ---
    else if (strcmp(a, "--workdir-probe") == 0) workdirProbe = true;
    // G.8a : prove IArchiveRequestMemoryUseCallback is QI-reachable on the Qt
    // extract callback AND that RequestMemoryUse answers correctly (mirrors the
    // engine's QI at Rar5Handler.cpp:2980 + its RequestMemoryUse call).
    else if (strcmp(a, "--mem-qi-probe") == 0) memQiProbe = true;
    // G.10a : headless Tools->Benchmark. Optional =<props> (e.g. "mt2,d1m") tunes
    // it; default is the dialog's empty prop set (engine picks dict/threads).
    else if (strcmp(a, "--benchmark") == 0) op = ScriptOp::Benchmark;
    else if (strncmp(a, "--benchmark=", 12) == 0) { op = ScriptOp::Benchmark; opArg = a + 12; }
    // G.10b : Help->About — dump the real version/date/homepage the dialog shows.
    else if (strcmp(a, "--about") == 0) op = ScriptOp::About;
    // G.10c : Tools->Delete Temporary Files — mint a temp dir, run deleteTempFiles().
    else if (strcmp(a, "--delete-temp") == 0) op = ScriptOp::DeleteTempFiles;
    else if (!leftDir) leftDir = a;
    else if (!rightDir) rightDir = a;
  }

  const bool offscreen = selftest || op != ScriptOp::None
      || dropMode != nullptr || optShotPath != nullptr || langDump || memQiProbe
      || langRetranslate != nullptr || workdirProbe;
  if (offscreen && !getenv("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");

  QApplication app(argc, argv);

  // Icon-theme fallback. The per-type file icons come from QIcon::fromTheme(), which
  // needs a current icon-theme NAME. On a normal desktop the platform-theme plugin
  // supplies it from the user's settings, but a self-contained build (AppImage) may
  // run WITHOUT that plugin, leaving QIcon::themeName() empty -> fromTheme() resolves
  // nothing and files show no icons. When no theme is active, pick the first installed
  // theme that actually provides icons (the theme FILES are still found on the system
  // via the XDG icon dirs). A non-empty themeName (the usual case) is left untouched.
  if (QIcon::themeName().isEmpty())
  {
    static const char * const kThemeCandidates[] = {
      "breeze", "Adwaita", "oxygen", "gnome", "elementary", "Papirus",
      "Mint-Y", "Yaru", "hicolor" };
    for (const char *t : kThemeCandidates)
    {
      QIcon::setThemeName(QString::fromLatin1(t));
      if (!QIcon::fromTheme(QStringLiteral("text-x-generic")).isNull())
        break;
    }
  }
  // A fallback theme covers individual icons the active theme happens to lack.
  if (QIcon::fallbackThemeName().isEmpty())
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));

  // P.2 : load the active translation BEFORE any window/menu is built so every
  // FmLang(id, english) resolves through the loaded CLang.
  //   --lang=<file>  : explicit txt (test hook), overrides the persisted setting;
  //   otherwise      : read [Options] Lang and load <langdir>/<name>.txt
  //                    (StartupLoadLang = the ReloadLang equivalent).
  // A missing / bad-signature / wrong-id-0 file leaves the English fallback active.
  if (langFile)
  {
    const UString p = GetUnicodeString(langFile);
    const bool ok = QtLang_LoadFile(us2fs(p));
    if (!ok)
      fprintf(stderr, "lang: failed to load \"%s\" (bad signature/id-0/missing) "
          "-> English fallback\n", langFile);
  }
  else
    QtFmSettings::StartupLoadLang();

  if (langDump)
    return RunLangDump();

  // P.3 : a forced dark theme for the dark-mode screenshot / dark-palette probe.
  // Fusion fully honors a custom QPalette; applied via setPalette BEFORE any
  // window is built so it propagates to the toolbar, tree, gridlines, alternating
  // rows and selection — visually proving everything is palette-driven. Because
  // QtFolderModel::deletedTextColor() reads QGuiApplication::palette(), the
  // deleted-red auto-switches to its lightened (dark-readable) shade under --dark.
  if (darkMode)
  {
    app.setStyle("Fusion");
    QPalette p;
    p.setColor(QPalette::Window,          QColor(53, 53, 53));
    p.setColor(QPalette::WindowText,      Qt::white);
    p.setColor(QPalette::Base,            QColor(35, 35, 35));
    p.setColor(QPalette::AlternateBase,   QColor(53, 53, 53));
    p.setColor(QPalette::Text,            Qt::white);
    p.setColor(QPalette::Button,          QColor(53, 53, 53));
    p.setColor(QPalette::ButtonText,      Qt::white);
    p.setColor(QPalette::Highlight,       QColor(42, 130, 218));
    p.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(p);
  }

  if (selftest)
    return RunSelfTest();

  // Resolve the two starting directories (default: cwd).
  FString cwd; NFile::NDir::GetCurrentDir(cwd);
  const UString cwdU = fs2us(cwd);
  const UString leftU  = leftDir  ? GetUnicodeString(leftDir)  : cwdU;
  const UString rightU = rightDir ? GetUnicodeString(rightDir) : cwdU;
  // G.4a : the RAW (un-defaulted) dirs — EMPTY when the user gave no positional
  // arg. The GUI launch and the view-persist round-trip pass these (not the
  // cwd-substituted leftU/rightU) so the window's per-panel last-path restore
  // (ReadPanelPath) is reachable: an empty dir means "no explicit launch dir",
  // letting a persisted path win, with cwd applied as the window's final fallback.
  const UString leftRaw  = leftDir  ? GetUnicodeString(leftDir)  : UString();
  const UString rightRaw = rightDir ? GetUnicodeString(rightDir) : UString();

  if (dropMode != nullptr)
    return RunScriptedDrop(leftU, rightU, dropMode, dropExt);

  // B.8 : render the Options dialog to a PNG (visual sanity of the four tabs).
  if (optShotPath != nullptr)
  {
    QtFmSettings::CInfo s; s.Load();
    QtOptionsDialog dlg;
    dlg.Settings = s;
    dlg.fillFromState();
    dlg.resize(460, 360);
    dlg.show();
    QPixmap shot = dlg.grab();
    const QString path = QString::fromUtf8(optShotPath);
    printf("options-screenshot: %s (%dx%d)\n",
        path.toUtf8().constData(), shot.width(), shot.height());
    return shot.save(path) ? 0 : 1;
  }

  // G.8a : IArchiveRequestMemoryUseCallback QI/answer probe (no panel needed).
  if (memQiProbe)
    return RunMemQiProbe();

  // G.10a : headless Tools->Benchmark (no panel needed).
  if (op == ScriptOp::Benchmark)
    return RunBenchmarkProbe(opArg);

  // G.10b/G.10c : headless Help->About / Tools->Delete Temporary Files.
  if (op == ScriptOp::About)
    return RunAboutProbe(leftU, rightU);
  if (op == ScriptOp::DeleteTempFiles)
    return RunDeleteTempProbe(leftU, rightU);

  // B.8 : Options persistence round-trip (no panel needed) + trash/perm delete.
  if (op == ScriptOp::OptSet)
    return RunScriptedOptSet(opArg);
  if (op == ScriptOp::OptGet)
    return RunScriptedOptGet(opArg);

  // G.9c/G.9e : work-dir overlay startup-honoring probe (settings-only, no window).
  if (workdirProbe)
    return RunWorkDirProbe();

  // G.9b : live menu/toolbar retranslate probe (builds a headless window).
  if (langRetranslate != nullptr)
    return RunLangRetranslateProbe(leftU, rightU, langRetranslate);
  if (op == ScriptOp::DeleteTrash || op == ScriptOp::DeletePerm)
    return RunScriptedDelete(leftU, rightU, op, opArg);

  // Comment ops are archive-only (a FS file has no kpidComment write path).
  const bool commentOp = (op == ScriptOp::CommentGet || op == ScriptOp::CommentSet);
  // B.7a Props/View/Edit route to the ARCHIVE path when --arc-name is given
  // (the entry lives inside the open archive); otherwise they hit the FS path.
  const bool b7aOp = (op == ScriptOp::Props || op == ScriptOp::OpenView
      || op == ScriptOp::OpenEdit
      || op == ScriptOp::PropsMulti        // G.5a : multi-select Properties
      || op == ScriptOp::PropsInteract);   // G.5b : Properties interactivity

  // P.1 : threaded archive open + listing dump (archive lives in <left-dir>).
  if (op == ScriptOp::OpenArchive)
  {
    if (!arcName)
    {
      fprintf(stderr, "open-archive: --open-archive=<archive-leaf> required\n");
      return 2;
    }
    return RunScriptedOpenArchive(leftU, rightU, arcName);
  }

  // P.3 : deleted-in-archive ForegroundRole mapping probe (archive in <left-dir>).
  if (op == ScriptOp::DarkProbe)
  {
    if (!arcName)
    {
      fprintf(stderr, "dark-probe: --dark-probe=<archive-leaf> required\n");
      return 2;
    }
    return RunScriptedDarkProbe(leftU, rightU, arcName);
  }

  // G.2b : nested-archive entry (outer archive in <left-dir> via --arc-name;
  // inner entry via --arc-nested). Enter the nested archive seamlessly + Up out.
  if (op == ScriptOp::Nested)
  {
    if (!arcName || !opArg)
    {
      fprintf(stderr, "arc-nested: --arc-name=<outer-leaf> and "
          "--arc-nested=<inner-entry> required\n");
      return 2;
    }
    return RunScriptedNested(leftU, rightU, arcName, opArg);
  }

  if (op == ScriptOp::ArcDelete || op == ScriptOp::ArcAdd
      || op == ScriptOp::ArcDragOut
      || op == ScriptOp::ArcHash
      || op == ScriptOp::ArcInlineRename   // G.4l
      || op == ScriptOp::ArcArrange        // G.4m
      || commentOp
      || (b7aOp && arcName)
      // G.4b : column chooser ON THE OPEN ARCHIVE when --arc-name is given.
      || ((op == ScriptOp::ColList || op == ScriptOp::ColHide) && arcName)
      // G.4c : Flat View ON THE OPEN ARCHIVE when --arc-name is given.
      || (op == ScriptOp::FlatView && arcName))
  {
    if (!arcName)
    {
      fprintf(stderr, "scripted-arc: --arc-name=<archive-leaf> required "
          "(archive lives in <left-dir>)\n");
      return 2;
    }
    return RunScriptedArc(leftU, rightU, arcName, op, opArg);
  }

  // G.4l : FS-panel inline rename via the model's setData() path.
  if (op == ScriptOp::InlineRename)
  {
    QByteArray ab(opArg ? opArg : "");
    const int comma = ab.indexOf(',');
    QByteArray e = comma >= 0 ? ab.left(comma) : ab;
    QByteArray nn = comma >= 0 ? ab.mid(comma + 1) : QByteArray();
    return RunScriptedInlineRename(leftU, rightU, e.constData(), nn.constData());
  }

  // B.7b : view-mode / arrange-by / select-by-mask & by-type (FS panel).
  if (op == ScriptOp::ViewMode || op == ScriptOp::Arrange
      || op == ScriptOp::SelectMask || op == ScriptOp::SelectType)
    return RunScriptedView(leftU, rightU, op, opArg);

  // G.4b : header column chooser on the FS panel (no --arc-name; the archive
  // variant is routed to RunScriptedArc above).
  if (op == ScriptOp::ColList || op == ScriptOp::ColHide)
  {
    FString fdir = us2fs(leftU);
    CMyComPtr<IFolderFolder> root;
    if (BindFsFolder(fdir, root) != S_OK || !root)
    {
      fprintf(stderr, "scripted-columns: cannot bind FS folder %s\n", OemUtf8(leftU));
      return 2;
    }
    QtFileManagerWindow win(leftU, rightU, /*headless*/ true);
    QtPanel *panel = win.leftPanel();
    win.focusPanelForTest(panel);
    return RunScriptedColumns(win, panel, op, opArg);
  }

  // G.4c : Flat View on the FS panel (no --arc-name; the archive variant routes to
  // RunScriptedArc above). CFSFolder::SetFlatMode -> LoadSubItems recurses the tree.
  if (op == ScriptOp::FlatView)
  {
    FString fdir = us2fs(leftU);
    CMyComPtr<IFolderFolder> root;
    if (BindFsFolder(fdir, root) != S_OK || !root)
    {
      fprintf(stderr, "scripted-flat: cannot bind FS folder %s\n", OemUtf8(leftU));
      return 2;
    }
    QtFileManagerWindow win(leftU, rightU, /*headless*/ true);
    return RunScriptedFlat(win, win.leftPanel());
  }

  // G.4a : per-list-type view-settings persistence round-trip (FS panel).
  if (op == ScriptOp::ViewPersistSet || op == ScriptOp::ViewPersistGet)
    return RunScriptedViewPersist(leftRaw, rightRaw, op, opArg);

  // G.4i/G.4j : status-bar focused-item fields + Up-focus restore (FS or, when
  // --arc-name is given, an open archive in the left panel).
  if (op == ScriptOp::StatusUp)
    return RunScriptedStatusUp(leftU, rightU, opArg, arcName);

  // G.4f : ShowDots ".." pseudo-row + index-mapping proof (FS panel, or — when
  // --arc-name is given — an open archive in the left panel).
  if (op == ScriptOp::ShowDots)
    return RunScriptedShowDots(leftU, rightU, opArg, arcName);

  // G.4k : dual-pane keyboard cross-panel commands (FS panels).
  if (op == ScriptOp::Key)
    return RunScriptedKey(leftU, rightU, opArg);

  // G.4d : Folders History + address-bar ancestors (FS panels).
  if (op == ScriptOp::HistDump)
    return RunScriptedHistory(leftU, rightU, opArg);

  // G.4e : Auto Refresh (FS directory-change watcher) self-check (LEFT FS panel).
  if (op == ScriptOp::AutoRefresh)
    return RunScriptedAutoRefresh(leftU, rightU);

  // G.4g : one/two-panel toggle + persistence (FS panels).
  if (op == ScriptOp::OnePanel)
    return RunScriptedOnePanel(leftU, rightU, opArg);

  // G.4h : Open Root Folder ('\') -> the focused panel navigates to "/" (and, with
  // --arc-name, exits the opened archive to "/").
  if (op == ScriptOp::OpenRoot)
    return RunScriptedOpenRoot(leftU, rightU, arcName);

  // G.5f : Edit -> Copy item names to clipboard.
  if (op == ScriptOp::EditCopy)
    return RunScriptedEditCopy(leftU, rightU, opArg);

  // LINK-TARGET : the QtLinkDialog current-target read-back for an existing symlink.
  if (op == ScriptOp::LinkTarget)
    return RunScriptedLinkTarget(leftU, rightU, opArg);

  // B.7c : Split / Combine / Link / Diff / Favorites (FS panels).
  if (op == ScriptOp::Split || op == ScriptOp::Combine || op == ScriptOp::Link
      || op == ScriptOp::Diff || op == ScriptOp::FavAdd || op == ScriptOp::FavGo)
    return RunScriptedSplitCombine(leftU, rightU, op,
        b7cArg1.IsEmpty() ? nullptr : b7cArg1.Ptr(),
        b7cArg2.IsEmpty() ? nullptr : b7cArg2.Ptr(),
        b7cArg3.IsEmpty() ? nullptr : b7cArg3.Ptr());

  if (op != ScriptOp::None)
    return RunScriptedOp(leftU, rightU, op, hashMethod, opArg);

  // If the positional argument is an existing FILE (not a directory) — e.g. an
  // archive opened through a desktop file-association or a file manager's "Open
  // with" — start the LEFT panel in the file's PARENT directory and open the file
  // once the event loop is running (an archive opens as a folder; anything else
  // opens with its associated program). A directory argument keeps the old behavior.
  UString leftStart = leftRaw;
  UString startupItem;
  if (!leftRaw.IsEmpty())
  {
    NFile::NFind::CFileInfo fi;
    if (fi.Find(us2fs(leftRaw)) && !fi.IsDir())
    {
      const int slash = leftRaw.ReverseFind_PathSepar();
      if (slash >= 0)
      {
        leftStart   = leftRaw.Left((unsigned)(slash + 1)); // parent dir (keeps the separator)
        startupItem = leftRaw.Ptr((unsigned)(slash + 1));  // leaf name
      }
      else
      {
        leftStart   = cwdU;       // bare name -> current directory
        startupItem = leftRaw;
      }
    }
  }

  // G.4a : pass the RAW dirs so an absent positional arg lets the window restore
  // the persisted per-panel last path (ReadPanelPath), falling back to cwd only
  // when nothing was stored. An explicit dir filled leftRaw/rightRaw and overrides.
  QtFileManagerWindow window(leftStart, rightRaw, /*headless*/ false);
  window.show();

  // Deferred so the panel is fully shown (and the open's progress dialog / password
  // prompt parents correctly) before the archive open runs.
  if (!startupItem.IsEmpty())
  {
    const UString item = startupItem;
    QTimer::singleShot(0, &window, [&window, item]() {
      if (window.leftPanel())
        window.leftPanel()->openFsItemByName(item);
    });
  }

  // Self-grab screenshot path: show the window, let it paint, grab it to a PNG,
  // then quit. Used to capture the real Wayland/X rendering of the two-panel
  // shell (toolbar with the ORIGINAL icons, menubar, status bar, FM.ico).
  if (screenshotPath)
  {
    const QString path = QString::fromUtf8(screenshotPath);
    QTimer::singleShot(900, &window, [&window, path]() {
      QPixmap shot = window.grab();
      if (shot.save(path))
        printf("screenshot saved: %s (%dx%d)\n",
            path.toUtf8().constData(), shot.width(), shot.height());
      else
        printf("screenshot FAILED to save: %s\n", path.toUtf8().constData());
      QApplication::quit();
    });
  }

  return app.exec();
}
