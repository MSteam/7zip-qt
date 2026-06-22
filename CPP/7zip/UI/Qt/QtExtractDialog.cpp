// QtExtractDialog.cpp
//
// Mirror of GUI/ExtractDialog.cpp's CExtractDialog. Layout uses Qt widgets but
// the option enums, combo order, enum<->index mapping and load/save semantics
// track the original line-for-line (cited inline).

#include "QtExtractDialog.h"
#include "QtLang.h"                            // P.2 : FmLang
#include "../FileManager/resource.h"          // P.2 : IDS_EXTRACT=7201
#include "../GUI/ExtractDialogRes.h"          // P.2 : IDD_EXTRACT=3400 (the dialog-caption langID)
                                              //       + IDT_*/IDX_*/IDG_* label langIDs (kLangIDs)
#include "../GUI/ExtractRes.h"                // G.1 : IDS_EXTRACT_PATHS_*/_OVERWRITE_*/_SET_FOLDER

#include "../../../Windows/FileName.h"
#include "../../../Common/Wildcard.h"          // G.7e : SplitPathToParts_Smart

#include <QtCore/QDir>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>


// === enum<->combo mapping : MIRRORS ExtractDialog.cpp lines 33-69 =============
//
// kPathModeButtonsVals[] = { kFullPaths, kNoPaths, kAbsPaths } with labels from
//   IDS_EXTRACT_PATHS_FULL / _NO / _ABS:
//     index 0 -> "Full pathnames"     -> NPathMode::kFullPaths
//     index 1 -> "No pathnames"       -> NPathMode::kNoPaths
//     index 2 -> "Absolute pathnames" -> NPathMode::kAbsPaths
//
// kOverwriteButtonsVals[] = { kAsk, kOverwrite, kSkip, kRename, kRenameExisting }
//   with labels from IDS_EXTRACT_OVERWRITE_ASK / _WITHOUT_PROMPT /
//   _SKIP_EXISTING / _RENAME / _RENAME_EXISTING:
//     index 0 -> "Ask before overwrite"       -> NOverwriteMode::kAsk
//     index 1 -> "Overwrite without prompt"    -> NOverwriteMode::kOverwrite
//     index 2 -> "Skip existing files"         -> NOverwriteMode::kSkip
//     index 3 -> "Auto rename"                 -> NOverwriteMode::kRename
//     index 4 -> "Auto rename existing files"  -> NOverwriteMode::kRenameExisting

namespace {

// langId mirrors ExtractDialog.cpp kPathMode_IDs / kOverwriteMode_IDs (the
// AddComboItems langID arg); FmLang(langId, english) reproduces LangString(id).
struct ComboItem { const char *text; int value; unsigned langId; };

// Order MUST match ExtractDialog.cpp kPathMode_IDs / kPathModeButtonsVals.
const ComboItem kPathModeItems[] =
{
  { QT_TRANSLATE_NOOP("QtExtractDialog", "Full pathnames"),     NExtract::NPathMode::kFullPaths, IDS_EXTRACT_PATHS_FULL },
  { QT_TRANSLATE_NOOP("QtExtractDialog", "No pathnames"),       NExtract::NPathMode::kNoPaths,   IDS_EXTRACT_PATHS_NO },
  { QT_TRANSLATE_NOOP("QtExtractDialog", "Absolute pathnames"), NExtract::NPathMode::kAbsPaths,  IDS_EXTRACT_PATHS_ABS },
};

// Order MUST match ExtractDialog.cpp kOverwriteMode_IDs / kOverwriteButtonsVals.
const ComboItem kOverwriteModeItems[] =
{
  { QT_TRANSLATE_NOOP("QtExtractDialog", "Ask before overwrite"),      NExtract::NOverwriteMode::kAsk,            IDS_EXTRACT_OVERWRITE_ASK },
  { QT_TRANSLATE_NOOP("QtExtractDialog", "Overwrite without prompt"),  NExtract::NOverwriteMode::kOverwrite,      IDS_EXTRACT_OVERWRITE_WITHOUT_PROMPT },
  { QT_TRANSLATE_NOOP("QtExtractDialog", "Skip existing files"),       NExtract::NOverwriteMode::kSkip,           IDS_EXTRACT_OVERWRITE_SKIP_EXISTING },
  { QT_TRANSLATE_NOOP("QtExtractDialog", "Auto rename"),               NExtract::NOverwriteMode::kRename,         IDS_EXTRACT_OVERWRITE_RENAME },
  { QT_TRANSLATE_NOOP("QtExtractDialog", "Auto rename existing files"),NExtract::NOverwriteMode::kRenameExisting, IDS_EXTRACT_OVERWRITE_RENAME_EXISTING },
};

const unsigned kHistorySize = 16; // mirrors ExtractDialog.cpp kHistorySize

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

// AddComboItems mirror (ExtractDialog.cpp 98-110): add each label, select the
// item whose enum value == curVal.
void addComboItems(QComboBox *combo, const ComboItem *items, unsigned n, int curVal)
{
  int curSel = 0;
  for (unsigned i = 0; i < n; i++)
  {
    combo->addItem(FmLang(items[i].langId, QString::fromUtf8(items[i].text)), items[i].value);
    if (items[i].value == curVal)
      curSel = (int)i;
  }
  combo->setCurrentIndex(curSel);
}

// GetBoolsVal mirror (ExtractDialog.cpp 114-119).
bool getBoolsVal(const CBoolPair &b1, const CBoolPair &b2)
{
  if (b1.Def) return b1.Val;
  if (b2.Def) return b2.Val;
  return b1.Val;
}

// AddUniqueString mirror (ExtractDialog.cpp 291-297): case-insensitive de-dup.
void addUniqueString(UStringVector &list, const UString &s)
{
  FOR_VECTOR (i, list)
    if (s.IsEqualTo_NoCase(list[i]))
      return;
  list.Add(s);
}

} // namespace


