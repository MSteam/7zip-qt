// QtCompressDialog.h
//
// Qt/Linux mirror of GUI/CompressDialog.h's CCompressDialog (milestone C.3a).
//
// FIDELITY: this dialog produces a real NCompressDialog::CInfo (the verbatim
// replica in QtCompressInfo.h) — the same OUTPUT data contract the upstream
// dialog produces. OnInit()/OnOK() are mirrored by fillFromState() (run by the
// caller after field-copy-in) and onAccept() (Qt's OK path); they fill/read the
// SAME CInfo fields and drive the SAME NCompressionQt::CInfo persistence that
// CCompressDialog drives via NCompression::CInfo.
//
// C.3a WIDGETS (mirror the Win32 controls actually used at this milestone):
//   _archivePath   editable QComboBox + "..." browse (m_ArchivePath / SetArchive)
//   _format        QComboBox            (m_Format)
//   _level         QComboBox            (m_Level)         -- Level only
//   _updateMode    QComboBox            (m_UpdateMode)
//   _pathMode      QComboBox            (m_PathMode)
//   _password1     QLineEdit (password) (_password1Control)
//   _password2     QLineEdit (password) (_password2Control)
//   _showPassword  QCheckBox            (IDX_PASSWORD_SHOW)
//   _encryptMethod QComboBox            (_encryptionMethod)
//   _encryptHeaders QCheckBox           (IDX_COMPRESS_ENCRYPT_FILE_NAMES)
//
// C.3b-1 WIDGETS (this milestone): the Method / Dictionary / Order combos, mirror
// of m_Method / m_Dictionary / m_Order. They populate Info.Method / Info.Dict64 /
// Info.Order / Info.OrderMode, driven by the verbatim g_Formats / kMethodsNames
// tables in QtCompressMethods.h via setMethod()/setDictionary()/setOrder().
//
// C.3b-2 WIDGETS (this milestone): the Solid / Number-of-threads / Memory-use
// combos (mirror of m_Solid / m_NumThreads / m_MemUse) plus the two read-only
// "Memory usage for Compressing/Decompressing" labels (IDT_COMPRESS_MEMORY_VALUE
// / IDT_COMPRESS_MEMORY_DE_VALUE). They populate Info.SolidBlockSize /
// Info.SolidIsSpecified / Info.NumThreads / Info.MemUsage, driven by the verbatim
// SetSolidBlockSize2 / SetNumThreads2 / SetMemUseCombo and the verbatim memory
// estimator (GetMemoryUsage*) ported from CompressDialog.cpp. The OK path also
// gates accept on estimated-memory > the chosen MemUse limit (OnOK 1097-1119).
//
// All of these use the engine's own NSystem::GetRamSize / GetNumberOfProcessors
// (CProcessAffinity) on Linux for _ramSize / _ramSize_Reduced / _auto_NumThreads,
// exactly as CCompressDialog::OnInit / SetNumThreads2 do.

#ifndef ZIP7_INC_QT_COMPRESS_DIALOG_H
#define ZIP7_INC_QT_COMPRESS_DIALOG_H

#include "../../../Common/MyString.h"

#include "../Common/LoadCodecs.h" // CArcInfoEx, CObjectVector<CArcInfoEx>

#include "QtCompressInfo.h"        // NCompressDialog::CInfo
#include "QtCompressMethods.h"     // EMethodID + g_Formats / kMethodsNames tables
#include "QtCompressSettings.h"    // NCompressionQt::CInfo

#include <QtCore/QStringList>
#include <QtWidgets/QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLineEdit;
class QCheckBox;
class QToolButton;
class QLabel;
class QPlainTextEdit;
QT_END_NAMESPACE


class QtCompressDialog : public QDialog
{
  Q_OBJECT

public:
  explicit QtCompressDialog(QWidget *parent = nullptr);

  // OnInit mirror. The caller sets ArcFormats + Info (FormatIndex / ArcPath /
  // Password preset) BEFORE construction-time work, then calls fillFromState()
  // (load _regInfo, populate combos), then exec(). Mirrors CCompressDialog where
  // OnInit runs from Create() after the public fields are set.
  void fillFromState();

