// QtFsDnd.cpp
// ----------------------------------------------------------------------------
// See QtFsDnd.h.
// ----------------------------------------------------------------------------

#include "QtFsDnd.h"

#include <QtCore/QMimeData>
#include <QtCore/QUrl>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QByteArray>   // G.6d : right-button-drag marker payload

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "../../../../Common/StringConvert.h"
#include "../../../../Common/MyCom.h"

#include "../../../../Windows/FileName.h"
#include "../../../../Windows/FileFind.h"
#include "../../../../Windows/PropVariant.h"   // NCOM::CPropVariant

#include "../../../PropID.h"               // kpidName
#include "../../FileManager/FSFolder.h"     // NFsFolder::CFSFolder
#include "../../FileManager/IFolder.h"      // IFolderFolder / IFolderOperations

#include "../QtExtractPrompts.h"            // QtOverwritePrompt
#include "QtFsOperations.h"                 // QtFsCopyWorker (B.4a — reused)

using namespace NWindows;

namespace QtFsDnd {

// ---------------------------------------------------------------------------
// small string helpers (same shape as QtPanel.cpp's)
static QString UStr_toQ(const UString &s)
{
  return QString::fromWCharArray(s.Ptr(), (int)s.Len());
}

static UString Q_toU(const QString &s)
{
  const std::wstring w = s.toStdWString();
  return UString(w.c_str());
}

// Split an absolute path into (parent-with-trailing-sep, leaf-name).
static void SplitPath(const UString &full, UString &parent, UString &leaf)
{
  UString p = full;
  // strip any trailing separators so a folder path resolves to its own leaf
  while (p.Len() > 1 && (p.Back() == L'/' || p.Back() == L'\\'))
    p.DeleteBack();
  const int slash = p.ReverseFind_PathSepar();
  if (slash < 0)
  {
    parent.Empty();
    leaf = p;
    return;
  }
  leaf = p.Ptr((unsigned)(slash + 1));
  parent = p;
  parent.DeleteFrom((unsigned)(slash + 1)); // keep the trailing separator
}

// ---------------------------------------------------------------------------
// mime build / parse

QMimeData *MakeUriListMime(const UStringVector &paths)
{
  if (paths.Size() == 0)
    return nullptr;
  QList<QUrl> urls;
  urls.reserve((int)paths.Size());
  FOR_VECTOR (i, paths)
    urls.push_back(QUrl::fromLocalFile(UStr_toQ(paths[i])));
  QMimeData *mime = new QMimeData();
  mime->setUrls(urls);   // sets text/uri-list with file:// URIs
  return mime;
}

// G.6d : a private, app-internal MIME type carrying the right-button-drag flag.
// It travels with the QDrag inside this process (same-process drop, exactly as the
// Win32 GetTransfer format only ever round-trips between 7-Zip windows); a foreign
// app's drop never carries it, so it stays inert there — matching the original,
// where only a 7-Zip source sets k_SourceFlags_RightButton.
static const char * const kRightButtonDragMime = "application/x-7zqt-rbutton-drag";

void MarkRightButtonDrag(QMimeData *mime)
{
  if (mime)
    mime->setData(QString::fromLatin1(kRightButtonDragMime), QByteArray("1"));
}

bool IsRightButtonDrag(const QMimeData *mime)
{
  return mime && mime->hasFormat(QString::fromLatin1(kRightButtonDragMime));
}

unsigned UriListToPaths(const QMimeData *mime, UStringVector &out)
{
  unsigned n = 0;
  if (!mime || !mime->hasUrls())
    return 0;
  const QList<QUrl> urls = mime->urls();
  for (const QUrl &u : urls)
  {
    if (!u.isLocalFile())
      continue;
    const QString local = u.toLocalFile();
    if (local.isEmpty())
      continue;
    out.Add(Q_toU(local));
    n++;
  }
  return n;
}

// ---------------------------------------------------------------------------
// move-vs-copy decision (PanelDrag IsItSameDrive / GetEffect analogue)

bool IsSameVolume(const UStringVector &srcPaths, const UString &destDir)
{
#ifdef _WIN32
  // PanelDrag's IsItSameDrive: same drive-letter / UNC prefix (case-insensitive).
  UString drive = destDir;
  // crude: compare the first 2 chars ("C:") for a drive-letter path.
  if (drive.Len() < 2)
    return false;
  if (srcPaths.Size() == 0)
    return false;
  FOR_VECTOR (i, srcPaths)
    if (!srcPaths[i].IsPrefixedBy_NoCase(UString(drive.Ptr(), 2)))
      return false;
  return true;
#else
  // Linux: there is no drive letter; approximate "same volume" with st_dev.
  if (srcPaths.Size() == 0)
    return false;
  struct stat dst;
  if (stat((const char *)GetAnsiString(destDir), &dst) != 0)
    return false;
  FOR_VECTOR (i, srcPaths)
  {
    UString parent, leaf;
    SplitPath(srcPaths[i], parent, leaf);
    if (parent.IsEmpty())
      return false;
    struct stat ss;
    if (stat((const char *)GetAnsiString(parent), &ss) != 0)
      return false;
    if (ss.st_dev != dst.st_dev)
      return false;
  }
  return true;
#endif
}

bool MapDropAction(Qt::DropAction action,
    const UStringVector &srcPaths, const UString &destDir)
{
  // Qt already folded the modifiers (Ctrl/Shift) and the source's
  // supportedActions into `action`, exactly mirroring GetEffect_ForKeys:
  //   Ctrl  -> Qt::CopyAction, Shift -> Qt::MoveAction.
  if (action == Qt::MoveAction)
    return true;   // move
  if (action == Qt::CopyAction)
    return false;  // copy
  // Ambiguous / default (no modifier): apply PanelDrag's GetEffect default —
  // MOVE within the same volume, COPY across volumes.
  return IsSameVolume(srcPaths, destDir);
}

// ---------------------------------------------------------------------------
// the EXTERNAL-style dispatch: group by parent dir, bind a CFSFolder per group,
// resolve indices by name, run the B.4a QtFsCopyWorker.

// Bind a CFSFolder at `dirPath` (trailing-sep normalized).
static bool BindFsFolder(const UString &dirPath, CMyComPtr<IFolderFolder> &folder)
{
  FString path = us2fs(dirPath);
  NFile::NName::NormalizeDirPathPrefix(path);
  NFsFolder::CFSFolder *fsSpec = new NFsFolder::CFSFolder;
  CMyComPtr<IFolderFolder> f = fsSpec;
  if (fsSpec->Init(path) != S_OK)
    return false;
  folder = f;
  return true;
}

// Find the item index whose kpidName equals `leaf` in `folder`.
static bool FindIndexByName(IFolderFolder *folder, const UString &leaf, UInt32 &index)
{
  // CFSFolder lists lazily: LoadItems() must run before GetNumberOfItems /
  // GetProperty return the directory contents (same as CPanel::RefreshListCtrl).
  folder->LoadItems();
  UInt32 num = 0;
  if (folder->GetNumberOfItems(&num) != S_OK)
    return false;
  for (UInt32 i = 0; i < num; i++)
  {
    NWindows::NCOM::CPropVariant prop;
    if (folder->GetProperty(i, kpidName, &prop) != S_OK)
      continue;
    if (prop.vt != VT_BSTR || !prop.bstrVal)
      continue;
    if (leaf == (const wchar_t *)prop.bstrVal)
    {
      index = i;
      return true;
    }
  }
  return false;
}

DropResult CopyPathsInto(const UStringVector &srcPaths, const UString &destDir,
    bool moveMode, QtOverwritePrompt *prompt, QWidget *parent, bool headless)
{
  DropResult r;
  r.MoveMode = moveMode;
  if (srcPaths.Size() == 0 || destDir.IsEmpty())
    return r;

  UString dest = destDir;
  NFile::NName::NormalizeDirPathPrefix(dest);

  // Group the source paths by parent directory (preserving order of first sight).
  UStringVector parents;
  CObjectVector<UStringVector> leavesByParent;
  FOR_VECTOR (i, srcPaths)
  {
    UString par, leaf;
    SplitPath(srcPaths[i], par, leaf);
    if (par.IsEmpty() || leaf.IsEmpty())
      continue;
    int g = -1;
    FOR_VECTOR (k, parents)
      if (parents[k] == par) { g = (int)k; break; }
    if (g < 0)
    {
      parents.Add(par);
      leavesByParent.AddNew();
      g = (int)parents.Size() - 1;
    }
    leavesByParent[(unsigned)g].Add(leaf);
  }

  bool allOk = true;
  FOR_VECTOR (g, parents)
  {
    CMyComPtr<IFolderFolder> folder;
    if (!BindFsFolder(parents[g], folder))
    {
      allOk = false;
      // G.6e : the source parent could not be bound — a reportable failure.
      if (r.LastError == S_OK) r.LastError = E_FAIL;
      continue;
    }
    CMyComPtr<IFolderOperations> ops;
    folder->QueryInterface(IID_IFolderOperations, (void **)&ops);
    if (!ops)
    {
      allOk = false;
      if (r.LastError == S_OK) r.LastError = E_NOINTERFACE;
      continue;
    }

    CRecordVector<UInt32> indices;
    const UStringVector &leaves = leavesByParent[g];
    FOR_VECTOR (j, leaves)
    {
      UInt32 idx;
      if (FindIndexByName(folder, leaves[j], idx))
        indices.Add(idx);
      else
      {
        allOk = false;
        // G.6e : a dropped source name does not exist under its parent — surface a
        // reportable failure so the drop is not silent (PanelDrag MessageBox_Error).
        // E_FAIL keeps this portable (the Win32 ERROR_FILE_NOT_FOUND macro is not in
        // the cross-platform headers); HResultToMessage decodes it for the dialog.
        if (r.LastError == S_OK)
          r.LastError = E_FAIL;
      }
    }
    if (indices.Size() == 0)
      continue;

    QtFsCopyWorker worker;
    worker.FolderOperations = ops;
    worker.DestPath = dest;
    worker.MoveMode = moveMode;
    worker.OverwritePrompt = prompt;
    worker.DisableUserQuestions = headless;
    worker.Indices = indices;

    const UString title = moveMode ? UString(L"Moving") : UString(L"Copying");
    const HRESULT hres = worker.Create(title, parent);
    if (hres != S_OK)
    {
      allOk = false;
      // G.6e : remember the first failing HRESULT so the caller can MessageBox_Error
      // it (while staying silent on E_ABORT/user cancel — PanelDrag.cpp:1779-1782).
      if (r.LastError == S_OK)
        r.LastError = hres;
    }
    r.NumGroups++;
    r.NumItems += indices.Size();
  }

  r.Ok = allOk;
  return r;
}

} // namespace QtFsDnd
