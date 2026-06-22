// QtCompressOptionsDialog.cpp
//
// Mirror of GUI/CompressDialog.cpp's COptionsDialog (B.9). See the header for the
// fidelity / Linux-appropriateness contract.

#include "QtCompressOptionsDialog.h"
#include "QtLang.h"                          // P.2 : FmLang
#include "../FileManager/resource.h"         // P.2 : IDS_OPTIONS=2100
#include "../GUI/CompressDialogRes.h"        // G.1c : IDG_COMPRESS_TIME / IDX_COMPRESS_* langIDs

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>       // G.1c : QDialogButtonBox standard-button text override
#include <QtWidgets/QVBoxLayout>

#include "../../../../C/7zTypes.h" // k_PropVar_TimePrec_* constants

#include "../../../Common/MyString.h"

// === local helpers ==========================================================
static QString UToQ(const UString &u)
{
  QString s;
  for (unsigned i = 0; i < u.Len(); i++)
    s.append(QChar((ushort)u[i]));
  return s;
}

// The UI precision indices (CompressDialog.cpp:3437-3440). These coincide with the
// engine's k_PropVar_TimePrec_* for 0/1/2 and use HighPrec(3) as "1 ns : Linux".
static const unsigned kTimePrec_Win  = 0;  // == k_PropVar_TimePrec_0   (100 ns)
static const unsigned kTimePrec_Unix = 1;  // == k_PropVar_TimePrec_Unix (1 sec)
static const unsigned kTimePrec_DOS  = 2;  // == k_PropVar_TimePrec_DOS  (2 sec)
static const unsigned kTimePrec_1ns  = 3;  // == k_PropVar_TimePrec_HighPrec (1 ns)


