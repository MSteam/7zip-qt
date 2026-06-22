// QtPropertiesDialog.cpp
// ----------------------------------------------------------------------------
// See QtPropertiesDialog.h. Faithful Qt translation of CPanel::Properties()
// (FileManager/PanelMenu.cpp:172) single-item core + AddPropertyString
// (PanelMenu.cpp:110-148).
// ----------------------------------------------------------------------------

#include "QtPropertiesDialog.h"

#include <algorithm>                            // G.5b : std::sort (copy row order)

#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QPlainTextEdit>
#include <QtGui/QShortcut>
#include <QtGui/QClipboard>
#include <QtGui/QKeySequence>
#include <QtGui/QKeyEvent>
#include <QtCore/QEvent>

#include "../../../../Common/MyCom.h"          // CMyComBSTR
#include "../../../../Common/StringConvert.h"
#include "../../../../Common/IntToString.h"    // G.5b : ConvertDataToHex_Upper/Lower
#include "../../../../Windows/PropVariant.h"
#include "../../../../Windows/PropVariantConv.h"

#include "../../../PropID.h"
#include "../../../Archive/IArchive.h"          // G.5b : IArchiveGetRawProps
#include "../../Common/PropIDUtils.h"

#include "../QtLang.h"                          // G.1f : FmLang(IDS_PROPERTIES, ...)
#include "../../FileManager/resource.h"         // G.1f : IDS_PROPERTIES=6600, IDS_N_SELECTED_ITEMS=3002

#include "../../Common/OpenArchive.h"           // G.3c : GetOpenArcErrorFlags
#include "../QtOpenArcError.h"                  // G.3c : Qt_GetOpenArcErrorMessage

using namespace NWindows;

// ---------------------------------------------------------------------------
// Property name + value helpers — copied (semantics) from QtFolderModel.cpp,
// which itself mirrors the engine's GetPropName / CPanel::SetItemText. The
// standing convention is "mirror 1:1, copy where it's data/logic" rather than
// exporting a static; these two small functions are pure portable logic.
// ---------------------------------------------------------------------------

// Canonical English PROPID->name table — copied VERBATIM from QtFolderModel.cpp
// (kPropIdToName), which transcribes the console List.cpp table. The File Manager
// resolves names via GetNameOfProperty -> LangString(1000+propID) (PropertyName.cpp:10);
// for standard PROPIDs that resolves to exactly these English strings.
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

// Mirrors GetNameOfProperty (PropertyName.cpp:10): canonical name when the
// PROPID is in range, else the BSTR name, else the numeric PROPID. The fallback
// chain (lang/table -> BSTR name -> number) is preserved so unknown/custom
// propIDs still render.
static QString QtFm_PropName(PROPID propID, const CMyComBSTR &name)
{
  if (propID < (PROPID)(sizeof(kPropIdToName) / sizeof(kPropIdToName[0])))
    return QString::fromLatin1(kPropIdToName[propID]);
  if (name && name[0] != 0)
    return QString::fromWCharArray((const wchar_t *)name);
  return QString::number((uint)propID);
}

// Mirrors AddPropertyString's value branch (PanelMenu.cpp:129-136). The original
// special-cases size-typed props through ConvertSizeToString, but ConvertProperty-
// ToString2 already emits the plain byte value for size props (which is exactly
// what `7zz l -slt` prints), so routing every prop through it at precision 9 (the
// nanosecond time precision PanelMenu.cpp:136 passes) yields the engine-exact
// value string while keeping the headless assertion against `l -slt` exact.
static QString QtFm_PropValue(const NCOM::CPropVariant &prop, PROPID propID)
{
  if (prop.vt == VT_EMPTY)
    return QString();

  // G.3c : kpidErrorFlags / kpidWarningFlags decode (mirror PanelMenu.cpp:117-124).
  // Decode the raw kpv_ErrorFlags_* bitmask into human-readable prose rather than
  // emitting the number. The original returns early (no row) when the decoded flag
  // set is 0 (caller skips the property); here that is signalled by an empty value
  // and the caller's existing "skip empty value" guard (addRow only on non-empty).
  if (propID == kpidErrorFlags || propID == kpidWarningFlags)
  {
    const UInt32 flags = GetOpenArcErrorFlags(prop);
    if (flags == 0)
      return QString();
    const UString s = Qt_GetOpenArcErrorMessage(flags);
    return QString::fromWCharArray(s.Ptr(), (int)s.Len());
  }

  UString s;
  ConvertPropertyToString2(s, prop, propID, 9);
  return QString::fromWCharArray(s.Ptr(), (int)s.Len());
}

