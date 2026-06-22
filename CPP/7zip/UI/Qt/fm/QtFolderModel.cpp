// QtFolderModel.cpp
// ----------------------------------------------------------------------------
// See QtFolderModel.h. This file is the faithful Qt translation of CPanel's
// folder-binding / column-setup / cell-text logic.
// ----------------------------------------------------------------------------

#include "QtFolderModel.h"

#include "FormatIcons.h"   // B.6 : ext -> stock per-format archive icon (fallback)

#include <string>                  // G.4l : std::wstring (QString -> UString, setData)

#include <QtCore/QMimeDatabase>    // per-type file icons: the Linux SHGetFileInfo analog
#include <QtCore/QMimeType>

#include <QtGui/QFont>
#include <QtGui/QGuiApplication>   // P.3 : QGuiApplication::palette() (no QtWidgets dep)
#include <QtGui/QPalette>

#include "../../../../Common/StringConvert.h"
#include "../../../../Windows/Defs.h"          // VARIANT_BOOLToBool
#include "../../../../Windows/PropVariant.h"
#include "../../../../Windows/PropVariantConv.h"

#include "../../../PropID.h"
#include "../../Common/PropIDUtils.h"

using namespace NWindows;

// ---------------------------------------------------------------------------
// Small helpers copied VERBATIM (semantics) from the FileManager TUs that are
// Win32-GUI-coupled and therefore not linkable on Linux. These are pure,
// portable logic; copying the function is the same approach already used for
// CompareFileNames_ForFolderList (see agent/FmCompareCompat.cpp).
// ---------------------------------------------------------------------------

// Verbatim semantics of CPanel's IsSizeProp (PanelListNotify.cpp:96): which
// PROPIDs are "size-like" (displayed grouped, right-aligned).
static bool QtFm_IsSizeProp(UINT propID)
{
  switch (propID)
  {
    case kpidSize:
    case kpidPackSize:
    case kpidNumSubDirs:
    case kpidNumSubFiles:
    case kpidOffset:
    case kpidLinks:
    case kpidNumBlocks:
    case kpidNumVolumes:
    case kpidPhySize:
    case kpidHeadersSize:
    case kpidTotalSize:
    case kpidFreeSpace:
    case kpidClusterSize:
    case kpidNumErrors:
    case kpidNumStreams:
    case kpidNumAltStreams:
    case kpidAltStreamsSize:
    case kpidVirtualSize:
    case kpidUnpackSize:
    case kpidTotalPhySize:
    case kpidTailSize:
    case kpidEmbeddedStubSize:
      return true;
  }
  return false;
}

// Verbatim from CPanel's static ConvertSizeToString (PanelListNotify.cpp:38):
// groups digits in threes with a space separator (e.g. 1234567 -> "1 234 567").
static void QtFm_ConvertSizeToString(UInt64 val, wchar_t *s)
{
  Byte temp[32];
  unsigned i = 0;

  if (val <= (UInt32)0xFFFFFFFF)
  {
    UInt32 val32 = (UInt32)val;
    do { temp[i++] = (Byte)('0' + (unsigned)(val32 % 10)); val32 /= 10; } while (val32 != 0);
  }
  else
  {
    do { temp[i++] = (Byte)('0' + (unsigned)(val % 10)); val /= 10; } while (val != 0);
  }

  if (i < 3)
  {
    if (i != 0)
    {
      *s++ = temp[(size_t)i - 1];
      if (i == 2)
        *s++ = temp[0];
    }
    *s = 0;
    return;
  }

  unsigned r = i % 3;
  if (r != 0)
  {
    s[0] = temp[--i];
    if (r == 2)
      s[1] = temp[--i];
    s += r;
  }

  do
  {
    s[0] = ' ';
    s[1] = temp[(size_t)i - 1];
    s[2] = temp[(size_t)i - 2];
    s[3] = temp[(size_t)i - 3];
    s += 4;
  }
  while (i -= 3);

  *s = 0;
}

// Canonical English PROPID->name table, copied VERBATIM from the engine's
// Console List.cpp (static kPropIdToName + GetPropName). The File Manager
// resolves names via GetNameOfProperty -> LangString(1000+propID) (a Win32
// resource string table not present in this engine-only build); for standard
// PROPIDs that resolves to exactly these English names, so mirroring the
// console table gives the same headers ("Name", "Size", "Modified", ...).
static const char * const kPropIdToName[] =
{
    "0", "1", "2", "Path", "Name", "Extension", "Folder", "Size",
    "Packed Size", "Attributes", "Created", "Accessed", "Modified", "Solid",
    "Commented", "Encrypted", "Split Before", "Split After", "Dictionary Size",
    "CRC", "Type", "Anti", "Method", "Host OS", "File System", "User", "Group",
    "Block", "Comment", "Position", "Path Prefix", "Folders", "Files", "Version",
    "Volume", "Multivolume", "Offset", "Links", "Blocks", "Volumes", "Time Type",
    "64-bit", "Big-endian", "CPU", "Physical Size", "Headers Size", "Checksum",
    "Characteristics", "Virtual Address", "ID", "Short Name",
    "Creator Application", "Sector Size", "Mode", "Symbolic Link", "Error",
    "Total Size", "Free Space", "Cluster Size", "Label", "Local Name",
    "Provider", "NT Security", "Alternate Stream", "Aux", "Deleted", "Tree",
    "SHA-1", "SHA-256", "Error Type", "Errors", "Errors", "Warnings", "Warning",
    "Streams", "Alternate Streams", "Alternate Streams Size", "Virtual Size",
    "Unpack Size", "Total Physical Size", "Volume Index", "SubType",
    "Short Comment", "Code Page", "Is not archive type",
    "Physical Size can't be detected", "Zeros Tail Is Allowed", "Tail Size",
    "Embedded Stub Size", "Link", "Hard Link", "iNode", "Stream ID",
    "Read-only", "Out Name", "Copy Link", "ArcFileName", "IsHash",
    "Metadata Changed", "User ID", "Group ID", "Device Major", "Device Minor",
    "Dev Major", "Dev Minor"
};

// Mirrors the engine's GetPropName (List.cpp): the canonical name when the
// PROPID is in range, else the BSTR name, else the numeric PROPID.
static QString QtFm_PropName(PROPID propID, const CMyComBSTR &name)
{
  if (propID < (PROPID)(sizeof(kPropIdToName) / sizeof(kPropIdToName[0])))
    return QString::fromLatin1(kPropIdToName[propID]);
  if (name && name[0] != 0)
    return QString::fromWCharArray((const wchar_t *)name);
  return QString::number((uint)propID);
}