QtCompressOptionsDialog::QtCompressOptionsDialog(QWidget *parent)
  : QDialog(parent),
    ArcFormats(nullptr),
    _formatIndex(-1),
    _prec(nullptr),
    _autoPrec(0),
    _timePrec((UInt32)(Int32)-1), // Reset_TimePrec()
    _precSet(nullptr),
    _timeInfo(nullptr),
    _symLinks(nullptr), _symLinksSupported(false),
    _hardLinks(nullptr), _hardLinksSupported(false),
    _altStreams(nullptr), _altStreamsSupported(false),
    _ntSecurity(nullptr), _ntSecuritySupported(false),
    _preserveATime(nullptr)
{
  setWindowTitle(FmLang(IDS_OPTIONS, QStringLiteral("Options")));   // P.2 : 2100
  setModal(true);

  QVBoxLayout *root = new QVBoxLayout(this);

  // ---- Time group (IDG_COMPRESS_TIME) --------------------------------------
  // G.1c : IDG_COMPRESS_TIME=4080 (CompressDialog.cpp kLangIDs_Options:3687)
  QGroupBox *timeGroup = new QGroupBox(FmLang(IDG_COMPRESS_TIME, QStringLiteral("Time")), this);
  QVBoxLayout *timeLayout = new QVBoxLayout(timeGroup);

  _timeInfo = new QLabel(timeGroup);
  timeLayout->addWidget(_timeInfo);

  {
    QFormLayout *precForm = new QFormLayout();
    _prec = new QComboBox(timeGroup);
    // The "Set" override checkbox mirrors the RC's MY_CONTROL_CHECKBOX_COLON
    // (IDX_COMPRESS_PREC_SET=201) which carries no text label and is NOT listed
    // in kLangIDs_Options -> no original langID -> port-only "Set" literal.
    _precSet = new QCheckBox(QStringLiteral("Set"), timeGroup);
    QHBoxLayout *precRow = new QHBoxLayout();
    precRow->addWidget(_prec, 1);
    precRow->addWidget(_precSet, 0);
    // G.1c : IDT_COMPRESS_TIME_PREC=4081 (kLangIDs_Options:3688)
    precForm->addRow(FmLang(IDT_COMPRESS_TIME_PREC, QStringLiteral("Precision:")), precRow);
    timeLayout->addLayout(precForm);
  }

  // The four time bool-boxes: a value checkbox + a "Set" override checkbox.
  // Value labels carry the original kLangIDs_Options langIDs (4082..4085); the
  // "Set" override checkboxes mirror the RC's text-less MY_CONTROL_CHECKBOX_COLON
  // controls (IDX_COMPRESS_*_SET 202..205), absent from kLangIDs_Options -> no
  // original langID -> port-only "Set" literal.
  // G.1c : IDX_COMPRESS_MTIME=4082 / CTIME=4083 / ATIME=4084 / ZTIME=4085
  //         (CompressDialog.cpp kLangIDs_Options:3689-3692)
  _mTime.Val = new QCheckBox(FmLang(IDX_COMPRESS_MTIME, QStringLiteral("Modified")), timeGroup);
  _cTime.Val = new QCheckBox(FmLang(IDX_COMPRESS_CTIME, QStringLiteral("Created")), timeGroup);
  _aTime.Val = new QCheckBox(FmLang(IDX_COMPRESS_ATIME, QStringLiteral("Accessed")), timeGroup);
  _zTime.Val = new QCheckBox(FmLang(IDX_COMPRESS_ZTIME, QStringLiteral("Store archive's modification time")), timeGroup);
  _mTime.Set = new QCheckBox(QStringLiteral("Set"), timeGroup);
  _cTime.Set = new QCheckBox(QStringLiteral("Set"), timeGroup);
  _aTime.Set = new QCheckBox(QStringLiteral("Set"), timeGroup);
  _zTime.Set = new QCheckBox(QStringLiteral("Set"), timeGroup);
  {
    CBoolBoxW *boxes[4] = { &_mTime, &_cTime, &_aTime, &_zTime };
    for (CBoolBoxW *bb : boxes)
    {
      QHBoxLayout *row = new QHBoxLayout();
      row->addWidget(bb->Val, 1);
      row->addWidget(bb->Set, 0);
      timeLayout->addLayout(row);
      connect(bb->Set, &QCheckBox::toggled, this, &QtCompressOptionsDialog::onTimeSetToggled);
    }
  }
  connect(_precSet, &QCheckBox::toggled, this, &QtCompressOptionsDialog::onPrecSetToggled);
  // SELCHANGE on the precision combo re-runs the per-format MAC gating (zip/tar),
  // mirroring OnCommand CBN_SELCHANGE (CompressDialog.cpp:3765-3779).
  connect(_prec, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { setTimeMAC(); });

  root->addWidget(timeGroup);

  // ---- NTFS / link group (IDG_COMPRESS_NTFS) -------------------------------
  // G.1c : group = IDG_COMPRESS_NTFS=115 (RC "NTFS"; gated/shown via OnInit:3722).
  // The port's longer group caption is the .rc/template fallback we preserve.
  QGroupBox *linkGroup = new QGroupBox(FmLang(IDG_COMPRESS_NTFS, QStringLiteral("Symbolic links / Hard links / Alternate data streams / Security")), this);
  QVBoxLayout *linkLayout = new QVBoxLayout(linkGroup);

  // G.1c : IDX_COMPRESS_NT_SYM_LINKS=4040 / HARD_LINKS=4041 / ALT_STREAMS=4042 /
  //         SECUR=4043 (CompressDialog.cpp kLangIDs_Options:3682-3685);
  //         IDX_COMPRESS_PRESERVE_ATIME=4086 (kLangIDs_Options:3693).
  _symLinks   = new QCheckBox(FmLang(IDX_COMPRESS_NT_SYM_LINKS, QStringLiteral("Store symbolic links")), linkGroup);
  _hardLinks  = new QCheckBox(FmLang(IDX_COMPRESS_NT_HARD_LINKS, QStringLiteral("Store hard links")), linkGroup);
  _altStreams = new QCheckBox(FmLang(IDX_COMPRESS_NT_ALT_STREAMS, QStringLiteral("Store alternate data streams")), linkGroup);
  _ntSecurity = new QCheckBox(FmLang(IDX_COMPRESS_NT_SECUR, QStringLiteral("Store NT security")), linkGroup);
  _preserveATime = new QCheckBox(FmLang(IDX_COMPRESS_PRESERVE_ATIME, QStringLiteral("Do not change source last access time")), linkGroup);

  // NT-only controls: disabled + tooltip on Linux, mirroring the extract-side
  // NtSecurity treatment. We never set their CBoolPair.Def in applyToInfo, so the
  // engine receives nothing for them (Update.cpp:463-465 rejects them anyway).
  // The tooltip text is port-specific (no original IDS_*); kept as a literal.
  const QString ntTip = QStringLiteral("Not supported on this platform (NTFS-specific).");
  _altStreams->setEnabled(false); _altStreams->setToolTip(ntTip);
  _ntSecurity->setEnabled(false); _ntSecurity->setToolTip(ntTip);

  linkLayout->addWidget(_symLinks);
  linkLayout->addWidget(_hardLinks);
  linkLayout->addWidget(_altStreams);
  linkLayout->addWidget(_ntSecurity);
  linkLayout->addWidget(_preserveATime);

  root->addWidget(linkGroup);

  QDialogButtonBox *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  // G.1c : standard-button text via LangUtils.cpp kLangPairs (401 OK / 402 Cancel),
  // mirroring fm/QtOptionsDialog.cpp:241-244.
  if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok))
    ok->setText(FmLang(401, QStringLiteral("OK")));
  if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel))
    cancel->setText(FmLang(402, QStringLiteral("Cancel")));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);
}