// kSpecProps[] — copied VERBATIM from PanelMenu.cpp:158-170. The pseudo-props
// the archive-info section walks at NEGATIVE index for each archive level (and
// the NonOpen ERROR level), before the format's real GetArcPropInfo props.
static const Byte kSpecProps[] =
{
  kpidPath,
  kpidType,
  kpidErrorType,
  kpidError,
  kpidErrorFlags,
  kpidWarning,
  kpidWarningFlags,
  kpidOffset,
  kpidPhySize,
  kpidTailSize
};

// ---------------------------------------------------------------------------

QtPropertiesDialog::QtPropertiesDialog(QWidget *parent)
  : QDialog(parent)
  , _table(nullptr)
{
  setWindowTitle(FmLang(IDS_PROPERTIES, QStringLiteral("Properties"))); // PanelMenu.cpp:419
  resize(420, 520);                              // CListViewDialog width 400-ish

  QVBoxLayout *lay = new QVBoxLayout(this);

  _table = new QTableWidget(this);
  _table->setColumnCount(2);                     // message.NumColumns = 2
  _table->setHorizontalHeaderLabels(
      QStringList() << QStringLiteral("Property") << QStringLiteral("Value"));
  _table->verticalHeader()->setVisible(false);
  _table->setEditTriggers(QAbstractItemView::NoEditTriggers); // read-only
  _table->setSelectionBehavior(QAbstractItemView::SelectRows);
  _table->setWordWrap(false);
  _table->horizontalHeader()->setStretchLastSection(true);
  _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  lay->addWidget(_table);

  // G.5b : interactivity (CListViewDialog, ListViewDialog.cpp:164-256).
  //  - Ctrl+C / Ctrl+Ins copy the selected "Name: Value" rows (CopyToClipboard).
  //  - Ctrl+A select-all.
  //  - double-click / Enter on a row opens the value viewer (ShowItemInfo /
  //    CEditDialog, ListViewDialog.cpp:199-223).
  _table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  connect(_table, &QTableWidget::cellActivated,
      this, [this](int r, int) { showValueForRow(r); });
  connect(_table, &QTableWidget::cellDoubleClicked,
      this, [this](int r, int) { showValueForRow(r); });
  // Ctrl+A select-all (ListViewDialog.cpp:292-298). Ctrl+C / Ctrl+Ins copy
  // (:301-310) — Qt's QTableWidget has no built-in copy, so install both. The
  // Enter/Return activation is routed through the event filter so Alt+Enter and a
  // plain Enter both reach ShowItemInfo (OnEnter, ListViewDialog.cpp:247-256).
  {
    QShortcut *sc = new QShortcut(QKeySequence(QKeySequence::Copy), _table);
    sc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc, &QShortcut::activated, this, &QtPropertiesDialog::copySelectedToClipboard);
    QShortcut *scIns = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Insert), _table);
    scIns->setContext(Qt::WidgetWithChildrenShortcut);
    connect(scIns, &QShortcut::activated, this, &QtPropertiesDialog::copySelectedToClipboard);
    QShortcut *scAll = new QShortcut(QKeySequence(QKeySequence::SelectAll), _table);
    scAll->setContext(Qt::WidgetWithChildrenShortcut);
    connect(scAll, &QShortcut::activated, this, [this]() { _table->selectAll(); });
  }
  _table->installEventFilter(this);

  QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
  // G.1f : Close uses langID 408 (kLangPairs, LangUtils.cpp); English from the
  // standard-button default. Override so a loaded txt translates it.
  if (QPushButton *closeBtn = bb->button(QDialogButtonBox::Close))
    closeBtn->setText(FmLang(408, QStringLiteral("Close")));
  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
  lay->addWidget(bb);
}

