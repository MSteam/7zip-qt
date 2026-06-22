// QtOptionsDialog.cpp
// ----------------------------------------------------------------------------
// See QtOptionsDialog.h. Layout/style mirrors QtSplitDialog (QFormLayout +
// QDialogButtonBox(Ok|Cancel) + onAccept(); element-wise QString<->UString
// bridges in an anon namespace). The four tabs map 1:1 onto the Linux-relevant
// subset of the original Options property sheet (Settings/Folders/Editor/Delete).
// ----------------------------------------------------------------------------

#include "QtOptionsDialog.h"
#include "../QtLang.h"                          // P.2 : FmLang(IDS_OPTIONS, ...)
#include "../../FileManager/resource.h"         // P.2 : IDS_OPTIONS=2100
// G.1k : per-tab/label langIDs from the ORIGINAL Options property-sheet pages.
// The tab captions use the page's dialog IDD as their langID (OptionsDialog.cpp:58-60
// LangString_OnlyFromLangFile(page.ID, ...)); the static labels/checkboxes/radios use
// each page's kLangIDs[] (LangSetDlgItems) where the control ID doubles as the langID.
#include "../../FileManager/SettingsPageRes.h"  // IDD_SETTINGS + IDX_SETTINGS_* + IDT_MEM_USAGE_EXTRACT (G.9a)
#include "../../FileManager/FoldersPageRes.h"   // IDD_FOLDERS + IDT/IDR_FOLDERS_* + IDS_FOLDERS_SET_WORK_PATH_TITLE
#include "../../FileManager/EditPageRes.h"      // IDD_EDIT + IDT_EDIT_*
#include "../../FileManager/LangPageRes.h"      // IDD_LANG + IDT_LANG_LANG + IDS_LANG_ENGLISH/NATIVE
#include "../QtExtractSettings.h"               // G.9a : NExtractQt::Read_LimitGB / Save_LimitGB
#include "../../../../Windows/System.h"         // G.9a : NSystem::GetRamSize (RAM-aware spin max)
#include "../../../../Common/Lang.h"            // P.2 : CLang — read each Lang/*.txt's
                                                //       own English/native name (LangPage mirror)

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>      // G.1k : bad-Lang-file error report (LangPage mirror)
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include <QtCore/QDir>
#include <QtCore/QFileInfo>

namespace {

UString QStr_to_UString(const QString &s)
{
  UString u;
  const int n = s.size();
  for (int i = 0; i < n; i++)
    u += (wchar_t)s.at(i).unicode();
  return u;
}

QString UString_to_QStr(const UString &u)
{
  QString s;
  for (unsigned i = 0; i < u.Len(); i++)
    s.append(QChar((char16_t)(unsigned)u[i]));
  return s;
}

} // namespace


