// QtOptionsDialog.h
// ----------------------------------------------------------------------------
// B.8 : the FM Options dialog — the Qt analogue of FileManager/OptionsDialog.cpp
// (a 6-page property sheet), simplified to the Linux-relevant tabs in a
// QTabWidget:
//
//   * Settings tab   (SettingsPage.cpp) : ShowGrid, FullRow (full-row select),
//                      AlternatingColors, SingleClick, ShowDots — view tweaks
//                      pushed live to BOTH panels (g_App.SetListSettings mirror).
//   * Folders tab    (FoldersPage.cpp)  : working/temp dir {System | Specified}
//                      + a path edit (overlays the NWorkDir shim, drives the
//                      in-place-archive-rewrite temp dir).
//   * Editor tab     (EditPage.cpp)     : Editor command + Diff command.
//   * Delete tab     (new)              : "Move deleted files to the Trash
//                      (Shift+Del = delete permanently)".
//
// DEFERRED (packaging phase): System (file associations / SystemPage.cpp),
// Menu (shell context menu / MenuPage.cpp), ShowSystemMenu, LargePages, MemUse,
// Lang — all Windows-shell-tied or i18n (P.2). See QtFmSettings.h.
//
// Caller sets .Settings, calls fillFromState(), exec()s, and on Accepted reads
// .Settings back (mirrors QtSplitDialog's Public-field handshake).
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_OPTIONS_DIALOG_H
#define ZIP7_INC_QT_OPTIONS_DIALOG_H

#include <QtWidgets/QDialog>

#include "QtFmSettings.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

class QtOptionsDialog Z7_final : public QDialog
{
  Q_OBJECT
public:
  explicit QtOptionsDialog(QWidget *parent = nullptr);

  // Set by the caller before exec(); read back on Accepted.
  QtFmSettings::CInfo Settings;

  // Populate the widgets from Settings (call after assigning Settings).
  void fillFromState();

private Q_SLOTS:
  void onAccept();        // write widgets -> Settings, then accept()
  void onBrowseWorkDir();
  void onBrowseEditor();
  void onWorkDirModeToggled();

private:
  // Settings tab
  QCheckBox *_showGrid;
  QCheckBox *_fullRow;
  QCheckBox *_altColors;
  QCheckBox *_singleClick;
  QCheckBox *_showDots;
  QCheckBox *_showRealIcons;   // G.9d : CFmSettings::ShowRealFileIcons (default ON)

  // G.9a : "Maximum amount of RAM memory usage allowed to unpack archives"
  // (SettingsPage IDX_SETTINGS_MEM_SET + spin). Persisted to NExtractQt::
  // Save_LimitGB / read by NExtractQt::Read_LimitGB (the [Extraction] MemLimit
  // store the extract callback's RequestMemoryUse consults). Unticked => no
  // configured limit ((UInt32)-1), the engine default.
  QCheckBox *_memSet;
  QSpinBox  *_memSpin;

  // Folders tab
  QRadioButton *_workSystem;
  QRadioButton *_workCurrent;     // G.9c : NWorkDir::NMode::kCurrent (archive's dir)
  QRadioButton *_workSpecified;
  QLineEdit    *_workPath;

  // Editor tab
  QLineEdit *_editorPath;
  QLineEdit *_diffCommand;

  // Delete tab
  QCheckBox *_deleteToTrash;

  // P.2 Language tab : a combo over <langdir>/*.txt (faithful LangPage mirror).
  // itemData carries the basename to persist into [Options] Lang ("" = English).
  QComboBox *_langCombo;
};

#endif