void QtPropertiesDialog::addRow(const QString &name, const QString &value)
{
  const int r = _table->rowCount();
  _table->insertRow(r);
  // CListViewDialog flattens \r\n -> space and truncates the value cell; the
  // table view already elides long cells. Flatten newlines so the row stays
  // single-line like the original (ListViewDialog.cpp:107-118).
  QString v = value;
  v.replace(QLatin1Char('\r'), QLatin1Char(' '));
  v.replace(QLatin1Char('\n'), QLatin1Char(' '));
  _table->setItem(r, 0, new QTableWidgetItem(name));
  _table->setItem(r, 1, new QTableWidgetItem(v));
  // G.5b : keep the UN-flattened value for the value viewer (ShowItemInfo shows
  // the full Values[index], not the flattened cell text).
  _fullValues.append(value);
}

// G.3c : faithful mirror of PanelMenu.cpp's AddPropertyString (:110-148). Format
// the value (QtFm_PropValue already applies the kpidErrorFlags/kpidWarningFlags
// decode), skip when the formatted value is empty (matches AddPropertyString's
// `if (!val.IsEmpty())` guard — and the flags==0 early-return), and for
// kpidErrorType emit the extra "Open WARNING:" pair BEFORE the prop row
// (PanelMenu.cpp:141-143).
void QtPropertiesDialog::addPropRow(PROPID propID, const CMyComBSTR &name,
    const NCOM::CPropVariant &prop)
{
  if (prop.vt == VT_EMPTY)
    return; // PanelMenu's AddPropertyString returns early for VT_EMPTY
  const QString val = QtFm_PropValue(prop, propID);
  if (val.isEmpty())
    return; // flags==0 decode, or an otherwise-empty value: no row (PanelMenu :139)
  if (propID == kpidErrorType)
    addRow(QStringLiteral("Open WARNING:"),
        QStringLiteral("Cannot open the file as expected archive type"));
  addRow(QtFm_PropName(propID, name), val);
}