const CArcInfoEx *QtCompressOptionsDialog::arcInfo() const
{
  if (!ArcFormats || _formatIndex < 0 || (unsigned)_formatIndex >= ArcFormats->Size())
    return nullptr;
  return &(*ArcFormats)[(unsigned)_formatIndex];
}


// === CheckButton_BoolBox / GetButton_BoolBox (3397-3417) ====================
void QtCompressOptionsDialog::checkBoolBox(bool supported, const CBoolPair &bp, CBoolBoxW &bb)
{
  const bool isSet = bp.Def;
  const bool val = isSet ? bp.Val : bb.DefaultVal;

  bb.IsSupported = supported;

  bb.Set->setChecked(isSet);
  bb.Set->setVisible(supported);
  bb.Val->setChecked(val);
  bb.Val->setEnabled(isSet);
  bb.Val->setVisible(supported);
}

CBoolPair QtCompressOptionsDialog::getBoolBox(const CBoolBoxW &bb)
{
  // We save the value for invisible buttons too (mirror GetButton_BoolBox).
  CBoolPair bp;
  bp.Val = bb.Val->isChecked();
  bp.Def = bb.Set->isChecked();
  return bp;
}


// === AddPrec (3455-3479) ====================================================
int QtCompressOptionsDialog::addPrec(unsigned prec, bool /*isDefault*/)
{
  const UInt32 writePrec = prec;
  QString s;
  // SecString / NsString: upstream loads these via LangString(IDS_COMPRESS_SEC,...)
  // / LangString(IDS_COMPRESS_NS,...) with the literal "sec"/"ns" only as fallback
  // (CompressDialog.cpp:3707-3712). G.1c: route through FmLang with the same langIDs.
  const QString sec = FmLang(IDS_COMPRESS_SEC, QStringLiteral("sec")); // 4090
  const QString ns = FmLang(IDS_COMPRESS_NS, QStringLiteral("ns"));    // 4091

  auto addTimeOption = [&](UInt32 val, const QString &unit, const char *sys)
  {
    s += QString::number(val);
    s += QChar(' ');
    s += unit;
    if (sys)
    {
      s += QString::fromLatin1(" : ");
      s += QString::fromLatin1(sys);
    }
  };

       if (prec == kTimePrec_Win)  addTimeOption(100, ns, "Windows");
  else if (prec == kTimePrec_Unix) addTimeOption(1, sec, "Unix");
  else if (prec == kTimePrec_DOS)  addTimeOption(2, sec, "DOS");
  else if (prec == kTimePrec_1ns)  addTimeOption(1, ns, "Linux");
  else if (prec == (unsigned)k_PropVar_TimePrec_Base) addTimeOption(1, sec, nullptr);
  else if (prec >= (unsigned)k_PropVar_TimePrec_Base)
  {
    UInt32 d = 1;
    for (unsigned i = prec; i < (unsigned)k_PropVar_TimePrec_Base + 9; i++)
      d *= 10;
    addTimeOption(d, ns, nullptr);
  }
  else
    s += QString::number(prec);

  _prec->addItem(s, (uint)writePrec);
  return _prec->count() - 1;
}


