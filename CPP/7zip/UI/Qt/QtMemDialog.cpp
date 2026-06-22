// QtMemDialog.cpp
//
// See QtMemDialog.h. Method-by-method this mirrors CMemDialog (MemDialog.cpp):
//   AddInfoMessage_To_String  -> the message-text builder below
//   OnInit                    -> the dialog construction in Ask()
//   OnButtonClicked           -> the "Change allowed limit" -> enable-spin toggle
//   OnContinue                -> the OK-time value read + validation
// substituting Win32 controls for Qt widgets and LangString(id) for FmLang(id,...).
// The threading/headless contract mirrors QtExtractPrompts.cpp.

#include "QtMemDialog.h"

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

#include <QtCore/QByteArray>

#include "../../../Windows/System.h"    // NSystem::GetRamSize (the Linux impl)

#include "QtLang.h"
// Original numeric langIDs so a loaded Lang/*.txt translates these strings,
// byte-equivalent to upstream LangString(id) with the .rc fallback. (None of
// these headers carry include guards, so each ID set is pulled from exactly one
// of them: resourceGui.h owns the IDS_MEM_* size labels + IDS_PROGRESS_TESTING,
// MemDialogRes.h the control IDs, GUI/ExtractRes.h IDS_MEM_ERROR +
// IDS_PROGRESS_EXTRACTING. No define overlaps between them.)
#include "../FileManager/resourceGui.h"      // IDS_MEM_REQUIRES_BIG_MEM / IDS_MEM_REQUIRED_MEM_SIZE / IDS_MEM_CURRENT_MEM_LIMIT / IDS_MEM_RAM_SIZE / IDS_PROGRESS_TESTING
#include "../FileManager/MemDialogRes.h"     // IDD_MEM / IDX_MEM_SAVE_LIMIT / IDX_MEM_REMEMBER / IDG_MEM_ACTION / IDR_MEM_ACTION_ALLOW / IDR_MEM_ACTION_SKIP_ARC
#include "../GUI/ExtractRes.h"               // IDS_MEM_ERROR (header) + IDS_PROGRESS_EXTRACTING

namespace {

// Mirrors QtExtractPrompts.cpp's IsHeadless() (and the g_DisableUserQuestions
// intent): true when there is no interactive display to host a modal dialog.
bool IsHeadless()
{
  const QByteArray qpa = qgetenv("QT_QPA_PLATFORM");
  if (qpa.contains("offscreen") || qpa.contains("minimal"))
    return true;
  return qgetenv("DISPLAY").isEmpty() && qgetenv("WAYLAND_DISPLAY").isEmpty();
}

// Mirror of MemDialog.cpp's AddSize_GB(): "\n    <N> GB : <label>".
void AddSize_GB(QString &s, unsigned size_GB, unsigned id, const QString &englishLabel)
{
  s += QLatin1Char('\n');
  s += QStringLiteral("    ");
  s += QString::number(size_GB);
  s += QStringLiteral(" GB : ");
  s += FmLang(id, englishLabel);
}

// Mirror of CMemDialog::AddInfoMessage_To_String (MemDialog.cpp:58-71): the
// "requires big memory" header + required/limit/(RAM) sizes + optional file line.
QString BuildInfoMessage(unsigned requiredGB, unsigned limitGB,
    const unsigned *ramSizeGB, const QString &filePath)
{
  QString s = FmLang(IDS_MEM_REQUIRES_BIG_MEM,
      QStringLiteral("The operation requires big amount of memory (RAM)."));
  AddSize_GB(s, requiredGB, IDS_MEM_REQUIRED_MEM_SIZE,
      QStringLiteral("required memory usage size"));
  AddSize_GB(s, limitGB, IDS_MEM_CURRENT_MEM_LIMIT,
      QStringLiteral("allowed memory usage limit"));
  if (ramSizeGB)
    AddSize_GB(s, *ramSizeGB, IDS_MEM_RAM_SIZE, QStringLiteral("RAM size"));
  if (!filePath.isEmpty())
  {
    s += QLatin1Char('\n');
    s += QStringLiteral("File: ");
    s += filePath;
  }
  return s;
}

} // namespace


