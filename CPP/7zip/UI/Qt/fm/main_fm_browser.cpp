// main_fm_browser.cpp
// ----------------------------------------------------------------------------
// Milestone B.2 : entry point + controller for the 7zqt_fm target.
//
//   7zqt_fm [<dir-or-archive>] [-p<password>] [--dump] [--nav]
//
// argv[1] may be:
//   * a DIRECTORY  -> bound as a CFSFolder rooted at that path (the B.2 Linux
//                     filesystem IFolderFolder),
//   * an ARCHIVE   -> opened with CAgent (B.0 pattern), starting inside it,
//   * omitted      -> the current working directory, as a CFSFolder.
//
// Normally it shows a QtFsBrowser (filesystem view with the seamless
// filesystem -> archive transition). With --dump / --nav it runs the same
// headless self-checks as main_archive_browser.cpp (offscreen), so the FS
// listing can be diffed against `ls`/`find`, and the navigation API exercised.
//
// This is the SINGLE GUID-emitting TU for the executable (MyInitGuid.h).
// ----------------------------------------------------------------------------

#include "../../FileManager/StdAfx.h"

#include "../agent/AgentLinuxCompat.h"

// The one place GUIDs are instantiated for the whole executable.
#include "../../../../Common/MyInitGuid.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <QtWidgets/QApplication>

#include "../../../../Common/MyString.h"
#include "../../../../Common/StringConvert.h"
#include "../../../../Windows/FileDir.h"
#include "../../../../Windows/FileFind.h"
#include "../../../../Windows/FileName.h"
#include "../../../../Windows/PropVariant.h"

#include "../../../PropID.h"
#include "../../../IPassword.h"

// Pull in the engine interface headers (ICoder / ICrypto / IArchive / IFolder
// GUIDs) so this - the SINGLE MyInitGuid.h TU for 7zqt_fm - emits the full set
// of interface GUIDs the linked engine needs (same set main_archive_browser.cpp
// gets via Agent.h). Without this the WHOLE_ARCHIVE engine pulls in codec
// QueryInterface()s that reference IID_ICompress* / IID_ICrypto* with no defn.
#include "../../Agent/Agent.h"

#include "../../FileManager/FSFolder.h"

#include "QtFolderModel.h"
#include "QtFsBrowser.h"
#include "ArchiveOpenHelper.h"

using namespace NWindows;

// --- OEM/UTF-8 printing helper (same as main_archive_browser.cpp) ------------
static const char *OemUtf8(const UString &s)
{
  static AString buf; // not reentrant; fine for single-threaded dump
  buf = GetOemString(s);
  return (const char *)buf;
}

// --- build a CFSFolder rooted at a directory ---------------------------------
// Mirrors the engine's own CFSFolder instantiation (FSFolder.cpp
// BindToFolderSpec / Clone): new CFSFolder; Init(path-with-trailing-separator).
static HRESULT BindFsFolder(const FString &dirPath, CMyComPtr<IFolderFolder> &folder)
{
  FString path = dirPath;
  NFile::NName::NormalizeDirPathPrefix(path); // ensure trailing separator
  NFsFolder::CFSFolder *fsSpec = new NFsFolder::CFSFolder;
  CMyComPtr<IFolderFolder> f = fsSpec;
  RINOK(fsSpec->Init(path))
  folder = f;
  return S_OK;
}