// G.4b : VERBATIM port of CPanel's static GetColumnVisible(propID, isFsFolder)
// (PanelItems.cpp:25). For a FILESYSTEM folder (FSFolder / AltStreamsFolder) the
// trimmed default column set hides ATime/ChangeTime/Attrib/PackSize/INode/Links/
// NtReparse; for any other folder (an archive) every property is visible by
// default. This is exactly the rule InitColumns seeds CPropColumn::IsVisible with.
static bool QtFm_GetColumnVisible(PROPID propID, bool isFsFolder)
{
  if (isFsFolder)
  {
    switch (propID)
    {
      case kpidATime:
      case kpidChangeTime:
      case kpidAttrib:
      case kpidPackSize:
      case kpidINode:
      case kpidLinks:
      case kpidNtReparse:
        return false;
      default:
        break;
    }
  }
  return true;
}

// Mirrors CPanel's GetColumnAlign (PanelItems.cpp:53): time props are
// left-aligned; numeric VARTYPEs are right-aligned; the rest left-aligned.
static bool QtFm_RightAlign(PROPID propID, VARTYPE varType)
{
  switch (propID)
  {
    case kpidCTime:
    case kpidATime:
    case kpidMTime:
    case kpidChangeTime:
      return false; // LVCFMT_LEFT
  }
  switch (varType)
  {
    case VT_UI1:
    case VT_I2:
    case VT_UI2:
    case VT_I4:
    case VT_INT:
    case VT_UI4:
    case VT_UINT:
    case VT_I8:
    case VT_UI8:
    case VT_BOOL:
      return true; // LVCFMT_RIGHT
    default:
      return false;
  }
}

static QString UStringToQ(const UString &s)
{
  // UString is UTF-16/UTF-32 wchar_t; QString::fromWCharArray handles both.
  return QString::fromWCharArray(s.Ptr(), (int)s.Len());
}

static bool Prop_GetBool(IFolderFolder *folder, UInt32 index, PROPID propID)
{
  NCOM::CPropVariant prop;
  if (folder->GetProperty(index, propID, &prop) != S_OK)
    return false;
  if (prop.vt == VT_BOOL)
    return VARIANT_BOOLToBool(prop.boolVal);
  return false;
}

// G.4l : VERBATIM port of CPanel's static IsCorrectFsName (PanelOperations.cpp:274):
// the typed name's LAST path part may not be "." or "..". (CPanel takes the last
// part via ReverseFind_PathSepar()+1 so a typed sub-path's leaf is what's checked;
// for a plain inline rename the whole string IS the last part.) Pure logic, copied
// the same way CompareFileNames_ForFolderList was.
static bool QtFm_IsCorrectFsName(const UString &name)
{
  const UString lastPart = name.Ptr((unsigned)(name.ReverseFind_PathSepar() + 1));
  return
      !lastPart.IsEqualTo(".") &&
      !lastPart.IsEqualTo("..");
}

// G.5c : the shared leaf-name validity rule (declared in the header). IsCorrectFsName
// rejects a "."/".." leaf; the added guard rejects ANY embedded path separator (a
// create/rename leaf must not be a sub-path — the B.5a fail-safe is E_INVALIDARG, and
// CorrectFsPath is the identity on Linux). renameRow() and the FM create-folder/file
// paths both route through this so the validation is written once.
bool QtFolderModel::IsCorrectNewFsName(const UString &name)
{
  if (!QtFm_IsCorrectFsName(name))
    return false;
  for (unsigned i = 0; i < name.Len(); i++)
    if (IsPathSepar(name[i]))
      return false;
  return true;
}

// ---------------------------------------------------------------------------

QtFolderModel::QtFolderModel(QObject *parent)
  : QAbstractItemModel(parent)
  , _numItems(0)
{
  // QFileIconProvider-style theme icons; folders get a folder icon, files a
  // generic file icon. Refine later (per-extension icons via the engine's
  // IFolderGetSystemIconIndex, as CPanel does).
  _dirIcon = QIcon::fromTheme(QStringLiteral("folder"));
  _fileIcon = QIcon::fromTheme(QStringLiteral("text-x-generic"));
  // G.4f : the ".." parent row's icon. CPanel uses the DIR icon for ".."
  // (GetIconIndex_DIR, PanelItems.cpp:640); we prefer a dedicated up/parent
  // glyph if the theme has one, falling back to the folder icon (faithful).
  _parentIcon = QIcon::fromTheme(QStringLiteral("go-up"));
  if (_parentIcon.isNull())
    _parentIcon = QIcon::fromTheme(QStringLiteral("folder-up"));
  if (_parentIcon.isNull())
    _parentIcon = _dirIcon;
}

QtFolderModel::~QtFolderModel() = default;

void QtFolderModel::setRootFolder(IFolderFolder *rootFolder)
{
  beginResetModel();
  _suspended = false;   // a (re)bind always re-lists; clears any in-place-update suspend
  _folder = rootFolder;
  rebuildColumns();
  reloadItems();
  endResetModel();
  // G.4b : the column set / default visibility was rebuilt — let the panel re-apply
  // the hidden sections to the (reset) header.
  Q_EMIT columnVisibilityChanged();
}

// === G.4f : ShowDots ".." pseudo-row + centralized row<->realIndex mapping =====

// The ONE pair every consumer routes through. With ".." shown (showDotsActive()),
// MODEL row 0 is the synthetic parent (-> kParentRow) and MODEL row R (R>=1) maps to
// engine realIndex R-1. With ".." hidden the mapping is the identity.
int QtFolderModel::modelRowToRealIndex(int row) const
{
  if (showDotsActive())
  {
    if (row == 0)
      return kParentRow;     // the ".." pseudo-row: no engine item
    return row - 1;
  }
  return row;
}

int QtFolderModel::realIndexToModelRow(int realIndex) const
{
  if (showDotsActive())
    return realIndex + 1;
  return realIndex;
}

bool QtFolderModel::isParentRow(int row) const
{
  return showDotsActive() && row == 0;
}

// Record the Options ShowDots flag and re-list if the EFFECTIVE state changes (the
// ".." row appears/disappears). Mirrors CPanel re-listing when _showDots toggles.
void QtFolderModel::setShowDots(bool on)
{
  if (_showDots == on)
    return;
  const bool was = showDotsActive();
  beginResetModel();
  _showDots = on;
  endResetModel();
  // If the effective state did not actually change (no parent anyway), the reset
  // above was a harmless no-op; when it DID change, the row count shifted by 1.
  (void)was;
}