QtExtractDialog::QtExtractDialog(QWidget *parent)
  : QDialog(parent)
  , PathMode_Force(false)        // mirror CExtractDialog() ctor
  , OverwriteMode_Force(false)
  , PathMode(NExtract::NPathMode::kFullPaths)
  , OverwriteMode(NExtract::NOverwriteMode::kAsk)
  , _path(nullptr)
  , _splitDest(nullptr)
  , _pathName(nullptr)
  , _arcPath(nullptr)
  , _pathMode(nullptr)
  , _overwriteMode(nullptr)
  , _password(nullptr)
  , _showPassword(nullptr)
  , _ntSecurity(nullptr)
  , _elimDup(nullptr)
{
  ElimDup.Val = true; // mirror CExtractDialog() ctor

  // --- output dir : editable combo (MRU) + "..." browse button --------------
  _path = new QComboBox(this);
  _path->setEditable(true);
  _path->setInsertPolicy(QComboBox::NoInsert);
  _path->setMinimumWidth(360);

  QToolButton *browse = new QToolButton(this);
  browse->setText(QString::fromLatin1("..."));

  QHBoxLayout *pathRow = new QHBoxLayout;
  pathRow->addWidget(_path, 1);
  pathRow->addWidget(browse);

  // --- split-destination sub-folder NAME row --------------------------------
  // Mirrors ExtractDialog.rc:35-36 (IDX_EXTRACT_NAME_ENABLE checkbox +
  // IDE_EXTRACT_NAME edit). When the checkbox is ticked the output path is split
  // into a directory prefix (_path) and a separately-editable sub-folder name
  // (_pathName); OnOK re-appends the name to the prefix. The .rc gives the
  // checkbox an EMPTY caption and it is absent from kLangIDs (no langID), so this
  // label is a plain port literal describing the toggle's function.
  _splitDest = new QCheckBox(QStringLiteral("Sub-folder name:"), this);
  _pathName  = new QLineEdit(this);

  QHBoxLayout *nameRow = new QHBoxLayout;
  nameRow->addWidget(_splitDest);
  nameRow->addWidget(_pathName, 1);

  // --- read-only archive path label -----------------------------------------
  _arcPath = new QLabel(this);
  _arcPath->setTextInteractionFlags(Qt::TextSelectableByMouse);

  // --- path-mode / overwrite-mode combos ------------------------------------
  _pathMode = new QComboBox(this);
  _overwriteMode = new QComboBox(this);

  // --- password group : line edit (password echo) + show checkbox -----------
  _password = new QLineEdit(this);
  _password->setEchoMode(QLineEdit::Password);
  // IDX_PASSWORD_SHOW (3803) — kLangIDs entry (control ID doubles as langID).
  _showPassword = new QCheckBox(FmLang(IDX_PASSWORD_SHOW, QStringLiteral("Show Password")), this);

  // --- option checkboxes -----------------------------------------------------
  // IDX_EXTRACT_NT_SECUR (3431) / IDX_EXTRACT_ELIM_DUP (3430) — kLangIDs entries.
  _ntSecurity = new QCheckBox(FmLang(IDX_EXTRACT_NT_SECUR, QStringLiteral("Restore file security")), this);
  _elimDup = new QCheckBox(FmLang(IDX_EXTRACT_ELIM_DUP, QStringLiteral("Eliminate duplication of root folder")), this);

  // --- form layout (mirrors the rc's label/control rows) --------------------
  // Labels use the kLangIDs control-IDs IDT_EXTRACT_EXTRACT_TO (3401) /
  // IDT_EXTRACT_PATH_MODE (3410) / IDT_EXTRACT_OVERWRITE_MODE (3420).
  QFormLayout *form = new QFormLayout;
  form->addRow(FmLang(IDT_EXTRACT_EXTRACT_TO, QStringLiteral("Extract to:")), pathRow);
  // Split-destination name row sits under "Extract to:" (rc:35-36 is directly
  // below the path edit). No label cell — the enable checkbox is its own label.
  form->addRow(QString(), nameRow);
  // "Archive:" is port-specific: the original .rc has no such label (the archive
  // appears only in the window caption). No genuine langID -> plain literal.
  form->addRow(QStringLiteral("Archive:"), _arcPath);
  form->addRow(FmLang(IDT_EXTRACT_PATH_MODE, QStringLiteral("Path mode:")), _pathMode);
  form->addRow(FmLang(IDT_EXTRACT_OVERWRITE_MODE, QStringLiteral("Overwrite mode:")), _overwriteMode);

  // IDG_PASSWORD (3807) — kLangIDs entry (the "Password" group box).
  QGroupBox *pwGroup = new QGroupBox(FmLang(IDG_PASSWORD, QStringLiteral("Password")), this);
  QVBoxLayout *pwLayout = new QVBoxLayout(pwGroup);
  pwLayout->addWidget(_password);
  pwLayout->addWidget(_showPassword);

  QDialogButtonBox *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  // G.1h : OK/Cancel use langIDs 401/402 (kLangPairs, LangUtils.cpp:65-66); English
  // from the dialog-template literal. Override the standard-button text so a loaded
  // txt translates them (mirrors fm/QtOptionsDialog.cpp:241-244).
  if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok))
    ok->setText(FmLang(401, QStringLiteral("OK")));
  if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel))
    cancel->setText(FmLang(402, QStringLiteral("Cancel")));

  QVBoxLayout *root = new QVBoxLayout(this);
  root->addLayout(form);
  root->addWidget(_ntSecurity);
  root->addWidget(_elimDup);
  root->addWidget(pwGroup);
  root->addWidget(buttons);

  connect(browse, &QToolButton::clicked, this, &QtExtractDialog::onBrowse);
  connect(_splitDest, &QCheckBox::toggled, this, &QtExtractDialog::onSplitDestToggled);
  connect(_showPassword, &QCheckBox::toggled, this, &QtExtractDialog::onShowPasswordToggled);
  connect(buttons, &QDialogButtonBox::accepted, this, &QtExtractDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  // Note: fillFromState() (the OnInit mirror) is NOT called here — DirPath /
  // ArcPath / option fields are set by the caller AFTER construction (exactly as
  // ExtractGUI.cpp sets dialog.DirPath etc. before dialog.Create()). The caller
  // invokes initFromState()/exec(); we expose fillFromState() via show/exec
  // override below.
}