  // Read-back half of onAccept(): copy the current widget selections (format,
  // level, update mode, path mode, encryption method/headers, archive path) into
  // Info, WITHOUT validation or persistence. Used by the headless harness path so
  // it sees the same Info the OK button would produce from the loaded defaults.
  void syncInfoFromWidgets();

  // === public IN/OUT, mirrors CCompressDialog ================================
  // The set of archive formats (== codecs->Formats), set by the caller.
  const CObjectVector<CArcInfoEx> *ArcFormats;

  // Indices into ArcFormats of the formats offered in the Format combo (only
  // update-capable formats). Built by fillFromState() if left empty, mirroring
  // CCompressDialog::ArcIndices (which ShowDialog fills). Must contain
  // Info.FormatIndex if it is >= 0.
  CUIntVector ArcIndices;

  // The output data contract (== CCompressDialog::Info). The caller seeds
  // Info.ArcPath (default archive name), Info.FormatIndex, Info.Password.
  NCompressDialog::CInfo Info;

  // The external (plugin) method names (mirror CCompressDialog::ExternalMethods).
  // Set by the caller from codecs->Get_CodecsInfoUser_Vector() via setMethods()
  // BEFORE fillFromState(), exactly as UpdateGUI.cpp does via SetMethods().
  AStringVector ExternalMethods;

  // G.7d: the leaf name of the single source file (mirror CCompressDialog::
  // OriginalFileName). Set by the caller (QtUpdateGUI) BEFORE fillFromState() when
  // compressing exactly one file; used by SetArchiveName's KeepName branch (a
  // single-file gzip/bzip2 keeps the source name verbatim). Empty otherwise.
  UString OriginalFileName;

  // SetMethods mirror (CompressDialog.cpp 405-426): filter userCodecs down to the
  // external (non-built-in, single-stream, encoder-assigned, non-filter) method
  // names into ExternalMethods. Call before fillFromState().
  void setMethods(const CObjectVector<CCodecInfoUser> &userCodecs);

  // === headless harness hooks (drive the REAL setter code) ===================
  // Select the Format combo row for the given format name (e.g. "7z","zip"),
  // firing onFormatChanged -> the full SetLevel/SetMethod cascade. Returns false
  // if the format is not offered. Mirrors a user changing the Format combo.
  bool selectFormatByName(const UString &name);
  // Select the Level combo row whose value == level, firing the SetMethod cascade
  // (mirror OnCommand IDC_COMPRESS_LEVEL). Returns false if no such level row.
  bool selectLevelByValue(UInt32 level);
  // Select a method by built-in name (e.g. "LZMA2","PPMd","BZip2","Deflate",
  // "Copy"); returns false if not present in the current Method combo. Mirrors a
  // user picking that row, then runs the SetDictionary2/SetOrder2 cascade.
  bool selectMethodByName(const UString &name);
  // Select the dictionary / order combo item whose stored value == val. Returns
  // false if no such item (the combo keeps its auto default). val is the real
  // dictionary size in bytes / the real order number.
  bool selectDictionaryByValue(UInt64 val);
  bool selectOrderByValue(UInt32 val);
  // C.3b-2 headless selectors (drive the REAL Solid/Threads/MemUse combos so the
  // same getters the OK path reads pick up the choice). selectSolidByLogSize:
  // -1 picks the "* auto" row, 0 -> "Non-solid", 64 -> "Solid", else the 2^log
  // block-size row. selectNumThreadsByValue: -1 picks "* auto", else the row whose
  // data == n. selectMemUseByText: matches the registry-form mem string (e.g.
  // "90%", "16M") against a combo row, else returns false.
  bool selectSolidByLogSize(UInt32 logSize);
  bool selectNumThreadsByValue(UInt32 n);
  bool selectMemUseByText(const UString &spec);

  // C.6 headless hook: set the editable Volume combo's text directly (e.g. "100K"),
  // exactly as a user typing into m_Volume would. The OK path then runs the SAME
  // ParseVolumeSizes + min-size check on it. Used by the SEVENZQT_VOLUME override and
  // to seed the field from a parsed `-v` switch.
  void setVolumeText(const UString &text);
  // Read the Volume combo text into the given string (mirror m_Volume.GetText).
  UString volumeText() const;