// === SetPrec (3482-3581) ====================================================
void QtCompressOptionsDialog::setPrec()
{
  const CArcInfoEx *ai = arcInfo();

  UInt32 flags = ai ? ai->Get_TimePrecFlags() : 0;
  UInt32 defaultPrec = ai ? ai->Get_DefaultTimePrec() : 0;
  if (defaultPrec != 0)
    flags |= ((UInt32)1 << defaultPrec);

  if (ai && ai->Is_GZip())
    defaultPrec = kTimePrec_Unix;

  if (_timeInfo)
  {
    QString s = QString::fromLatin1("Type: ");
    if (ai)
      s += UToQ(ai->Name);
    _timeInfo->setText(s);
  }

  _prec->blockSignals(true);
  _prec->clear();
  _autoPrec = defaultPrec;

  unsigned selectedPrec = defaultPrec;
  if ((Int32)_timePrec >= 0)
    selectedPrec = _timePrec;

  int curSel = -1;
  int defaultPrecIndex = -1;
  for (unsigned prec = 0; prec <= (unsigned)k_PropVar_TimePrec_1ns; prec++)
  {
    if (((flags >> prec) & 1) == 0)
      continue;
    const bool isDefault = (defaultPrec == prec);
    const int index = addPrec(prec, isDefault);
    if (isDefault)
      defaultPrecIndex = index;
    if (selectedPrec == prec)
      curSel = index;
  }

  if (curSel < 0 && selectedPrec > kTimePrec_DOS)
    curSel = addPrec(selectedPrec, false);
  if (curSel < 0)
    curSel = defaultPrecIndex;
  if (curSel >= 0)
    _prec->setCurrentIndex(curSel);
  _prec->blockSignals(false);

  {
    const bool isSet = (_timePrec != (UInt32)(Int32)-1);
    const int count = _prec->count();
    const bool showPrec = (count != 0);
    _prec->setVisible(showPrec);
    _prec->setEnabled(isSet && (count > 1));

    _precSet->setChecked(isSet);
    const bool setIsSupported = isSet || (count > 1);
    _precSet->setEnabled(setIsSupported);
    _precSet->setVisible(setIsSupported);
  }

  setTimeMAC();
}


// === SetTimeMAC (3584-3651) =================================================
void QtCompressOptionsDialog::setTimeMAC()
{
  const CArcInfoEx *ai = arcInfo();
  if (!ai)
    return;

  bool m_allow = ai->Flags_MTime();
  bool c_allow = ai->Flags_CTime();
  bool a_allow = ai->Flags_ATime();

  if (ai->Is_Tar())
  {
    // The original gates on the chosen tar method (POSIX allows ATime). The Qt
    // compress dialog has no tar-method combo here, so we mirror the conservative
    // non-POSIX default (CompressDialog.cpp:3593-3603): C/A disabled for tar.
    c_allow = false;
    a_allow = false;
  }

  if (ai->Is_Zip())
  {
    UInt32 prec = getPrec();
    if (prec == (UInt32)(Int32)-1)
      prec = _autoPrec;
    if (prec != kTimePrec_Win)
    {
      c_allow = false;
      a_allow = false;
    }
  }

  _mTime.DefaultVal = ai->Flags_MTime_Default();
  _cTime.DefaultVal = ai->Flags_CTime_Default();
  _aTime.DefaultVal = ai->Flags_ATime_Default();
  _zTime.DefaultVal = false;

  // Re-apply the current widget pairs against the (possibly changed) allow flags.
  // The pairs were seeded in loadFromInfo; preserve the user's edits on re-gate by
  // reading them back first (mirror the original which keeps fo.* across SetPrec).
  const CBoolPair mp = getBoolBox(_mTime);
  const CBoolPair cp = getBoolBox(_cTime);
  const CBoolPair ap = getBoolBox(_aTime);
  const CBoolPair zp = getBoolBox(_zTime);

  checkBoolBox(m_allow, mp, _mTime);
  checkBoolBox(c_allow, cp, _cTime);
  checkBoolBox(a_allow, ap, _aTime);
  checkBoolBox(true,    zp, _zTime);
}


// === On_CheckBoxSet_*_Clicked (3655-3674) ===================================
void QtCompressOptionsDialog::onPrecSetToggled()
{
  const bool isSet = _precSet->isChecked();
  if (!isSet)
  {
    _timePrec = (UInt32)(Int32)-1; // Reset_TimePrec()
    setPrec();
  }
  _prec->setEnabled(isSet);
}

void QtCompressOptionsDialog::onTimeSetToggled()
{
  // A "Set" toggle: enable/disable its value box and reset it to the format
  // default when turned off (mirror On_CheckBoxSet_Clicked).
  CBoolBoxW *boxes[4] = { &_mTime, &_cTime, &_aTime, &_zTime };
  for (CBoolBoxW *bb : boxes)
  {
    const bool isSet = bb->Set->isChecked();
    if (!isSet)
      bb->Val->setChecked(bb->DefaultVal);
    bb->Val->setEnabled(isSet);
  }
}