bool QtExtractDialog::isShowPasswordChecked() const
{
  return _showPassword->isChecked();
}

// CheckButton_TwoBools mirror (ExtractDialog.cpp 121-124).
void QtExtractDialog::checkBox_TwoBools(QCheckBox *box, const CBoolPair &b1, const CBoolPair &b2)
{
  box->setChecked(getBoolsVal(b1, b2));
}

// GetButton_Bools mirror (ExtractDialog.cpp 126-133).
void QtExtractDialog::getBox_Bools(QCheckBox *box, CBoolPair &b1, CBoolPair &b2)
{
  const bool val = box->isChecked();
  const bool oldVal = getBoolsVal(b1, b2);
  if (val != oldVal)
    b1.Def = b2.Def = true;
  b1.Val = b2.Val = val;
}

// === OnInit mirror ===========================================================
// In Win32, OnInit runs once the dialog is created; here the caller has already
// populated DirPath / ArcPath / PathMode / etc., so fillFromState() does the rest
// (load _info, resolve forced values, fill combos & checkboxes).
void QtExtractDialog::fillFromState()
{
  // Window title : "<base title> : <ArcPath>" (ExtractDialog.cpp 144-150).
  {
    // P.2 : the dialog CAPTION uses IDD_EXTRACT (3400), mirroring ExtractDialog.cpp:142
    // LangString_OnlyFromLangFile(IDD_EXTRACT) — NOT the toolbar-button IDS_EXTRACT (7201).
    QString title = FmLang(IDD_EXTRACT, QStringLiteral("Extract"));
    if (!ArcPath.IsEmpty())
    {
      title += QString::fromLatin1(" : ");
      title += UString_to_QStr(ArcPath);
    }
    setWindowTitle(title);
  }

  _arcPath->setText(UString_to_QStr(ArcPath));
  _password->setText(UString_to_QStr(Password));

  // _info.Load() + forced-value resolution (ExtractDialog.cpp 170-185).
  _info.Load();

  if (_info.PathMode == NExtract::NPathMode::kCurPaths)
    _info.PathMode = NExtract::NPathMode::kFullPaths;

  if (!PathMode_Force && _info.PathMode_Force)
    PathMode = _info.PathMode;
  if (!OverwriteMode_Force && _info.OverwriteMode_Force)
    OverwriteMode = _info.OverwriteMode;

  checkBox_TwoBools(_ntSecurity, NtSecurity, _info.NtSecurity);
  checkBox_TwoBools(_elimDup,    ElimDup,    _info.ElimDup);

  _showPassword->setChecked(_info.ShowPassword.Val);
  onShowPasswordToggled(); // UpdatePasswordControl()

  // Output-path combo : current dir prefix + MRU history (ExtractDialog.cpp
  // 189-215). When _info.SplitDest is on, the DirPath is split into a directory
  // prefix (kept in _path) and a sub-folder name (kept in _pathName), mirroring
  // ExtractDialog.cpp:191-210.
  UString pathPrefix = DirPath;
  if (_info.SplitDest.Val)
  {
    _splitDest->setChecked(true);
    UString pathName;
    SplitPathToParts_Smart(DirPath, pathPrefix, pathName);
    if (pathPrefix.IsEmpty())
      pathPrefix = pathName;
    else
      _pathName->setText(UString_to_QStr(pathName));
  }
  else
  {
    _splitDest->setChecked(false);
  }
  onSplitDestToggled(); // ShowItem_Bool(IDE_EXTRACT_NAME, ...) — name edit visibility

  _path->setEditText(UString_to_QStr(pathPrefix));
  for (unsigned i = 0; i < _info.Paths.Size() && i < kHistorySize; i++)
    _path->addItem(UString_to_QStr(_info.Paths[i]));

  // Mode combos (ExtractDialog.cpp 229-230).
  addComboItems(_pathMode, kPathModeItems,
      (unsigned)(sizeof(kPathModeItems) / sizeof(kPathModeItems[0])), PathMode);
  addComboItems(_overwriteMode, kOverwriteModeItems,
      (unsigned)(sizeof(kOverwriteModeItems) / sizeof(kOverwriteModeItems[0])), OverwriteMode);
}