// The panel tells the model whether a parent exists at the current folder (Up is
// possible). Re-lists when the EFFECTIVE ShowDots state flips.
void QtFolderModel::setHasParent(bool on)
{
  if (_hasParent == on)
    return;
  beginResetModel();
  _hasParent = on;
  endResetModel();
}

// B.5b crash fix : freeze the view (0 rows, no per-item GetProperty) while an
// in-place archive update rebinds the CAgentFolder on the worker thread. The
// reset makes the QTreeView drop its rows immediately; the following refresh()
// -> setRootFolder() lifts the suspend and re-lists the reopened archive.
void QtFolderModel::setSuspended(bool on)
{
  if (_suspended == on)
    return;
  beginResetModel();
  _suspended = on;
  endResetModel();
}

// G.9d : ShowRealFileIcons live toggle. Repaint the Name column's decoration for
// every row by emitting dataChanged over column 0 (DecorationRole), so an Options
// change takes effect without a full reset/re-list.
void QtFolderModel::setShowRealIcons(bool on)
{
  if (_showRealIcons == on)
    return;
  _showRealIcons = on;
  const int rows = rowCount();
  const int nameCol = columnForPropID(kpidName);
  if (rows > 0 && nameCol >= 0)
    emit dataChanged(index(0, nameCol), index(rows - 1, nameCol),
        QVector<int>() << Qt::DecorationRole);
}

// G.4c : push _flatMode onto the bound folder via IFolderSetFlatMode (a no-op if
// the folder type doesn't expose it). Called BEFORE rebuildColumns() enumerates
// properties (an archive folder reports the extra kpidPrefix property only while
// flat — CAgentFolder::GetNumberOfProperties:1152-1153) and again in reloadItems()
// before LoadItems(), mirroring CPanel::RefreshListCtrl applying SetFlatMode ahead
// of both InitColumns and LoadItems (PanelItems.cpp:526-547).
void QtFolderModel::applyFlatModeToFolder()
{
  if (!_folder)
    return;
  CMyComPtr<IFolderSetFlatMode> setFlat;
  if (_folder->QueryInterface(IID_IFolderSetFlatMode, (void **)&setFlat) == S_OK
      && setFlat)
    setFlat->SetFlatMode(_flatMode ? 1 : 0);
}

// CPanel::InitColumns : enumerate GetNumberOfProperties / GetPropertyInfo,
// skip kpidIsDir, build the visible-column list with kpidName forced first.
void QtFolderModel::rebuildColumns()
{
  _columns.clear();
  if (!_folder)
    return;

  // G.4c : apply flat mode before enumerating, so a flat archive folder exposes
  // its kpidPrefix column (the path-prefix shown in flat mode).
  applyFlatModeToFolder();

  UInt32 numProps = 0;
  if (_folder->GetNumberOfProperties(&numProps) != S_OK)
    return;

  // G.4b : the FS-vs-archive gate the GetColumnVisible default-hide rule keys on
  // (CPanel: isFsFolder = IsFSFolder() || IsAltStreamsFolder()). Read once here.
  const bool fsFolder = isFsFolder();

  for (UInt32 i = 0; i < numProps; i++)
  {
    CMyComBSTR name;
    PROPID propID = 0;
    VARTYPE varType = 0;
    if (_folder->GetPropertyInfo(i, &name, &propID, &varType) != S_OK)
      continue; // CPanel ignores a failing field rather than erroring
    if (propID == kpidIsDir)
      continue; // CPanel never shows the dir flag as a column

    CColumn col;
    col.ID = propID;
    col.Type = varType;
    // CPanel uses GetNameOfProperty(propID, name); for standard PROPIDs that
    // resolves (via the FM string table) to the same English names as the
    // engine's console GetPropName, which we mirror here.
    col.Name = QtFm_PropName(propID, name);
    col.RightAlign = QtFm_RightAlign(propID, varType);
    // G.4b : CPropColumn::IsVisible = GetColumnVisible(propID, isFsFolder),
    // then OVERRIDDEN by any user/persistence toggle for this PROPID. kpidName
    // is always visible (the locked main column).
    // G.4c : in flat mode the kpidPrefix (Path Prefix) column carries each item's
    // subfolder path — the whole point of Flat View — so force it visible (CPanel
    // shows it as a normal column once the flat folder reports it; we don't let a
    // stale hidden-override suppress it). It only exists as a column while flat.
    if (propID == kpidName)
      col.Visible = true;
    else if (_flatMode && propID == kpidPrefix)
      col.Visible = true;
    else if (_savedVisible.contains(propID))
      col.Visible = _savedVisible.value(propID);
    else
      col.Visible = QtFm_GetColumnVisible(propID, fsFolder);
    _columns.push_back(col);
  }

  // CPanel keeps kpidName as the first (main) column. Move it to front.
  for (int i = 0; i < _columns.size(); i++)
  {
    if (_columns[i].ID == kpidName)
    {
      if (i != 0)
        _columns.move(i, 0);
      break;
    }
  }
}

void QtFolderModel::reloadItems()
{
  _numItems = 0;
  if (!_folder)
    return;
  // G.4c : CPanel::RefreshListCtrl applies the flat-mode flag to the folder via
  // IFolderSetFlatMode::SetFlatMode(_flatMode) BEFORE LoadItems (PanelItems.cpp:
  // 526-547), so the subsequent LoadItems enumerates flat (all descendants) or
  // direct-children per the flag. Re-applied here (idempotent with rebuildColumns)
  // so the flag survives a folder change / refresh, exactly as RefreshListCtrl does.
  applyFlatModeToFolder();
  // CPanel::RefreshListCtrl calls LoadItems() once after binding, then
  // GetNumberOfItems.
  _folder->LoadItems();
  UInt32 n = 0;
  if (_folder->GetNumberOfItems(&n) == S_OK)
    _numItems = (int)n;
}

// --- QAbstractItemModel (flat / table-like) --------------------------------

QModelIndex QtFolderModel::index(int row, int column, const QModelIndex &parent) const
{
  if (parent.isValid())
    return QModelIndex();
  // G.4f : the valid MODEL-row range includes the synthetic ".." row when shown.
  const int rows = _suspended ? 0 : (_numItems + (showDotsActive() ? 1 : 0));
  if (row < 0 || row >= rows || column < 0 || column >= _columns.size())
    return QModelIndex();
  return createIndex(row, column);
}