QtOptionsDialog::QtOptionsDialog(QWidget *parent)
  : QDialog(parent)
  , _showGrid(nullptr), _fullRow(nullptr), _altColors(nullptr)
  , _singleClick(nullptr), _showDots(nullptr), _showRealIcons(nullptr)
  , _memSet(nullptr), _memSpin(nullptr)
  , _workSystem(nullptr), _workCurrent(nullptr), _workSpecified(nullptr)
  , _workPath(nullptr)
  , _editorPath(nullptr), _diffCommand(nullptr)
  , _deleteToTrash(nullptr)
  , _langCombo(nullptr)
{
  // P.2 : Options title from IDS_OPTIONS=2100 (resource.rc:121 "Options").
  setWindowTitle(FmLang(IDS_OPTIONS, QStringLiteral("Options")));

  QVBoxLayout *top = new QVBoxLayout(this);
  QTabWidget *tabs = new QTabWidget(this);
  top->addWidget(tabs);

  // === Settings tab (SettingsPage.cpp checkboxes, Linux subset) =============
  {
    QWidget *page = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(page);
    // G.1k : SettingsPage checkbox langIDs (SettingsPage.cpp:29-42 kLangIDs[]).
    _showGrid    = new QCheckBox(FmLang(IDX_SETTINGS_SHOW_GRID,
                       QStringLiteral("Show grid lines")), page);
    _fullRow     = new QCheckBox(FmLang(IDX_SETTINGS_FULL_ROW,
                       QStringLiteral("Full row select")), page);
    _altColors   = new QCheckBox(FmLang(IDX_SETTINGS_ALTERNATIVE_SELECTION,
                       QStringLiteral("Alternating row colors")), page);
    _singleClick = new QCheckBox(FmLang(IDX_SETTINGS_SINGLE_CLICK,
                       QStringLiteral("Single-click to open an item")), page);
    _showDots    = new QCheckBox(FmLang(IDX_SETTINGS_SHOW_DOTS,
                       QStringLiteral("Show \"..\" item")), page);
    // G.9d : "Show real file icons" (SettingsPage2.rc IDX_SETTINGS_SHOW_REAL_FILE_ICONS,
    // English "Show real file &icons"). When OFF, QtFolderModel returns a generic icon
    // instead of the per-format one. Default ON (current behavior).
    _showRealIcons = new QCheckBox(FmLang(IDX_SETTINGS_SHOW_REAL_FILE_ICONS,
                       QStringLiteral("Show real file icons")), page);
    v->addWidget(_showGrid);
    v->addWidget(_fullRow);
    v->addWidget(_altColors);
    v->addWidget(_singleClick);
    v->addWidget(_showDots);
    v->addWidget(_showRealIcons);

    // G.9a : "Maximum amount of RAM memory usage allowed to unpack archives:" +
    // the checkbox-gated GB spin (SettingsPage2.rc IDT_MEM_USAGE_EXTRACT label,
    // IDX_SETTINGS_MEM_SET checkbox, IDE/IDC_SETTINGS_MEM spin, IDT_SETTINGS_MEM_GB
    // "GB / N GB (RAM)" suffix). Persisted via NExtractQt::Save_LimitGB, consulted
    // by QtExtractCallback::RequestMemoryUse. The RAM-aware spin max mirrors
    // CSettingsPage::OnInit (SettingsPage.cpp:200-243).
    v->addWidget(new QLabel(
        FmLang(IDT_MEM_USAGE_EXTRACT,
            QStringLiteral("Maximum amount of RAM memory usage allowed to unpack archives:")),
        page));
    {
      QHBoxLayout *memRow = new QHBoxLayout();
      _memSet = new QCheckBox(page);
      memRow->addWidget(_memSet);
      _memSpin = new QSpinBox(page);
      {
        // valMin=1, valMax RAM-aware (SettingsPage.cpp:216-227), capped at 1<<14.
        size_t ramSize = (size_t)sizeof(size_t) << 29;
        const bool ramDefined = NWindows::NSystem::GetRamSize(ramSize);
        unsigned ramGB = (unsigned)(((UInt64)ramSize + (1u << 29)) >> 30);
        if (ramGB == 0)
          ramGB = 1;
        unsigned valMax = 64; // 64GB for RAR7
        if (ramDefined)
        {
          const unsigned k_max_val = 1u << 14;
          if (ramGB >= k_max_val)       valMax = k_max_val;
          else if (ramGB > 1)           valMax = ramGB - 1;
          else                          valMax = 1;
        }
        _memSpin->setRange(1, (int)valMax);
        _memSpin->setValue(4);  // default shown when unticked (SettingsPage.cpp:234)
        // "GB" / "GB / N GB (RAM)" suffix (SettingsPage.cpp:207-214).
        QString suffix = QStringLiteral(" GB");
        if (ramDefined)
          suffix += QStringLiteral(" / %1 GB (RAM)").arg(ramGB);
        _memSpin->setSuffix(suffix);
      }
      memRow->addWidget(_memSpin);
      memRow->addStretch(1);
      // The spin is enabled only when the checkbox is ticked (CSettingsPage::
      // EnableSpin, toggled with IDX_SETTINGS_MEM_SET).
      connect(_memSet, &QCheckBox::toggled, _memSpin, &QSpinBox::setEnabled);
      v->addLayout(memRow);
    }

    v->addStretch(1);
    // DEFERRED (packaging): ShowRealFileIcons (B.6 already on), ShowSystemMenu,
    // LargePages — Windows-shell-tied concerns. (MemUse for EXTRACTING is now the
    // field above; compression MemUse stays deferred.)
    // G.1k : tab caption from the page's dialog IDD (OptionsDialog.cpp:58-60
    // LangString_OnlyFromLangFile(IDD_SETTINGS, ...); English = the CAPTION literal).
    tabs->addTab(page, FmLang(IDD_SETTINGS, QStringLiteral("Settings")));
  }

  // === Folders tab (FoldersPage.cpp working-folder mode) ====================
  {
    QWidget *page = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(page);
    // G.1k : FoldersPage label/radio langIDs (FoldersPage.cpp:15-21 kLangIDs[]).
    QGroupBox *box = new QGroupBox(
        FmLang(IDT_FOLDERS_WORKING_FOLDER, QStringLiteral("Working / temp folder")),
        page);
    QVBoxLayout *bv = new QVBoxLayout(box);
    _workSystem    = new QRadioButton(
        FmLang(IDR_FOLDERS_WORK_SYSTEM, QStringLiteral("System temp folder")), box);
    // G.9c : the kCurrent mode (FoldersPage IDR_FOLDERS_WORK_CURRENT, English
    // "&Current") — put the in-place-rewrite temp file in the ARCHIVE's own folder.
    _workCurrent   = new QRadioButton(
        FmLang(IDR_FOLDERS_WORK_CURRENT,
            QStringLiteral("Current (archive's folder)")), box);
    _workSpecified = new QRadioButton(
        FmLang(IDR_FOLDERS_WORK_SPECIFIED, QStringLiteral("Specified folder:")), box);
    bv->addWidget(_workSystem);
    bv->addWidget(_workCurrent);
    bv->addWidget(_workSpecified);
    QHBoxLayout *pathRow = new QHBoxLayout();
    _workPath = new QLineEdit(box);
    pathRow->addWidget(_workPath);
    QToolButton *browse = new QToolButton(box);
    browse->setText(QStringLiteral("..."));
    connect(browse, &QToolButton::clicked, this, &QtOptionsDialog::onBrowseWorkDir);
    pathRow->addWidget(browse);
    bv->addLayout(pathRow);
    connect(_workSystem, &QRadioButton::toggled, this,
        &QtOptionsDialog::onWorkDirModeToggled);
    connect(_workCurrent, &QRadioButton::toggled, this,
        &QtOptionsDialog::onWorkDirModeToggled);
    connect(_workSpecified, &QRadioButton::toggled, this,
        &QtOptionsDialog::onWorkDirModeToggled);
    v->addWidget(box);
    // G.1k : PORT-SPECIFIC explanatory label (no such control in the original
    // FoldersPage), so no genuine langID — left as QStringLiteral.
    v->addWidget(new QLabel(
        QStringLiteral("Used for the temporary file when modifying an archive in place."),
        page));
    v->addStretch(1);
    // G.9c : all three NWorkDir modes are now exposed — System (the OS temp dir),
    // Current (the archive's own folder), and Specified (a custom path).
    // G.1k : tab caption from IDD_FOLDERS (OptionsDialog.cpp:58-60).
    tabs->addTab(page, FmLang(IDD_FOLDERS, QStringLiteral("Folders")));
  }

  // === Editor tab (EditPage.cpp : editor + diff command) ====================
  {
    QWidget *page = new QWidget(this);
    QFormLayout *form = new QFormLayout(page);

    QHBoxLayout *edRow = new QHBoxLayout();
    _editorPath = new QLineEdit(page);
    _editorPath->setPlaceholderText(QStringLiteral("$VISUAL / $EDITOR"));
    edRow->addWidget(_editorPath);
    QToolButton *edBrowse = new QToolButton(page);
    edBrowse->setText(QStringLiteral("..."));
    connect(edBrowse, &QToolButton::clicked, this, &QtOptionsDialog::onBrowseEditor);
    edRow->addWidget(edBrowse);
    // G.1k : EditPage label langIDs (EditPage.cpp:16-21 kLangIDs[]).
    form->addRow(FmLang(IDT_EDIT_EDITOR, QStringLiteral("Editor:")), edRow);

    _diffCommand = new QLineEdit(page);
    _diffCommand->setPlaceholderText(QStringLiteral("meld"));
    form->addRow(FmLang(IDT_EDIT_DIFF, QStringLiteral("Diff command:")), _diffCommand);

    // NOTE: the original EditPage also has a Viewer slot; on Linux View routes to
    // xdg-open, so a separate Viewer command is omitted (optional, deferred).
    // G.1k : tab caption from IDD_EDIT (OptionsDialog.cpp:58-60).
    tabs->addTab(page, FmLang(IDD_EDIT, QStringLiteral("Editor")));
  }

  // === Delete tab (recycle-vs-permanent toggle) =============================
  {
    QWidget *page = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(page);
    // G.1k : the Delete tab is PORT-SPECIFIC (the original Options property sheet
    // has no Delete page — it is the Linux Trash toggle), so these strings have no
    // genuine original langID and stay plain literals (left as QStringLiteral).
    _deleteToTrash = new QCheckBox(
        QStringLiteral("Move deleted files to the Trash"), page);
    v->addWidget(_deleteToTrash);
    v->addWidget(new QLabel(
        QStringLiteral("When enabled, Delete moves items to the Trash (recoverable).\n"
           "Hold Shift while deleting to delete permanently."),
        page));
    v->addStretch(1);
    tabs->addTab(page, QStringLiteral("Delete"));
  }

  // === Language tab (P.2 : faithful LangPage mirror) ========================
  {
    QWidget *page = new QWidget(this);
    QFormLayout *form = new QFormLayout(page);
    _langCombo = new QComboBox(page);
    // First entry = English default (persists "" into [Options] Lang).
    _langCombo->addItem(QStringLiteral("English (default)"), QString());
    // Enumerate <langdir>/*.txt (LangPage.cpp OnInit mirror). For each file we
    // open it with CLang (the engine's own loader, same "7-Zip" id-0 guard as
    // LangPage's LangOpen / LangUtils.cpp:30) and build the display name from the
    // file's OWN language-name strings — id IDS_LANG_ENGLISH(1) for the English
    // name, id IDS_LANG_NATIVE(2) for the native name, joined as "English : Native"
    // (NativeLangString, LangPage.cpp:49-53 + 160-168). itemData stays the
    // basename WITHOUT extension — exactly what [Options] Lang stores. Files that
    // fail to open (no signature / wrong id-0) are skipped, as LangPage does
    // (it only records them in its error report, never in the combo).
    // (en.ttt is excluded by the *.txt filter — it is the English template, the
    // built-in default that the "English (default)" entry above represents.)
    static const unsigned k_IDS_LANG_ENGLISH = IDS_LANG_ENGLISH;  // LangPageRes.h:4 == 1
    static const unsigned k_IDS_LANG_NATIVE  = IDS_LANG_NATIVE;   // LangPageRes.h:5 == 2
    const QString langDir =
        UString_to_QStr(QtFmSettings::GetLangDirPrefix());
    // G.1k : accumulate names of *.txt files that fail to open (bad signature /
    // wrong id-0), mirroring LangPage.cpp OnInit's `error` accumulator
    // (LangPage.cpp:119-124); reported after the loop (LangPage.cpp:262-263).
    QStringList badLangFiles;
    QDir d(langDir);
    if (d.exists())
    {
      const QStringList files =
          d.entryList(QStringList() << QStringLiteral("*.txt"),
              QDir::Files, QDir::Name);
      for (const QString &f : files)
      {
        const QString base = QFileInfo(f).completeBaseName();

        CLang lang;
        const FString path = us2fs(QStr_to_UString(langDir + f));
        if (!lang.Open(path, "7-Zip"))
        {
          // bad signature / wrong id-0 — LangPage skips it from the combo too,
          // but records the file name to report after the loop (LangPage.cpp:119-124).
          badLangFiles << f;
          continue;
        }

        // s = English name (id 1) if present, else the basename (LangPage:160-163).
        QString s = base;
        const wchar_t *eng = lang.Get((UInt32)k_IDS_LANG_ENGLISH);
        if (eng)
          s = QString::fromWCharArray(eng);
        // Append " : native" (id 2) when present (NativeLangString, LangPage:164-166).
        const wchar_t *native = lang.Get((UInt32)k_IDS_LANG_NATIVE);
        if (native)
        {
          s += QStringLiteral(" : ");
          s += QString::fromWCharArray(native);
        }

        _langCombo->addItem(s, base);
      }
    }
    // G.1k : if any *.txt failed to open, report them — mirroring LangPage.cpp
    // OnInit (LangPage.cpp:262-263): the names joined by spaces, title literally
    // "Error in Lang file" (an inline C++ literal in the original, NOT a langID,
    // so it stays a QStringLiteral here too — left as literal).
    if (!badLangFiles.isEmpty())
      QMessageBox::critical(this,
          QStringLiteral("Error in Lang file"),
          badLangFiles.join(QLatin1Char(' ')));
    // G.1k : "Language:" label langID (LangPage.cpp:62 / LangPage.rc:12 IDT_LANG_LANG).
    form->addRow(FmLang(IDT_LANG_LANG, QStringLiteral("Language:")), _langCombo);
    page->setLayout(form);
    // G.1k : tab caption from IDD_LANG (OptionsDialog.cpp:58-60).
    tabs->addTab(page, FmLang(IDD_LANG, QStringLiteral("Language")));
  }

  QDialogButtonBox *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  // P.2 : OK/Cancel use langIDs 401/402 (kLangPairs, LangUtils.cpp:65-66); English
  // from the dialog-template literal. Override the standard-button text so a loaded
  // txt translates them (the std buttons otherwise show the Qt-theme default).
  if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok))
    ok->setText(FmLang(401, QStringLiteral("OK")));
  if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel))
    cancel->setText(FmLang(402, QStringLiteral("Cancel")));
  connect(buttons, &QDialogButtonBox::accepted, this, &QtOptionsDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  top->addWidget(buttons);
}