// === UpdatePasswordControl mirror (ExtractDialog.cpp 246-252) ================
void QtExtractDialog::onShowPasswordToggled()
{
  _password->setEchoMode(isShowPasswordChecked()
      ? QLineEdit::Normal : QLineEdit::Password);
}

// === OnButtonClicked(IDX_EXTRACT_NAME_ENABLE) mirror (ExtractDialog.cpp 263-265) ==
// Show/hide the sub-folder name edit to track the enable checkbox.
void QtExtractDialog::onSplitDestToggled()
{
  _pathName->setVisible(_splitDest->isChecked());
}

// === OnButtonSetPath mirror (ExtractDialog.cpp 276-288) ======================
void QtExtractDialog::onBrowse()
{
  const QString current = _path->currentText();
  // ExtractDialog.cpp:280 — LangString(IDS_EXTRACT_SET_FOLDER) (3402).
  const QString result = QFileDialog::getExistingDirectory(this,
      FmLang(IDS_EXTRACT_SET_FOLDER, QStringLiteral("Specify a location for extracted files.")), current);
  if (result.isEmpty())
    return;
  // Mirror _path.SetCurSel(-1) + SetText(resultPath): clear selection, set text.
  _path->setCurrentIndex(-1);
  _path->setEditText(result);
}