// === QtMemDialog::Ask : GUI-thread analogue of CMemDialog =====================
void QtMemDialog::Ask(
    int requiredGB, int limitGB,
    bool testMode, QString arcPath, QString filePath,
    bool showRemember,
    int *outLimitGB, bool *outNeedSave, bool *outRemember,
    bool *outSkipArc, bool *outAccepted)
{
  // Defaults mirror CMemDialog's initial member state: limit unchanged, nothing
  // saved/remembered, Allow (no skip).
  *outLimitGB  = limitGB;
  *outNeedSave = false;
  *outRemember = false;
  *outSkipArc  = false;
  *outAccepted = true;

  // ---- RAM size (mirror CMemDialog::OnInit's GetRamSize block) --------------
  size_t ramSize = (size_t)sizeof(size_t) << 29;
  const bool ramSize_defined = NWindows::NSystem::GetRamSize(ramSize);
  unsigned ramSize_GB = (unsigned)(((UInt64)ramSize + (1u << 29)) >> 30);
  if (ramSize_GB == 0)
    ramSize_GB = 1;

  // is_Allowed: there IS enough RAM (the dialog pre-selects Allow). Mirror
  // MemDialog.cpp:99.
  const bool is_Allowed = (!ramSize_defined || ramSize > ((UInt64)requiredGB << 30));

  if (IsHeadless())
  {
    // No display: the engine's own headless path (g_DisableUserQuestions) leaves
    // the default k_Allow and does not show CMemDialog. We mirror that: Allow,
    // limit unchanged, accepted. SEVENZQT_MEM_SKIP=1 lets a test drive the
    // Skip-archive answer through the same out-field plumbing.
    if (!qgetenv("SEVENZQT_MEM_SKIP").isEmpty())
      *outSkipArc = true;
    return;
  }

  QDialog dlg(_parentWidget);
  // Caption: original CMemDialog::OnInit -> LangSetWindowText(*this, IDD_MEM)
  // (the IDD_MEM CAPTION is "Memory usage request", MemDialog.rc).
  dlg.setWindowTitle(FmLang(IDD_MEM, QStringLiteral("Memory usage request")));
  dlg.setModal(true);

  QVBoxLayout *lay = new QVBoxLayout(&dlg);

  // ---- the message text (mirror OnInit's IDT_MEM_MESSAGE build) -------------
  {
    QString s;
    if (!is_Allowed)
    {
      // IDS_MEM_ERROR "The system cannot allocate the required amount of memory"
      s += FmLang(IDS_MEM_ERROR,
          QStringLiteral("The system cannot allocate the required amount of memory"));
      s += QLatin1Char('\n');
    }
    s += BuildInfoMessage(requiredGB, limitGB,
        is_Allowed ? nullptr : &ramSize_GB, filePath);
    if (!arcPath.isEmpty())
    {
      s += QLatin1Char('\n');
      // IDS_PROGRESS_TESTING / IDS_PROGRESS_EXTRACTING + ": " + arc path.
      s += testMode
          ? FmLang(IDS_PROGRESS_TESTING, QStringLiteral("Testing"))
          : FmLang(IDS_PROGRESS_EXTRACTING, QStringLiteral("Extracting"));
      s += QStringLiteral(": ");
      s += arcPath;
    }
    QLabel *msg = new QLabel(s, &dlg);
    msg->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(msg);
  }

  // ---- "Change allowed limit for next operations" checkbox -----------------
  // IDX_MEM_SAVE_LIMIT (MemDialog.rc:20).
  QCheckBox *saveLimit = new QCheckBox(
      FmLang(IDX_MEM_SAVE_LIMIT,
          QStringLiteral("Change allowed limit for next operations")), &dlg);
  lay->addWidget(saveLimit);

  // ---- the GB spin (mirror OnInit's UDM_SETRANGE/UDM_SETPOS + GB label) -----
  // valMin=1; valMax RAM-aware (MemDialog.cpp:129-140), capped at 1<<14 (RAR7).
  QSpinBox *spin = new QSpinBox(&dlg);
  {
    const unsigned valMin = 1;
    unsigned valMax = 64; // 64GB for RAR7
    if (ramSize_defined)
    {
      const unsigned k_max_val = 1u << 14;
      if (ramSize_GB >= k_max_val)
        valMax = k_max_val;
      else if (ramSize_GB > 1)
        valMax = ramSize_GB - 1;
      else
        valMax = 1;
    }
    spin->setRange((int)valMin, (int)valMax);
    // UDM_SETPOS seeds the spin with Required_GB (MemDialog.cpp:144); the spin
    // clamps to its max, exactly like UDM_SETPOS won't exceed valMax.
    spin->setValue((int)requiredGB);
    spin->setSuffix(QStringLiteral(" GB"));
  }
  // The spin starts disabled and is enabled by the "save limit" checkbox
  // (CMemDialog::EnableSpin(false) in OnInit, toggled in OnButtonClicked).
  spin->setEnabled(false);
  QObject::connect(saveLimit, &QCheckBox::toggled, spin, &QSpinBox::setEnabled);
  lay->addWidget(spin);

  // ---- Action group: Allow / Skip-archive (mirror IDG_MEM_ACTION radios) ----
  QGroupBox *actionBox = new QGroupBox(
      FmLang(IDG_MEM_ACTION, QStringLiteral("Action")), &dlg);
  QVBoxLayout *av = new QVBoxLayout(actionBox);
  QRadioButton *rAllow = new QRadioButton(
      FmLang(IDR_MEM_ACTION_ALLOW, QStringLiteral("Allow archive unpacking")), actionBox);
  QRadioButton *rSkip = new QRadioButton(
      FmLang(IDR_MEM_ACTION_SKIP_ARC, QStringLiteral("Skip archive unpacking")), actionBox);
  av->addWidget(rAllow);
  av->addWidget(rSkip);
  // Default selection: Allow if there is enough RAM, else Skip-archive
  // (CMemDialog::OnInit buttonId = is_Allowed ? ALLOW : SKIP_ARC).
  if (is_Allowed)
    rAllow->setChecked(true);
  else
    rSkip->setChecked(true);
  lay->addWidget(actionBox);

  // ---- "Repeat selected action for current operation" (Remember) -----------
  // IDX_MEM_REMEMBER; hidden when !ShowRemember (MemDialog.cpp:172-173).
  QCheckBox *remember = new QCheckBox(
      FmLang(IDX_MEM_REMEMBER,
          QStringLiteral("Repeat selected action for current operation")), &dlg);
  remember->setVisible(showRemember);
  lay->addWidget(remember);

  // ---- Continue / Cancel (mirror CONTINUE_CANCEL) --------------------------
  // The Win32 dialog uses Continue/Cancel; OnContinue accepts. IDCANCEL -> Stop.
  QDialogButtonBox *box = new QDialogButtonBox(&dlg);
  // "Continue" == langID 411 (kLangPairs {IDCONTINUE,411}, LangUtils.cpp:71);
  // Cancel == 402.
  QPushButton *cont = box->addButton(
      FmLang(411, QStringLiteral("Continue")), QDialogButtonBox::AcceptRole);
  box->addButton(FmLang(402, QStringLiteral("Cancel")), QDialogButtonBox::RejectRole);
  lay->addWidget(box);

  // OnContinue validates the spin value (only when "save limit" is ticked) before
  // accepting; an out-of-range value shows the error and keeps the dialog open
  // (MemDialog.cpp:194-216). We do the same in the accept handler.
  QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  QObject::connect(cont, &QPushButton::clicked, &dlg, [&]() {
    if (saveLimit->isChecked())
    {
      // QSpinBox is already range-bounded, but mirror the original's explicit
      // "> 1<<30 => E_INVALIDARG" rejection for completeness. (The spin can't
      // exceed 1<<14 here, so this never trips; kept for fidelity.)
      const long v = spin->value();
      if (v < 1 || v > (long)(1u << 30))
      {
        QMessageBox::critical(&dlg, dlg.windowTitle(),
            QStringLiteral("The parameter is incorrect."));
        return;
      }
    }
    dlg.accept();
  });

  if (dlg.exec() == QDialog::Accepted)
  {
    // Mirror CMemDialog::OnContinue: read Remember / NeedSave / SkipArc; Limit_GB
    // updated only when NeedSave (MemDialog.cpp:189-217).
    *outRemember = remember->isChecked();
    *outNeedSave = saveLimit->isChecked();
    *outSkipArc  = rSkip->isChecked();
    if (*outNeedSave)
      *outLimitGB = spin->value();
    *outAccepted = true;
  }
  else
  {
    // IDCANCEL -> CMemDialog returns != IDCONTINUE -> RequestMemoryUse answers
    // k_Stop and returns E_ABORT.
    *outAccepted = false;
  }
}