  // G.7a headless hook: set the Parameters edit's text directly (e.g. "lc=4 pb=0"),
  // exactly as a user typing into m_Params would. The OK read-back then copies it to
  // Info.Options (m_Params.GetText(Info.Options) at OnOK 1209). Used by SEVENZQT_PARAMS.
  void setParamsText(const UString &text);
  // G.7b headless hook: toggle the "Delete files after compression" checkbox, exactly
  // as a user clicking IDX_COMPRESS_DEL would. OnOK reads it into Info.DeleteAfterCompressing.
  void setDeleteAfterCompressing(bool on);

  // G.7c headless hook: set the editable archive-path combo's edit text, exactly as a
  // user typing into m_ArchivePath would. This fires the editTextChanged handler, so
  // the ArcPath_WasChanged mirror runs: if the typed extension matches a different
  // format, the Format combo re-selects (and its FormatChanged cascade re-runs).
  void setArchivePathText(const UString &text);
  // G.7c headless hook: the per-format Browse filter list (mirror OnButtonSetArchive's
  // CBrowseFilterInfo build) as the descriptive strings the QFileDialog would show, in
  // combo order, plus the "archive (all exts)" and "All Files" trailers. For the test.
  UStringVector browseFilterStrings() const;

  // The estimated compress-memory bytes for the current selection (for the headless
  // debug line / tests). (UInt64)-1 means "unknown". Mirrors GetMemoryUsage_DecompMem.
  UInt64 estimatedCompressMemory(UInt64 &decompressMemory) const;
  // The currently selected memory-use limit in bytes (mirror Get_MemUse_Bytes()).
  UInt64 getMemUseBytes() const;
  // The OnOK RAM gate (CompressDialog.cpp 1097-1119): returns true if estimated
  // compress memory exceeds the chosen limit (accept should be BLOCKED). Fills
  // `message` with the SetErrorMessage_MemUsage text for display. Shared by
  // onAccept() and the headless harness.
  bool memGateBlocks(UString &message) const;

private slots:
  void onBrowse();              // mirrors OnButtonSetArchive()
  void onFormatChanged(int);    // mirrors FormatChanged()
  void onMethodChanged(int);    // mirrors MethodChanged() (SetDictionary2+SetOrder2)
  void onShowPasswordToggled(); // mirrors UpdatePasswordControl()
  void onOptions();             // mirrors IDB_COMPRESS_OPTIONS (open COptionsDialog)
  void onAccept();              // mirrors OnOK()

private:
  bool isShowPasswordChecked() const;

  // C.6: AddVolumeItems mirror (SplitUtils.cpp 75-79): populate the editable Volume
  // combo with the preset split-size labels (10M, 100M, ..., 23040M - BD).
  void addVolumeItems();

  // Format combo helpers (mirror GetFormatIndex / SetArchiveName).
  int currentFormatIndex() const;     // index into ArcFormats of current combo item
  void setArchiveName(const UString &name); // mirror SetArchiveName()

  // G.7c: ArcPath_WasChanged mirror (CompressDialog.cpp:1263-1287). When the typed/MRU
  // archive name carries an extension that belongs to a DIFFERENT format than the one
  // selected, re-select that format in the combo (which re-runs its FormatChanged
  // cascade). No-op if the extension already matches the current format or is unknown.
  void arcPathWasChanged(const UString &path);

  // G.7c: build the per-format Browse filter list (mirror OnButtonSetArchive's
  // CBrowseFilterInfo build). Fills `descriptions` (one per Format-combo row, in combo
  // order, then the "archive (all exts)" entry, then "All Files"), `qtFilters` (the same
  // entries as Qt "Name (*.ext ...)" name-filter strings), and `filterFormatRow` (for
  // each entry, the Format-combo row index it maps to, or -1 for the archive/all-files
  // trailers). Honors k_DontSave_Exts. Returns the number of per-format rows (numFormats).
  unsigned buildBrowseFilters(UStringVector &descriptions,
      QStringList &qtFilters, CRecordVector<int> &filterFormatRow) const;

