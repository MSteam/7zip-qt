// QtFolderModel.h
// ----------------------------------------------------------------------------
// Milestone B.1 : a QAbstractItemModel that wraps the 7-Zip engine's
// IFolderFolder (the archive-browsing seam), so a Qt view can show an open
// archive exactly the way the original File Manager's CPanel shows it.
//
// This is a faithful Qt translation of the relevant slice of CPanel
// (CPP/7zip/UI/FileManager/Panel*.cpp):
//
//   * columns          <- CPanel::InitColumns   (PanelItems.cpp)
//        GetNumberOfProperties / GetPropertyInfo, skip kpidIsDir, hide the same
//        property set CPanel hides (GetColumnVisible), right-align numeric
//        properties (GetColumnAlign).
//   * header text       <- GetPropertyInfo's BSTR name (GetNameOfProperty).
//   * row count          <- GetNumberOfItems (after LoadItems()).
//   * cell text           <- CPanel::SetItemText (PanelListNotify.cpp): size
//        props via ConvertSizeToString, everything else via the engine's
//        canonical ConvertPropertyToString2 (PropIDUtils, sevenzip_engine).
//   * navigation         <- CPanel::OpenFolder / OpenParentFolder
//        (PanelFolderChange.cpp): swap the held _folder on BindToFolder /
//        BindToParentFolder, then reset the model.
//
// The model is "flat" (table-like): index() has no parent nesting, because a
// CPanel shows the CURRENT folder's items as rows, not a recursive tree. We
// still implement it as a QAbstractItemModel (per the milestone spec) with a
// single invalid root.
//
// The model does NOT own the archive open: a controller opens the archive with
// CAgent and calls setRootFolder(rootFolder). (Mirrors how CPanel is handed an
// IFolderFolder it did not create.)
//
// THREADING: B.1 opens/lists synchronously on the GUI thread (archives list
// fast). Large-archive open/LoadItems should later move to a worker thread; the
// model API (setRootFolder / enterItem / goParent) is the seam where that would
// hook in.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_FOLDER_MODEL_H
#define ZIP7_INC_QT_FM_FOLDER_MODEL_H

#include <QtCore/QAbstractItemModel>
#include <QtCore/QVector>
#include <QtCore/QPair>     // G.4b : saved (PROPID, visible) restore seam
#include <QtCore/QHash>     // G.4b : per-PROPID visibility overrides
#include <QtGui/QIcon>
#include <QtGui/QBrush>   // P.3 : deleted-item ForegroundRole
#include <QtGui/QColor>

// 7-Zip COM idiom.
#include "../../../../Common/MyWindows.h"
#include "../../../../Common/MyCom.h"
#include "../../../../Common/MyString.h"
#include "../../../../Windows/PropVariant.h"   // G.4m : NCOM::CPropVariant (raw sort key)
#include "../../FileManager/IFolder.h"

class QtFolderModel Z7_final : public QAbstractItemModel
{
  Q_OBJECT
public:
  explicit QtFolderModel(QObject *parent = nullptr);
  ~QtFolderModel() override;

  // --- controller entry point -------------------------------------------
  // Hand the model an already-opened root IFolderFolder (from a CAgent the
  // controller created). Mirrors CPanel::SetNewFolder on the root folder.
  void setRootFolder(IFolderFolder *rootFolder);

