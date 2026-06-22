// QtCompressOptionsDialog.h
//
// Qt/Linux mirror of GUI/CompressDialog.h's COptionsDialog (milestone B.9): the
// Compress "Options..." sub-dialog. It edits a NCompressDialog::CInfo in place,
// exposing the time-precision combo, the four time bool-boxes (MTime / CTime /
// ATime / ZTime==SetArcMTime) and the four link/security bool-boxes (SymLinks /
// HardLinks / AltStreams / NtSecurity), 1:1 with the upstream dialog.
//
// FIDELITY notes (mirror COptionsDialog, CompressDialog.cpp:3386-3818):
//  - CBoolBox: a value checkbox + a "Set" override checkbox per time field. When
//    "Set" is off the value follows the format DefaultVal and is disabled; the
//    stored CBoolPair is {Val=value, Def="Set"} (GetButton_BoolBox 3412-3417).
//  - The precision combo entries / labels come from AddPrec/SetPrec (3455-3564);
//    each item's itemData is the raw write-precision. "auto" == the format's
//    default precision; GetPrecSpec() collapses it to the -1 sentinel (no `tp`).
//  - SymLinks/HardLinks/AltStreams/NtSecurity are CBool1-style: a single value
//    checkbox; on OK the value is folded into the CInfo CBoolPair with the
//    Set_Final_BoolPairs rule (Def=Val when the format Supports it, else Init()).
//
// LINUX-APPROPRIATENESS (standing convention): AltStreams (NTFS ADS) and
// NtSecurity (NT ACLs) are NT-only. We mirror the extract-side NtSecurity
// treatment: the checkbox is created but disabled with a tooltip, and we never
// set its Def, so AddProp_BoolPair / options.X emit nothing and the engine is
// unaffected (Update.cpp:463-465 rejects them for non-supporting formats anyway).
// SymLinks / HardLinks / the time options stay fully functional on Linux.

#ifndef ZIP7_INC_QT_COMPRESS_OPTIONS_DIALOG_H
#define ZIP7_INC_QT_COMPRESS_OPTIONS_DIALOG_H

#include <QtWidgets/QDialog>

#include "../Common/LoadCodecs.h" // CArcInfoEx, CObjectVector<CArcInfoEx>

#include "QtCompressInfo.h"        // NCompressDialog::CInfo

QT_BEGIN_NAMESPACE
class QComboBox;
class QCheckBox;
class QLabel;
QT_END_NAMESPACE


class QtCompressOptionsDialog : public QDialog
{
  Q_OBJECT

public:
  explicit QtCompressOptionsDialog(QWidget *parent = nullptr);

  // The full format set (== codecs->Formats), set by the caller BEFORE
  // loadFromInfo(): the sub-dialog reads ArcFormats[Info.FormatIndex].Flags_* to
  // gate which controls are supported, exactly like COptionsDialog::OnInit.
  const CObjectVector<CArcInfoEx> *ArcFormats;

  // OnInit mirror: read the time / link CBoolPairs + TimePrec out of `info` into
  // the widgets (gated by the chosen format's flags). Must run after ArcFormats
  // and info.FormatIndex are set.
  void loadFromInfo(const NCompressDialog::CInfo &info);

  // OnOK mirror (Store_TimeBoxes + Set_Final_BoolPairs + the fo time block): write
  // the widget state back into `info` (TimePrec, the 4 time CBoolPairs, the 4
  // link/security CBoolPairs). Call only on Accepted.
  void applyToInfo(NCompressDialog::CInfo &info) const;

private slots:
  // On_CheckBoxSet_Clicked (3668-3674): a time "Set" toggle enables/resets its
  // value box. On_CheckBoxSet_Prec_Clicked (3655-3666): the precision "Set" toggle
  // resets the combo to auto when unchecked.
  void onTimeSetToggled();
  void onPrecSetToggled();

private:
  // A faithful CBoolBox: value + "Set" override checkbox, plus the per-format
  // support flag and DefaultVal (CompressDialog.h:396-415).
  struct CBoolBoxW
  {
    QCheckBox *Val;
    QCheckBox *Set;
    bool IsSupported;
    bool DefaultVal;
    CBoolBoxW(): Val(nullptr), Set(nullptr), IsSupported(false), DefaultVal(false) {}
  };

  const CArcInfoEx *arcInfo() const; // ArcFormats[_formatIndex] or nullptr

  // AddPrec/SetPrec mirror (3455-3564): rebuild the precision combo for the chosen
  // format; seed the selection from _timePrec (or the format default == auto).
  int addPrec(unsigned prec, bool isDefault);
  void setPrec();
  // GetPrecSpec (CompressDialog.h:444-450): the selected write-precision, or -1
  // when the selection == the format default (auto).
  UInt32 getPrecSpec() const;
  UInt32 getPrec() const; // GetPrec(): the raw selected precision (no -1 collapse)

  // CheckButton_BoolBox / GetButton_BoolBox (3397-3417).
  void checkBoolBox(bool supported, const CBoolPair &bp, CBoolBoxW &bb);
  static CBoolPair getBoolBox(const CBoolBoxW &bb);

  // SetTimeMAC (3584-3651): per-format allow flags + defaults for the 4 time boxes.
  void setTimeMAC();

  int _formatIndex; // == Info.FormatIndex at loadFromInfo time

  QComboBox *_prec;
  UInt32 _autoPrec; // the format default precision index (== AddPrec curSel base)
  UInt32 _timePrec; // -1 == auto/unset (Reset_TimePrec sentinel)
  QCheckBox *_precSet; // the IDX_COMPRESS_PREC_SET override toggle
  QLabel *_timeInfo;   // IDT_COMPRESS_TIME_INFO (format + method label)

  CBoolBoxW _mTime;
  CBoolBoxW _cTime;
  CBoolBoxW _aTime;
  CBoolBoxW _zTime; // ZTime == SetArcMTime (store the archive's own mtime)

  // SymLinks/HardLinks/AltStreams/NtSecurity: single value checkboxes (CBool1).
  // We keep the per-format Supported flag alongside so applyToInfo can run the
  // Set_Final_BoolPairs merge (Def=Val when supported, else Init()).
  QCheckBox *_symLinks;   bool _symLinksSupported;
  QCheckBox *_hardLinks;  bool _hardLinksSupported;
  QCheckBox *_altStreams; bool _altStreamsSupported; // NT-only: disabled on Linux
  QCheckBox *_ntSecurity; bool _ntSecuritySupported; // NT-only: disabled on Linux
  QCheckBox *_preserveATime;
};

#endif