// === OnOK mirror (ExtractDialog.cpp 299-411) =================================
void QtExtractDialog::onAccept()
{
  // PathMode read-back (ExtractDialog.cpp 302-305): keep the kCurPaths special
  // case so a fresh default (kCurPaths) is not clobbered by a kFullPaths pick.
  const int pathMode2 = _pathMode->currentData().toInt();
  if (PathMode != NExtract::NPathMode::kCurPaths ||
      pathMode2 != NExtract::NPathMode::kFullPaths)
    PathMode = (NExtract::NPathMode::EEnum)pathMode2;

  OverwriteMode = (NExtract::NOverwriteMode::EEnum)_overwriteMode->currentData().toInt();

  Password = QStr_to_UString(_password->text());

  // Bools back into the field pairs + into _info (ExtractDialog.cpp 318-326).
  getBox_Bools(_ntSecurity, NtSecurity, _info.NtSecurity);
  getBox_Bools(_elimDup,    ElimDup,    _info.ElimDup);

  const bool showPassword = isShowPasswordChecked();
  if (showPassword != _info.ShowPassword.Val)
  {
    _info.ShowPassword.Def = true;
    _info.ShowPassword.Val = showPassword;
  }

  // PathMode/OverwriteMode forcing into _info (ExtractDialog.cpp 328-341).
  if ((int)_info.PathMode != pathMode2)
  {
    _info.PathMode_Force = true;
    _info.PathMode = (NExtract::NPathMode::EEnum)pathMode2;
  }

  if (!OverwriteMode_Force && _info.OverwriteMode != OverwriteMode)
    _info.OverwriteMode_Force = true;
  _info.OverwriteMode = OverwriteMode;

  // Resolve the chosen output path: prefer the selected MRU item, else the typed
  // text (ExtractDialog.cpp 358-368).
  UString s;
  int currentItem = _path->currentIndex();
  const QString editText = _path->currentText();
  // currentIndex() stays valid only if the edit text still matches that item.
  if (currentItem < 0 || _path->itemText(currentItem) != editText)
  {
    s = QStr_to_UString(editText);
    currentItem = -1;
    if (_path->count() >= (int)kHistorySize)
      currentItem = _path->count() - 1;
  }
  else
    s = QStr_to_UString(_path->itemText(currentItem));

  s.Trim();
  NWindows::NFile::NName::NormalizeDirPathPrefix(s);

  // SplitDest re-append + persist (ExtractDialog.cpp 373-388). When enabled, the
  // effective destination is the directory prefix (s) + the sub-folder name.
  const bool splitDest = _splitDest->isChecked();
  if (splitDest)
  {
    UString pathName = QStr_to_UString(_pathName->text());
    pathName.Trim();
    s += pathName;
    NWindows::NFile::NName::NormalizeDirPathPrefix(s);
  }
  if (splitDest != _info.SplitDest.Val)
  {
    _info.SplitDest.Def = true;
    _info.SplitDest.Val = splitDest;
  }

  DirPath = s;

  // Rebuild the MRU history (ExtractDialog.cpp 395-407): chosen path first, then
  // the rest (skipping the selected item), de-duplicated case-insensitively.
  _info.Paths.Clear();
  addUniqueString(_info.Paths, s);
  for (int i = 0; i < _path->count(); i++)
    if (i != currentItem)
    {
      UString sTemp = QStr_to_UString(_path->itemText(i));
      sTemp.Trim();
      addUniqueString(_info.Paths, sTemp);
    }
  _info.Save();

  accept(); // CModalDialog::OnOK() -> close with OK
}