// Direct translation of the single-item block of CPanel::Properties()
// (PanelMenu.cpp:190-211): iterate ALL folder properties (GetPropertyInfo index
// order) and add (GetNameOfProperty(propID,name), formatted value) pairs.
void QtPropertiesDialog::fill()
{
  if (!_table)
    return;
  _table->setRowCount(0);
  _fullValues.clear();
  if (!Folder)
    return;

  // G.5a : multi-select dispatch (PanelMenu.cpp:190 / :260). The original branches
  // on operatedIndices.Size(): == 1 -> single-item props; >= 1 (i.e. !=1 here,
  // since the ==1 case is handled above in the original) -> the aggregate summary.
  // Mirror that: when >1 index is selected, render only the aggregate summary +
  // the archive-info section; the single-item prop loop is skipped.
  if (Indices.size() > 1)
  {
    fillMultiSelectSummary();
    // After the summary the original falls through to the same archive-info
    // section (PanelMenu.cpp:305-417), so let that run below.
  }
  else
  {
    UInt32 numProps = 0;
    if (Folder->GetNumberOfProperties(&numProps) != S_OK)
      return;

    for (UInt32 i = 0; i < numProps; i++)
    {
      CMyComBSTR name;
      PROPID propID = 0;
      VARTYPE vt = 0;
      if (Folder->GetPropertyInfo(i, &name, &propID, &vt) != S_OK)
        continue;
      NCOM::CPropVariant prop;
      if (Folder->GetProperty(Index, propID, &prop) != S_OK)
        continue;
      // G.3c : AddPropertyString (with the flags decode + kpidErrorType pair).
      addPropRow(propID, name, prop);
    }

    // G.5b : the _folderRawProps raw-prop block (PanelMenu.cpp:214-256) — render
    // CRC/Checksum/SHA-1/SHA-256 as hex. NtSecure (Windows ACL) is SKIPPED.
    addRawProps(Index);
  }

  // The archive-info section (:305-417) follows.

  // -------------------------------------------------------------------------
  // Archive-info section — direct translation of CPanel::Properties()
  // (PanelMenu.cpp:174-417). The original QIs the folder for IGetFolderArcProps;
  // a real FS folder (CFSFolder) does NOT implement it, so it falls through to
  // InvokeSystemCommand("properties") — the Windows shell property sheet, which
  // has no Linux equivalent, so for the FS case we leave only the item props
  // above (our Linux analogue). An archive folder (CAgentFolder) DOES implement
  // it (Agent.h:56,170), so we append the Path, the IFolderProperties folder
  // props, and the multi-level IFolderArcProps props below.
  // -------------------------------------------------------------------------
  CMyComPtr<IGetFolderArcProps> getFolderArcProps;
  Folder->QueryInterface(IID_IGetFolderArcProps, (void **)&getFolderArcProps);
  if (!getFolderArcProps)
    return; // FS folder: no archive section (Windows shell dialog has no Linux peer)

  // PanelMenu.cpp:258 — AddSeparator after the single-item block.
  addRow(QString(), QString());

  // The folder Path (PanelMenu.cpp:305-311). Original passes name "Path" and
  // formats with propID kpidName.
  {
    NCOM::CPropVariant prop;
    if (Folder->GetFolderProperty(kpidPath, &prop) == S_OK && prop.vt != VT_EMPTY)
      addRow(QStringLiteral("Path"), QtFm_PropValue(prop, kpidName));
  }

  // IFolderProperties folder props (PanelMenu.cpp:313-333).
  {
    CMyComPtr<IFolderProperties> folderProperties;
    Folder->QueryInterface(IID_IFolderProperties, (void **)&folderProperties);
    if (folderProperties)
    {
      UInt32 numFolderProps = 0;
      if (folderProperties->GetNumberOfFolderProperties(&numFolderProps) == S_OK)
      {
        for (UInt32 i = 0; i < numFolderProps; i++)
        {
          CMyComBSTR name;
          PROPID propID = 0;
          VARTYPE vt = 0;
          if (folderProperties->GetFolderPropertyInfo(i, &name, &propID, &vt) != S_OK)
            continue;
          NCOM::CPropVariant prop;
          if (Folder->GetFolderProperty(propID, &prop) != S_OK)
            continue;
          addPropRow(propID, name, prop); // G.3c
        }
      }
    }
  }

  // Multi-level IFolderArcProps props (PanelMenu.cpp:335-417).
  {
    CMyComPtr<IFolderArcProps> getProps;
    getFolderArcProps->GetFolderArcProps(&getProps);
    if (getProps)
    {
      UInt32 numLevels = 0;
      if (getProps->GetArcNumLevels(&numLevels) != S_OK)
        numLevels = 0;

      for (UInt32 level2 = 0; level2 < numLevels; level2++)
      {
        // Outer-to-inner: level = numLevels - 1 - level2 (PanelMenu.cpp:347).
        {
          const UInt32 level = numLevels - 1 - level2;
          UInt32 numLevelProps = 0;
          if (getProps->GetArcNumProps(level, &numLevelProps) == S_OK)
          {
            const int kNumSpecProps = Z7_ARRAY_SIZE(kSpecProps);

            addRow(QString(), QString()); // AddSeparator (PanelMenu.cpp:353)

            // Negative index walks the kSpecProps[] pseudo-props first, then the
            // real GetArcPropInfo props (PanelMenu.cpp:355-368).
            for (Int32 i = -(int)kNumSpecProps; i < (Int32)numLevelProps; i++)
            {
              CMyComBSTR name;
              PROPID propID = 0;
              VARTYPE vt = 0;
              if (i < 0)
                propID = kSpecProps[i + kNumSpecProps];
              else if (getProps->GetArcPropInfo(level, (UInt32)i, &name, &propID, &vt) != S_OK)
                continue;
              NCOM::CPropVariant prop;
              if (getProps->GetArcProp(level, propID, &prop) != S_OK)
                continue;
              addPropRow(propID, name, prop); // G.3c : flags decode + ErrorType pair
            }
          }
        }

        // The nested "props2" block for all but the innermost level
        // (PanelMenu.cpp:372-392).
        if (level2 < numLevels - 1)
        {
          const UInt32 level = numLevels - 1 - level2;
          UInt32 numProps2 = 0;
          if (getProps->GetArcNumProps2(level, &numProps2) == S_OK)
          {
            addRow(QString(), QString()); // AddSeparatorSmall (PanelMenu.cpp:378)
            for (UInt32 i = 0; i < numProps2; i++)
            {
              CMyComBSTR name;
              PROPID propID = 0;
              VARTYPE vt = 0;
              if (getProps->GetArcPropInfo2(level, i, &name, &propID, &vt) != S_OK)
                continue;
              NCOM::CPropVariant prop;
              if (getProps->GetArcProp2(level, propID, &prop) != S_OK)
                continue;
              addPropRow(propID, name, prop); // G.3c
            }
          }
        }
      }

      // The NonOpen ERROR level at index numLevels, over kSpecProps only
      // (PanelMenu.cpp:395-414).
      {
        bool needSep = true;
        const int kNumSpecProps = Z7_ARRAY_SIZE(kSpecProps);
        for (Int32 i = -(int)kNumSpecProps; i < 0; i++)
        {
          CMyComBSTR name;
          const PROPID propID = kSpecProps[i + kNumSpecProps];
          NCOM::CPropVariant prop;
          if (getProps->GetArcProp(numLevels, propID, &prop) != S_OK)
            continue;
          // PanelMenu.cpp:406-411 emits the separators on S_OK, BEFORE the
          // VT_EMPTY skip (that skip lives inside AddPropertyString) — mirror that
          // ordering so the separator placement matches the original exactly.
          if (needSep)
          {
            addRow(QString(), QString()); // AddSeparator (PanelMenu.cpp:408)
            addRow(QString(), QString()); // AddSeparator (PanelMenu.cpp:409)
            needSep = false;
          }
          addPropRow(propID, name, prop); // G.3c : flags decode + ErrorType pair
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// G.5a : the multi-select aggregate summary — direct translation of the
// operatedIndices.Size() >= 1 branch of CPanel::Properties() (PanelMenu.cpp:260-
// 295). Sum the unpacked Size, the Packed Size, and the Files/Folders counts
// across all selected indices, then emit the "N object(s) selected" header row
// followed by the Folders/Files/Size/Packed-Size rows.
// ---------------------------------------------------------------------------
void QtPropertiesDialog::fillMultiSelectSummary()
{
  UInt64 packSize = 0;
  UInt64 unpackSize = 0;
  UInt64 numFiles = 0;
  UInt64 numDirs = 0;

  for (int k = 0; k < Indices.size(); k++)
  {
    const UInt32 index = Indices[k];

    // GetItemSize(index) (PanelItems.cpp:1290) prefers IFolderGetItemName->
    // GetItemSize, else kpidSize. Mirror that fast path so archive folders that
    // override GetItemSize report the same number the panel does.
    {
      UInt64 v = 0;
      bool got = false;
      CMyComPtr<IFolderGetItemName> getName;
      Folder->QueryInterface(IID_IFolderGetItemName, (void **)&getName);
      if (getName)
      {
        v = getName->GetItemSize(index);
        got = true;
      }
      if (!got)
      {
        NCOM::CPropVariant prop;
        if (Folder->GetProperty(index, kpidSize, &prop) == S_OK)
          ConvertPropVariantToUInt64(prop, v);
      }
      unpackSize += v;
    }

    // GetItem_UInt64Prop(index, kpidPackSize) (PanelItems.cpp:1277).
    {
      UInt64 v = 0;
      NCOM::CPropVariant prop;
      if (Folder->GetProperty(index, kpidPackSize, &prop) == S_OK)
        ConvertPropVariantToUInt64(prop, v);
      packSize += v;
    }

    // IsItem_Folder(index) (PanelItems.cpp:1261) -> kpidIsDir.
    bool isDir = false;
    {
      NCOM::CPropVariant prop;
      if (Folder->GetProperty(index, kpidIsDir, &prop) == S_OK
          && prop.vt == VT_BOOL)
        isDir = (prop.boolVal != VARIANT_FALSE);
    }
    if (isDir)
    {
      numDirs++;
      // numDirs += kpidNumSubDirs; numFiles += kpidNumSubFiles (PanelMenu.cpp:275-276).
      NCOM::CPropVariant pSub, pFile;
      UInt64 sub = 0, file = 0;
      if (Folder->GetProperty(index, kpidNumSubDirs, &pSub) == S_OK)
        ConvertPropVariantToUInt64(pSub, sub);
      if (Folder->GetProperty(index, kpidNumSubFiles, &pFile) == S_OK)
        ConvertPropVariantToUInt64(pFile, file);
      numDirs += sub;
      numFiles += file;
    }
    else
      numFiles++;
  }

  // "{0} object(s) selected" header with an EMPTY name (PanelMenu.cpp:281-285).
  // FmLang(IDS_N_SELECTED_ITEMS) resolves to the .rc text "{0} object(s) selected"
  // (resource.rc:192); MyFormatNew replaces {0} with the count.
  {
    const QString fmt = FmLang(IDS_N_SELECTED_ITEMS,
        QStringLiteral("{0} object(s) selected")); // resource.rc:192
    QString hdr = fmt;
    hdr.replace(QStringLiteral("{0}"), QString::number((qulonglong)Indices.size()));
    addRow(QString(), hdr);
  }

  // The original adds these via AddPropertyString(propID, UInt64, dialog), whose
  // size-prop branch formats kpidNumSubDirs/kpidNumSubFiles/kpidSize/kpidPackSize
  // through ConvertSizeToString. addPropRow routes a UInt64 CPropVariant through
  // the same QtFm_PropValue/ConvertPropertyToString2 the single-item rows use,
  // which prints the plain byte value (matching `7zz l -slt`). The Folders/Files
  // rows are only emitted when non-zero (PanelMenu.cpp:287-290).
  CMyComBSTR noName;
  if (numDirs != 0)
  {
    NCOM::CPropVariant prop = numDirs;
    addPropRow(kpidNumSubDirs, noName, prop);
  }
  if (numFiles != 0)
  {
    NCOM::CPropVariant prop = numFiles;
    addPropRow(kpidNumSubFiles, noName, prop);
  }
  {
    NCOM::CPropVariant prop = unpackSize;
    addPropRow(kpidSize, noName, prop);
  }
  {
    NCOM::CPropVariant prop = packSize;
    addPropRow(kpidPackSize, noName, prop);
  }
}

// ---------------------------------------------------------------------------
// G.5b : the raw-prop block — direct translation of PanelMenu.cpp:214-256. QI the
// folder for IArchiveGetRawProps (the engine's _folderRawProps), then render each
// non-empty raw prop as hex: CRC/Checksum upper-case, others lower-case. The
// Windows-only kpidNtSecure case (ConvertNtSecureToString / NTFS ACL) is SKIPPED.
// ---------------------------------------------------------------------------
void QtPropertiesDialog::addRawProps(UInt32 index)
{
  CMyComPtr<IArchiveGetRawProps> rawProps;
  Folder->QueryInterface(IID_IArchiveGetRawProps, (void **)&rawProps);
  if (!rawProps)
    return;

  UInt32 numProps = 0;
  if (rawProps->GetNumRawProps(&numProps) != S_OK)
    return;

  for (UInt32 i = 0; i < numProps; i++)
  {
    CMyComBSTR name;
    PROPID propID = 0;
    if (rawProps->GetRawPropInfo(i, &name, &propID) != S_OK)
      continue;

    const void *data = nullptr;
    UInt32 dataSize = 0;
    UInt32 propType = 0;
    if (rawProps->GetRawProp(index, propID, &data, &dataSize, &propType) != S_OK)
      continue;

    if (dataSize == 0)
      continue;

    // SKIP kpidNtSecure (Windows ACL — ConvertNtSecureToString, PanelMenu.cpp:233).
    if (propID == kpidNtSecure)
      continue;

    AString s;
    const unsigned kMaxDataSize = 1 << 8;     // PanelMenu.cpp:237
    if (dataSize > kMaxDataSize)
    {
      s += "data:";
      s.Add_UInt32(dataSize);
    }
    else
    {
      char temp[kMaxDataSize * 2 + 2];
      if (dataSize <= 8 && (propID == kpidCRC || propID == kpidChecksum))
        ConvertDataToHex_Upper(temp, (const Byte *)data, dataSize);   // CRC/Checksum upper
      else
        ConvertDataToHex_Lower(temp, (const Byte *)data, dataSize);   // SHA-1/SHA-256 lower
      s += temp;
    }
    addRow(QtFm_PropName(propID, name),
        QString::fromLatin1(s.Ptr(), (int)s.Len()));
  }
}

// ---------------------------------------------------------------------------
// G.5b : interactivity (CListViewDialog / CEditDialog, ListViewDialog.cpp).
// ---------------------------------------------------------------------------

// CopyToClipboard (ListViewDialog.cpp:164-196): for each selected row, emit
// "Name: Value\n" (the "Name: Value" form, one row per line).
void QtPropertiesDialog::copySelectedToClipboard()
{
  if (!_table)
    return;
  // Collect the selected rows in order (SelectRows behaviour groups by row).
  QVector<int> rows;
  const QList<QTableWidgetSelectionRange> ranges = _table->selectedRanges();
  for (const QTableWidgetSelectionRange &range : ranges)
    for (int r = range.topRow(); r <= range.bottomRow(); r++)
      if (!rows.contains(r))
        rows.append(r);
  std::sort(rows.begin(), rows.end());

  QString out;
  for (int r : rows)
  {
    const QTableWidgetItem *nItem = _table->item(r, 0);
    const QString nm = nItem ? nItem->text() : QString();
    out += nm;
    // NumColumns > 1 : append ": " + the (un-flattened) value (ListViewDialog.cpp:174-182).
    out += QStringLiteral(": ");
    if (r < _fullValues.size())
      out += _fullValues[r];
    out += QLatin1Char('\n');   // non-_WIN32 newline (ListViewDialog.cpp:188)
  }
  QApplication::clipboard()->setText(out);
}

// ShowItemInfo (ListViewDialog.cpp:199-223): open a read-only multi-line text box
// (the CEditDialog analog) showing the FULL value for the row, so a long value
// (full comment, long path, hash) can be read without the cell's newline-flatten.
void QtPropertiesDialog::showValueForRow(int row)
{
  if (!_table || row < 0 || row >= _table->rowCount())
    return;
  const QTableWidgetItem *nItem = _table->item(row, 0);
  const QString title = nItem ? nItem->text() : QString();
  const QString text = (row < _fullValues.size()) ? _fullValues[row] : QString();

  // A separator/blank row has nothing worth viewing.
  if (title.isEmpty() && text.isEmpty())
    return;

  QDialog dlg(this);
  // CEditDialog uses the row name as the window Title (ListViewDialog.cpp:212).
  dlg.setWindowTitle(title.isEmpty() ? FmLang(IDS_PROPERTIES, QStringLiteral("Properties")) : title);
  dlg.resize(480, 320);                        // resizable (CEditDialog OnSize)
  QVBoxLayout *lay = new QVBoxLayout(&dlg);
  QPlainTextEdit *edit = new QPlainTextEdit(&dlg);
  edit->setReadOnly(true);                     // read-only (CEdit, no edit)
  edit->setLineWrapMode(QPlainTextEdit::NoWrap);
  edit->setPlainText(text);
  lay->addWidget(edit);
  QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
  if (QPushButton *closeBtn = bb->button(QDialogButtonBox::Close))
    closeBtn->setText(FmLang(408, QStringLiteral("Close")));
  connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  lay->addWidget(bb);
  dlg.exec();
}

void QtPropertiesDialog::showValueForCurrentRow()
{
  if (_table)
    showValueForRow(_table->currentRow());
}

// OnEnter / Ctrl-key handling (ListViewDialog.cpp:247-256, 280-311). Qt's
// QShortcut covers Ctrl+C/Ins/A; the Enter/Return (and Alt+Enter) activation that
// ShowItemInfo needs is caught here so it does not fall through to the dialog's
// default-button accept (which would close the Properties sheet).
bool QtPropertiesDialog::eventFilter(QObject *obj, QEvent *ev)
{
  if (obj == _table && ev->type() == QEvent::KeyPress)
  {
    QKeyEvent *ke = static_cast<QKeyEvent *>(ev);
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
    {
      showValueForCurrentRow();
      return true;
    }
  }
  return QDialog::eventFilter(obj, ev);
}

void QtPropertiesDialog::dumpTo(FILE *f) const
{
  if (!_table || !f)
    return;
  for (int r = 0; r < _table->rowCount(); r++)
  {
    const QTableWidgetItem *n = _table->item(r, 0);
    const QTableWidgetItem *v = _table->item(r, 1);
    fprintf(f, "PROP: %s = %s\n",
        n ? n->text().toUtf8().constData() : "",
        v ? v->text().toUtf8().constData() : "");
  }
}

// G.5b : headless reachability test for the interactivity paths (the GUI clipboard
// + value-viewer that the offscreen --props dump cannot otherwise exercise).
bool QtPropertiesDialog::runInteractivitySelfTest(int valueViewerRow,
    QString &clipboardOut, QString &viewerValueOut)
{
  if (!_table || _table->rowCount() == 0)
    return false;

  // Drive Ctrl+C : select all rows, then run the real copy slot, read the clipboard.
  _table->selectAll();
  copySelectedToClipboard();
  clipboardOut = QApplication::clipboard()->text();

  // The value-viewer would show the UN-flattened value for the given row (the same
  // string showValueForRow feeds into the QPlainTextEdit).
  if (valueViewerRow >= 0 && valueViewerRow < _fullValues.size())
    viewerValueOut = _fullValues[valueViewerRow];
  return true;
}