  // --- QAbstractItemModel ------------------------------------------------
  QModelIndex index(int row, int column,
      const QModelIndex &parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &child) const override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
      int role = Qt::DisplayRole) const override;
  // G.4l : inline (in-place) rename. flags() marks the Name column editable on a
  // writable folder (a CFSFolder, or an updatable-archive CAgentFolder — the same
  // gate CPanel::OnBeginLabelEdit applies via !IsThereReadOnlyFolder()). setData()
  // on the Name column / EditRole validates the typed name (IsCorrectFsName +
  // CorrectFsPath, mirroring OnEndLabelEdit) and runs IFolderOperations::Rename —
  // the SAME live B.5a/FS path QtFileManagerWindow::doRename uses.
  Qt::ItemFlags flags(const QModelIndex &index) const override;
  // Drag-OUT fix: the view passes this into startDrag(); offer Copy+Move so the
  // FS-panel move-drag is live (archive drag-OUT is narrowed to Copy downstream).
  Qt::DropActions supportedDragActions() const override;
  bool setData(const QModelIndex &index, const QVariant &value,
      int role = Qt::EditRole) override;

  // --- navigation (mirrors CPanel) --------------------------------------
  // Enter row's folder: BindToFolder(row) -> swap _folder -> reset model.
  // Returns false (and does nothing) if the row is a leaf file. In that case it
  // also emits leafActivated(row) so a controller can decide what to do with the
  // leaf (e.g. CPanel::OpenItem opens an archive file as a folder - the seamless
  // filesystem -> archive transition wired by the FS browser controller in B.2).
  // G.4f : a MODEL row. When ShowDots synthesizes the ".." row (row 0), enterItem
  // on it does NOT bind a folder — it emits parentActivated() so the panel runs its
  // own Up (which can also exit an archive root, where goParent alone returns false).
  bool enterItem(int row);
  // Go to the parent folder: BindToParentFolder. Returns false at the root.
  bool goParent();
  bool isRoot() const;

  // === G.4f : ShowDots ".." parent pseudo-row ===========================
  // The "Show '..' item" Options toggle (QtFmSettings::ShowDots). When ON and a
  // parent exists, QtFolderModel synthesizes a single ".." row at MODEL row 0 that
  // ALWAYS sorts first; rowCount() is _numItems + 1. It is excluded from every
  // operation/selection (selectedSourceRows skips it) and is not editable. Faithful
  // to CPanel inserting a kParentIndex ".." item at the top when (_showDots &&
  // !IsRootFolder()) (PanelItems.cpp:578-647), which sorts first (PanelSort.cpp:183).
  //
  //   setShowDots(on)   : record the option flag and re-list (a model reset that
  //     adds/removes the ".." row). The panel calls this from applySettings live.
  //   setHasParent(on)  : the panel tells the model whether Up is possible at the
  //     CURRENT folder — true for an FS non-root dir, or anywhere inside an archive
  //     (including the archive root, where Up exits the archive via the panel's
  //     parent-stack). The FS filesystem root has no parent, so ".." is suppressed
  //     there (mirrors !IsRootFolder()). Re-lists when the effective state changes.
  //   showDotsActive()  : the EFFECTIVE state — _showDots && _hasParent. When true
  //     the ".." row exists at model row 0.
  void setShowDots(bool on);
  void setHasParent(bool on);
  bool showDotsActive() const { return _showDots && _hasParent; }

  // G.4f : the ONE pair of helpers every consumer routes row<->realIndex through.
  // modelRowToRealIndex(row) : the engine item index for a MODEL row. Returns
  //   kParentRow (-1) for the synthetic ".." row, else row - (showDotsActive()?1:0).
  // realIndexToModelRow(realIndex) : the MODEL row for an engine item index (the
  //   inverse), shifting down by 1 when ".." is shown. Centralizing these is the
  //   whole point of the task: NO consumer adds its own +1/-1.
  static const int kParentRow = -1;   // modelRowToRealIndex(..) for the ".." row
  int modelRowToRealIndex(int row) const;
  int realIndexToModelRow(int realIndex) const;
  // G.4f : is this MODEL row the synthetic ".." parent row?
  bool isParentRow(int row) const;
  // G.4f : the number of REAL engine items (GetNumberOfItems), EXCLUDING the ".."
  // pseudo-row — the count the status bar's "N / total" total uses (CPanel uses
  // _selectedStatusVector.Size() == numItems, not the list-view item count which
  // includes ".."). rowCount() includes ".."; this never does.
  int realItemCount() const { return _numItems; }

  // --- item queries (mirror CPanel::IsItem_Folder / GetItemName) --------
  bool isFolder(int row) const;
  UString itemName(int row) const;            // kpidName (MODEL row)
  // G.4f : kpidName by ENGINE item index (NO ".." row translation). The original's
  // GetItemName(realIndex) is likewise keyed by the engine index. Used where a caller
  // already holds an engine realIndex (e.g. a sort/proxy or a mapping self-check).
  UString itemNameByRealIndex(int realIndex) const;
  // B.6 : the row's file extension (after the last '.'), lower-cased, no dot;
  // empty if none. Used as the archive-.ico fallback inside fileIconForRow().
  QString extensionOf(int row) const;
  // Per-type file icon (MIME database + icon theme; SHGetFileInfo analog). Cached
  // by MIME name in _iconCache. Used by data()/DecorationRole when ShowRealFileIcons.
  QIcon fileIconForRow(int row) const;
  UInt64 itemSize(int row, bool &defined) const; // raw kpidSize (for --dump diff)
  bool itemSize_IsDefined(int row) const;

  // G.4i : the status bar's per-FOCUSED-item fields, mirroring CPanel::Refresh_StatusBar
  // (PanelListNotify.cpp:800-828). focusedSizeString = ConvertSizeToString(GetItemSize(row))
  // (the SAME grouped formatter used for size columns); focusedMTimeString =
  // ConvertPropertyToShortString2(kpidMTime) — empty if the row has no MTime.
  QString focusedSizeString(int row) const;
  QString focusedMTimeString(int row) const;
  // Same grouped formatter applied to an explicit byte count (the selection-total
  // size, Refresh_StatusBar part 1, PanelListNotify.cpp:794).
  QString sizeStringForBytes(UInt64 bytes) const;

  // G.4j : the source-model row whose kpidName matches `name` (case-sensitive
  // compare on the engine's name), or -1 if none. Mirrors how RefreshListCtrl
  // locates CSelectedState::FocusedName after a folder change (PanelItems.cpp).
  int rowForName(const UString &name) const;
  // The current folder's path (kpidPath via GetFolderProperty), the way
  // CPanel's GetFolderPath() reads it.
  UString currentPath() const;

  // G.4l : is the current folder writable (does it expose IFolderOperations)?
  // A CFSFolder and an updatable-archive CAgentFolder do; a read-only archive
  // folder / in-memory stream archive does not. This is the model-side equivalent
  // of CPanel::IsThereReadOnlyFolder() being false (OnBeginLabelEdit's gate), and
  // drives both flags() (editable) and which folders allow the inline rename.
  bool isFolderWritable() const;

  // G.4l : the rename core, shared by setData() (inline edit) and reachable for a
  // headless test. Validates `newName` exactly as CPanel::OnEndLabelEdit does
  // (IsCorrectFsName rejects "."/".." ; a path separator in the leaf name is
  // rejected too, matching the B.5a RenameItem fail-safe), no-ops on an empty or
  // unchanged name, then calls IFolderOperations::Rename(row, newName) — the live
  // write path. Returns S_OK on a real rename, S_FALSE on a no-op (empty/unchanged),
  // or the engine HRESULT / E_INVALIDARG on rejection. Does NOT itself refresh the
  // model: the caller (QtPanel) applies the B.5b suspend/refresh discipline for an
  // archive (Close+ReOpen) rename. `renamed` is set true only when Rename ran S_OK.
  HRESULT renameRow(int row, const UString &newName, bool &renamed);

  // G.5c : the SHARED new-FS-name validator factored out of renameRow's body, so
  // the create-folder / create-file paths (QtFileManagerWindow) validate the typed
  // name with the SAME rule the inline rename uses, mirroring CPanel's IsCorrectFsName
  // (PanelOperations.cpp:274 — the leaf may not be "."/"..") plus the leaf path-separator
  // reject (the B.5a fail-safe; CorrectFsPath is the identity on Linux). Returns true
  // when `name` is an acceptable leaf name, false when it must be rejected with
  // E_INVALIDARG (exactly OnEndLabelEdit / CreateFolder's MessageBox_Error_HRESULT path).
  static bool IsCorrectNewFsName(const UString &name);

  // The IFolderFolder currently shown as rows (the model keeps a strong ref).
  // A controller uses this to capture a stable handle to the current folder
  // before swapping the root (e.g. to remember the filesystem folder to return
  // to when leaving an archive opened from it). Returns nullptr if none.
  IFolderFolder *currentFolder() const { return _folder; }

  // G.4c : Flat View (recursive flat listing). The Qt mirror of the engine seam
  // CPanel uses (PanelItems.cpp:526-529): QI the bound folder for
  // IFolderSetFlatMode and, when supported, SetFlatMode(_flatMode) is applied to
  // it BEFORE every LoadItems() — so a flat folder enumerates ALL descendants in
  // one list (CFSFolder::LoadSubItems recurses; CAgentFolder::LoadFolder walks the
  // proxy tree) and reports each item's relative subfolder path via kpidPrefix.
  //   setFlatMode(on) : record the flag and re-list (a model reset). The reset
  //     rebuilds columns too, because the folder reports the extra kpidPrefix
  //     property only in flat mode (CAgentFolder::GetNumberOfProperties:1152-1153
  //     drops the kpidPrefix column when !_flatMode), and forces kpidPrefix
  //     visible so the user sees each item's path. No-op (and returns false) when
  //     the current folder does not expose IFolderSetFlatMode.
  //   isFlatMode()  : the current flag.
  //   flatModeSupported() : does the bound folder expose IFolderSetFlatMode? Both
  //     CFSFolder and CAgentFolder do; a folder type that does not would gray the
  //     menu (faithful: the original offers flat mode only where supported).
  bool setFlatMode(bool on);
  bool isFlatMode() const { return _flatMode; }
  bool flatModeSupported() const;

  // The PROPID shown by a given visible column (column -> PROPID), so a
  // controller / self-check can ask for a specific property's text.
  PROPID columnPropID(int column) const;
  int columnForPropID(PROPID propID) const;

  // G.4b : the displayed name of a column (its header text), so the header
  // context menu can label each checkable action.
  QString columnName(int column) const;

  // G.4b : per-column visibility (the IsVisible concept CPanel's CPropColumn
  // carries). rebuildColumns() seeds it from the GetColumnVisible rule — for a
  // FILESYSTEM folder it default-HIDES the trimmed FS set (ATime/ChangeTime/
  // Attrib/PackSize/INode/Links/NtReparse), an ARCHIVE folder shows every column
  // — and the header context menu / persistence layer toggles it. The QTreeView
  // mirrors the flag via QHeaderView::setSectionHidden (the column stays in the
  // model so its PROPID/sort key are unchanged; only the section is hidden, the
  // Qt analogue of CPanel AddColumn/DeleteColumn from _visibleColumns).
  bool isColumnVisible(int column) const;
  bool isColumnVisibleByPropID(PROPID propID) const;
  // Set a column's visibility by PROPID. The Name column (kpidName) is LOCKED
  // visible (CPanel grays/MF_GRAYED item 0, ShowColumnsContextMenu); a request to
  // hide it is ignored and false is returned. Returns true when the flag actually
  // changed. Emits columnVisibilityChanged() so the panel re-applies the hidden
  // sections. Does NOT itself reset the sort (the panel owns the sort-column
  // reset, mirroring ShowColumnsContextMenu de-selecting the active sort column).
  bool setColumnVisibleByPropID(PROPID propID, bool visible);
  // G.4b : seed the visibility set from a saved map (PROPID -> visible) — the seam
  // a persistence layer (next task) restores into. Unknown PROPIDs are ignored;
  // kpidName is forced visible. Applied on top of the GetColumnVisible defaults
  // after a rebuild. Provided as a vector of (PROPID, bool) pairs to avoid pulling
  // QHash into the header.
  void setVisibleColumnsFromSaved(const QVector<QPair<PROPID, bool>> &saved);
  // G.4b : true when the CURRENT folder is a filesystem folder (kpidType ==
  // "FSFolder" / "AltStreamsFolder"), the gate the GetColumnVisible default-hide
  // rule keys on. Mirrors CPanel::IsFSFolder() || IsAltStreamsFolder().
  bool isFsFolder() const;

  // G.4a : the current folder's kpidType BSTR (e.g. "FSFolder", "7-Zip.zip"), the
  // VERBATIM analogue of CPanel::GetFolderTypeID() (Panel.cpp:818) — the per-list-
  // type key under which view settings (column layout, sort, view mode) persist
  // (CListViewInfo::Save/Read take exactly this `id`). Empty if no folder / no type.
  UString folderTypeId() const;

  // G.4a : drop every per-PROPID visibility override (the _savedVisible map). The
  // panel calls this when the list TYPE changes so the next type's persisted
  // visibility set is applied fresh — mirroring CPanel::InitColumns reading
  // _listViewInfo per-type rather than carrying one type's toggles into another.
  // Does NOT re-emit / rebuild on its own; the caller follows with
  // setVisibleColumnsFromSaved() (or a rebuild) for the new type.
  void clearSavedVisible() { _savedVisible.clear(); }

  // G.4m : the row's RAW PropVariant for `propID` (the engine's GetProperty,
  // unformatted), so the sort proxy can compare time/numeric columns by VALUE —
  // exactly as CPanel::CompareItems2 (PanelSort.cpp:168-171) reads both items'
  // prop1/prop2 and compares the variant by its native VARTYPE (CompareFileTime
  // for VT_FILETIME, numeric for VT_UI8/…, string for VT_BSTR) rather than by the
  // formatted column text. Returns the GetProperty HRESULT; on failure (or while
  // suspended / out of range) `prop` is left VT_EMPTY and a non-S_OK is returned.
  HRESULT getRawProperty(int row, PROPID propID,
      NWindows::NCOM::CPropVariant &prop) const;

  // P.3 : the _markDeletedItems-equivalent gate for the deleted-in-archive red
  // text (CPanel::OnCustomDraw, PanelListNotify.cpp:724-728). In the original FM
  // this member defaults to true and is never toggled by any option, so it is
  // effectively always-on; we mirror that (default true) but expose a setter so a
  // headless test can prove the gate (toggling it off must drop every red row)
  // without needing a real kpidIsDeleted-bearing disk image.
  void setMarkDeletedItems(bool on) { _markDeletedItems = on; }
  bool markDeletedItems() const { return _markDeletedItems; }

  // G.9d : the CFmSettings::ShowRealFileIcons toggle (SettingsPage IDX_SETTINGS_
  // SHOW_REAL_FILE_ICONS). When ON (default, the prior behavior) a file shows its
  // per-format archive icon; when OFF, data()/DecorationRole returns the generic
  // file icon for every file (a plainer/faster list). Folders keep the folder icon
  // either way (the original's ShowRealFileIcons gates only the per-FILE system icon
  // lookup, never the folder glyph). The panel pushes this via applySettings; a live
  // toggle re-paints because we emit dataChanged over the Name column.
  void setShowRealIcons(bool on);
  bool showRealIcons() const { return _showRealIcons; }
  // The palette-aware red used for a deleted-in-archive row's text. Faithful to
  // the original RGB(255,0,0) on a light palette; lightened on a dark palette so
  // it stays readable. Exposed so a test can assert the mapping/hue directly.
  QColor deletedTextColor() const;

  // B.5b crash fix : during an in-place archive modify (Delete/CopyFrom) the
  // worker thread does CommonUpdateOperation Close()+ReOpen() on the SAME
  // CAgentFolder, rebuilding its internal _proxy. Meanwhile the GUI thread is in
  // the modal progress loop, so a QTreeView relayout can call data()/rowCount()
  // and hit GetProperty() on the half-rebuilt folder -> SIGSEGV (a real data race
  // between the two threads on the same agent). The window calls setSuspended(true)
  // BEFORE running the worker so the view drops to 0 rows and issues no per-item
  // GetProperty during the op; the subsequent refresh() -> setRootFolder() clears
  // it and re-lists the reopened archive. (Mirrors the Win32 FM disabling list
  // redraw during the update.)
  void setSuspended(bool on);