// --- headless dump : list the current folder like `ls`, recurse subdirs ------
static void DumpFolder(QtFolderModel &model, const UString &prefix,
    unsigned &files, unsigned &dirs, UInt64 &totalSize)
{
  const int sizeCol = model.columnForPropID(kpidSize);
  const int mtimeCol = model.columnForPropID(kpidMTime);
  const int attrCol = model.columnForPropID(kpidAttrib);

  const int rows = model.rowCount();
  for (int r = 0; r < rows; r++)
  {
    const bool isDir = model.isFolder(r);
    const UString name = model.itemName(r);
    const UString full = prefix + name;

    bool sizeDef = false;
    const UInt64 sz = model.itemSize(r, sizeDef);
    QString qSize, qMTime, qAttr;
    if (sizeCol >= 0)
      qSize = model.data(model.index(r, sizeCol), Qt::DisplayRole).toString();
    if (mtimeCol >= 0)
      qMTime = model.data(model.index(r, mtimeCol), Qt::DisplayRole).toString();
    if (attrCol >= 0)
      qAttr = model.data(model.index(r, attrCol), Qt::DisplayRole).toString();

    if (isDir)
    {
      dirs++;
      printf("[DIR ] %-40s attr='%s'  mtime='%s'\n",
          OemUtf8(full), qAttr.toUtf8().constData(), qMTime.toUtf8().constData());
    }
    else
    {
      files++;
      if (sizeDef)
        totalSize += sz;
      printf("[FILE] %-40s size=%llu  size_col='%s'  attr='%s'  mtime='%s'\n",
          OemUtf8(full),
          sizeDef ? (unsigned long long)sz : 0ULL,
          qSize.toUtf8().constData(),
          qAttr.toUtf8().constData(),
          qMTime.toUtf8().constData());
    }
  }

  // Recurse into each subfolder via enterItem / goParent.
  for (int r = 0; r < model.rowCount(); r++)
  {
    if (!model.isFolder(r))
      continue;
    const UString name = model.itemName(r);
    if (model.enterItem(r))
    {
      DumpFolder(model, prefix + name + UString(L"/"), files, dirs, totalSize);
      model.goParent();
    }
  }
}

static int RunDump(IFolderFolder *rootFolder)
{
  QtFolderModel model;
  model.setRootFolder(rootFolder);

  printf("---- model-driven FS listing (QtFolderModel over CFSFolder) ----\n");
  printf("root path: %s\n", OemUtf8(model.currentPath()));
  printf("columns:");
  for (int c = 0; c < model.columnCount(); c++)
    printf(" [%s]",
        model.headerData(c, Qt::Horizontal, Qt::DisplayRole).toString().toUtf8().constData());
  printf("\n");

  unsigned files = 0, dirs = 0;
  UInt64 totalSize = 0;
  DumpFolder(model, UString(), files, dirs, totalSize);

  printf("---- summary ----\n");
  printf("files=%u dirs=%u total_size=%llu\n",
      files, dirs, (unsigned long long)totalSize);
  return 0;
}

static int RunNav(IFolderFolder *rootFolder)
{
  QtFolderModel model;
  model.setRootFolder(rootFolder);

  printf("---- navigation transcript (enterItem / goParent) ----\n");
  const int rootRows = model.rowCount();
  printf("root: rowCount=%d  path='%s'\n", rootRows, OemUtf8(model.currentPath()));

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
    printf("  enter '%s' -> rowCount=%d  path='%s'\n",
        OemUtf8(name), model.rowCount(), OemUtf8(model.currentPath()));
  }
  while (model.goParent())
    printf("  goParent -> rowCount=%d  path='%s'\n",
        model.rowCount(), OemUtf8(model.currentPath()));

  const int backRows = model.rowCount();
  printf("back at: rowCount=%d (root was %d) -> %s\n",
      backRows, rootRows, backRows == rootRows ? "RESTORED" : "MISMATCH");
  return backRows == rootRows ? 0 : 1;
}