void QtOptionsDialog::fillFromState()
{
  _showGrid->setChecked(Settings.ShowGrid);
  _fullRow->setChecked(Settings.FullRow);
  _altColors->setChecked(Settings.AlternatingColors);
  _singleClick->setChecked(Settings.SingleClick);
  _showDots->setChecked(Settings.ShowDots);
  if (_showRealIcons)
    _showRealIcons->setChecked(Settings.ShowRealFileIcons);   // G.9d

  // G.9a : the extract-memory GB limit (mirror CSettingsPage::OnInit:229-242).
  // Read_LimitGB returns (UInt32)-1 (or 0) when no limit is configured -> the
  // checkbox is unticked, the spin disabled, showing the default 4.
  if (_memSet && _memSpin)
  {
    const UInt32 limit = NExtractQt::Read_LimitGB();
    if (limit != 0 && limit != (UInt32)(Int32)-1)
    {
      _memSet->setChecked(true);
      // Clamp to the spin range (UDM_SETPOS won't exceed valMax).
      int v = (int)limit;
      if (v < _memSpin->minimum()) v = _memSpin->minimum();
      if (v > _memSpin->maximum()) v = _memSpin->maximum();
      _memSpin->setValue(v);
      _memSpin->setEnabled(true);
    }
    else
    {
      _memSet->setChecked(false);
      _memSpin->setEnabled(false);
    }
  }

  // G.9c : select the radio matching the three-way mode (kSystem=0/kCurrent=1/
  // kSpecified=2), mirroring FoldersPage::OnInit's CheckRadioButton(kWorkModeButtons
  // [m_WorkDirInfo.Mode]). An out-of-range value falls back to System.
  switch (Settings.WorkDirMode)
  {
    case 1:  _workCurrent->setChecked(true);   break;
    case 2:  _workSpecified->setChecked(true); break;
    default: _workSystem->setChecked(true);    break;
  }
  _workPath->setText(UString_to_QStr(Settings.WorkDirPath));
  onWorkDirModeToggled();   // enable/disable the path edit

  _editorPath->setText(UString_to_QStr(Settings.EditorPath));
  _diffCommand->setText(UString_to_QStr(Settings.DiffCommand));

  _deleteToTrash->setChecked(Settings.DeleteToTrash);

  // P.2 : select the current language by its stored basename (itemData), else
  // fall back to index 0 (English). "-" (forced English) maps to English too.
  if (_langCombo)
  {
    const QString cur = UString_to_QStr(Settings.LangName);
    int idx = 0;
    if (!cur.isEmpty() && cur != QStringLiteral("-"))
    {
      const int found = _langCombo->findData(cur);
      if (found >= 0)
        idx = found;
      else
      {
        // Stored name not present in the dir (e.g. test path) — show it anyway.
        _langCombo->addItem(cur, cur);
        idx = _langCombo->count() - 1;
      }
    }
    _langCombo->setCurrentIndex(idx);
  }
}