QModelIndex QtFolderModel::parent(const QModelIndex & /*child*/) const
{
  return QModelIndex(); // flat: no nesting
}

int QtFolderModel::rowCount(const QModelIndex &parent) const
{
  if (parent.isValid())
    return 0;
  if (_suspended)   // B.5b crash fix : no rows -> no GetProperty during an in-place update
    return 0;
  // G.4f : +1 for the synthetic ".." row when ShowDots is effective.
  return _numItems + (showDotsActive() ? 1 : 0);
}

int QtFolderModel::columnCount(const QModelIndex &parent) const
{
  if (parent.isValid())
    return 0;
  return _columns.size();
}

QVariant QtFolderModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal)
    return QVariant();
  if (section < 0 || section >= _columns.size())
    return QVariant();
  if (role == Qt::DisplayRole)
    return _columns[section].Name;            // GetPropertyInfo's BSTR name
  if (role == Qt::TextAlignmentRole)
    return _columns[section].RightAlign
        ? int(Qt::AlignRight | Qt::AlignVCenter)
        : int(Qt::AlignLeft | Qt::AlignVCenter);
  return QVariant();
}

QVariant QtFolderModel::data(const QModelIndex &idx, int role) const
{
  // B.5b crash fix : never touch the folder (GetProperty) while an in-place
  // archive update is rebinding it on the worker thread.
  if (_suspended || !idx.isValid() || !_folder)
    return QVariant();
  const int row = idx.row();
  const int col = idx.column();
  if (col < 0 || col >= _columns.size())
    return QVariant();

  const CColumn &column = _columns[col];
  const PROPID propID = column.ID;

  // G.4f : the synthetic ".." parent pseudo-row. Mirrors CPanel's kParentIndex item:
  // Name="..", the DIR/up icon, every other column empty. It has no engine realIndex,
  // so it never calls GetProperty.
  if (isParentRow(row))
  {
    if (role == Qt::DisplayRole)
      return (propID == kpidName) ? QStringLiteral("..") : QString();
    if (role == Qt::DecorationRole && propID == kpidName)
      return _parentIcon;
    if (role == Qt::TextAlignmentRole)
      return column.RightAlign
          ? int(Qt::AlignRight | Qt::AlignVCenter)
          : int(Qt::AlignLeft | Qt::AlignVCenter);
    return QVariant();   // no ForegroundRole (never a deleted item)
  }

  // G.4f : map the MODEL row to the engine realIndex for every real-item path.
  const int realIndex = modelRowToRealIndex(row);
  if (realIndex < 0 || realIndex >= _numItems)
    return QVariant();

  // --- text (mirrors CPanel::SetItemText, PanelListNotify.cpp) -----------
  if (role == Qt::DisplayRole)
  {
    NCOM::CPropVariant prop;
    if (_folder->GetProperty((UInt32)realIndex, propID, &prop) != S_OK)
      return QStringLiteral("Error");

    // Size-like props with an integer value: grouped via ConvertSizeToString
    // (exactly what SetItemText does for IsSizeProp).
    if ((prop.vt == VT_UI8 || prop.vt == VT_UI4 || prop.vt == VT_UI2)
        && QtFm_IsSizeProp(propID))
    {
      UInt64 v = 0;
      ConvertPropVariantToUInt64(prop, v);
      wchar_t buf[64];
      QtFm_ConvertSizeToString(v, buf);
      return QString::fromWCharArray(buf);
    }

    if (prop.vt == VT_EMPTY)
      return QString();

    // Everything else: the engine's canonical PROPVARIANT->UString formatter
    // (sizes/dates/attribs), reused verbatim so formatting matches 7-Zip.
    UString s;
    ConvertPropertyToString2(s, prop, propID);
    return UStringToQ(s);
  }

  // --- folder/file icon on the Name column (DecorationRole) --------------
  if (role == Qt::DecorationRole && propID == kpidName)
  {
    if (isFolder(row))
      return _dirIcon;
    // Per-type icon from the MIME database + icon theme (the Linux SHGetFileInfo
    // analog): distinct icons for pdf/jpg/txt/zip/7z/... like Dolphin. The bundled
    // 7-Zip archive .ico (B.6) is a fallback inside fileIconForRow().
    //
    // NOT gated on ShowRealFileIcons. The original only skips the system-icon query
    // for SLOW (network) folders when the flag is off (PanelItems.cpp:587):
    //   if (!Is_Slow_Icon_Folder() || _showRealFileIcons) <query IFolderGetSystemIconIndex>
    // For a normal local FS folder or an archive folder (never a "slow icon folder")
    // the per-type icon ALWAYS shows, in BOTH flag states — indeed ShowRealFileIcons
    // defaults to false upstream (RegistryUtils.cpp:137) yet local files still show
    // their type icons. The port has no slow/network folders, so the flag is inert
    // for icons here and the earlier "OFF -> generic" (G.9d) gate was unfaithful.
    return fileIconForRow(row);
  }

  // --- deleted-in-archive item text color (ForegroundRole) ---------------
  // Faithful port of the ONLY triggerable branch of CPanel::OnCustomDraw
  // (PanelListNotify.cpp:724-728): when _markDeletedItems && _thereAreDeletedItems
  // && IsItem_Deleted(realIndex) it sets clrText = RGB(255,0,0). IsItem_Deleted
  // reads kpidIsDeleted (PanelItems.cpp:1254-1259) — VT_BOOL->bool, VT_EMPTY->false,
  // exactly what Prop_GetBool does here. _markDeletedItems defaults on and is never
  // toggled in the original, so we gate on our equivalent member (default true).
  //
  // The original's whole-archive _thereAreDeletedItems guard (PanelItems.cpp:598)
  // is only an optimization: the per-row VT_EMPTY->false path is self-guarding, so
  // we rely on it. Only archive (FS-image) folders ever expose kpidIsDeleted=true;
  // normal 7z/zip rows, folders, and CFSFolder rows all return false here, so they
  // yield NO ForegroundRole -> QVariant() falls through and QPalette::Text drives
  // the color -> dark-theme safe. deletedTextColor() itself is palette-aware.
  //
  // The OTHER OnCustomDraw color — the _mySelectMode pink mark-bg
  // (PanelListNotify.cpp:718-722, RGB(255,192,192)) — is FAITHFULLY OMITTED: the
  // port uses native Qt ExtendedSelection and has no _selectedStatusVector /
  // _mySelectMode mark-set, so that branch has no trigger. (The commented-out
  // compressed/encrypted/subitem colorings in the original are dead — not ported.)
  if (role == Qt::ForegroundRole && _markDeletedItems)
  {
    if (Prop_GetBool(_folder, (UInt32)realIndex, kpidIsDeleted))
      return QBrush(deletedTextColor());
    return QVariant();
  }

  // --- alignment (right-align numeric columns, like GetColumnAlign) ------
  if (role == Qt::TextAlignmentRole)
    return column.RightAlign
        ? int(Qt::AlignRight | Qt::AlignVCenter)
        : int(Qt::AlignLeft | Qt::AlignVCenter);

  return QVariant();
}