Q_SIGNALS:
  // Emitted by enterItem(row) when the row is a LEAF (a file, not a folder).
  // CPanel turns a leaf double-click into OpenItem(); the B.2 FS controller uses
  // this to attempt a seamless filesystem -> archive open. (Archive folders never
  // have leaf "files" you can enter; only the FS view raises this in practice.)
  // `row` is a MODEL row (the controller maps it to a realIndex via the model).
  void leafActivated(int row);

  // G.4f : emitted by enterItem() when the activated row is the synthetic ".."
  // parent pseudo-row. CPanel maps a kParentIndex activation to OpenParentFolder
  // (PanelFolderChange.cpp:1060-1063); here the panel connects this to its onUp()
  // so ".." also exits an archive root via the parent-stack (where the model's own
  // goParent() returns false).
  void parentActivated();

  // G.4b : a column's visibility changed (setColumnVisibleByPropID), OR the
  // column set / its default visibility was rebuilt (rebuildColumns after a folder
  // change). The panel connects this to re-apply QHeaderView::setSectionHidden so
  // the view's hidden sections always match the model's IsVisible flags. (Emitted
  // OUTSIDE begin/endResetModel so the header geometry is valid when the panel
  // reads it.)
  void columnVisibilityChanged();

  // G.4l : emitted by setData() after a SUCCESSFUL inline rename (Rename ran S_OK).
  // The panel uses it to re-list the folder: for an ARCHIVE rename the engine did
  // Close()+ReOpen() on the CAgentFolder, so the panel must re-bind from the
  // reopened root (the B.5b refresh() path); for a plain FS folder it re-scans
  // disk. setData() itself does not refresh, so the post-rename state stays
  // consistent only via this signal -> QtPanel::refresh().
  void itemRenamed();