void QtOptionsDialog::onWorkDirModeToggled()
{
  // The path edit is enabled only in the kSpecified mode (FoldersPage::MyEnableControls
  // enables IDE/IDB_FOLDERS_WORK_PATH only when GetWorkMode()==kSpecified).
  if (_workPath)
    _workPath->setEnabled(_workSpecified && _workSpecified->isChecked());
}

void QtOptionsDialog::onBrowseWorkDir()
{
  // G.1k : work-path browse title langID (FoldersPage.cpp:152
  // LangString(IDS_FOLDERS_SET_WORK_PATH_TITLE)); inline English kept verbatim.
  const QString dir = QFileDialog::getExistingDirectory(this,
      FmLang(IDS_FOLDERS_SET_WORK_PATH_TITLE,
          QStringLiteral("Working / temp folder")), _workPath->text());
  if (!dir.isEmpty())
  {
    _workPath->setText(dir);
    _workSpecified->setChecked(true);
  }
}

void QtOptionsDialog::onBrowseEditor()
{
  // G.1k : the original editor browse-for-file (EditPage.cpp Edit_BrowseForFile)
  // uses the default OS file-dialog title — no langID — so this title has no
  // genuine original id and stays a plain literal (left as QStringLiteral).
  const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Editor"),
      _editorPath->text());
  if (!f.isEmpty())
    _editorPath->setText(f);
}