// G.4l : inline-rename support. ------------------------------------------------

// Is the current folder writable? Faithful inverse of CPanel::IsThereReadOnlyFolder
// (PanelMenu.cpp:866): writable iff it exposes IFolderOperations AND the folder is
// not flagged read-only (kpidReadOnly, the same VT_BOOL check IsReadOnlyFolder
// makes, PanelMenu.cpp:851). A CFSFolder and an UPDATABLE archive CAgentFolder pass;
// a read-only-format archive folder (exposes IFolderOperations but reports
// kpidReadOnly) and an in-memory stream archive (no IFolderOperations) do not.
// (The Win32 _parentFolders read-only sweep is the archive parent chain; the
// current-folder kpidReadOnly check is the gate that matters for a leaf rename.)
bool QtFolderModel::isFolderWritable() const
{
  if (_suspended || !_folder)
    return false;
  CMyComPtr<IFolderOperations> ops;
  // (_folder is a CMyComPtr; operator-> yields the IFolderFolder*. QueryInterface
  // does not mutate the folder, so calling it from a const method is fine — the
  // same way currentPath() reads _folder->GetFolderProperty() in a const method.)
  if (_folder->QueryInterface(IID_IFolderOperations, (void **)&ops) != S_OK || !ops)
    return false;
  // IsReadOnlyFolder(_folder) : kpidReadOnly VT_BOOL true == read-only.
  NCOM::CPropVariant prop;
  if (_folder->GetFolderProperty(kpidReadOnly, &prop) == S_OK
      && prop.vt == VT_BOOL
      && VARIANT_BOOLToBool(prop.boolVal))
    return false;
  return true;
}

// The Name column is editable on a writable folder; everything else is the
// default selectable/enabled. Mirrors CPanel::OnBeginLabelEdit allowing the
// edit only when !IsThereReadOnlyFolder() (and never on the ".." parent row,
// which this flat model never materialises as an item).
Qt::ItemFlags QtFolderModel::flags(const QModelIndex &idx) const
{
  // G.4f : the synthetic ".." row is selectable/enabled for activation (so a
  // double-click/Enter reaches enterItem -> parentActivated) but NEVER editable
  // and NEVER an operation target — it carries no ItemIsEditable, mirroring
  // OnBeginLabelEdit refusing the kParentIndex row.
  if (isParentRow(idx.row()))
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  Qt::ItemFlags f = QAbstractItemModel::flags(idx);
  if (idx.isValid())
  {
    // Drag-OUT fix: QAbstractItemModel::flags() returns only
    // ItemIsSelectable|ItemIsEnabled, so without this the view treats a
    // press-and-drag on a row as a rubber-band SELECTION and never calls
    // startDrag() — items could not be dragged out of the window (to the
    // other panel, Dolphin, or the desktop). Marking every cell drag-enabled
    // lets QtPanelView::startDrag -> PanelStartDrag run (extract-to-temp +
    // text/uri-list for an archive panel; FS paths for a disk panel).
    f |= Qt::ItemIsDragEnabled;
    if (columnPropID(idx.column()) == kpidName && isFolderWritable())
      f |= Qt::ItemIsEditable;
  }
  return f;
}

// Drag-OUT fix: the view calls startDrag(model->supportedDragActions()). The
// default is CopyAction only; offering Move too lets the FS-panel move-drag work
// (an archive drag-OUT is still narrowed to Copy inside PanelStartDrag, since
// move-OUT = extract+delete-from-archive is deferred — PanelDrag.cpp:1683).
Qt::DropActions QtFolderModel::supportedDragActions() const
{
  return Qt::CopyAction | Qt::MoveAction;
}

// Per-type file icon — the faithful Linux analog of the Win32 FM's SHGetFileInfo
// (ShowRealFileIcons ON path): resolve the item's MIME type from its NAME alone
// (MatchExtension => no disk I/O, so archive-interior items resolve too), then map
// it to the icon theme (mt.iconName() -> mt.genericIconName()). Falls back to the
// bundled 7-Zip per-format .ico (B.6) when the theme lacks an icon, then to the
// generic file icon. Cached by MIME name so per-cell data() stays cheap.
QIcon QtFolderModel::fileIconForRow(int row) const
{
  const QString name = UStringToQ(itemName(row));
  QMimeDatabase db;
  const QMimeType mt = db.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
  const QString key = mt.isValid() ? mt.name() : QStringLiteral("application/octet-stream");
  const auto cached = _iconCache.constFind(key);
  if (cached != _iconCache.constEnd())
    return cached.value();
  QIcon ic = QIcon::fromTheme(mt.iconName());
  if (ic.isNull())
    ic = QIcon::fromTheme(mt.genericIconName());
  if (ic.isNull())
    ic = FormatIcons::iconForExtension(extensionOf(row));   // B.6 archive .ico fallback
  if (ic.isNull())
    ic = _fileIcon;
  _iconCache.insert(key, ic);
  return ic;
}