  // G.7a: load the per-format remembered Parameters string into the edit (mirror
  // SetParams, CompressDialog.cpp:3244-3254). Run on FormatChanged.
  void setParams();
  // G.7f: rebuild the one-line read-only Options summary from Info's time/link
  // switches (mirror ShowOptionsString, CompressDialog.cpp:3353-3377).
  void showOptionsString();
  void setLevelCombo();               // mirror SetLevel2()
  void setEncryptionCombo();          // mirror SetEncryptionMethod()
  UString getEncryptionMethodSpec();  // mirror GetEncryptionMethodSpec()

  // === Method / Dictionary / Order cascade (mirror CompressDialog.{h,cpp}) ====
  // The static-table index of the current format (mirror GetStaticFormatIndex()).
  unsigned getStaticFormatIndex() const;
  // Current Level value (mirror GetLevel()/GetLevel2()).
  UInt32 getLevel() const;            // GetLevel(): -1 if none
  UInt32 getLevel2() const;           // GetLevel2(): 5 if none

  void setMethodCombo(int keepMethodId = -1); // mirror SetMethod2()
  void methodChanged();                       // mirror MethodChanged()
  void enableMultiCombo(QComboBox *combo);    // mirror EnableMultiCombo()

  int getMethodID_RAW() const;        // mirror GetMethodID_RAW()
  int getMethodID() const;            // mirror GetMethodID()
  UString getMethodSpec(UString &estimatedName) const; // mirror GetMethodSpec()
  UString getMethodSpec() const;
  bool isMethodEqualTo(const UString &s) const; // mirror IsMethodEqualTo()

  void setDictionaryCombo();          // mirror SetDictionary2()
  void setOrderCombo();               // mirror SetOrder2()
  bool getOrderMode() const;          // mirror GetOrderMode()

  // GetComboValue mirrors: read the stored value of the current item, or -1 when
  // the combo has <= defMax items (defMax=1 means "ignore the lone auto item").
  UInt32 comboValue(QComboBox *c, int defMax = 0) const;
  UInt64 comboValue64(QComboBox *c, int defMax = 0) const;

  // GetDict2/GetDictSpec (CompressDialog.h): the auto-resolving dictionary getter.
  UInt64 getDictSpec() const;         // GetComboValue_64(m_Dictionary, 1)
  UInt32 getOrderSpec() const;        // GetComboValue(m_Order, 1)
  UInt64 getDict2() const;            // GetDict2(): auto-resolving 64-bit dict

  // === C.3b-2 Solid / Threads / Memory cascade (mirror CompressDialog.{h,cpp}) =
  void setSolidBlockSize();           // mirror SetSolidBlockSize2()
  void setNumThreads();               // mirror SetNumThreads2()
  void setMemUseCombo();              // mirror SetMemUseCombo()
  int  addMemComboItem(UInt64 val, bool isPercent, bool isDefault = false); // mirror AddMemComboItem()
  void setMemoryUsage();              // mirror SetMemoryUsage()
  void printMemUsage(QLabel *label, UInt64 value, bool isCompress); // mirror PrintMemUsage()

  // The estimator, ported VERBATIM (CompressDialog.cpp 2880-3068).
  UInt64 getMemoryUsage_DecompMem(UInt64 &decompressMemory) const;
  UInt64 getMemoryUsage_Dict_DecompMem(UInt64 dict64, UInt64 &decompressMemory) const;
  UInt64 getMemoryUsage_Threads_Dict_DecompMem(UInt32 numThreads, UInt64 dict64, UInt64 &decompressMemory) const;

  // Get_MemUse_Spec / Get_MemUse_Bytes (CompressDialog.cpp 2857-2876).
  UString getMemUseSpec() const;

  // GetNumThreadsSpec / GetNumThreads2 / GetBlockSizeSpec (CompressDialog.h).
  UInt32 getNumThreadsSpec() const;   // GetComboValue(m_NumThreads, 1)
  UInt32 getNumThreads2() const;      // auto-resolving thread count
  UInt32 getBlockSizeSpec() const;    // GetComboValue(m_Solid, 1)

  bool isZipFormat() const;           // mirror IsZipFormat()
  bool isXzFormat() const;            // mirror IsXzFormat()