// === OnInit mirror (3698-3761) ==============================================
void QtCompressOptionsDialog::loadFromInfo(const NCompressDialog::CInfo &info)
{
  _formatIndex = info.FormatIndex;
  const CArcInfoEx *ai = arcInfo();

  _symLinksSupported   = ai ? ai->Flags_SymLinks()   : false;
  _hardLinksSupported  = ai ? ai->Flags_HardLinks()  : false;
  _altStreamsSupported = ai ? ai->Flags_AltStreams() : false;
  _ntSecuritySupported = ai ? ai->Flags_NtSecurity() : false;

  // Show each link checkbox only when the format supports it (OnInit 3717-3727).
  _symLinks->setVisible(_symLinksSupported);
  _hardLinks->setVisible(_hardLinksSupported);
  _altStreams->setVisible(_altStreamsSupported);
  _ntSecurity->setVisible(_ntSecuritySupported);

  // CheckButton_Bool1: seed the value from Info (its CBoolPair carries the loaded
  // registry/command-line Val).
  _symLinks->setChecked(info.SymLinks.Val);
  _hardLinks->setChecked(info.HardLinks.Val);
  _altStreams->setChecked(info.AltStreams.Val);
  _ntSecurity->setChecked(info.NtSecurity.Val);
  _preserveATime->setChecked(info.PreserveATime.Val);

  // Seed the time fields from Info (mirror OnInit's fo.* read; in the Qt port
  // fillFromState already folded the per-format fo block into Info).
  _timePrec = info.TimePrec;
  // Seed the bool-box pairs by populating the widgets via checkBoolBox; setPrec()
  // -> setTimeMAC() then re-checks them against the format allow flags, but
  // setTimeMAC reads the current widget state, so prime it now from Info.
  _mTime.DefaultVal = ai ? ai->Flags_MTime_Default() : false;
  _cTime.DefaultVal = ai ? ai->Flags_CTime_Default() : false;
  _aTime.DefaultVal = ai ? ai->Flags_ATime_Default() : false;
  _zTime.DefaultVal = false;
  checkBoolBox(true, info.MTime, _mTime);
  checkBoolBox(true, info.CTime, _cTime);
  checkBoolBox(true, info.ATime, _aTime);
  checkBoolBox(true, info.SetArcMTime, _zTime);

  setPrec();
}


// === OnOK mirror (3797-3816) ================================================
void QtCompressOptionsDialog::applyToInfo(NCompressDialog::CInfo &info) const
{
  // Store_TimeBoxes(): TimePrec = GetPrecSpec(); the four time pairs.
  info.TimePrec = getPrecSpec();
  info.MTime = getBoolBox(_mTime);
  info.CTime = getBoolBox(_cTime);
  info.ATime = getBoolBox(_aTime);
  info.SetArcMTime = getBoolBox(_zTime);

  // Set_Final_BoolPairs for the link/security fields (CompressDialog.cpp:676-693):
  // when the format supports the field, Def=Val=checkbox; else Init() (clear), so
  // an unsupported field emits nothing. AltStreams / NtSecurity are NT-only and
  // never supported on Linux, so they always clear here regardless of the (always
  // disabled) checkbox state.
  auto finalize = [](bool supported, bool checked, CBoolPair &out)
  {
    if (supported)
    {
      out.Val = checked;
      out.Def = checked;
    }
    else
      out.Init();
  };

  finalize(_symLinksSupported,   _symLinks->isChecked(),   info.SymLinks);
  finalize(_hardLinksSupported,  _hardLinks->isChecked(),  info.HardLinks);
  finalize(_altStreamsSupported, _altStreams->isChecked(), info.AltStreams);
  finalize(_ntSecuritySupported, _ntSecurity->isChecked(), info.NtSecurity);

  // PreserveATime is always "supported" on Linux (mirror PreserveATime.Supported
  // = true). Use the same Set_Final_BoolPairs rule: Def=Val=checkbox.
  {
    const bool checked = _preserveATime->isChecked();
    info.PreserveATime.Val = checked;
    info.PreserveATime.Def = checked;
  }
}


// === GetPrecSpec / GetPrec (CompressDialog.h:444-451) =======================
UInt32 QtCompressOptionsDialog::getPrec() const
{
  if (_prec->count() <= 0 || _prec->currentIndex() < 0)
    return (UInt32)(Int32)-1;
  return (UInt32)_prec->currentData().toUInt();
}

UInt32 QtCompressOptionsDialog::getPrecSpec() const
{
  // GetComboValue(m_Prec, 1): -1 when count <= 1.
  if (_prec->count() <= 1 || _prec->currentIndex() < 0)
    return (UInt32)(Int32)-1;
  UInt32 prec = (UInt32)_prec->currentData().toUInt();
  if (prec == _autoPrec)
    prec = (UInt32)(Int32)-1;
  return prec;
}