void QtOptionsDialog::onAccept()
{
  Settings.ShowGrid          = _showGrid->isChecked();
  Settings.FullRow           = _fullRow->isChecked();
  Settings.AlternatingColors = _altColors->isChecked();
  Settings.SingleClick       = _singleClick->isChecked();
  Settings.ShowDots          = _showDots->isChecked();
  if (_showRealIcons)
    Settings.ShowRealFileIcons = _showRealIcons->isChecked();   // G.9d

  // G.9a : persist the extract-memory GB limit independently of the QtFmSettings
  // struct (it lives in the [Extraction] store). Mirror CSettingsPage::OnApply
  // (SettingsPage.cpp:304-321): unticked => (UInt32)-1 ("no limit"); ticked =>
  // the spin value (already range-validated by QSpinBox, so no E_INVALIDARG path).
  if (_memSet && _memSpin)
  {
    UInt32 val = (UInt32)(Int32)-1;
    if (_memSet->isChecked())
      val = (UInt32)_memSpin->value();
    NExtractQt::Save_LimitGB(val);
  }

  // G.9c : write the three-way work-dir mode (kSystem=0/kCurrent=1/kSpecified=2),
  // mirroring CFoldersPage::GetWorkDir's GetWorkMode(). Keep WorkDirUseSystemTemp in
  // sync (== kSystem) for the legacy bool consumers (the agent overlay reads Mode).
  if (_workCurrent && _workCurrent->isChecked())
    Settings.WorkDirMode = 1;        // kCurrent
  else if (_workSpecified->isChecked())
    Settings.WorkDirMode = 2;        // kSpecified
  else
    Settings.WorkDirMode = 0;        // kSystem
  Settings.WorkDirUseSystemTemp = (Settings.WorkDirMode == 0);
  Settings.WorkDirPath          = QStr_to_UString(_workPath->text());

  Settings.EditorPath  = QStr_to_UString(_editorPath->text());
  Settings.DiffCommand = QStr_to_UString(_diffCommand->text());

  Settings.DeleteToTrash = _deleteToTrash->isChecked();

  // P.2 : the chosen language basename ("" = English) -> [Options] Lang. The
  // caller (onOptions) Save()s and re-runs the startup load to apply live.
  if (_langCombo)
    Settings.LangName =
        QStr_to_UString(_langCombo->currentData().toString());

  accept();
}