// The rename core (shared by setData() and the headless test). Validates exactly
// as CPanel::OnEndLabelEdit (PanelOperations.cpp:289-313): IsCorrectFsName rejects
// "."/".." ; on Linux CorrectFsPath is the identity (BrowseDialog.cpp:1113), so the
// only added guard is the path-separator reject (a leaf rename must not embed a
// separator — the B.5a RenameItem fail-safe is E_INVALIDARG). Empty/unchanged name
// is a no-op (S_FALSE). On a valid distinct name it calls IFolderOperations::Rename
// — the SAME write path doRename uses. Does NOT refresh the model.
HRESULT QtFolderModel::renameRow(int row, const UString &newName, bool &renamed)
{
  renamed = false;
  // G.4f : `row` is a MODEL row. The ".." row is never renamable (it is not an
  // engine item); map to the engine realIndex and reject the parent row.
  const int realIndex = modelRowToRealIndex(row);
  if (_suspended || !_folder || realIndex < 0 || realIndex >= _numItems)
    return E_FAIL;
  row = realIndex;   // operate in engine-index space below

  // Empty or unchanged name: no-op (OnEndLabelEdit returns without renaming;
  // doRename returns early on empty/unchanged too).
  const UString oldName = itemNameByRealIndex(row);
  if (newName.IsEmpty() || newName == oldName)
    return S_FALSE;

  // CPanel::OnEndLabelEdit:299 — IsCorrectFsName(newName) reject -> E_INVALIDARG,
  // plus the B.5a leaf-must-not-contain-a-separator fail-safe. G.5c factored both
  // into IsCorrectNewFsName (the SAME rule the create-folder/file paths now use).
  if (!IsCorrectNewFsName(newName))
    return E_INVALIDARG;

  // (CorrectFsPath on Linux is identity, so newName is unchanged for FS folders.)

  CMyComPtr<IFolderOperations> ops;
  if (_folder->QueryInterface(IID_IFolderOperations, (void **)&ops) != S_OK || !ops)
    return E_NOTIMPL;

  // IFolderOperations::Rename(index, newName, progress) — the live B.5a/FS path.
  const HRESULT res = ops->Rename((UInt32)row, newName, nullptr);
  if (res == S_OK)
    renamed = true;
  return res;
}

// QAbstractItemModel::setData on the Name column / EditRole. This is what an
// inline list-view label edit (F2 / slow-double-click) commits, the Qt analogue of
// CPanel::OnEndLabelEdit. Validates + renames via renameRow(); on success it emits
// itemRenamed() so the panel re-lists (the FS re-scan, or the B.5b archive
// Close+ReOpen re-bind). Returns true only when a real rename happened — a no-op
// (empty/unchanged) or a rejected name returns false, leaving the row's text as-is.
bool QtFolderModel::setData(const QModelIndex &idx, const QVariant &value, int role)
{
  if (role != Qt::EditRole || !idx.isValid())
    return false;
  if (columnPropID(idx.column()) != kpidName)
    return false;

  const std::wstring w = value.toString().toStdWString();
  const UString newName(w.c_str());

  bool renamed = false;
  const HRESULT res = renameRow(idx.row(), newName, renamed);
  if (res != S_OK || !renamed)
    return false;   // no-op / rejected: the view keeps the original label

  // Re-list the folder (FS re-scan, or archive Close+ReOpen re-bind with the
  // B.5b suspend/refresh discipline handled by the panel slot).
  Q_EMIT itemRenamed();
  return true;
}

// P.3 : the deleted-in-archive text color. The original is a hardcoded
// RGB(255,0,0), but a fixed pure red is poor-contrast / near-invisible on a dark
// palette. We keep the EXACT original red on a light palette (true 1:1) and only
// switch to a lightened red on a dark palette — minimal, documented deviation
// that preserves the "red == deleted" semantic and stays readable in both themes.
// Read via QGuiApplication::palette() so the model stays widget-free (no QtWidgets
// dependency) and auto-tracks a custom/dark application palette.
QColor QtFolderModel::deletedTextColor() const
{
  const QColor base = QGuiApplication::palette().color(QPalette::Base);
  // Perceived luminance (Rec.601-ish), 0..255.
  const int lum = (base.red() * 299 + base.green() * 587 + base.blue() * 114) / 1000;
  if (lum < 128)                  // dark palette
    return QColor(255, 96, 96);   // lightened red: clear, readable on dark
  return QColor(255, 0, 0);       // light palette: faithful RGB(255,0,0)
}

// --- navigation (mirrors CPanel::OpenFolder / OpenParentFolder) ------------

bool QtFolderModel::enterItem(int row)
{
  // G.4f : `row` is a MODEL row. The ".." pseudo-row activates the parent navigation
  // (CPanel::OpenFolder(kParentIndex) -> OpenParentFolder). We emit parentActivated()
  // so the PANEL runs its own onUp() — which also exits an archive root via the
  // parent-stack, where the model's own goParent() returns false. Return false
  // (we did not bind a child folder here).
  if (isParentRow(row))
  {
    Q_EMIT parentActivated();
    return false;
  }

  const int realIndex = modelRowToRealIndex(row);
  if (!_folder || realIndex < 0 || realIndex >= _numItems)
    return false;
  if (!isFolder(row))
  {
    // leaf file: CPanel would OpenItem() (e.g. open an archive file as a folder).
    // Hand it to the controller via leafActivated; the model itself does not
    // change folders here. We emit the MODEL row (the controller re-reads it via
    // the model's MODEL-row-aware queries / mapping).
    Q_EMIT leafActivated(row);
    return false;
  }

  CMyComPtr<IFolderFolder> newFolder;
  // CPanel::OpenFolder : _folder->BindToFolder(index, &newFolder)
  if (_folder->BindToFolder((UInt32)realIndex, &newFolder) != S_OK || !newFolder)
    return false;

  beginResetModel();
  _folder = newFolder;
  rebuildColumns();
  reloadItems();
  endResetModel();
  Q_EMIT columnVisibilityChanged();   // G.4b : re-apply hidden sections after the rebuild
  return true;
}

bool QtFolderModel::goParent()
{
  if (!_folder)
    return false;
  CMyComPtr<IFolderFolder> newFolder;
  // CPanel::OpenParentFolder : _folder->BindToParentFolder(&newFolder)
  if (_folder->BindToParentFolder(&newFolder) != S_OK || !newFolder)
    return false; // at the archive root: no parent folder

  beginResetModel();
  _folder = newFolder;
  rebuildColumns();
  reloadItems();
  endResetModel();
  Q_EMIT columnVisibilityChanged();   // G.4b : re-apply hidden sections after the rebuild
  return true;
}

bool QtFolderModel::isRoot() const
{
  if (!_folder)
    return true;
  // Probe: a root archive folder returns no parent. We don't mutate state;
  // BindToParentFolder is a const-ish query that just yields a new folder ptr.
  CMyComPtr<IFolderFolder> parentFolder;
  IFolderFolder *f = _folder;
  if (f->BindToParentFolder(&parentFolder) != S_OK)
    return true;
  return !parentFolder;
}

// --- item queries (mirror CPanel::IsItem_Folder / GetItemName / GetItemSize)