  QComboBox  *_archivePath;
  QToolButton *_browse;
  QComboBox  *_format;
  QComboBox  *_level;
  QComboBox  *_updateMode;
  QComboBox  *_pathMode;
  // C.6: editable Volume (split into volumes) combo (mirror m_Volume).
  QComboBox  *_volume;
  QLineEdit  *_password1;
  QLineEdit  *_password2;
  QCheckBox  *_showPassword;
  QComboBox  *_encryptMethod;
  QCheckBox  *_encryptHeaders;

  // === C.3b-1 ADVANCED COMBOS ================================================
  // Method / Dictionary / Order (mirror m_Method / m_Dictionary / m_Order).
  QComboBox  *_method;
  QComboBox  *_dictionary;
  QComboBox  *_order;

  // === C.3b-2 ADVANCED COMBOS ================================================
  // Solid / Threads / Memory-use (mirror m_Solid / m_NumThreads / m_MemUse) and the
  // two read-only memory-usage display labels (IDT_COMPRESS_MEMORY_VALUE /
  // IDT_COMPRESS_MEMORY_DE_VALUE).
  QComboBox  *_solid;
  QComboBox  *_numThreads;
  QComboBox  *_memUse;
  QLabel     *_memValueComp;  // "Memory usage for Compressing" value
  QLabel     *_memValueDecomp;// "Memory usage for Decompressing" value

  // === G.7 ADVANCED FIELDS ===================================================
  // Parameters free-form edit (mirror m_Params / IDE_COMPRESS_PARAMETERS), the
  // "Delete files after compression" checkbox (mirror IDX_COMPRESS_DEL) and the
  // read-only Options summary label (mirror IDT_COMPRESS_OPTIONS).
  QPlainTextEdit *_params;
  QCheckBox      *_deleteAfter;
  QLabel         *_optionsString;

  // The "auto" values computed by the setters (mirror _auto_MethodId / _auto_Dict
  // / _auto_Order). _auto_MethodId is the method id the empty "* auto" row maps to
  // (used by GetMethodID when no explicit row is picked). _auto_Dict / _auto_Order
  // are the level-derived defaults shown in the "* auto" combo rows.
  int    _auto_MethodId;
  UInt32 _auto_Dict;   // (UInt32)(Int32)-1 means unknown
  UInt32 _auto_Order;
  UInt64 _auto_Solid;       // mirror _auto_Solid: the level/dict-derived solid block
  UInt32 _auto_NumThreads;  // mirror _auto_NumThreads: the RAM/method-capped auto threads

  // RAM facts, computed in fillFromState() exactly as OnInit does, via the engine's
  // NSystem::GetRamSize (mirror CompressDialog.cpp 437-459).
  bool   _ramSize_Defined;
  size_t _ramSize;          // full RAM size avail
  size_t _ramSize_Reduced;  // full for 64-bit and reduced for 32-bit
  UInt64 _ramUsage_Auto;    // 80% of _ramSize_Reduced (auto usage limit in handlers)

  // The registry-form mem strings parallel to the MemUse combo rows (mirror
  // _memUse_Strings): m_MemUse item data is the index into this list.
  UStringVector _memUse_Strings;
  // ===========================================================================

  // The encryption-method combo's default index (mirror
  // _default_encryptionMethod_Index): the selection at this index means "use the
  // format default", so GetEncryptionMethodSpec() returns empty for it.
  int _defaultEncryptionMethodIndex;

  // The persisted settings (mirror CCompressDialog::m_RegistryInfo).
  NCompressionQt::CInfo _regInfo;

  // G.7a: the format index the Parameters edit currently belongs to (mirror
  // m_PrevFormat). onFormatChanged saves the edit text to this format's per-format
  // options (SaveOptionsInMem-of-Params) BEFORE re-deriving for the new format,
  // exactly as OnCommand IDC_COMPRESS_FORMAT does. -1 == not yet established.
  int _prevFormatIndex;

  // G.7c: re-entrancy guard for the archive-path editTextChanged handler. The handler
  // (arcPathWasChanged) may re-select the Format combo, whose onFormatChanged rewrites
  // the archive-path edit text (setArchiveName) — which would re-fire editTextChanged.
  // While set, the editTextChanged handler is a no-op (mirrors the original where the
  // SELCHANGE-only IDC_COMPRESS_ARCHIVE handler never re-enters on programmatic text).
  bool _inArcPathSync;
};

#endif
