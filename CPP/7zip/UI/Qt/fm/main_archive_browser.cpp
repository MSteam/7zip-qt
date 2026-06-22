// main_archive_browser.cpp
// ----------------------------------------------------------------------------
// Milestone B.1 : entry point + controller for the 7zqt_browser target.
//
//   7zqt_browser <archive> [-p<password>] [--dump]
//
// It opens the archive with CAgent (reusing the B.0 open pattern + an open
// callback that supplies the password), gets the root IFolderFolder, and:
//   * normally          : constructs a QtArchiveBrowser over that root and shows it,
//   * with --dump (or env SEVENZQT_BROWSE_DUMP=1) : drives QtFolderModel
//     programmatically WITHOUT showing a window (a headless self-check), walking
//     the whole tree via enterItem/goParent and reading data() per cell, printing
//     a listing that can be diffed against `7zz l`.
//
// This is the SINGLE GUID-emitting TU for the executable (MyInitGuid.h), exactly
// like agent_browse_test.cpp / main_7zqt.cpp.
//
// THREADING: the open + listing run synchronously on the GUI thread. Archives
// list fast; a large-archive open should later move to a worker (B.1 note).
// ----------------------------------------------------------------------------

#include "../../Agent/StdAfx.h"

#include "../agent/AgentLinuxCompat.h"

// The one place GUIDs are instantiated for the whole executable.
#include "../../../../Common/MyInitGuid.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <QtWidgets/QApplication>

#include "../../../../Common/MyString.h"
#include "../../../../Common/StringConvert.h"
#include "../../../../Windows/PropVariant.h"

#include "../../../Common/FileStreams.h"
#include "../../../PropID.h"
#include "../../../IPassword.h"

#include "../../Agent/Agent.h"

#include "QtFolderModel.h"
#include "QtArchiveBrowser.h"

using namespace NWindows;

// --- minimal open callback : supplies the password headlessly ---------------
// Same shape as agent_browse_test.cpp's COpenCallbackTest (B.0).
class COpenCallbackBrowser Z7_final:
  public IArchiveOpenCallback,
  public ICryptoGetTextPassword,
  public CMyUnknownImp
{
  Z7_COM_QI_BEGIN2(IArchiveOpenCallback)
    Z7_COM_QI_ENTRY(ICryptoGetTextPassword)
  Z7_COM_QI_END
  Z7_COM_ADDREF_RELEASE

  Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
public:
  bool PasswordIsDefined;
  UString Password;
  COpenCallbackBrowser(): PasswordIsDefined(false) {}
};

Z7_COM7F_IMF(COpenCallbackBrowser::SetTotal(const UInt64 *, const UInt64 *)) { return S_OK; }
Z7_COM7F_IMF(COpenCallbackBrowser::SetCompleted(const UInt64 *, const UInt64 *)) { return S_OK; }
Z7_COM7F_IMF(COpenCallbackBrowser::CryptoGetTextPassword(BSTR *password))
{
  if (!PasswordIsDefined)
    return StringToBstr(UString(), password);
  return StringToBstr(Password, password);
}

// --- headless self-check : drive QtFolderModel like a view would -----------
// Walks the current folder, printing one line per item (Name / Size / DIR),
// then recurses into each subfolder via enterItem(), and goParent() back out,
// proving the navigation API. The listing is built from data()/itemSize() so it
// exercises exactly the code a Qt view consumes.

static const char *OemUtf8(const UString &s)
{
  static AString buf; // not reentrant; fine for single-threaded dump
  buf = GetOemString(s);
  return (const char *)buf;
}

static UString full_path_join(const UString &prefix, const UString &name)
{
  return prefix + name + UString(L"/");
}