bool QtFolderModel::isFolder(int row) const
{
  // G.4f : `row` is a MODEL row. The ".." pseudo-row IS a folder (it activates as a
  // navigation, like CPanel's kParentIndex). Map real items to the engine index.
  if (isParentRow(row))
    return true;
  const int realIndex = modelRowToRealIndex(row);
  if (!_folder || realIndex < 0 || realIndex >= _numItems)
    return false;
  return Prop_GetBool(_folder, (UInt32)realIndex, kpidIsDir); // CPanel: kpidIsDir
}

// G.4f : kpidName by ENGINE index (no MODEL-row translation). The internal core that
// the public itemName(MODEL row) and renameRow() share.
UString QtFolderModel::itemNameByRealIndex(int realIndex) const
{
  if (!_folder || realIndex < 0 || realIndex >= _numItems)
    return UString();
  NCOM::CPropVariant prop;
  if (_folder->GetProperty((UInt32)realIndex, kpidName, &prop) != S_OK)
    return UString();
  if (prop.vt == VT_BSTR)
    return UString(prop.bstrVal);
  return UString();
}

UString QtFolderModel::itemName(int row) const
{
  // G.4f : `row` is a MODEL row. The ".." pseudo-row's name is "..".
  if (isParentRow(row))
    return UString("..");
  return itemNameByRealIndex(modelRowToRealIndex(row));
}

// B.6 : the row's file extension (after the last '.'), lower-cased, no dot.
// Uniform name-suffix approach (works for FS and archive folders alike), the
// same way RunScriptedDrop sniffs .7z/.zip by name. Empty if there is no dot.
QString QtFolderModel::extensionOf(int row) const
{
  const UString nm = itemName(row);
  const int dot = nm.ReverseFind_Dot();
  if (dot < 0 || dot + 1 >= (int)nm.Len())
    return QString();
  const UString ext = nm.Ptr(dot + 1);
  return UStringToQ(ext).toLower();
}

UInt64 QtFolderModel::itemSize(int row, bool &defined) const
{
  defined = false;
  // G.4f : `row` is a MODEL row; the ".." pseudo-row has no size.
  const int realIndex = modelRowToRealIndex(row);
  if (!_folder || realIndex < 0 || realIndex >= _numItems)
    return 0;
  NCOM::CPropVariant prop;
  if (_folder->GetProperty((UInt32)realIndex, kpidSize, &prop) != S_OK)
    return 0;
  UInt64 v = 0;
  if (prop.vt != VT_EMPTY && ConvertPropVariantToUInt64(prop, v))
  {
    defined = true;
    return v;
  }
  return 0;
}

bool QtFolderModel::itemSize_IsDefined(int row) const
{
  bool defined = false;
  itemSize(row, defined);
  return defined;
}

// G.4i : the focused item's size text for the status bar. CPanel::Refresh_StatusBar
// (PanelListNotify.cpp:810) does ConvertSizeToString(GetItemSize(realIndex), ...);
// GetItemSize returns the raw kpidSize UInt64 (0 when undefined). We mirror it with
// the SAME grouped formatter the size columns use (QtFm_ConvertSizeToString). An
// undefined size yields "0", exactly as the original (GetItemSize -> 0 -> "0").
QString QtFolderModel::focusedSizeString(int row) const
{
  // G.4f : `row` is a MODEL row; the ".." pseudo-row has no size field.
  if (isParentRow(row))
    return QString();
  const int realIndex = modelRowToRealIndex(row);
  if (!_folder || realIndex < 0 || realIndex >= _numItems)
    return QString();
  bool defined = false;
  const UInt64 v = itemSize(row, defined);
  wchar_t buf[64];
  QtFm_ConvertSizeToString(v, buf);
  return QString::fromWCharArray(buf);
}

// Same grouped formatter as the size columns, applied to an explicit byte count
// (the selection's total size, Refresh_StatusBar part 1).
QString QtFolderModel::sizeStringForBytes(UInt64 bytes) const
{
  wchar_t buf[64];
  QtFm_ConvertSizeToString(bytes, buf);
  return QString::fromWCharArray(buf);
}

// G.4i : the focused item's modified-date text for the status bar. CPanel
// (PanelListNotify.cpp:812-823) reads kpidMTime and formats it with
// ConvertPropertyToShortString2(..., kpidMTime); a missing MTime leaves the field
// empty (the GetProperty != S_OK / VT_EMPTY paths). We reuse the same engine
// formatter so the short date matches 7-Zip exactly.
QString QtFolderModel::focusedMTimeString(int row) const
{
  // G.4f : `row` is a MODEL row; the ".." pseudo-row has no date field.
  if (isParentRow(row))
    return QString();
  const int realIndex = modelRowToRealIndex(row);
  if (!_folder || realIndex < 0 || realIndex >= _numItems)
    return QString();
  NCOM::CPropVariant prop;
  if (_folder->GetProperty((UInt32)realIndex, kpidMTime, &prop) != S_OK)
    return QString();
  if (prop.vt == VT_EMPTY)
    return QString();
  char dateString[64];
  dateString[0] = 0;
  ConvertPropertyToShortString2(dateString, prop, kpidMTime);
  return QString::fromLatin1(dateString);
}

// G.4j : find the source row whose kpidName equals `name`. Used to restore the
// focus to the just-left subfolder after going Up (mirrors RefreshListCtrl
// honoring CSelectedState::FocusedName). Case-sensitive on the engine name, the
// way the Win32 FM matches the stored FocusedName.
int QtFolderModel::rowForName(const UString &name) const
{
  if (!_folder || name.IsEmpty())
    return -1;
  // G.4f : search engine items by name, then return the MODEL row (shifted by the
  // ".." row when shown). The ".." pseudo-row is never matched here (a real child
  // never has the engine name ".."), so this only ever returns a real-item row.
  for (int realIndex = 0; realIndex < _numItems; realIndex++)
    if (itemNameByRealIndex(realIndex) == name)
      return realIndexToModelRow(realIndex);
  return -1;
}

UString QtFolderModel::currentPath() const
{
  if (!_folder)
    return UString();
  // CPanel's GetFolderPath(folder): GetFolderProperty(kpidPath) as BSTR.
  NCOM::CPropVariant prop;
  if (_folder->GetFolderProperty(kpidPath, &prop) == S_OK && prop.vt == VT_BSTR)
    return UString(prop.bstrVal);
  return UString();
}