// --- seamless FS->archive headless proof -------------------------------------
// Opens the FS dir, finds an archive file named `arcName`, proves that swapping
// the SAME model object's root folder to the archive root yields the archive's
// contents, and that restoring the FS folder returns the directory listing.
static int RunSeamless(IFolderFolder *fsRoot, const char *arcName)
{
  QtFolderModel model;
  model.setRootFolder(fsRoot);

  const UString want = GetUnicodeString(arcName);
  int row = -1;
  for (int r = 0; r < model.rowCount(); r++)
    if (!model.isFolder(r) && model.itemName(r) == want) { row = r; break; }
  if (row < 0)
  {
    printf("seamless: archive '%s' not found in FS dir\n", arcName);
    return 1;
  }

  const UString fsPath = model.currentPath();
  const int fsRows = model.rowCount();
  printf("---- seamless FS -> archive proof ----\n");
  printf("FS dir   : path='%s'  rows=%d  (archive row=%d name='%s')\n",
      OemUtf8(fsPath), fsRows, row, arcName);

  const UString full = fsPath + model.itemName(row);
  CMyComPtr<IFolderFolder> archiveRoot;
  CMyComPtr<IUnknown> agentHolder;
  HRESULT res = OpenArchiveAsFolder(full, UString(), false, archiveRoot, agentHolder);
  if (res != S_OK || !archiveRoot)
  {
    printf("seamless: OpenArchiveAsFolder failed (0x%08X)\n", (unsigned)res);
    return 1;
  }

  // Capture the FS folder to return to, then SWAP the SAME model's root.
  CMyComPtr<IFolderFolder> fsReturn = model.currentFolder();
  model.setRootFolder(archiveRoot);
  printf("IN ARCHIVE: path='%s'  rows=%d\n",
      OemUtf8(model.currentPath()), model.rowCount());
  for (int r = 0; r < model.rowCount(); r++)
    printf("   %s %s\n", model.isFolder(r) ? "[DIR ]" : "[FILE]",
        OemUtf8(model.itemName(r)));

  // Now "Up" out of the archive root: goParent returns false at the archive root,
  // so the controller restores the FS folder. Prove the same model reflects FS.
  bool wentUp = model.goParent();
  printf("goParent at archive root returned %s (expected false)\n",
      wentUp ? "true" : "false");
  model.setRootFolder(fsReturn);
  const int backRows = model.rowCount();
  printf("BACK IN FS: path='%s'  rows=%d -> %s\n",
      OemUtf8(model.currentPath()), backRows,
      backRows == fsRows ? "RESTORED" : "MISMATCH");
  return backRows == fsRows ? 0 : 1;
}

int main(int argc, char **argv)
{
  const char *target = nullptr;
  const char *seamlessArc = nullptr;
  UString password;
  bool passwordDefined = false;
  bool dump = false, nav = false;

  for (int i = 1; i < argc; i++)
  {
    const char *a = argv[i];
    if (strcmp(a, "--dump") == 0) dump = true;
    else if (strcmp(a, "--nav") == 0) nav = true;
    else if (strncmp(a, "--seamless=", 11) == 0) seamlessArc = a + 11;
    else if (strncmp(a, "-p", 2) == 0) { password = GetUnicodeString(a + 2); passwordDefined = true; }
    else if (!target) target = a;
  }

  if (const char *env = getenv("SEVENZQT_BROWSE_DUMP"))
    if (env[0] && env[0] != '0')
      dump = true;

  // Resolve the target. Default = current working directory.
  FString targetPath;
  if (target)
    targetPath = us2fs(GetUnicodeString(target));
  else
    NFile::NDir::GetCurrentDir(targetPath);

  // Decide FS-folder vs archive: a directory -> CFSFolder; otherwise try archive.
  NFile::NFind::CFileInfo fi;
  bool isDir = false;
  if (fi.Find(targetPath))
    isDir = fi.IsDir();

  CMyComPtr<IFolderFolder> rootFolder;
  CMyComPtr<IUnknown> agentHolder; // keep CAgent alive if we open an archive
  bool isFsRoot = false;

  if (isDir)
  {
    HRESULT res = BindFsFolder(targetPath, rootFolder);
    if (res != S_OK || !rootFolder)
    {
      fprintf(stderr, "cannot bind FS folder: %s\n", OemUtf8(fs2us(targetPath)));
      return 1;
    }
    isFsRoot = true;
  }
  else
  {
    // Not a directory: try opening as an archive.
    const UString arcPath = fs2us(targetPath);
    HRESULT res = OpenArchiveAsFolder(arcPath, password, passwordDefined,
        rootFolder, agentHolder);
    if (res != S_OK || !rootFolder)
    {
      fprintf(stderr, "not a directory and not an openable archive: %s (0x%08X)\n",
          OemUtf8(arcPath), (unsigned)res);
      return 1;
    }
    isFsRoot = false;
  }

  if (dump || nav || seamlessArc)
  {
    if (!getenv("QT_QPA_PLATFORM"))
      qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    int rc = 0;
    if (dump)        rc |= RunDump(rootFolder);
    if (nav)         rc |= RunNav(rootFolder);
    if (seamlessArc) rc |= RunSeamless(rootFolder, seamlessArc);
    return rc;
  }

  QApplication app(argc, argv);
  QtFsBrowser browser(rootFolder, isFsRoot,
      QString::fromUtf8(target ? target : "."));
  browser.show();
  return app.exec();
}