static void DumpFolder(QtFolderModel &model, const UString &prefix, unsigned &files,
    unsigned &dirs, UInt64 &totalSize)
{
  const int nameCol = model.columnForPropID(kpidName);
  const int sizeCol = model.columnForPropID(kpidSize);
  const int mtimeCol = model.columnForPropID(kpidMTime);

  const int rows = model.rowCount();
  for (int r = 0; r < rows; r++)
  {
    const bool isDir = model.isFolder(r);
    const UString name = model.itemName(r);
    const UString full = prefix + name;

    // Name column text via data() (what the view shows).
    QString qName;
    if (nameCol >= 0)
      qName = model.data(model.index(r, nameCol), Qt::DisplayRole).toString();

    // Size: raw kpidSize integer (matches `7zz l`); also the grouped display
    // text the Size column shows, to prove the column formatting.
    bool sizeDef = false;
    const UInt64 sz = model.itemSize(r, sizeDef);
    QString qSizeText;
    if (sizeCol >= 0)
      qSizeText = model.data(model.index(r, sizeCol), Qt::DisplayRole).toString();

    QString qMTime;
    if (mtimeCol >= 0)
      qMTime = model.data(model.index(r, mtimeCol), Qt::DisplayRole).toString();

    if (isDir)
    {
      dirs++;
      printf("[DIR ] %s/\n", OemUtf8(full));
    }
    else
    {
      files++;
      if (sizeDef)
        totalSize += sz;
      printf("[FILE] %-40s size=%llu  size_col='%s'  mtime='%s'\n",
          OemUtf8(full),
          sizeDef ? (unsigned long long)sz : 0ULL,
          qSizeText.toUtf8().constData(),
          qMTime.toUtf8().constData());
    }
    (void)qName;
  }

  // Recurse: enter each subfolder, dump, then go back to parent. Re-resolve
  // folder rows after each return (rowCount/order is restored by goParent).
  for (int r = 0; r < model.rowCount(); r++)
  {
    if (!model.isFolder(r))
      continue;
    const UString name = model.itemName(r);
    if (model.enterItem(r))
    {
      DumpFolder(model, full_path_join(prefix, name), files, dirs, totalSize);
      model.goParent();
    }
  }
}

// --- explicit navigation transcript ----------------------------------------
// Proves the navigation API directly: enterItem() into the first folder repeatedly
// (descending into the tree), printing rowCount + currentPath at each level, then
// goParent() all the way back, confirming rowCount returns to the original value.
static int RunNav(IFolderFolder *rootFolder)
{
  QtFolderModel model;
  model.setRootFolder(rootFolder);

  printf("---- navigation transcript (enterItem / goParent) ----\n");
  const int rootRows = model.rowCount();
  printf("root: rowCount=%d  path='%s'\n", rootRows, OemUtf8(model.currentPath()));

  // Descend: at each level enter the FIRST folder row.
  for (;;)
  {
    int folderRow = -1;
    for (int r = 0; r < model.rowCount(); r++)
      if (model.isFolder(r)) { folderRow = r; break; }
    if (folderRow < 0)
      break;
    const UString name = model.itemName(folderRow);
    if (!model.enterItem(folderRow))
      break;
    printf("  enter '%s' -> rowCount=%d  path='%s'  isRoot=%d\n",
        OemUtf8(name), model.rowCount(), OemUtf8(model.currentPath()),
        model.isRoot() ? 1 : 0);
  }

  // Ascend: goParent back to the root.
  while (model.goParent())
    printf("  goParent -> rowCount=%d  path='%s'  isRoot=%d\n",
        model.rowCount(), OemUtf8(model.currentPath()), model.isRoot() ? 1 : 0);

  const int backRows = model.rowCount();
  printf("back at: rowCount=%d (root was %d) -> %s\n",
      backRows, rootRows, backRows == rootRows ? "RESTORED" : "MISMATCH");
  return backRows == rootRows ? 0 : 1;
}