// G.4c : Flat View. Does the bound folder support recursive flat listing? Both
// CFSFolder and CAgentFolder expose IFolderSetFlatMode (FSFolder.h:93 / Agent.h:65),
// so this is true for every folder the FM binds; a folder type lacking it would
// have Flat View grayed (faithful: the original only offers flat where supported).
bool QtFolderModel::flatModeSupported() const
{
  if (!_folder)
    return false;
  CMyComPtr<IFolderSetFlatMode> setFlat;
  IFolderFolder *f = _folder;
  return f->QueryInterface(IID_IFolderSetFlatMode, (void **)&setFlat) == S_OK
      && setFlat;
}

// G.4c : toggle/set Flat View on the CURRENT folder. Records the flag, then does a
// full model reset (rebuildColumns re-applies SetFlatMode + re-enumerates so the
// kpidPrefix column appears/disappears, reloadItems re-lists flat/non-flat). This
// is the model side of CPanel::ChangeFlatMode -> RefreshListCtrl_SaveFocused
// (Panel.cpp:894-902). Returns false (no change) when the folder can't go flat.
bool QtFolderModel::setFlatMode(bool on)
{
  if (!_folder || !flatModeSupported())
    return false;
  if (_flatMode == on)
    return true;   // already in the requested mode (idempotent)
  beginResetModel();
  _flatMode = on;
  rebuildColumns();   // applies SetFlatMode + adds/drops the kpidPrefix column
  reloadItems();      // re-lists with the new flat state
  endResetModel();
  // G.4b : the column set changed (kpidPrefix added/removed) — let the panel
  // re-apply the hidden sections to the reset header.
  Q_EMIT columnVisibilityChanged();
  return true;
}

PROPID QtFolderModel::columnPropID(int column) const
{
  if (column < 0 || column >= _columns.size())
    return 0;
  return _columns[column].ID;
}

int QtFolderModel::columnForPropID(PROPID propID) const
{
  for (int i = 0; i < _columns.size(); i++)
    if (_columns[i].ID == propID)
      return i;
  return -1;
}

QString QtFolderModel::columnName(int column) const
{
  if (column < 0 || column >= _columns.size())
    return QString();
  return _columns[column].Name;
}

// G.4b : the current folder a filesystem folder? Mirrors CPanel::IsFSFolder() ||
// IsAltStreamsFolder() — IsFolderTypeEqTo reads GetFolderProperty(kpidType) as a
// BSTR and ASCII-compares it (Panel.cpp:829-837). An FSFolder / AltStreamsFolder
// reports those type strings; an archive folder reports "7-Zip..." (so false here),
// which is exactly the gate GetColumnVisible keys on.
bool QtFolderModel::isFsFolder() const
{
  if (!_folder)
    return false;
  NCOM::CPropVariant prop;
  if (_folder->GetFolderProperty(kpidType, &prop) != S_OK || prop.vt != VT_BSTR)
    return false;
  const UString type(prop.bstrVal);
  return type.IsEqualTo("FSFolder") || type.IsEqualTo("AltStreamsFolder");
}

// G.4a : the current folder's kpidType BSTR — VERBATIM CPanel::GetFolderTypeID()
// (Panel.cpp:818-827): read GetFolderProperty(kpidType) as a VT_BSTR, else empty.
// This is the per-list-type id CListViewInfo persists view settings under.
UString QtFolderModel::folderTypeId() const
{
  if (!_folder)
    return UString();
  NCOM::CPropVariant prop;
  if (_folder->GetFolderProperty(kpidType, &prop) == S_OK && prop.vt == VT_BSTR)
    return UString(prop.bstrVal);
  return UString();
}

// --- G.4b : per-column visibility -------------------------------------------

bool QtFolderModel::isColumnVisible(int column) const
{
  if (column < 0 || column >= _columns.size())
    return false;
  return _columns[column].Visible;
}

bool QtFolderModel::isColumnVisibleByPropID(PROPID propID) const
{
  const int col = columnForPropID(propID);
  if (col < 0)
    return false;
  return _columns[col].Visible;
}

// Toggle a column's visibility by PROPID. The Name column is locked visible
// (CPanel grays item 0 in ShowColumnsContextMenu: a hide request is a no-op). The
// per-PROPID override is recorded so a subsequent folder change (rebuildColumns)
// preserves the toggle (the seam persistence saves/restores). Emits
// columnVisibilityChanged() so the panel re-applies setSectionHidden.
bool QtFolderModel::setColumnVisibleByPropID(PROPID propID, bool visible)
{
  const int col = columnForPropID(propID);
  if (col < 0)
    return false;
  // kpidName : the locked main column — never hidden.
  if (propID == kpidName)
  {
    // Keep it visible; ignore any hide request (matches MF_GRAYED on item 0).
    if (!_columns[col].Visible)
    {
      _columns[col].Visible = true;
      Q_EMIT columnVisibilityChanged();
    }
    return false;
  }
  if (_columns[col].Visible == visible)
    return false;   // no change
  _columns[col].Visible = visible;
  _savedVisible.insert(propID, visible);
  Q_EMIT columnVisibilityChanged();
  return true;
}

void QtFolderModel::setVisibleColumnsFromSaved(const QVector<QPair<PROPID, bool>> &saved)
{
  for (const QPair<PROPID, bool> &kv : saved)
  {
    if (kv.first == kpidName)
      continue;   // always-visible: never restored as hidden
    _savedVisible.insert(kv.first, kv.second);
    const int col = columnForPropID(kv.first);
    if (col >= 0)
      _columns[col].Visible = kv.second;
  }
  Q_EMIT columnVisibilityChanged();
}

// G.4m : the row's raw PropVariant, the seam the sort proxy uses to compare
// time/numeric columns by VALUE (CPanel::CompareItems2 reads exactly this via
// _folder->GetProperty, PanelSort.cpp:170-171). Same suspend/bounds guards as
// data(): while an in-place archive update is rebinding the folder we must not
// touch GetProperty (B.5b). Leaves `prop` VT_EMPTY on any failure.
HRESULT QtFolderModel::getRawProperty(int row, PROPID propID,
    NCOM::CPropVariant &prop) const
{
  prop.Clear();
  // G.4f : `row` is a MODEL row. The ".." pseudo-row has no engine property (it
  // sorts first via the proxy regardless); map real items to the engine index.
  const int realIndex = modelRowToRealIndex(row);
  if (_suspended || !_folder || realIndex < 0 || realIndex >= _numItems)
    return E_FAIL;
  return _folder->GetProperty((UInt32)realIndex, propID, &prop);
}