private:
  struct CColumn
  {
    PROPID ID;
    VARTYPE Type;
    QString Name;
    bool RightAlign;
    // G.4b : CPropColumn::IsVisible. A hidden column stays in the model (its
    // PROPID still indexes data()/sort) but the view hides its section. Seeded
    // by rebuildColumns() from GetColumnVisible (FS folders default-hide the
    // trimmed set; archives show all), toggled by the header context menu.
    bool Visible = true;
  };

  void rebuildColumns();     // CPanel::InitColumns
  void reloadItems();        // LoadItems + GetNumberOfItems
  // G.4c : QI the bound folder for IFolderSetFlatMode and push _flatMode onto it
  // (no-op if unsupported). Called by rebuildColumns()/reloadItems() before the
  // folder enumerates, mirroring CPanel::RefreshListCtrl (PanelItems.cpp:526).
  void applyFlatModeToFolder();

  // The engine seam: the folder currently shown as rows.
  // (CPanel also QueryInterfaces IFolderGetItemName as a fast path for
  // name/size, but the canonical GetProperty path is sufficient and equally
  // faithful for B.1, so we read every cell through GetProperty.)
  CMyComPtr<IFolderFolder> _folder;

  QVector<CColumn> _columns;   // all columns (visible flag per CColumn), kpidName first
  int _numItems;

  // G.4b : per-PROPID visibility OVERRIDES the user (or a restored persistence
  // layer) has set, applied on TOP of the GetColumnVisible defaults after every
  // rebuildColumns(). Without this a folder change (which re-seeds _columns from
  // defaults) would discard the user's column toggles; CPanel keeps the same state
  // in _listViewInfo. kpidName is never stored here (it is always-visible). This
  // is the seam the persistence task saves/restores (setVisibleColumnsFromSaved /
  // the per-PROPID getter feed it). A PROPID absent from the map keeps its default.
  QHash<PROPID, bool> _savedVisible;

  // Folder-vs-file icons (QFileIconProvider-equivalent via theme icons).
  // Folders get a folder icon, files a generic fallback; per-type file icons
  // come from fileIconForRow() (MIME database + icon theme).
  QIcon _dirIcon;
  QIcon _fileIcon;
  // Per-type file-icon cache, keyed by MIME name (e.g. "application/pdf").
  // mutable: populated lazily from the const data()/fileIconForRow() path.
  mutable QHash<QString, QIcon> _iconCache;

  // P.3 : deleted-in-archive red-text gate (mirrors CPanel::_markDeletedItems,
  // Panel.h:602, default true / never toggled).
  bool _markDeletedItems = true;

  // G.9d : ShowRealFileIcons gate (CFmSettings::ShowRealFileIcons). Default true
  // (the prior per-format-icon behavior); when false, files show the generic icon.
  bool _showRealIcons = true;

  // B.5b crash fix : see setSuspended(). When true, rowCount()->0 and
  // data()->QVariant() so the view issues no GetProperty while the in-place
  // archive update rebinds the CAgentFolder on the worker thread.
  bool _suspended = false;

  // G.4c : Flat View flag (mirrors CPanel::_flatMode, Panel.h:334). Applied to the
  // bound folder via IFolderSetFlatMode::SetFlatMode in reloadItems() before
  // LoadItems() — exactly where CPanel::RefreshListCtrl applies it. The panel owns
  // the per-context defaults (_flatModeForArc/_flatModeForDisk) and drives this via
  // setFlatMode(); the model just carries the live flag so it survives a folder
  // change (the panel re-applies it on each bind, like RefreshListCtrl).
  bool _flatMode = false;

  // G.4f : ShowDots state. _showDots is the Options flag (CFmSettings::ShowDots);
  // _hasParent is the per-folder "Up is possible" flag the panel sets. The ".."
  // pseudo-row exists iff showDotsActive() == (_showDots && _hasParent). Mirrors
  // CPanel's `_showDots && !IsRootFolder()` gate (PanelItems.cpp:578).
  bool _showDots = false;
  bool _hasParent = false;
  // The ".." row's icon (an up/parent-folder icon). Falls back to _dirIcon.
  QIcon _parentIcon;
};

#endif