static int RunDump(IFolderFolder *rootFolder)
{
  QtFolderModel model;
  model.setRootFolder(rootFolder);

  printf("---- model-driven listing (QtFolderModel over IFolderFolder) ----\n");
  printf("columns:");
  for (int c = 0; c < model.columnCount(); c++)
  {
    const QVariant h = model.headerData(c, Qt::Horizontal, Qt::DisplayRole);
    printf(" [%s]", h.toString().toUtf8().constData());
  }
  printf("\n");

  unsigned files = 0, dirs = 0;
  UInt64 totalSize = 0;
  DumpFolder(model, UString(), files, dirs, totalSize);

  printf("---- summary ----\n");
  printf("files=%u dirs=%u total_size=%llu\n",
      files, dirs, (unsigned long long)totalSize);
  return 0;
}

int main(int argc, char **argv)
{
  const char *archivePath = nullptr;
  UString password;
  bool passwordDefined = false;
  bool dump = false;
  bool nav = false;

  for (int i = 1; i < argc; i++)
  {
    const char *a = argv[i];
    if (strcmp(a, "--dump") == 0)
      dump = true;
    else if (strcmp(a, "--nav") == 0)
      nav = true;
    else if (strncmp(a, "-p", 2) == 0)
    {
      password = GetUnicodeString(a + 2);
      passwordDefined = true;
    }
    else if (!archivePath)
      archivePath = a;
  }

  if (const char *env = getenv("SEVENZQT_BROWSE_DUMP"))
    if (env[0] && env[0] != '0')
      dump = true;

  if (!archivePath)
  {
    fprintf(stderr, "usage: %s <archive> [-p<password>] [--dump] [--nav]\n", argv[0]);
    return 2;
  }

  const UString arcPath = GetUnicodeString(archivePath);

  // 1. Open the archive file as an IInStream (CInFileStream), B.0 pattern.
  CInFileStream *fileSpec = new CInFileStream;
  CMyComPtr<IInStream> fileStream = fileSpec;
  if (!fileSpec->Open(us2fs(arcPath)))
  {
    fprintf(stderr, "cannot open file: %s\n", archivePath);
    return 1;
  }

  COpenCallbackBrowser *cbSpec = new COpenCallbackBrowser;
  CMyComPtr<IArchiveOpenCallback> openCallback = cbSpec;
  if (passwordDefined)
  {
    cbSpec->PasswordIsDefined = true;
    cbSpec->Password = password;
  }

  // 2. Create the CAgent and open. arcFormat must be L"" (NOT NULL) - B.0 note.
  CAgent *agentSpec = new CAgent;
  CMyComPtr<IInFolderArchive> agent = agentSpec;
  CMyComBSTR archiveType;
  HRESULT res = agent->Open(fileStream, arcPath, L"", &archiveType, openCallback);
  if (res != S_OK)
  {
    fprintf(stderr, "Open failed: 0x%08X\n", (unsigned)res);
    if (!agentSpec->_archiveLink.NonOpen_ErrorInfo.IsThereErrorOrWarning())
      return 1;
    fprintf(stderr, "(continuing: NonOpen error info present)\n");
  }

  // 3. Get the root IFolderFolder.
  CMyComPtr<IFolderFolder> rootFolder;
  res = agent->BindToRootFolder(&rootFolder);
  if (res != S_OK || !rootFolder)
  {
    fprintf(stderr, "BindToRootFolder failed: 0x%08X\n", (unsigned)res);
    return 1;
  }

  if (dump || nav)
  {
    // Headless self-check: no GUI shown; just drive the model.
    // (A QApplication is still constructed for any Qt global state the model
    // might touch, with the offscreen platform so it needs no display.)
    if (!getenv("QT_QPA_PLATFORM"))
      qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    int rc = 0;
    if (dump)
      rc |= RunDump(rootFolder);
    if (nav)
      rc |= RunNav(rootFolder);
    agent->Close();
    return rc;
  }

  // 4. GUI: construct the browser over the root folder and show it.
  QApplication app(argc, argv);
  QtArchiveBrowser browser(rootFolder, QString::fromUtf8(archivePath));
  browser.show();
  const int rc = app.exec();
  agent->Close();
  return rc;
}
