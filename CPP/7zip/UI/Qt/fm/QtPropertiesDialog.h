// QtPropertiesDialog.h
// ----------------------------------------------------------------------------
// B.7a : the Properties sheet — the Qt analogue of CPanel::Properties()
// (FileManager/PanelMenu.cpp:172). The original builds a 2-column name/value
// list in a CListViewDialog by enumerating the focused folder's OWN property
// descriptors (GetNumberOfProperties / GetPropertyInfo / GetProperty), exactly
// the same triad QtFolderModel uses for its columns — but over the FULL property
// set (not the column subset, which drops kpidIsDir). Each value string is built
// by ConvertPropertyToString2(val, prop, propID, 9) (the `9` = nanosecond time
// precision, PanelMenu.cpp:136), and each name via GetNameOfProperty (kpid ->
// LangString(1000+propID) -> BSTR name -> numeric), which we mirror with the
// transcribed English kpid->name table.
//
// Works uniformly for FS rows (CFSFolder) and archive rows (CAgentFolder) because
// both expose GetProperty through currentFolder().
//
// The IFolderProperties / IGetFolderArcProps "archive info" block (Path, folder
// props, and the multi-level IFolderArcProps props + kSpecProps + the NonOpen
// error level, PanelMenu.cpp:305-417) IS appended after the item props when the
// folder is an archive (CAgentFolder).
//
// G.5a/G.5b additions:
//   - the _folderRawProps raw-prop block (PanelMenu.cpp:214-256): QI the folder
//     for IArchiveGetRawProps and render each non-empty raw prop as hex (CRC /
//     Checksum upper-case, others lower-case). The Windows-only kpidNtSecure case
//     (ConvertNtSecureToString / NTFS ACL) is SKIPPED — not present on Linux.
//   - the multi-select summary branch (PanelMenu.cpp:260-295): when >1 item is
//     selected, show the aggregate (count + total Size + total Packed Size + the
//     Files/Folders counts) instead of single-item props.
//   - interactivity (CListViewDialog / CEditDialog, ListViewDialog.cpp:164-256):
//     Ctrl+C / Ctrl+Ins copies the selected "Name: Value" rows to the clipboard,
//     Ctrl+A selects all, and double-click / Enter on a row opens a read-only
//     multi-line value viewer (the CEditDialog analog).
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_PROPERTIES_DIALOG_H
#define ZIP7_INC_QT_FM_PROPERTIES_DIALOG_H

#include <QtWidgets/QDialog>
#include <QtCore/QVector>
#include <cstdio>

#include "../../../../Common/MyString.h"
#include "../../../../Common/MyCom.h"            // G.3c : CMyComBSTR (addPropRow)
#include "../../../../Windows/PropVariant.h"     // G.3c : NWindows::NCOM::CPropVariant
#include "../../FileManager/IFolder.h"

class QTableWidget;

class QtPropertiesDialog Z7_final : public QDialog
{
  Q_OBJECT
public:
  explicit QtPropertiesDialog(QWidget *parent = nullptr);

  // Caller sets these (mirroring the OnInit field-copy-in pattern of the other
  // Qt dialogs), then calls fill().
  IFolderFolder *Folder = nullptr;   // panel->currentFolder()
  UInt32 Index = 0;                  // the realIndex (selectedSourceRows().first())

  // G.5a : the FULL set of selected engine realIndices. When this has >1 entry
  // the multi-select aggregate summary is shown (PanelMenu.cpp:260-295) instead
  // of the single-item props; when it has <=1 entry the single-item path (Index)
  // is used. Caller sets this before fill().
  QVector<UInt32> Indices;

  // Run the GetProperty loop (PanelMenu.cpp:194-211) and populate the table.
  void fill();

  // Headless test hook : print each populated "PROP: <name> = <value>" row to f
  // (used by main_fm.cpp's --props=, asserted against `7zz l -slt`).
  void dumpTo(FILE *f) const;

  // G.5b headless test hook : exercise the two interactivity paths without a live
  // mouse. Selects ALL rows, runs copySelectedToClipboard(), and reports the
  // resulting clipboard text (the "Name: Value" form) plus the UN-flattened value
  // the value viewer (showValueForRow) would display for `valueViewerRow`. Returns
  // false if there are no rows. Used by main_fm.cpp's --props-interact marker test.
  bool runInteractivitySelfTest(int valueViewerRow,
      QString &clipboardOut, QString &viewerValueOut);

private slots:
  // G.5b : interactivity (ListViewDialog.cpp:164-256). Copy the selected rows as
  // "Name: Value" text to the clipboard; open the value viewer for a row.
  void copySelectedToClipboard();          // Ctrl+C / Ctrl+Ins
  void showValueForRow(int row);           // double-click / Enter -> CEditDialog
  void showValueForCurrentRow();

private:
  void addRow(const QString &name, const QString &value);

  // G.3c : mirror of PanelMenu.cpp's AddPropertyString (:110-148) — format the
  // value (with the kpidErrorFlags/kpidWarningFlags decode), skip when empty, and
  // for kpidErrorType emit the extra "Open WARNING:" pair before the prop row.
  void addPropRow(PROPID propID, const CMyComBSTR &name,
      const NWindows::NCOM::CPropVariant &prop);

  // G.5a : the multi-select aggregate summary (PanelMenu.cpp:260-295).
  void fillMultiSelectSummary();

  // G.5b : the _folderRawProps raw-prop block (PanelMenu.cpp:214-256).
  void addRawProps(UInt32 index);

  bool eventFilter(QObject *obj, QEvent *ev) Z7_override;  // Ctrl+A/C/Ins, Enter

  QTableWidget *_table;              // 2 cols: Property | Value, read-only
  // G.5b : the UN-flattened value for each table row (addRow flattens newlines
  // for the cell; the value viewer needs the original). Parallel to the rows.
  QVector<QString> _fullValues;
};

#endif
