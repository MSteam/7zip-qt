// QtCompressDialog.cpp
//
// Mirror of GUI/CompressDialog.cpp's CCompressDialog, C.3a scope (Level only).
// Layout uses Qt widgets but the option enums, combo order, enum<->index mapping
// and load/save semantics track the original (cited inline).

#include "QtCompressDialog.h"
#include "QtCompressOptionsDialog.h" // B.9 "Options..." sub-dialog
#include "QtLang.h"                              // P.2 : FmLang
#include "../GUI/CompressDialogRes.h"            // P.2 : IDD_COMPRESS=4000, level/update/label/split langIDs
#include "../GUI/ExtractRes.h"                   // G.1 : IDS_EXTRACT_PATHS_*/IDS_PATH_MODE_RELAT (path-mode), IDS_MEM_ERROR
#include "../FileManager/resourceGui.h"          // G.1 : IDS_MEM_* mem-gate group, IDS_INCORRECT_VOLUME_SIZE

#include "../../../Common/IntToString.h"
#include "../../../Common/StringConvert.h"  // GetUnicodeString (external method names)
#include "../../../Common/StringToInt.h"    // ConvertStringToUInt64 (CMemUse::Parse)

#include "../../../../C/CpuArch.h"          // (size_t) helpers are in MyTypes; harmless
#include "../../../7zip/Common/MethodProps.h" // Calc_From_Val_Percents (mem-limit math)
#include "../../../Windows/System.h"        // NSystem::GetRamSize / CProcessAffinity

#include <QtCore/QVariant>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>


using namespace NCompressMethods;

// === NCompression::CMemUse::Parse : VERBATIM from Common/ZipRegistry.cpp ======
// (lines 377-456). ZipRegistry.cpp itself is Win32-only (it includes
// Windows/Registry.h and calls the Win32 registry) and so is NOT compiled into the
// engine on Linux; but its CMemUse::Parse (used by Get_MemUse_Bytes / the OnOK
// memuse population) is pure string parsing with no Win32 dependency. So, exactly
// as we lifted the CInfo data contract and the g_Formats tables, we provide JUST
// this one method here verbatim so it links. (CMemUse itself is declared in the
// engine's ZipRegistry.h, which IS compiled.)
namespace NCompression {

static bool ParseMemUse(const wchar_t *s, CMemUse &mu)
{
  mu.Clear();

  bool percentMode = false;
  {
    const wchar_t c = *s;
    if (MyCharLower_Ascii(c) == 'p')
    {
      percentMode = true;
      s++;
    }
  }
  const wchar_t *end;
  UInt64 number = ConvertStringToUInt64(s, &end);
  if (end == s)
    return false;

  wchar_t c = *end;

  if (percentMode)
  {
    if (c != 0)
      return false;
    mu.IsPercent = true;
    mu.Val = number;
    return true;
  }

  if (c == 0)
  {
    mu.Val = number;
    return true;
  }

  c = MyCharLower_Ascii(c);

  const wchar_t c1 = end[1];

  if (c == '%')
  {
    if (c1 != 0)
      return false;
    mu.IsPercent = true;
    mu.Val = number;
    return true;
  }

  if (c == 'b')
  {
    if (c1 != 0)
      return false;
    mu.Val = number;
    return true;
  }

  if (c1 != 0)
    if (MyCharLower_Ascii(c1) != 'b' || end[2] != 0)
      return false;

  unsigned numBits;
  switch (c)
  {
    case 'k': numBits = 10; break;
    case 'm': numBits = 20; break;
    case 'g': numBits = 30; break;
    case 't': numBits = 40; break;
    default: return false;
  }
  if (number >= ((UInt64)1 << (64 - numBits)))
    return false;
  mu.Val = number << numBits;
  return true;
}

void CMemUse::Parse(const UString &s)
{
  IsDefined = ParseMemUse(s, *this);
}

} // namespace NCompression
// === end verbatim ============================================================


// === mapping tables : MIRROR CompressDialog.cpp ==============================
namespace {

// Verbatim constants from CompressDialog.cpp (lines 88-91, 1823).
const UInt32 kLzmaMaxDictSize = (UInt32)15 << 28;
const size_t k_Auto_Dict = (size_t)0 - 1;
const UInt32 kSolidLog_NoSolid = 0;
const UInt32 kSolidLog_FullSolid = 64;

// CompressDialog.cpp 2557.
const char * const k_ST_Threads = " (ST)";

// Modify_Auto mirror (CompressDialog.cpp 1620-1625): prepend "*  " to the label
// of an auto/default combo item.
const char * const k_Auto_Prefix = "*  ";

void modifyAuto(AString &s)
{
  s.Insert(0, k_Auto_Prefix);
}

// Combo_AddDict2 mirror (CompressDialog.cpp 1825-1840): format `sizeShow` as a
// "<n> [K|M]B" label, mark auto with the "*  " prefix, store `sizeReal` as the
// item's data. Returns the new combo index.
int comboAddDict2(QComboBox *cb, size_t sizeReal, size_t sizeShow)
{
  char c = 0;
  unsigned moveBits = 0;
       if ((sizeShow & 0xFFFFF) == 0) { moveBits = 20; c = 'M'; }
  else if ((sizeShow &   0x3FF) == 0) { moveBits = 10; c = 'K'; }
  AString s;
  s.Add_UInt64(sizeShow >> moveBits);
  s.Add_Space();
  if (c != 0)
    s.Add_Char(c);
  s.Add_Char('B');
  if (sizeReal == k_Auto_Dict)
    modifyAuto(s);
  const int index = cb->count();
  cb->addItem(QString::fromLatin1(s.Ptr()), QVariant((qulonglong)(quint64)(UInt64)sizeReal));
  return index;
}

// --- Level labels (CompressDialog.cpp g_Levels[], lines 110-122). The standard
//     7-Zip level set: 0=Store, 1=Fastest, 3=Fast, 5=Normal, 7=Maximum, 9=Ultra.
//     Levels actually offered are gated by the per-format LevelsMask (below).
// G.1 : langId from CompressDialog.cpp g_Levels[] (lines 110-122, indexed by level
// value): 0=IDS_METHOD_STORE .. 9=IDS_METHOD_ULTRA. English = the .rc STRINGTABLE
// text (CompressDialog.rc 206-211).
struct LevelItem { int value; const char *label; unsigned langId; };
const LevelItem kLevels[] =
{
  { 0, "Store",   IDS_METHOD_STORE },
  { 1, "Fastest", IDS_METHOD_FASTEST },
  { 3, "Fast",    IDS_METHOD_FAST },
  { 5, "Normal",  IDS_METHOD_NORMAL },
  { 7, "Maximum", IDS_METHOD_MAXIMUM },
  { 9, "Ultra",   IDS_METHOD_ULTRA },
};

// Per-format LevelsMask, mirroring CompressDialog.cpp's g_Formats[] entries:
//   7z / copy(generic) : (1<<10)-1            -> bits 0..9 all set
//   zip                : 0|1|3|5|7|9 selected -> { 0,1,3,5,7,9 }
// (Other formats fall back to the zip-like even set, which is the conservative
// CompressDialog behavior for non-7z compressors.)
UInt32 levelsMaskForFormat(const UString &name)
{
  if (name.IsEqualTo_Ascii_NoCase("7z") || name.IsEmpty())
    return ((UInt32)1 << 10) - 1;
  // zip and the rest: 0,1,3,5,7,9
  return (1u << 0) | (1u << 1) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 9);
}

// --- Update-mode labels (CompressDialog.cpp k_UpdateMode_IDs / _Vals 378-384).
//     Order MUST match: Add / Update / Refresh(Fresh) / Synchronize(Sync). langId
//     from k_UpdateMode_IDs[]; English from the .rc STRINGTABLE (CompressDialog.rc
//     213-216). (The inline literals are the port's existing English fallback.)
struct ComboItem { const char *label; int value; unsigned langId; };
const ComboItem kUpdateModeItems[] =
{
  { "Add and replace files",  NCompressDialog::NUpdateMode::kAdd,    IDS_COMPRESS_UPDATE_MODE_ADD },
  { "Update and add files",   NCompressDialog::NUpdateMode::kUpdate, IDS_COMPRESS_UPDATE_MODE_UPDATE },
  { "Refresh existing files", NCompressDialog::NUpdateMode::kFresh,  IDS_COMPRESS_UPDATE_MODE_FRESH },
  { "Synchronize files",      NCompressDialog::NUpdateMode::kSync,   IDS_COMPRESS_UPDATE_MODE_SYNC },
};

// --- Path-mode labels (CompressDialog.cpp k_PathMode_IDs / _Vals 396-401).
//     Order MUST match: Relative / Full / Absolute. langId from k_PathMode_IDs[]
//     (ExtractRes.h); English from Extract.rc 24-27.
const ComboItem kPathModeItems[] =
{
  { "Relative pathnames", NWildcard::k_RelatPath, IDS_PATH_MODE_RELAT },
  { "Full pathnames",     NWildcard::k_FullPath,  IDS_EXTRACT_PATHS_FULL },
  { "Absolute pathnames", NWildcard::k_AbsPath,   IDS_EXTRACT_PATHS_ABS },
};

const unsigned kHistorySize = 16; // mirrors CompressDialog.cpp kHistorySize

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

void addComboItems(QComboBox *combo, const ComboItem *items, unsigned n, int curVal)
{
  int curSel = 0;
  for (unsigned i = 0; i < n; i++)
  {
    combo->addItem(FmLang(items[i].langId, QString::fromUtf8(items[i].label)), items[i].value);
    if (items[i].value == curVal)
      curSel = (int)i;
  }
  combo->setCurrentIndex(curSel);
}

// AddUniqueString mirror (CompressDialog.cpp): case-insensitive de-dup.
void addUniqueString(UStringVector &list, const UString &s)
{
  if (s.IsEmpty())
    return;
  FOR_VECTOR (i, list)
    if (s.IsEqualTo_NoCase(list[i]))
      return;
  list.Add(s);
}

// GetExtDotPos mirror (CompressDialog.cpp 773-779).
int getExtDotPos(const UString &s)
{
  const int dotPos = s.ReverseFind_Dot();
  if (dotPos > s.ReverseFind_PathSepar() + 1)
    return dotPos;
  return -1;
}


// === C.3b-2 verbatim helpers from CompressDialog.cpp =========================

// Get_Lzma2_ChunkSize mirror (CompressDialog.cpp 2375-2387).
UInt64 Get_Lzma2_ChunkSize(UInt64 dict)
{
  // we use same default chunk sizes as defined in 7z encoder and lzma2 encoder
  UInt64 cs = (UInt64)dict << 2;
  const UInt32 kMinSize = (UInt32)1 << 20;
  const UInt32 kMaxSize = (UInt32)1 << 28;
  if (cs < kMinSize) cs = kMinSize;
  if (cs > kMaxSize) cs = kMaxSize;
  if (cs < dict) cs = dict;
  cs += (kMinSize - 1);
  cs &= ~(UInt64)(kMinSize - 1);
  return cs;
}

// Add_Size mirror (CompressDialog.cpp 2390-2402): the "<n> [K|M|G]B" solid-size label.
void Add_Size(AString &s, UInt64 val)
{
  unsigned moveBits = 0;
  char c = 0;
       if ((val & 0x3FFFFFFF) == 0) { moveBits = 30; c = 'G'; }
  else if ((val &    0xFFFFF) == 0) { moveBits = 20; c = 'M'; }
  else if ((val &      0x3FF) == 0) { moveBits = 10; c = 'K'; }
  s.Add_UInt64(val >> moveBits);
  s.Add_Space();
  if (moveBits != 0)
    s.Add_Char(c);
  s.Add_Char('B');
}

// AddMemSize mirror (CompressDialog.cpp 2714-2728): the "<n> [G|M]B" mem-combo label.
void AddMemSize(UString &res, UInt64 size)
{
  char c;
  unsigned moveBits = 0;
  if (size >= ((UInt64)1 << 31) && (size & 0x3FFFFFFF) == 0)
    { moveBits = 30; c = 'G'; }
  else // if (size >= ((UInt32)1 << 21) && (size & 0xFFFFF) == 0)
    { moveBits = 20; c = 'M'; }
  // else { moveBits = 10; c = 'K'; }
  res.Add_UInt64(size >> moveBits);
  res.Add_Space();
  if (moveBits != 0)
    res.Add_Char(c);
  res.Add_Char('B');
}

// AddMemUsage mirror (CompressDialog.cpp 3072-3096): the displayed "<n> MB/GB/TB".
void AddMemUsage(UString &s, UInt64 v)
{
  const char *post;
  if (v <= ((UInt64)16 << 30))
  {
    v = (v + (1 << 20) - 1) >> 20;
    post = "MB";
  }
  else if (v <= ((UInt64)64 << 40))
  {
    v = (v + (1 << 30) - 1) >> 30;
    post = "GB";
  }
  else
  {
    const UInt64 v2 = v + ((UInt64)1 << 40) - 1;
    if (v <= v2)
      v = v2;
    v >>= 40;
    post = "TB";
  }
  s.Add_UInt64(v);
  s.Add_Space();
  s += post;
}

// AddSize_MB / AddSize_MB_id mirror (CompressDialog.cpp 1030-1044). Used by the
// memGate error message. AddLangString is replaced by the literal English label
// (we do not link the Win32 LangString table); the numbers are byte-identical.
void AddSize_MB(UString &s, UInt64 size)
{
  s.Add_LF();
  const UInt64 v2 = size + ((UInt32)1 << 20) - 1;
  if (size < v2)
      size = v2;
  s.Add_UInt64(size >> 20);
  s += " MB : ";
}


// === ParseVolumeSizes : VERBATIM from FileManager/SplitUtils.cpp (lines 9-58) =
// SplitUtils.cpp itself is Win32-tied (its header includes
// Windows/Control/ComboBox.h) and is not compiled on Linux, but ParseVolumeSizes
// is pure string parsing (only ConvertStringToUInt64 / MyCharLower_Ascii), so we
// lift it here verbatim exactly as CompressDialog's OnOK calls it. Byte-identical.
bool ParseVolumeSizes(const UString &s, CRecordVector<UInt64> &values)
{
  values.Clear();
  bool prevIsNumber = false;
  for (unsigned i = 0; i < s.Len();)
  {
    wchar_t c = s[i++];
    if (c == L' ')
      continue;
    if (c == L'-')
      return true;
    if (prevIsNumber)
    {
      prevIsNumber = false;
      unsigned numBits = 0;
      switch (MyCharLower_Ascii(c))
      {
        case 'b': continue;
        case 'k': numBits = 10; break;
        case 'm': numBits = 20; break;
        case 'g': numBits = 30; break;
        case 't': numBits = 40; break;
      }
      if (numBits != 0)
      {
        UInt64 &val = values.Back();
        if (val >= ((UInt64)1 << (64 - numBits)))
          return false;
        val <<= numBits;

        for (; i < s.Len(); i++)
          if (s[i] == L' ')
            break;
        continue;
      }
    }
    i--;
    const wchar_t *start = s.Ptr(i);
    const wchar_t *end;
    UInt64 val = ConvertStringToUInt64(start, &end);
    if (start == end)
      return false;
    if (val == 0)
      return false;
    values.Add(val);
    prevIsNumber = true;
    i += (unsigned)(end - start);
  }
  return true;
}

// k_Sizes : VERBATIM from FileManager/SplitUtils.cpp (lines 61-73) ============
const char * const k_Sizes[] =
{
    "10M"
  , "100M"
  , "1000M"
  , "650M - CD"
  , "700M - CD"
  , "4092M - FAT"
  , "4480M - DVD"     //  4489 MiB limit
  , "8128M - DVD DL"  //  8147 MiB limit
  , "23040M - BD"     // 23866 MiB limit
  // , "1457664 - 3.5\" floppy"
};

} // namespace


QtCompressDialog::QtCompressDialog(QWidget *parent)
  : QDialog(parent)
  , ArcFormats(nullptr)
  , _archivePath(nullptr)
  , _browse(nullptr)
  , _format(nullptr)
  , _level(nullptr)
  , _updateMode(nullptr)
  , _pathMode(nullptr)
  , _volume(nullptr)
  , _password1(nullptr)
  , _password2(nullptr)
  , _showPassword(nullptr)
  , _encryptMethod(nullptr)
  , _encryptHeaders(nullptr)
  , _method(nullptr)
  , _dictionary(nullptr)
  , _order(nullptr)
  , _solid(nullptr)
  , _numThreads(nullptr)
  , _memUse(nullptr)
  , _memValueComp(nullptr)
  , _memValueDecomp(nullptr)
  , _params(nullptr)
  , _deleteAfter(nullptr)
  , _optionsString(nullptr)
  , _auto_MethodId(-1)
  , _auto_Dict((UInt32)(Int32)-1)
  , _auto_Order(1)
  , _auto_Solid((UInt64)1 << 20)
  , _auto_NumThreads(1)
  , _ramSize_Defined(false)
  , _ramSize((size_t)sizeof(size_t) << 29)
  , _ramSize_Reduced((size_t)sizeof(size_t) << 29)
  , _ramUsage_Auto((UInt64)sizeof(size_t) << 29)
  , _defaultEncryptionMethodIndex(-1)
  , _prevFormatIndex(-1)
  , _inArcPathSync(false)
{
  // P.2 : IDD_COMPRESS=4000 dialog CAPTION "Add to Archive".
  setWindowTitle(FmLang(IDD_COMPRESS, QStringLiteral("Add to Archive")));

  // --- archive path : editable combo (MRU) + "..." browse -------------------
  _archivePath = new QComboBox(this);
  _archivePath->setEditable(true);
  _archivePath->setInsertPolicy(QComboBox::NoInsert);
  _archivePath->setMinimumWidth(360);

  _browse = new QToolButton(this);
  _browse->setText(QString::fromLatin1("..."));

  QHBoxLayout *pathRow = new QHBoxLayout;
  pathRow->addWidget(_archivePath, 1);
  pathRow->addWidget(_browse);

  // --- core combos ----------------------------------------------------------
  _format = new QComboBox(this);
  _level = new QComboBox(this);
  _method = new QComboBox(this);
  _dictionary = new QComboBox(this);
  _order = new QComboBox(this);
  _solid = new QComboBox(this);
  _numThreads = new QComboBox(this);
  _memUse = new QComboBox(this);
  _memValueComp = new QLabel(this);
  _memValueDecomp = new QLabel(this);
  _updateMode = new QComboBox(this);
  _pathMode = new QComboBox(this);
  // C.6: editable Volume combo (mirror m_Volume, the CBS-editable resource combo).
  _volume = new QComboBox(this);
  _volume->setEditable(true);
  _volume->setInsertPolicy(QComboBox::NoInsert);

  // G.7a: Parameters free-form edit (mirror m_Params / IDE_COMPRESS_PARAMETERS).
  // The resource control is a single-line ES_AUTOHSCROLL edit; we use a short
  // QPlainTextEdit so long codec-switch lists wrap, but the OnOK read-back trims it
  // to the same single Info.Options string m_Params.GetText would yield.
  _params = new QPlainTextEdit(this);
  _params->setTabChangesFocus(true);
  {
    const int h = _params->fontMetrics().lineSpacing() * 2 + 12;
    _params->setMaximumHeight(h);
  }
  // G.7b: "Delete files after compression" checkbox (mirror IDX_COMPRESS_DEL).
  // G.1 : langID IDX_COMPRESS_DEL; English = the .rc CONTROL text (CompressDialog.rc:125).
  _deleteAfter = new QCheckBox(FmLang(IDX_COMPRESS_DEL, tr("Delete files after compression")), this);
  // G.7f: read-only one-line Options summary (mirror IDT_COMPRESS_OPTIONS, an empty
  // SS_NOPREFIX LTEXT filled by ShowOptionsString).
  _optionsString = new QLabel(this);

  // --- password group : two edits (password + confirm) + show + enc method --
  _password1 = new QLineEdit(this);
  _password1->setEchoMode(QLineEdit::Password);
  _password2 = new QLineEdit(this);
  _password2->setEchoMode(QLineEdit::Password);
  // G.1 : checkbox langIDs from kLangIDs[] (IDX_PASSWORD_SHOW / IDX_COMPRESS_ENCRYPT_FILE_NAMES);
  // English = the .rc CONTROL text (CompressDialog.rc 136 / 142), minus the '&' mnemonic.
  _showPassword = new QCheckBox(FmLang(IDX_PASSWORD_SHOW, tr("Show Password")), this);
  _encryptMethod = new QComboBox(this);
  _encryptHeaders = new QCheckBox(FmLang(IDX_COMPRESS_ENCRYPT_FILE_NAMES, tr("Encrypt file names")), this);

  // --- form layout ----------------------------------------------------------
  // G.1 : static labels — langIDs from CompressDialog.cpp kLangIDs[] (lines 45-77)
  // applied via LangSetDlgItems; each is the dialog-template control whose ID is its
  // langID. English = the .rc LTEXT (CompressDialog.rc 62-116), minus the '&' mnemonic.
  QFormLayout *form = new QFormLayout;
  form->addRow(FmLang(IDT_COMPRESS_ARCHIVE, tr("Archive:")), pathRow);
  form->addRow(FmLang(IDT_COMPRESS_FORMAT, tr("Archive format:")), _format);
  form->addRow(FmLang(IDT_COMPRESS_LEVEL, tr("Compression level:")), _level);
  // C.3b-1: Method / Dictionary / Order, matching the original grid order
  // (Archive, Format, Level, Method, Dictionary, Order, ...).
  form->addRow(FmLang(IDT_COMPRESS_METHOD, tr("Compression method:")), _method);
  form->addRow(FmLang(IDT_COMPRESS_DICTIONARY, tr("Dictionary size:")), _dictionary);
  form->addRow(FmLang(IDT_COMPRESS_ORDER, tr("Word size:")), _order);
  // C.3b-2: Solid block size / Number of CPU threads / Memory usage, matching the
  // original grid order (..., Order, Solid, Threads, Memory).
  form->addRow(FmLang(IDT_COMPRESS_SOLID, tr("Solid Block size:")), _solid);
  form->addRow(FmLang(IDT_COMPRESS_THREADS, tr("Number of CPU threads:")), _numThreads);
  form->addRow(FmLang(IDT_COMPRESS_MEMORY, tr("Memory usage for Compressing:")), _memValueComp);
  form->addRow(FmLang(IDT_COMPRESS_MEMORY_DE, tr("Memory usage for Decompressing:")), _memValueDecomp);
  // "Memory usage limit:" is port-specific (the mem-use combo has no LTEXT label in
  // the original dialog template / kLangIDs) — left as a literal, no original langID.
  form->addRow(QStringLiteral("Memory usage limit:"), _memUse);
  form->addRow(FmLang(IDT_COMPRESS_UPDATE_MODE, tr("Update mode:")), _updateMode);
  form->addRow(FmLang(IDT_COMPRESS_PATH_MODE, tr("Path mode:")), _pathMode);
  // C.6: "Split to volumes, bytes:" (mirror IDT_SPLIT_TO_VOLUMES + m_Volume).
  form->addRow(FmLang(IDT_SPLIT_TO_VOLUMES, tr("Split to volumes, bytes:")), _volume);
  // G.7a: "Parameters:" + the free-form edit (mirror IDT_COMPRESS_PARAMETERS +
  // IDE_COMPRESS_PARAMETERS, CompressDialog.rc:105-106). G.1: langID IDT_COMPRESS_PARAMETERS;
  // English = the .rc LTEXT "Parameters:".
  form->addRow(FmLang(IDT_COMPRESS_PARAMETERS, tr("Parameters:")), _params);

  QGroupBox *pwGroup = new QGroupBox(FmLang(IDG_COMPRESS_ENCRYPTION, tr("Encryption")), this);
  QFormLayout *pwForm = new QFormLayout(pwGroup);
  pwForm->addRow(FmLang(IDT_PASSWORD_ENTER, tr("Enter password:")), _password1);
  pwForm->addRow(FmLang(IDT_PASSWORD_REENTER, tr("Reenter password:")), _password2);
  pwForm->addRow(QString(), _showPassword);
  pwForm->addRow(FmLang(IDT_COMPRESS_ENCRYPTION_METHOD, tr("Encryption method:")), _encryptMethod);
  pwForm->addRow(QString(), _encryptHeaders);

  QDialogButtonBox *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  // G.1 : OK/Cancel use langIDs 401/402 (kLangPairs, LangUtils.cpp:65-66); English
  // from the dialog-template literal. Override the standard-button text so a loaded
  // txt translates them (mirror fm/QtOptionsDialog.cpp:252-255).
  if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok))
    ok->setText(FmLang(401, QStringLiteral("OK")));
  if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel))
    cancel->setText(FmLang(402, QStringLiteral("Cancel")));

  // B.9: the "Options..." button (mirror IDB_COMPRESS_OPTIONS). ActionRole keeps
  // it on the buttonbox without triggering accept/reject. G.1: langID IDB_COMPRESS_OPTIONS
  // (== IDS_OPTIONS == 2100, kLangIDs[]); .rc button text "Options" (the port keeps
  // the "..." ellipsis as its inline English fallback).
  QPushButton *optionsButton = buttons->addButton(
      FmLang(IDB_COMPRESS_OPTIONS, tr("Options...")), QDialogButtonBox::ActionRole);
  connect(optionsButton, &QPushButton::clicked, this, &QtCompressDialog::onOptions);

  QVBoxLayout *root = new QVBoxLayout(this);
  root->addLayout(form);
  // G.7b: the "Delete files after compression" checkbox (mirror the .rc Options
  // groupbox control). G.7f: the read-only Options summary, shown next to the
  // "Options..." button as in the original (IDT_COMPRESS_OPTIONS beside IDB_COMPRESS_OPTIONS).
  root->addWidget(_deleteAfter);
  root->addWidget(_optionsString);
  root->addWidget(pwGroup);
  root->addWidget(buttons);

  connect(_browse, &QToolButton::clicked, this, &QtCompressDialog::onBrowse);
  // G.7c: ArcPath_WasChanged mirror. The original wires this on the archive-path
  // combo's CBN_SELCHANGE (an MRU pick) AND re-runs it from OnButtonSetArchive; a
  // typed name's extension is honored too (OnOK/SetArcPathFields feed the same path).
  // We use editTextChanged so BOTH a typed name and an MRU selection (which updates
  // the edit text) drive the auto-switch. _inArcPathSync guards the programmatic
  // text rewrite that onFormatChanged -> setArchiveName performs, so we never loop.
  connect(_archivePath, &QComboBox::editTextChanged,
          this, [this](const QString &text) {
            if (_inArcPathSync)
              return;
            arcPathWasChanged(QStr_to_UString(text));
          });
  connect(_format, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &QtCompressDialog::onFormatChanged);
  // Level change rebuilds the Method list (mirror OnCommand IDC_COMPRESS_LEVEL ->
  // SetMethod()); Method change rebuilds Dictionary+Order (IDC_COMPRESS_METHOD ->
  // MethodChanged()). Both setMethodCombo() and methodChanged() repopulate child
  // combos with signals blocked, so the cascade does not re-enter.
  connect(_level, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) {
            // OnCommand IDC_COMPRESS_LEVEL (CompressDialog.cpp 1348-1367).
            setMethodCombo();
            setSolidBlockSize();
            setNumThreads();
            setMemoryUsage();
          });
  connect(_method, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &QtCompressDialog::onMethodChanged);
  // C.3b-2 cascade tails (mirror OnCommand IDC_COMPRESS_DICTIONARY / _SOLID /
  // _THREADS / _MEM_USE, CompressDialog.cpp 1383-1436). Dictionary changes
  // re-derive Solid/Threads/Memory; Solid+Threads changes just re-estimate memory;
  // MemUse changes re-derive the auto-thread count then re-estimate memory.
  connect(_dictionary, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { setSolidBlockSize(); setNumThreads(); setMemoryUsage(); });
  connect(_solid, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { setMemoryUsage(); });
  connect(_numThreads, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { setMemoryUsage(); });
  connect(_memUse, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { setNumThreads(); setMemoryUsage(); });
  connect(_showPassword, &QCheckBox::toggled, this, &QtCompressDialog::onShowPasswordToggled);
  connect(buttons, &QDialogButtonBox::accepted, this, &QtCompressDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool QtCompressDialog::isShowPasswordChecked() const
{
  return _showPassword->isChecked();
}

int QtCompressDialog::currentFormatIndex() const
{
  // The combo stores the ArcFormats index as item data (mirror GetFormatIndex).
  if (!_format || _format->currentIndex() < 0)
    return Info.FormatIndex;
  return _format->currentData().toInt();
}


// === OnInit mirror (the parts CompressDialog.cpp OnInit does) ================
void QtCompressDialog::fillFromState()
{
  // --- RAM facts (CompressDialog.cpp OnInit 437-459), via the engine's
  //     NSystem::GetRamSize (the Linux sysctl/_SC_PHYS_PAGES impl). Copied verbatim.
  {
    size_t size = (size_t)sizeof(size_t) << 29;
    _ramSize_Defined = NWindows::NSystem::GetRamSize(size);
    // size = (UInt64)3 << 62; // for debug only;
    {
      // we use reduced limit for 32-bit version:
      unsigned bits = sizeof(size_t) * 8;
      if (bits == 32)
      {
        const UInt32 limit2 = (UInt32)7 << 28;
        if (size > limit2)
            size = limit2;
      }
    }
    _ramSize = size;
    const size_t kMinUseSize = 1 << 26;
    if (size < kMinUseSize)
        size = kMinUseSize;
    _ramSize_Reduced = size;

    // 80% - is auto usage limit in handlers
    _ramUsage_Auto = Calc_From_Val_Percents(size, 80);
  }

  // AddVolumeItems(m_Volume) (CompressDialog.cpp OnInit 495): populate the editable
  // Volume combo with the preset split sizes. The field starts empty (single
  // archive); the user picks/types a size to split.
  addVolumeItems();

  // C.6: if the caller seeded Info.VolumeSizes (e.g. from a parsed `-v` switch),
  // reflect it as the active edit text so the dialog shows the requested split and
  // OnOK re-emits it. Each size is rendered as its raw byte count (ParseVolumeSizes
  // round-trips a bare number unchanged); multiple sizes are space-separated.
  if (!Info.VolumeSizes.IsEmpty())
  {
    UString s;
    FOR_VECTOR (i, Info.VolumeSizes)
    {
      if (i != 0)
        s.Add_Space();
      s.Add_UInt64(Info.VolumeSizes[i]);
    }
    setVolumeText(s);
  }

  // m_RegistryInfo.Load() (CompressDialog.cpp OnInit ~480).
  _regInfo.Load();

  // --- Format combo : list only update-capable formats (ShowDialog 398-414 +
  //     OnInit format-combo fill). Build ArcIndices if the caller left it empty.
  if (ArcIndices.IsEmpty() && ArcFormats)
  {
    // G.7d: mirror ShowDialog's format filter (UpdateGUI.cpp:398-414). oneFile == a
    // single source file (the inverse of Info.KeepName, which ShowDialog sets to
    // !oneFile). KeepName (single-file gzip/bzip2) formats are offered ONLY for one
    // file; HashHandler formats are offered ONLY when one IS the preselected default
    // (di.FormatIndex). The "swfc" special case requires the libswf plugin (not in
    // the Linux build) so it never matches here; we keep the structure faithful.
    const bool oneFile = !Info.KeepName;
    FOR_VECTOR (i, *ArcFormats)
    {
      const CArcInfoEx &ai = (*ArcFormats)[i];
      if (!ai.UpdateEnabled)
        continue;
      if (!oneFile && ai.Flags_KeepName())
        continue;
      if ((int)i != Info.FormatIndex)
      {
        if (ai.Flags_HashHandler())
          continue;
        if (ai.Name.IsEqualTo_Ascii_NoCase("swfc"))
          continue;
      }
      ArcIndices.Add(i);
    }
  }

  {
    // Preselect Info.FormatIndex if set, else the registry ArcType, else first.
    int sel = 0;
    bool selSet = false;
    for (unsigned k = 0; k < ArcIndices.Size(); k++)
    {
      const unsigned fi = ArcIndices[k];
      const CArcInfoEx &ai = (*ArcFormats)[fi];
      _format->addItem(UString_to_QStr(ai.Name), (int)fi);
      if (!selSet)
      {
        if (Info.FormatIndex >= 0)
        {
          if ((int)fi == Info.FormatIndex) { sel = (int)k; selSet = true; }
        }
        else if (ai.Name.IsEqualTo_NoCase(_regInfo.ArcType))
        {
          sel = (int)k; selSet = true;
        }
      }
    }
    // Block signals so onFormatChanged doesn't run before the rest is built.
    _format->blockSignals(true);
    _format->setCurrentIndex(sel);
    _format->blockSignals(false);
    Info.FormatIndex = currentFormatIndex();
  }

  // --- Update-mode / Path-mode combos (CompressDialog.cpp 533-540) ----------
  addComboItems(_updateMode, kUpdateModeItems,
      (unsigned)(sizeof(kUpdateModeItems) / sizeof(kUpdateModeItems[0])), Info.UpdateMode);
  addComboItems(_pathMode, kPathModeItems,
      (unsigned)(sizeof(kPathModeItems) / sizeof(kPathModeItems[0])), Info.PathMode);

  // --- Password presets + show-password (CompressDialog.cpp OnInit 461-499) -
  _password1->setText(UString_to_QStr(Info.Password));
  _password2->setText(UString_to_QStr(Info.Password));
  _showPassword->setChecked(_regInfo.ShowPassword);
  _encryptHeaders->setChecked(_regInfo.EncryptHeaders);
  onShowPasswordToggled(); // UpdatePasswordControl()

  // G.7b: CheckButton(IDX_COMPRESS_DEL, Info.DeleteAfterCompressing) (OnInit 543).
  _deleteAfter->setChecked(Info.DeleteAfterCompressing);

  // --- Archive-path combo : MRU history FIRST, then the caller-seeded default
  //     name as the active edit text. Order matters: adding the first item to an
  //     editable combo would otherwise select item 0 and clobber a pre-set edit
  //     text, so we add the MRU items first and set the default name last.
  for (unsigned i = 0; i < _regInfo.ArcPaths.Size() && i < kHistorySize; i++)
    _archivePath->addItem(UString_to_QStr(_regInfo.ArcPaths[i]));

  // --- format-dependent combos (SetLevel2 / SetMethod / SetEncryptionMethod) -
  // SetLevel() in the original calls SetMethod() (which calls MethodChanged() ->
  // SetDictionary2 + SetOrder2). We split that: setLevelCombo() fills Level with
  // signals blocked, then setMethodCombo() runs the rest of the cascade.
  setLevelCombo();
  setMethodCombo();
  // C.3b-2: the solid/threads/memory tail of FormatChanged (CompressDialog.cpp
  // 698-764): SetSolidBlockSize, SetMemUseCombo, SetNumThreads, then (after the
  // encryption combo) SetMemoryUsage. The order matches the original.
  setSolidBlockSize();
  setMemUseCombo();
  setNumThreads();
  setEncryptionCombo();
  setMemoryUsage();

  // Derive the archive name (base + the current format's extension) from the
  // caller-seeded base, mirroring OnInit's SetArchiveName. This also sets it as
  // the active edit text (setCurrentIndex(-1) so an MRU item-0 selection does not
  // shadow it).
  _archivePath->setCurrentIndex(-1);
  setArchiveName(Info.ArcPath);

  // B.9: fold the persisted link/security + per-format time options out of
  // _regInfo into Info, so the Options sub-dialog (and the headless path) sees the
  // saved defaults. Mirror CompressDialog.cpp:726-730 (SET_GUI_BOOL for the link
  // CBool1s) + 1202-1206 (Info.TimePrec/MTime/... = fo.*). The link CBoolPairs
  // persist at the global CInfo level; the time block persists per-format.
  if (Info.SymLinks.Def == false)   Info.SymLinks   = _regInfo.SymLinks;
  if (Info.HardLinks.Def == false)  Info.HardLinks  = _regInfo.HardLinks;
  if (Info.AltStreams.Def == false) Info.AltStreams = _regInfo.AltStreams;
  if (Info.NtSecurity.Def == false) Info.NtSecurity = _regInfo.NtSecurity;
  if (Info.PreserveATime.Def == false) Info.PreserveATime = _regInfo.PreserveATime;
  {
    const int idx = (Info.FormatIndex >= 0 && ArcFormats)
        ? _regInfo.FindFormat((*ArcFormats)[(unsigned)Info.FormatIndex].Name) : -1;
    if (idx >= 0)
    {
      const NCompressionQt::CFormatOptions &fo = _regInfo.Formats[(unsigned)idx];
      Info.TimePrec    = fo.TimePrec;
      Info.MTime       = fo.MTime;
      Info.CTime       = fo.CTime;
      Info.ATime       = fo.ATime;
      Info.SetArcMTime = fo.SetArcMTime;
    }
  }

  // G.7a: load the per-format remembered Parameters string (mirror SetParams,
  // run from FormatChanged in the original). G.7f: build the Options summary from
  // the folded-out time/link state (mirror ShowOptionsString in FormatChanged).
  setParams();
  showOptionsString();
  _prevFormatIndex = Info.FormatIndex; // the format the Params edit now belongs to
}


// === FormatChanged mirror (CompressDialog.cpp FormatChanged) ================
void QtCompressDialog::onFormatChanged(int)
{
  // G.7a: SaveOptionsInMem() runs BEFORE FormatChanged on a format switch (OnCommand
  // IDC_COMPRESS_FORMAT 1342) so the PREVIOUS format's Params edit text is persisted
  // to its per-format options before SetParams() loads the new format's. We only
  // persist the Params here (the rest of SaveOptionsInMem's per-format fields are
  // already written by onAccept's full save).
  if (_prevFormatIndex >= 0 && ArcFormats)
  {
    UString params = QStr_to_UString(_params->toPlainText());
    params.Trim();
    NCompressionQt::CFormatOptions &fo =
        _regInfo.Get_FormatOptions((*ArcFormats)[(unsigned)_prevFormatIndex].Name);
    fo.Options = params;
  }

  Info.FormatIndex = currentFormatIndex();
  // Re-derive the archive extension, level set and encryption method for the
  // newly chosen format (mirror SetArchiveName2 + SetLevel + SetEncryptionMethod).
  {
    // change the extension of the current archive path text in place.
    UString cur = QStr_to_UString(_archivePath->currentText());
    const int dotPos = getExtDotPos(cur);
    if (dotPos >= 0)
      cur.DeleteFrom(dotPos);
    setArchiveName(cur);
  }
  // FormatChanged -> SetLevel() (-> SetMethod() -> MethodChanged()). Same split as
  // fillFromState: rebuild Level (signals blocked) then run the Method cascade,
  // then the solid/threads/memory tail (CompressDialog.cpp 698-764).
  setLevelCombo();
  setMethodCombo();
  setSolidBlockSize();
  setMemUseCombo();
  setNumThreads();
  setEncryptionCombo();
  setMemoryUsage();

  // G.7a/G.7f: SetParams() loads the new format's remembered Params; ShowOptionsString()
  // rebuilds the summary (mirror FormatChanged 702/743). Then record the new format as
  // the one the Params edit belongs to.
  setParams();
  showOptionsString();
  _prevFormatIndex = Info.FormatIndex;
}


// === MethodChanged dispatch (mirror OnCommand IDC_COMPRESS_METHOD) ===========
// CompressDialog.cpp 1370-1381: MethodChanged -> SetSolidBlockSize -> SetNumThreads
// -> SetMemoryUsage. (setMethodCombo also calls methodChanged() directly during a
// rebuild; the solid/threads/memory tail there is run by the caller -
// fillFromState/onFormatChanged - so we add it here only for the user/selector path.)
void QtCompressDialog::onMethodChanged(int)
{
  methodChanged();
  setSolidBlockSize();
  setNumThreads();
  setMemoryUsage();
}


// === SetLevel2 mirror (CompressDialog.cpp 1572-1617) ========================
void QtCompressDialog::setLevelCombo()
{
  if (!ArcFormats)
    return;
  const CArcInfoEx &ai = (*ArcFormats)[(unsigned)Info.FormatIndex];

  // default level: from the per-format registry options, else 5 (SetLevel2).
  UInt32 level = 5;
  {
    const int idx = _regInfo.FindFormat(ai.Name);
    if (idx >= 0)
    {
      const UInt32 fl = _regInfo.Formats[(unsigned)idx].Level;
      if (fl <= 9)            level = fl;
      else if (fl == (UInt32)(Int32)-1) level = 5;
      else                    level = 9;
    }
    else
      level = _regInfo.Level; // global default level
  }

  const UInt32 mask = levelsMaskForFormat(ai.Name);

  // Block signals while rebuilding: the caller (onFormatChanged / fillFromState)
  // drives the Method cascade explicitly via setMethodCombo(), so the level
  // combo's currentIndexChanged must not also trigger it here (would double-run).
  const bool wasBlocked = _level->blockSignals(true);
  _level->clear();
  int curSel = 0;
  for (unsigned i = 0; i < (unsigned)(sizeof(kLevels) / sizeof(kLevels[0])); i++)
  {
    const int v = kLevels[i].value;
    if ((mask >> v) & 1)
    {
      UString s;
      s.Add_UInt32((UInt32)v);
      s += " - ";
      s += QStr_to_UString(FmLang(kLevels[i].langId, QString::fromUtf8(kLevels[i].label)));
      _level->addItem(UString_to_QStr(s), v);
    }
  }
  // SetNearestSelectComboBox (CompressDialog.cpp 1560-1570): pick the highest
  // item whose value <= level.
  for (int i = _level->count() - 1; i >= 0; i--)
    if ((UInt32)_level->itemData(i).toInt() <= level)
    {
      curSel = i;
      break;
    }
  _level->setCurrentIndex(curSel);
  _level->blockSignals(wasBlocked);
}


// === SetEncryptionMethod mirror (CompressDialog.cpp 1721-1751) ==============
void QtCompressDialog::setEncryptionCombo()
{
  if (!ArcFormats)
    return;
  const CArcInfoEx &ai = (*ArcFormats)[(unsigned)Info.FormatIndex];

  _encryptMethod->clear();
  _defaultEncryptionMethodIndex = -1;

  const bool is7z = ai.Is_7z();
  // EncryptHeaders (encrypt file names) is only valid for 7z (the only g_Formats
  // entry with kFF_EncryptFileNames). Enable/show the checkbox accordingly.
  Info.EncryptHeadersIsAllowed = is7z;
  _encryptHeaders->setEnabled(is7z);
  _encryptHeaders->setVisible(is7z);

  if (is7z)
  {
    _encryptMethod->addItem(QString::fromLatin1("AES-256"));
    _encryptMethod->setCurrentIndex(0);
    _defaultEncryptionMethodIndex = 0;
  }
  else if (ai.Is_Zip())
  {
    UString encMethod;
    const int idx = _regInfo.FindFormat(ai.Name);
    if (idx >= 0)
      encMethod = _regInfo.Formats[(unsigned)idx].EncryptionMethod;
    _encryptMethod->addItem(QString::fromLatin1("ZipCrypto"));
    const int sel = encMethod.IsPrefixedBy_Ascii_NoCase("aes") ? 1 : 0;
    _defaultEncryptionMethodIndex = 0;
    _encryptMethod->addItem(QString::fromLatin1("AES-256"));
    _encryptMethod->setCurrentIndex(sel);
  }
  // formats without kFF_Encrypt: no encryption methods; combo stays empty.
}


// === GetEncryptionMethodSpec mirror (CompressDialog.cpp 1810-1820) ==========
// Returns empty when the selection equals the format default (so the engine uses
// the default), else the method name with '-' removed (e.g. "AES256").
UString QtCompressDialog::getEncryptionMethodSpec()
{
  UString s;
  if (_encryptMethod->count() > 0
      && _encryptMethod->currentIndex() != _defaultEncryptionMethodIndex)
  {
    s = QStr_to_UString(_encryptMethod->currentText());
    s.RemoveChar(L'-');
  }
  return s;
}


// ============================================================================
// === Method / Dictionary / Order cascade : mirror CompressDialog.cpp ========
// ============================================================================

// SetMethods mirror (CompressDialog.cpp 405-426): keep only external (plugin)
// codecs that are encoder-assigned, non-filter, single-stream, AND whose name is
// not already one of the built-in g_7zMethods names.
void QtCompressDialog::setMethods(const CObjectVector<CCodecInfoUser> &userCodecs)
{
  ExternalMethods.Clear();
  {
    FOR_VECTOR (i, userCodecs)
    {
      const CCodecInfoUser &c = userCodecs[i];
      if (!c.EncoderIsAssigned
          || !c.IsFilter_Assigned
          || c.IsFilter
          || c.NumStreams != 1)
        continue;
      unsigned k;
      for (k = 0; k < Z7_ARRAY_SIZE(g_7zMethods); k++)
        if (c.Name.IsEqualTo_Ascii_NoCase(kMethodsNames[g_7zMethods[k]]))
          break;
      if (k != Z7_ARRAY_SIZE(g_7zMethods))
        continue;
      ExternalMethods.Add(c.Name);
    }
  }
}


// GetStaticFormatIndex mirror (CompressDialog.cpp 1551-1558).
unsigned QtCompressDialog::getStaticFormatIndex() const
{
  if (!ArcFormats || currentFormatIndex() < 0)
    return 0;
  const CArcInfoEx &ai = (*ArcFormats)[(unsigned)currentFormatIndex()];
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_Formats); i++)
    if (ai.Name.IsEqualTo_Ascii_NoCase(g_Formats[i].Name))
      return i;
  return 0; // -1;
}


// GetLevel()/GetLevel2() mirror (CompressDialog.h 262, .cpp 2189-2195).
UInt32 QtCompressDialog::getLevel() const
{
  return comboValue(_level);
}

UInt32 QtCompressDialog::getLevel2() const
{
  UInt32 level = getLevel();
  if (level == (UInt32)(Int32)-1)
    level = 5;
  return level;
}


// GetComboValue mirror (CompressDialog.cpp 2170-2175): the item-data of the
// current selection, or -1 when the combo has <= defMax items.
UInt32 QtCompressDialog::comboValue(QComboBox *c, int defMax) const
{
  if (c->count() <= defMax)
    return (UInt32)(Int32)-1;
  const int sel = c->currentIndex();
  if (sel < 0)
    return (UInt32)(Int32)-1;
  return (UInt32)c->itemData(sel).toUInt();
}

// GetComboValue_64 mirror (CompressDialog.cpp 2178-2187): same, 64-bit, with the
// -1 sentinel preserved for the "* auto" (k_Auto_Dict) row.
UInt64 QtCompressDialog::comboValue64(QComboBox *c, int defMax) const
{
  if (c->count() <= defMax)
    return (UInt64)(Int64)-1;
  const int sel = c->currentIndex();
  if (sel < 0)
    return (UInt64)(Int64)-1;
  const UInt64 val = (UInt64)c->itemData(sel).toULongLong();
  if (val == (UInt64)(Int64)-1)
    return (UInt64)(Int64)-1;
  return val;
}


// EnableMultiCombo mirror (CompressDialog.cpp 642-648): a combo is enabled only if
// it offers more than one choice (the lone "* auto" item means "no real choice").
void QtCompressDialog::enableMultiCombo(QComboBox *combo)
{
  combo->setEnabled(combo->count() > 1);
}


// SetMethod2 mirror (CompressDialog.cpp 1627-1707), then EnableMultiCombo
// (the inline SetMethod() in CompressDialog.h 225-229).
void QtCompressDialog::setMethodCombo(int keepMethodId)
{
  const bool wasBlocked = _method->blockSignals(true);
  _method->clear();
  _auto_MethodId = -1;
  const CFormatInfo &fi = g_Formats[getStaticFormatIndex()];
  const CArcInfoEx &ai = (*ArcFormats)[(unsigned)currentFormatIndex()];
  if (getLevel() == 0 && !ai.Flags_HashHandler())
  {
    if (!ai.Is_Tar() &&
        !ai.Is_Zstd())
    {
      _method->blockSignals(wasBlocked);
      methodChanged();
      enableMultiCombo(_method);
      return;
    }
  }
  UString defaultMethod;
  {
    const int index = _regInfo.FindFormat(ai.Name);
    if (index >= 0)
      defaultMethod = _regInfo.Formats[(unsigned)index].Method;
  }
  const bool isSfx = false; // SFX deferred (C.3a/b: no SFX checkbox yet)
  bool weUseSameMethod = false;

  const bool is7z = ai.Is_7z();

  for (unsigned m = 0;; m++)
  {
    int methodID;
    const char *method;
    if (m < fi.NumMethods)
    {
      methodID = fi.MethodIDs[m];
      method = kMethodsNames[methodID];
      if (is7z)
      if (methodID == kCopy
          || methodID == kDeflate
          || methodID == kDeflate64
          )
        continue;
    }
    else
    {
      if (!is7z)
        break;
      const unsigned extIndex = m - fi.NumMethods;
      if (extIndex >= ExternalMethods.Size())
        break;
      methodID = (int)(Z7_ARRAY_SIZE(kMethodsNames) + extIndex);
      method = ExternalMethods[extIndex].Ptr();
    }
    if (isSfx)
      if (!IsMethodSupportedBySfx(methodID))
        continue;

    AString s (method);
    int writtenMethodId = methodID;
    if (m == 0)
    {
      _auto_MethodId = methodID;
      writtenMethodId = -1;
      modifyAuto(s);
    }
    const int itemIndex = _method->count();
    _method->addItem(QString::fromLatin1(s.Ptr()), QVariant((int)writtenMethodId));
    if (keepMethodId == methodID)
    {
      _method->setCurrentIndex(itemIndex);
      weUseSameMethod = true;
      continue;
    }
    if ((defaultMethod.IsEqualTo_Ascii_NoCase(method) || m == 0) && !weUseSameMethod)
      _method->setCurrentIndex(itemIndex);
  }

  _method->blockSignals(wasBlocked);
  if (!weUseSameMethod)
    methodChanged();
  enableMultiCombo(_method);
}


// MethodChanged mirror (CompressDialog.h 231-238): SetDictionary2 + SetOrder2,
// each followed by EnableMultiCombo.
void QtCompressDialog::methodChanged()
{
  setDictionaryCombo();
  enableMultiCombo(_dictionary);
  setOrderCombo();
  enableMultiCombo(_order);
}


// GetMethodID_RAW mirror (CompressDialog.cpp 1754-1759).
int QtCompressDialog::getMethodID_RAW() const
{
  if (_method->count() <= 0 || _method->currentIndex() < 0)
    return -1;
  return _method->currentData().toInt();
}

// GetMethodID mirror (CompressDialog.cpp 1761-1767).
int QtCompressDialog::getMethodID() const
{
  const int raw = getMethodID_RAW();
  if (raw < 0)
    return _auto_MethodId;
  return raw;
}

// GetMethodSpec mirror (CompressDialog.cpp 1770-1798). Returns the explicit method
// name (empty when the "* auto" row is selected so the engine derives from level);
// estimatedName is always the resolved method name.
UString QtCompressDialog::getMethodSpec(UString &estimatedName) const
{
  estimatedName.Empty();
  if (_method->count() < 1)
    return estimatedName;
  const int methodIdRaw = getMethodID_RAW();
  int methodId = methodIdRaw;
  if (methodIdRaw < 0)
    methodId = _auto_MethodId;
  UString s;
  if (methodId >= 0)
  {
    if ((unsigned)methodId < Z7_ARRAY_SIZE(kMethodsNames))
      estimatedName = kMethodsNames[methodId];
    else
      estimatedName = GetUnicodeString(
          ExternalMethods[(unsigned)methodId - (unsigned)Z7_ARRAY_SIZE(kMethodsNames)]);
    if (methodIdRaw >= 0)
      s = estimatedName;
  }
  return s;
}

UString QtCompressDialog::getMethodSpec() const
{
  UString estimatedName;
  return getMethodSpec(estimatedName);
}

// IsMethodEqualTo mirror (CompressDialog.cpp 1800-1807).
bool QtCompressDialog::isMethodEqualTo(const UString &s) const
{
  UString estimatedName;
  const UString shortName = getMethodSpec(estimatedName);
  if (s.IsEmpty())
    return shortName.IsEmpty();
  return s.IsEqualTo_NoCase(estimatedName);
}


// GetDictSpec mirror (CompressDialog.h 266).
UInt64 QtCompressDialog::getDictSpec() const
{
  return comboValue64(_dictionary, 1);
}

// GetOrderSpec mirror (CompressDialog.h 282).
UInt32 QtCompressDialog::getOrderSpec() const
{
  return comboValue(_order, 1);
}


// SetDictionary2 mirror (CompressDialog.cpp 1859-2167). The RECT/MoveItem layout
// block (1887-1930) is Win32-only chrome and is omitted; the dictionary-size
// enumeration and the auto-default selection are reproduced verbatim per method.
void QtCompressDialog::setDictionaryCombo()
{
  const bool wasBlocked = _dictionary->blockSignals(true);
  _dictionary->clear();

  _auto_Dict = (UInt32)(Int32)-1; // for debug

  const CArcInfoEx &ai = (*ArcFormats)[(unsigned)currentFormatIndex()];
  UInt32 defaultDict = (UInt32)(Int32)-1;
  {
    const int index = _regInfo.FindFormat(ai.Name);
    if (index >= 0)
    {
      const NCompressionQt::CFormatOptions &fo = _regInfo.Formats[(unsigned)index];
      if (isMethodEqualTo(fo.Method))
        defaultDict = fo.Dictionary;
    }
  }

  const int methodID = getMethodID();
  const UInt32 level = getLevel2();

  if (methodID < 0)
  {
    _dictionary->blockSignals(wasBlocked);
    return;
  }

  switch (methodID)
  {
    case kLZMA:
    case kLZMA2:
    {
      {
        _auto_Dict = level <= 4 ?
            (UInt32)1 << (level * 2 + 16) :
            level <= sizeof(size_t) / 2 + 4 ?
              (UInt32)1 << (level + 20) :
              (UInt32)1 << (sizeof(size_t) / 2 + 24);
      }

      // we use threshold 3.75 GiB to switch to kLzmaMaxDictSize.
      if (defaultDict != (UInt32)(Int32)-1
          && defaultDict >= ((UInt32)15 << 28))
        defaultDict = kLzmaMaxDictSize;

      const size_t kLzmaMaxDictSize_Up = (size_t)1 << (20 + sizeof(size_t) / 4 * 6);

      int curSel = comboAddDict2(_dictionary, k_Auto_Dict, _auto_Dict);

      for (unsigned i = (16 - 1) * 2; i <= (32 - 1) * 2; i++)
      {
        if (i < (20 - 1) * 2
            && i != (16 - 1) * 2
            && i != (18 - 1) * 2)
          continue;
        if (i == (20 - 1) * 2 + 1)
          continue;
        const size_t dict_up = (size_t)(2 + (i & 1)) << (i / 2);
        size_t dict = dict_up;
        if (dict_up >= kLzmaMaxDictSize)
          dict = kLzmaMaxDictSize; // we reduce dictionary

        const int index = comboAddDict2(_dictionary, dict, dict);

        if (defaultDict != (UInt32)(Int32)-1)
          if (dict <= defaultDict || curSel <= 0)
            curSel = index;
        if (dict_up >= kLzmaMaxDictSize_Up)
          break;
      }

      _dictionary->setCurrentIndex(curSel);
      break;
    }

    case kPPMd:
    {
      _auto_Dict = (UInt32)1 << (level + 19);

      const UInt32 kPpmd_Default_4g = (UInt32)0 - ((UInt32)1 << 10);
      const size_t kPpmd_MaxDictSize_Up = (size_t)1 << (29 + sizeof(size_t) / 8);

      if (defaultDict != (UInt32)(Int32)-1
          && defaultDict >= ((UInt32)15 << 28)) // threshold
        defaultDict = kPpmd_Default_4g;

      int curSel = comboAddDict2(_dictionary, k_Auto_Dict, _auto_Dict);

      for (unsigned i = (20 - 1) * 2; i <= (32 - 1) * 2; i++)
      {
        if (i == (20 - 1) * 2 + 1)
          continue;

        const size_t dict_up = (size_t)(2 + (i & 1)) << (i / 2);
        size_t dict = dict_up;
        if (dict_up >= kPpmd_Default_4g)
          dict = kPpmd_Default_4g;

        const int index = comboAddDict2(_dictionary, dict, dict_up);
        if (defaultDict != (UInt32)(Int32)-1)
          if (dict <= defaultDict || curSel <= 0)
            curSel = index;
        if (dict_up >= kPpmd_MaxDictSize_Up)
          break;
      }
      _dictionary->setCurrentIndex(curSel);
      break;
    }

    case kPPMdZip:
    {
      _auto_Dict = (UInt32)1 << (level + 19);

      int curSel = comboAddDict2(_dictionary, k_Auto_Dict, _auto_Dict);

      for (unsigned i = 20; i <= 28; i++)
      {
        const UInt32 dict = (UInt32)1 << i;
        const int index = comboAddDict2(_dictionary, dict, dict);
        if (defaultDict != (UInt32)(Int32)-1)
          if (dict <= defaultDict || curSel <= 0)
            curSel = index;
      }
      _dictionary->setCurrentIndex(curSel);
      break;
    }

    case kDeflate:
    case kDeflate64:
    {
      const UInt32 dict = (methodID == kDeflate ? (UInt32)(1 << 15) : (UInt32)(1 << 16));
      _auto_Dict = dict;
      comboAddDict2(_dictionary, k_Auto_Dict, _auto_Dict);
      _dictionary->setCurrentIndex(0);
      break;
    }

    case kBZip2:
    {
      {
             if (level >= 5) _auto_Dict = (900 << 10);
        else if (level >= 3) _auto_Dict = (500 << 10);
        else                 _auto_Dict = (100 << 10);
      }

      int curSel = comboAddDict2(_dictionary, k_Auto_Dict, _auto_Dict);

      for (unsigned i = 1; i <= 9; i++)
      {
        const UInt32 dict = ((UInt32)i * 100) << 10;
        comboAddDict2(_dictionary, dict, dict);
        if (defaultDict != (UInt32)(Int32)-1)
          if (i <= defaultDict / 100000 || curSel <= 0)
            curSel = _dictionary->count() - 1;
      }
      _dictionary->setCurrentIndex(curSel);
      break;
    }

    case kCopy:
    {
      _auto_Dict = 0;
      comboAddDict2(_dictionary, 0, 0);
      _dictionary->setCurrentIndex(0);
      break;
    }
  }
  _dictionary->blockSignals(wasBlocked);
}


// AddOrder / AddOrder_Auto mirror (CompressDialog.cpp 2198-2211): the order combo
// is built from plain UInt32 labels, the auto row carries data == -1.
static int comboAddOrder(QComboBox *c, UInt32 size)
{
  char s[32];
  ConvertUInt32ToString(size, s);
  const int index = c->count();
  c->addItem(QString::fromLatin1(s), QVariant((uint)size));
  return index;
}

static int comboAddOrder_Auto(QComboBox *c, UInt32 autoOrder)
{
  AString s;
  s.Add_UInt32(autoOrder);
  modifyAuto(s);
  const int index = c->count();
  // data == (UInt32)-1 (the "auto" sentinel; GetOrderSpec returns -1 for it).
  c->addItem(QString::fromLatin1(s.Ptr()), QVariant((uint)(UInt32)(Int32)-1));
  return index;
}


// SetOrder2 mirror (CompressDialog.cpp 2213-2361).
void QtCompressDialog::setOrderCombo()
{
  const bool wasBlocked = _order->blockSignals(true);
  _order->clear();

  _auto_Order = 1;

  const CArcInfoEx &ai = (*ArcFormats)[(unsigned)currentFormatIndex()];
  UInt32 defaultOrder = (UInt32)(Int32)-1;

  {
    const int index = _regInfo.FindFormat(ai.Name);
    if (index >= 0)
    {
      const NCompressionQt::CFormatOptions &fo = _regInfo.Formats[(unsigned)index];
      if (isMethodEqualTo(fo.Method))
        defaultOrder = fo.Order;
    }
  }

  const int methodID = getMethodID();
  const UInt32 level = getLevel2();
  if (methodID < 0)
  {
    _order->blockSignals(wasBlocked);
    return;
  }

  switch (methodID)
  {
    case kLZMA:
    case kLZMA2:
    {
      _auto_Order = (level < 7 ? 32 : 64);
      int curSel = comboAddOrder_Auto(_order, _auto_Order);
      for (unsigned i = 2 * 2; i < 8 * 2; i++)
      {
        UInt32 order = ((UInt32)(2 + (i & 1)) << (i / 2));
        if (order > 256)
          order = 273;
        const int index = comboAddOrder(_order, order);
        if (defaultOrder != (UInt32)(Int32)-1)
          if (order <= defaultOrder || curSel <= 0)
            curSel = index;
      }
      _order->setCurrentIndex(curSel);
      break;
    }

    case kDeflate:
    case kDeflate64:
    {
      {
             if (level >= 9) _auto_Order = 128;
        else if (level >= 7) _auto_Order = 64;
        else                 _auto_Order = 32;
      }
      int curSel = comboAddOrder_Auto(_order, _auto_Order);
      for (unsigned i = 2 * 2; i < 8 * 2; i++)
      {
        UInt32 order = ((UInt32)(2 + (i & 1)) << (i / 2));
        if (order > 256)
          order = (methodID == kDeflate64 ? 257 : 258);
        const int index = comboAddOrder(_order, order);
        if (defaultOrder != (UInt32)(Int32)-1)
          if (order <= defaultOrder || curSel <= 0)
            curSel = index;
      }

      _order->setCurrentIndex(curSel);
      break;
    }

    case kPPMd:
    {
      {
             if (level >= 9) _auto_Order = 32;
        else if (level >= 7) _auto_Order = 16;
        else if (level >= 5) _auto_Order = 6;
        else                 _auto_Order = 4;
      }

      int curSel = comboAddOrder_Auto(_order, _auto_Order);

      for (unsigned i = 0;; i++)
      {
        UInt32 order = i + 2;
        if (i >= 2)
          order = (4 + ((i - 2) & 3)) << ((i - 2) / 4);
        const int index = comboAddOrder(_order, order);
        if (defaultOrder != (UInt32)(Int32)-1)
          if (order <= defaultOrder || curSel <= 0)
            curSel = index;
        if (order >= 32)
          break;
      }
      _order->setCurrentIndex(curSel);
      break;
    }

    case kPPMdZip:
    {
      _auto_Order = level + 3;
      int curSel = comboAddOrder_Auto(_order, _auto_Order);
      for (unsigned i = 2; i <= 16; i++)
      {
        const int index = comboAddOrder(_order, i);
        if (defaultOrder != (UInt32)(Int32)-1)
          if (i <= defaultOrder || curSel <= 0)
            curSel = index;
      }
      _order->setCurrentIndex(curSel);
      break;
    }

    // case kBZip2:
    default:
      break;
  }
  _order->blockSignals(wasBlocked);
}


// GetOrderMode mirror (CompressDialog.cpp 2363-2372): PPMd orders are "o", every
// other method's order is a word-size / fast-bytes value (OrderMode == false).
bool QtCompressDialog::getOrderMode() const
{
  switch (getMethodID())
  {
    case kPPMd:
    case kPPMdZip:
      return true;
  }
  return false;
}


// ============================================================================
// === C.3b-2 : Solid / Threads / Memory cascade : mirror CompressDialog.cpp ===
// ============================================================================

bool QtCompressDialog::isZipFormat() const
{
  return ArcFormats && currentFormatIndex() >= 0
      && (*ArcFormats)[(unsigned)currentFormatIndex()].Is_Zip();
}

bool QtCompressDialog::isXzFormat() const
{
  return ArcFormats && currentFormatIndex() >= 0
      && (*ArcFormats)[(unsigned)currentFormatIndex()].Is_Xz();
}

// GetDict2 mirror (CompressDialog.h 269-279): the auto-resolving dictionary getter.
UInt64 QtCompressDialog::getDict2() const
{
  UInt64 num = getDictSpec();
  if (num == (UInt64)(Int64)-1)
  {
    if (_auto_Dict == (UInt32)(Int32)-1)
      return (UInt64)(Int64)-1; // unknown
    num = _auto_Dict;
  }
  return num;
}

// GetNumThreadsSpec / GetNumThreads2 / GetBlockSizeSpec (CompressDialog.h 283-293).
UInt32 QtCompressDialog::getNumThreadsSpec() const
{
  return comboValue(_numThreads, 1);
}

UInt32 QtCompressDialog::getNumThreads2() const
{
  UInt32 num = getNumThreadsSpec();
  if (num == (UInt32)(Int32)-1)
    num = _auto_NumThreads;
  return num;
}

UInt32 QtCompressDialog::getBlockSizeSpec() const
{
  return comboValue(_solid, 1);
}


// === SetSolidBlockSize2 mirror (CompressDialog.cpp 2405-2519). The RECT/MoveItem
//     Win32 chrome is not present; the block-size enumeration and the auto-default
//     selection are reproduced verbatim. (7z-only; greyed for non-kFF_Solid.)
void QtCompressDialog::setSolidBlockSize()
{
  const bool wasBlocked = _solid->blockSignals(true);
  _solid->clear();
  _auto_Solid = 1 << 20;

  const CFormatInfo &fi = g_Formats[getStaticFormatIndex()];
  if (!fi.Solid_())
  {
    _solid->blockSignals(wasBlocked);
    enableMultiCombo(_solid);
    return;
  }

  const UInt32 level = getLevel2();
  if (level == 0)
  {
    _solid->blockSignals(wasBlocked);
    enableMultiCombo(_solid);
    return;
  }

  UInt64 dict = getDict2();
  if (dict == (UInt64)(Int64)-1)
  {
    dict = 1 << 25; // default dict for unknown methods
    // return;
  }


  UInt32 defaultBlockSize = (UInt32)(Int32)-1;

  const CArcInfoEx &ai = (*ArcFormats)[(unsigned)currentFormatIndex()];

  {
    const int index = _regInfo.FindFormat(ai.Name);
    if (index >= 0)
    {
      const NCompressionQt::CFormatOptions &fo = _regInfo.Formats[(unsigned)index];
      if (isMethodEqualTo(fo.Method))
        defaultBlockSize = fo.BlockLogSize;
    }
  }

  const bool is7z = ai.Is_7z();

  const UInt64 cs = Get_Lzma2_ChunkSize(dict);

  // Solid Block Size
  UInt64 blockSize = cs; // for xz

  if (is7z)
  {
    // we use same default block sizes as defined in 7z encoder
    UInt64 kMaxSize = (UInt64)1 << 32;
    const int methodId = getMethodID();
    if (methodId == kLZMA2)
    {
      blockSize = cs << 6;
      kMaxSize = (UInt64)1 << 34;
    }
    else
    {
      UInt64 dict2 = dict;
      if (methodId == kBZip2)
      {
        dict2 /= 100000;
        if (dict2 < 1)
          dict2 = 1;
        dict2 *= 100000;
      }
      blockSize = dict2 << 7;
    }

    const UInt32 kMinSize = (UInt32)1 << 24;
    if (blockSize < kMinSize) blockSize = kMinSize;
    if (blockSize > kMaxSize) blockSize = kMaxSize;
  }

  _auto_Solid = blockSize;

  int curSel;
  {
    AString s;
    Add_Size(s, _auto_Solid);
    modifyAuto(s);
    curSel = _solid->count();
    _solid->addItem(QString::fromLatin1(s.Ptr()), QVariant((uint)(UInt32)(Int32)-1));
  }

  if (is7z)
  {
    // kSolidLog_NoSolid = 0 for xz means default blockSize
    const int index = _solid->count();
    _solid->addItem(QString::fromLatin1("- Non-solid"),
        QVariant((uint)(UInt32)kSolidLog_NoSolid));
    if (defaultBlockSize == kSolidLog_NoSolid)
      curSel = index;
  }

  for (unsigned i = 20; i <= 36; i++)
  {
    AString s;
    Add_Size(s, (UInt64)1 << i);
    const int index = _solid->count();
    _solid->addItem(QString::fromLatin1(s.Ptr()), QVariant((uint)(UInt32)i));
    if (defaultBlockSize != (UInt32)(Int32)-1)
      if (i <= defaultBlockSize || index <= 1)
        curSel = index;
  }

  {
    const int index = _solid->count();
    _solid->addItem(QString::fromLatin1("Solid"),
        QVariant((uint)(UInt32)kSolidLog_FullSolid));
    if (defaultBlockSize == kSolidLog_FullSolid)
      curSel = index;
  }

  _solid->setCurrentIndex(curSel);
  _solid->blockSignals(wasBlocked);
  enableMultiCombo(_solid);
}


// === SetNumThreads2 mirror (CompressDialog.cpp 2559-2711). Uses the engine's
//     CProcessAffinity / NSystem::GetNumberOfProcessors. (kFF_MultiThread only.)
void QtCompressDialog::setNumThreads()
{
  const bool wasBlocked = _numThreads->blockSignals(true);
  _auto_NumThreads = 1;

  _numThreads->clear();
  const CFormatInfo &fi = g_Formats[getStaticFormatIndex()];
  if (!fi.MultiThread_())
  {
    _numThreads->blockSignals(wasBlocked);
    enableMultiCombo(_numThreads);
    return;
  }

  UInt32 numCPUs = 1;            // process threads
  UInt32 numHardwareThreads = 1; // system threads
  NWindows::NSystem::CProcessAffinity threadsInfo;
  threadsInfo.InitST();
#ifndef Z7_ST
  // Mirror CProcessAffinity::Get_and_return_NumProcessThreads_and_SysThreads (the
  // Win32-only inline method) using the cross-platform members the Linux struct
  // exposes (Get / GetNumProcessThreads / GetNumSystemThreads) and the engine's
  // NSystem::GetNumberOfProcessors() as the fallback, exactly per System.h 109-125.
  {
    UInt32 num1 = 0, num2 = 0;
    if (threadsInfo.Get())
    {
      num1 = threadsInfo.GetNumProcessThreads();
      num2 = threadsInfo.GetNumSystemThreads();
    }
    if (num1 == 0)
      num1 = NWindows::NSystem::GetNumberOfProcessors();
    if (num1 == 0)
      num1 = 1;
    if (num2 < num1)
      num2 = num1;
    numCPUs = num1;
    numHardwareThreads = num2;
  }
#endif

  UInt32 defaultValue = numCPUs;
  bool useAutoThreads = true;

  {
    const CArcInfoEx &ai = (*ArcFormats)[(unsigned)currentFormatIndex()];
    const int index = _regInfo.FindFormat(ai.Name);
    if (index >= 0)
    {
      const NCompressionQt::CFormatOptions &fo = _regInfo.Formats[(unsigned)index];
      if (isMethodEqualTo(fo.Method) && fo.NumThreads != (UInt32)(Int32)-1)
      {
        defaultValue = fo.NumThreads;
        useAutoThreads = false;
      }
    }
  }

  const int methodID = getMethodID();
  const bool isZip = isZipFormat();

  UInt32 numAlgoThreadsMax = numHardwareThreads * 2; // for unknow methods
  if (isZip)
    numAlgoThreadsMax =
        8 << (sizeof(size_t) / 2); // 32 threads for 32-bit : 128 threads for 64-bit
  else if (isXzFormat())
    numAlgoThreadsMax = 256 * 2; // MTCODER_THREADS_MAX * 2
  else switch (methodID)
  {
    case kLZMA: numAlgoThreadsMax = 2; break;
    case kLZMA2: numAlgoThreadsMax = 256 * 2; break; // MTCODER_THREADS_MAX * 2
    case kBZip2: numAlgoThreadsMax = 64; break;
    case kCopy:
    case kPPMd:
    case kDeflate:
    case kDeflate64:
    case kPPMdZip:
      numAlgoThreadsMax = 1;
  }
  UInt32 autoThreads = numCPUs;
  if (autoThreads > numAlgoThreadsMax)
      autoThreads = numAlgoThreadsMax;

  const UInt64 memUse_Limit = getMemUseBytes();

  if (_ramSize_Defined)
  if (autoThreads > 1)
  {
    if (isZip)
    {
      for (; autoThreads > 1; autoThreads--)
      {
        const UInt64 dict64 = getDict2();
        UInt64 decompressMemory;
        const UInt64 usage = getMemoryUsage_Threads_Dict_DecompMem(autoThreads, dict64, decompressMemory);
        if (usage <= memUse_Limit)
          break;
      }
    }
    else if (methodID == kLZMA2)
    {
      const UInt64 dict64 = getDict2();
      const UInt32 numThreads1 = (getLevel2() >= 5 ? 2 : 1);
      UInt32 numBlockThreads = autoThreads / numThreads1;
      for (; numBlockThreads > 1; numBlockThreads--)
      {
        autoThreads = numBlockThreads * numThreads1;
        UInt64 decompressMemory;
        const UInt64 usage = getMemoryUsage_Threads_Dict_DecompMem(autoThreads, dict64, decompressMemory);
        if (usage <= memUse_Limit)
          break;
      }
      autoThreads = numBlockThreads * numThreads1;
    }
  }

  _auto_NumThreads = autoThreads;

  int curSel = -1;
  {
    AString s;
    s.Add_UInt32(autoThreads);
    if (autoThreads == 0) s += k_ST_Threads;
    modifyAuto(s);
    const int index = _numThreads->count();
    _numThreads->addItem(QString::fromLatin1(s.Ptr()), QVariant((uint)(UInt32)(Int32)-1));
    if (useAutoThreads)
      curSel = index;
  }

  if (numAlgoThreadsMax != autoThreads || autoThreads != 1)
  for (UInt32 i = 1;
      i <= numHardwareThreads * 2 && i <= numAlgoThreadsMax; i++)
  {
    AString s;
    s.Add_UInt32(i);
    if (i == 0) s += k_ST_Threads;
    const int index = _numThreads->count();
    _numThreads->addItem(QString::fromLatin1(s.Ptr()), QVariant((uint)(UInt32)i));
    if (!useAutoThreads && i == defaultValue)
      curSel = index;
  }

  if (curSel < 0)
    curSel = 0;
  _numThreads->setCurrentIndex(curSel);
  _numThreads->blockSignals(wasBlocked);
  enableMultiCombo(_numThreads);
}


// === AddMemComboItem mirror (CompressDialog.cpp 2731-2763). =================
int QtCompressDialog::addMemComboItem(UInt64 val, bool isPercent, bool isDefault)
{
  UString sUser;
  UString sRegistry;
  if (isPercent)
  {
    UString s;
    s.Add_UInt64(val);
    s.Add_Char('%');
    if (isDefault)
      sUser = k_Auto_Prefix;
    else
      sRegistry = s;
    sUser += s;
  }
  else
  {
    AddMemSize(sUser, val);
    sRegistry = sUser;
    for (;;)
    {
      const int pos = sRegistry.Find(L' ');
      if (pos < 0)
        break;
      sRegistry.Delete(pos);
    }
    if (!sRegistry.IsEmpty())
      if (sRegistry.Back() == 'B')
        sRegistry.DeleteBack();
  }
  const unsigned dataIndex = _memUse_Strings.Add(sRegistry);
  const int index = _memUse->count();
  _memUse->addItem(UString_to_QStr(sUser), QVariant((uint)dataIndex));
  return index;
}


// === SetMemUseCombo mirror (CompressDialog.cpp 2767-2854). =================
void QtCompressDialog::setMemUseCombo()
{
  const bool wasBlocked = _memUse->blockSignals(true);
  _memUse_Strings.Clear();
  _memUse->clear();
  const CFormatInfo &fi = g_Formats[getStaticFormatIndex()];

  {
    const bool enable = fi.MemUse_();
    // ShowItem_Bool for the memory labels + combo (mirror 2775-2780).
    _memUse->setVisible(enable);
    _memUse->setEnabled(enable);
    _memValueComp->setVisible(enable);
    _memValueDecomp->setVisible(enable);
    if (!enable)
    {
      _memUse->blockSignals(wasBlocked);
      return;
    }
  }

  UInt64 curMem_Bytes = 0;
  UInt64 curMem_Percents = 0;
  bool needSetCur_Bytes = false;
  bool needSetCur_Percents = false;
  {
    const CArcInfoEx &ai = (*ArcFormats)[(unsigned)currentFormatIndex()];
    const int index = _regInfo.FindFormat(ai.Name);
    if (index >= 0)
    {
      const NCompressionQt::CFormatOptions &fo = _regInfo.Formats[(unsigned)index];
      if (!fo.MemUse.IsEmpty())
      {
        NCompression::CMemUse mu;
        mu.Parse(fo.MemUse);
        if (mu.IsDefined)
        {
          if (mu.IsPercent)
          {
            curMem_Percents = mu.Val;
            needSetCur_Percents = true;
          }
          else
          {
            curMem_Bytes = mu.GetBytes(_ramSize_Reduced);
            needSetCur_Bytes = true;
          }
        }
      }
    }
  }


  // 80% - is auto usage limit in handlers
  addMemComboItem(80, true, true);
  _memUse->setCurrentIndex(0);

  {
    for (unsigned i = 10;; i += 10)
    {
      UInt64 size = i;
      if (i > 100)
        size = (UInt64)(Int64)-1;
      if (needSetCur_Percents && size >= curMem_Percents)
      {
        const int index = addMemComboItem(curMem_Percents, true);
        _memUse->setCurrentIndex(index);
        needSetCur_Percents = false;
        if (size == curMem_Percents)
          continue;
      }
      if (size == (UInt64)(Int64)-1)
        break;
      addMemComboItem(size, true);
    }
  }
  {
    for (unsigned i = (27) * 2;; i++)
    {
      UInt64 size = (UInt64)(2 + (i & 1)) << (i / 2);
      if (i > (20 + sizeof(size_t) * 3 - 1) * 2)
        size = (UInt64)(Int64)-1;
      if (needSetCur_Bytes && size >= curMem_Bytes)
      {
        const int index = addMemComboItem(curMem_Bytes, false);
        _memUse->setCurrentIndex(index);
        needSetCur_Bytes = false;
        if (size == curMem_Bytes)
          continue;
      }
      if (size == (UInt64)(Int64)-1)
        break;
      addMemComboItem(size, false);
    }
  }
  _memUse->blockSignals(wasBlocked);
}


// === Get_MemUse_Spec mirror (CompressDialog.cpp 2857-2862). =================
UString QtCompressDialog::getMemUseSpec() const
{
  if (_memUse->count() < 1)
    return UString();
  const int sel = _memUse->currentIndex();
  if (sel < 0)
    return UString();
  const unsigned dataIndex = (unsigned)_memUse->itemData(sel).toUInt();
  if (dataIndex >= _memUse_Strings.Size())
    return UString();
  return _memUse_Strings[dataIndex];
}

// === Get_MemUse_Bytes mirror (CompressDialog.cpp 2865-2876). ================
UInt64 QtCompressDialog::getMemUseBytes() const
{
  const UString mus = getMemUseSpec();
  NCompression::CMemUse mu;
  if (!mus.IsEmpty())
  {
    mu.Parse(mus);
    if (mu.IsDefined)
      return mu.GetBytes(_ramSize_Reduced);
  }
  return _ramUsage_Auto; // _ramSize_Reduced; // _ramSize;;
}


// === GetMemoryUsage* estimator : VERBATIM from CompressDialog.cpp 2880-3068 ===

// GetMemoryUsage_DecompMem mirror (2880-2883).
UInt64 QtCompressDialog::getMemoryUsage_DecompMem(UInt64 &decompressMemory) const
{
  return getMemoryUsage_Dict_DecompMem(getDict2(), decompressMemory);
}

// GetMemoryUsage_Dict_DecompMem mirror (2896-2899).
UInt64 QtCompressDialog::getMemoryUsage_Dict_DecompMem(UInt64 dict64, UInt64 &decompressMemory) const
{
  return getMemoryUsage_Threads_Dict_DecompMem(getNumThreads2(), dict64, decompressMemory);
}

// GetMemoryUsage_Threads_Dict_DecompMem mirror (2901-3068). VERBATIM.
UInt64 QtCompressDialog::getMemoryUsage_Threads_Dict_DecompMem(UInt32 numThreads, UInt64 dict64, UInt64 &decompressMemory) const
{
  decompressMemory = (UInt64)(Int64)-1;

  const UInt32 level = getLevel2();
  const CArcInfoEx &ai = (*ArcFormats)[(unsigned)currentFormatIndex()];
  if (level == 0 && !ai.Is_Zstd())
  {
    decompressMemory = (1 << 20);
    return decompressMemory;
  }
  UInt64 size = 0;

  const CFormatInfo &fi = g_Formats[getStaticFormatIndex()];
  if (fi.Filter_() && level >= 9)
    size += (12 << 20) * 2 + (5 << 20);
  // UInt32 numThreads = GetNumThreads2();

  UInt32 numMainZipThreads = 1;

  if (isZipFormat())
  {
    UInt32 numSubThreads = 1;
    if (getMethodID() == kLZMA && numThreads > 1 && level >= 5)
      numSubThreads = 2;
    numMainZipThreads = numThreads / numSubThreads;
    if (numMainZipThreads > 1)
      size += (UInt64)numMainZipThreads * ((size_t)sizeof(size_t) << 23);
    else
      numMainZipThreads = 1;
  }

  const int methodId = getMethodID();

  if (dict64 == (UInt64)(Int64)-1
      // && methodId != kZSTD
      )
    return (UInt64)(Int64)-1;


  switch (methodId)
  {
    case kLZMA:
    case kLZMA2:
    {
      const UInt32 dict = (dict64 >= kLzmaMaxDictSize ? kLzmaMaxDictSize : (UInt32)dict64);
      UInt32 hs = dict - 1;
      hs |= (hs >> 1);
      hs |= (hs >> 2);
      hs |= (hs >> 4);
      hs |= (hs >> 8);
      hs >>= 1;
      if (hs >= (1 << 24))
        hs >>= 1;
      hs |= (1 << 16) - 1;
      // if (numHashBytes >= 5)
      if (level < 5)
        hs |= (256 << 10) - 1;
      hs++;
      UInt64 size1 = (UInt64)hs * 4;
      size1 += (UInt64)dict * 4;
      if (level >= 5)
        size1 += (UInt64)dict * 4;
      size1 += (2 << 20);

      UInt32 numThreads1 = 1;
      if (numThreads > 1 && level >= 5)
      {
        size1 += (2 << 20) + (4 << 20);
        numThreads1 = 2;
      }

      UInt32 numBlockThreads = numThreads / numThreads1;

      UInt64 chunkSize = 0; // it's solid chunk

      if (methodId != kLZMA && numBlockThreads != 1)
      {
        chunkSize = Get_Lzma2_ChunkSize(dict);

        if (isXzFormat())
        {
          UInt32 blockSizeLog = getBlockSizeSpec();
          if (blockSizeLog != (UInt32)(Int32)-1)
          {
            if (blockSizeLog == kSolidLog_FullSolid)
            {
              numBlockThreads = 1;
              chunkSize = 0;
            }
            else if (blockSizeLog != kSolidLog_NoSolid)
              chunkSize = (UInt64)1 << blockSizeLog;
          }
        }
      }

      if (chunkSize == 0)
      {
        const UInt32 kBlockSizeMax = (UInt32)0 - (UInt32)(1 << 16);
        UInt64 blockSize = (UInt64)dict + (1 << 16)
          + (numThreads1 > 1 ? (1 << 20) : 0);
        blockSize += (blockSize >> (blockSize < ((UInt32)1 << 30) ? 1 : 2));
        if (blockSize >= kBlockSizeMax)
          blockSize = kBlockSizeMax;
        size += numBlockThreads * (size1 + blockSize);
      }
      else
      {
        size += numBlockThreads * (size1 + chunkSize);
        const UInt32 numPackChunks = numBlockThreads + (numBlockThreads / 8) + 1;
        if (chunkSize < ((UInt32)1 << 26)) numBlockThreads++;
        if (chunkSize < ((UInt32)1 << 24)) numBlockThreads++;
        if (chunkSize < ((UInt32)1 << 22)) numBlockThreads++;
        size += numPackChunks * chunkSize;
      }

      decompressMemory = dict + (2 << 20);
      return size;
    }

    case kPPMd:
    {
      decompressMemory = dict64 + (2 << 20);
      return size + decompressMemory;
    }

    case kDeflate:
    case kDeflate64:
    {
      UInt64 size1 = 3 << 20;
      // if (level >= 7)
        size1 += (1 << 20);
      size += size1 * numMainZipThreads;
      decompressMemory = (2 << 20);
      return size;
    }

    case kBZip2:
    {
      decompressMemory = (7 << 20);
      UInt64 memForOneThread = (10 << 20);
      return size + memForOneThread * numThreads;
    }

    case kPPMdZip:
    {
      decompressMemory = dict64 + (2 << 20);
      return size + (UInt64)decompressMemory * numThreads;
    }
  }

  return (UInt64)(Int64)-1;
}


// === PrintMemUsage mirror (CompressDialog.cpp 3099-3132). Displays the estimate
//     (and, for the compress label, the chosen limit / RAM size like the original).
void QtCompressDialog::printMemUsage(QLabel *label, UInt64 value, bool isCompress)
{
  if (value == (UInt64)(Int64)-1)
  {
    label->setText(QString::fromLatin1("?"));
    return;
  }
  UString s;
  AddMemUsage(s, value);
  if (isCompress)
  {
    const UString mus = getMemUseSpec();
    NCompression::CMemUse mu;
    if (!mus.IsEmpty())
      mu.Parse(mus);
    if (mu.IsDefined)
    {
      s += " / ";
      AddMemUsage(s, mu.GetBytes(_ramSize_Reduced));
    }
    else if (_ramSize_Defined)
    {
      s += " / ";
      AddMemUsage(s, _ramUsage_Auto);
    }

    if (_ramSize_Defined)
    {
      s += " / ";
      AddMemUsage(s, _ramSize);
    }
  }
  label->setText(UString_to_QStr(s));
}


// === SetMemoryUsage mirror (CompressDialog.cpp 3135-3144). =================
void QtCompressDialog::setMemoryUsage()
{
  UInt64 decompressMem;
  const UInt64 memUsage = getMemoryUsage_DecompMem(decompressMem);
  printMemUsage(_memValueComp, memUsage, true);
  printMemUsage(_memValueDecomp, decompressMem, false);
}


// === estimatedCompressMemory (headless debug / tests) ======================
UInt64 QtCompressDialog::estimatedCompressMemory(UInt64 &decompressMemory) const
{
  return getMemoryUsage_DecompMem(decompressMemory);
}


// === memGateBlocks : the OnOK RAM gate (CompressDialog.cpp 1097-1119) =======
// Returns true if accept should be BLOCKED (estimated > limit), filling `message`
// with the SetErrorMessage_MemUsage text (1046-1063) using _ramSize.
bool QtCompressDialog::memGateBlocks(UString &message) const
{
  message.Empty();
  UInt64 decompressMem;
  const UInt64 memUsage = getMemoryUsage_DecompMem(decompressMem);
  if (memUsage == (UInt64)(Int64)-1)
    return false;
  const UInt64 limit = getMemUseBytes();
  if (memUsage <= limit)
    return false;

  // SetErrorMessage_MemUsage (CompressDialog.cpp 1046-1063): the operation-blocked /
  // requires-big-mem text. G.1: each AddLangString(s, IDS_*) maps to its original
  // langID; English = the .rc STRINGTABLE text (resourceGui.rc 21-25, CompressDialog.rc
  // 229, Extract.rc 7). The byte sizes are byte-identical.
  // s2 == OnOK's usageString (CompressDialog.cpp 1105-1112): primarily
  // IDS_MEM_REQUIRED_MEM_SIZE, else the IDT_COMPRESS_MEMORY label minus ':'. The port's
  // inline "Memory usage for Compressing" is that IDT_COMPRESS_MEMORY fallback text, so
  // it carries the IDT_COMPRESS_MEMORY (4017) langID.
  const UString usageString = QStr_to_UString(
      FmLang(IDT_COMPRESS_MEMORY, tr("Memory usage for Compressing")));
  UString &s = message;
  s += QStr_to_UString(FmLang(IDS_MEM_OPERATION_BLOCKED, tr("The operation was blocked by 7-Zip.")));
  s.Add_LF();
  s += QStr_to_UString(FmLang(IDS_MEM_REQUIRES_BIG_MEM, tr("The operation can require big amount of RAM (memory):")));
  s.Add_LF();
  AddSize_MB(s, memUsage);
  s += usageString;
  AddSize_MB(s, _ramSize);
  s += QStr_to_UString(FmLang(IDS_MEM_RAM_SIZE, tr("RAM size")));
  AddSize_MB(s, limit);
  s += QStr_to_UString(FmLang(IDS_MEM_USAGE_LIMIT_SET_BY_7ZIP, tr("usage limit set by 7-Zip")));
  s.Add_LF();
  s.Add_LF();
  s += QStr_to_UString(FmLang(IDS_MEM_ERROR, tr("You can change settings to reduce memory requirements")));
  return true;
}


// === headless harness hooks =================================================
// These drive the SAME combos the real OK path reads, so the real setter logic is
// exercised. selectMethodByName() finds the row whose resolved method name matches
// `name` (the explicit, non-auto rows), selects it, and runs the cascade via the
// currentIndexChanged signal (onMethodChanged -> methodChanged).
bool QtCompressDialog::selectFormatByName(const UString &name)
{
  for (int i = 0; i < _format->count(); i++)
  {
    const int fi = _format->itemData(i).toInt();
    if (fi >= 0 && ArcFormats
        && (*ArcFormats)[(unsigned)fi].Name.IsEqualTo_NoCase(name))
    {
      _format->setCurrentIndex(i); // fires onFormatChanged -> cascade
      return true;
    }
  }
  return false;
}

bool QtCompressDialog::selectLevelByValue(UInt32 level)
{
  for (int i = 0; i < _level->count(); i++)
    if ((UInt32)_level->itemData(i).toInt() == level)
    {
      _level->setCurrentIndex(i); // fires the SetMethod cascade
      return true;
    }
  return false;
}

bool QtCompressDialog::selectMethodByName(const UString &name)
{
  if (name.IsEmpty())
    return false;
  for (int i = 0; i < _method->count(); i++)
  {
    const int data = _method->itemData(i).toInt();
    // Skip the "* auto" row (data == -1): selecting a method by name means an
    // explicit choice, mirroring a user clicking a named row.
    if (data < 0)
      continue;
    const char *mname;
    if ((unsigned)data < Z7_ARRAY_SIZE(kMethodsNames))
      mname = kMethodsNames[data];
    else
    {
      const unsigned ext = (unsigned)data - (unsigned)Z7_ARRAY_SIZE(kMethodsNames);
      if (ext >= ExternalMethods.Size())
        continue;
      mname = ExternalMethods[ext].Ptr();
    }
    if (name.IsEqualTo_Ascii_NoCase(mname))
    {
      _method->setCurrentIndex(i); // fires onMethodChanged -> cascade
      return true;
    }
  }
  return false;
}

bool QtCompressDialog::selectDictionaryByValue(UInt64 val)
{
  for (int i = 0; i < _dictionary->count(); i++)
  {
    const UInt64 d = (UInt64)_dictionary->itemData(i).toULongLong();
    if (d == val)
    {
      _dictionary->setCurrentIndex(i);
      return true;
    }
  }
  return false;
}

bool QtCompressDialog::selectOrderByValue(UInt32 val)
{
  for (int i = 0; i < _order->count(); i++)
  {
    const UInt32 o = (UInt32)_order->itemData(i).toUInt();
    if (o == val)
    {
      _order->setCurrentIndex(i);
      return true;
    }
  }
  return false;
}

// C.3b-2 headless selectors. The combos store the SAME item data the OK getters
// read (GetBlockSizeSpec / GetNumThreadsSpec / Get_MemUse_Spec), so selecting a row
// here drives the exact OK-path logic. The selection fires the cascade tail via the
// connected lambdas (re-estimating memory / re-deriving threads), as for a user.
bool QtCompressDialog::selectSolidByLogSize(UInt32 logSize)
{
  for (int i = 0; i < _solid->count(); i++)
    if ((UInt32)_solid->itemData(i).toUInt() == logSize)
    {
      _solid->setCurrentIndex(i);
      return true;
    }
  return false;
}

bool QtCompressDialog::selectNumThreadsByValue(UInt32 n)
{
  for (int i = 0; i < _numThreads->count(); i++)
    if ((UInt32)_numThreads->itemData(i).toUInt() == n)
    {
      _numThreads->setCurrentIndex(i);
      return true;
    }
  return false;
}

bool QtCompressDialog::selectMemUseByText(const UString &spec)
{
  // Match `spec` against the registry-form mem string parallel to each row.
  for (int i = 0; i < _memUse->count(); i++)
  {
    const unsigned dataIndex = (unsigned)_memUse->itemData(i).toUInt();
    if (dataIndex < _memUse_Strings.Size()
        && _memUse_Strings[dataIndex].IsEqualTo_NoCase(spec))
    {
      _memUse->setCurrentIndex(i);
      return true;
    }
  }
  return false;
}


// === AddVolumeItems mirror (SplitUtils.cpp 75-79) ===========================
// Populate the editable Volume combo with the preset split-size labels. The combo
// then starts with an EMPTY edit text (single archive); the user types/picks a size.
void QtCompressDialog::addVolumeItems()
{
  for (unsigned i = 0; i < (unsigned)(sizeof(k_Sizes) / sizeof(k_Sizes[0])); i++)
    _volume->addItem(QString::fromLatin1(k_Sizes[i]));
  // An editable combo auto-selects item 0; clear the edit text so the default is
  // "no split" (mirror m_Volume which starts blank until the user enters a size).
  _volume->setCurrentIndex(-1);
  _volume->setEditText(QString());
}

void QtCompressDialog::setVolumeText(const UString &text)
{
  _volume->setCurrentIndex(-1);
  _volume->setEditText(UString_to_QStr(text));
}

UString QtCompressDialog::volumeText() const
{
  return QStr_to_UString(_volume->currentText());
}


// === SetArchiveName mirror (CompressDialog.cpp 1477-1516) ===================
void QtCompressDialog::setArchiveName(const UString &name)
{
  if (!ArcFormats)
    return;
  UString fileName = name;
  Info.FormatIndex = currentFormatIndex();
  const CArcInfoEx &ai = (*ArcFormats)[(unsigned)Info.FormatIndex];
  _prevFormatIndex = Info.FormatIndex; // m_PrevFormat = Info.FormatIndex (1482)

  // G.7d: KeepName (single-file gzip/bzip2) names the archive after the source file
  // verbatim (OriginalFileName); otherwise strip the source extension unless KeepName
  // (the multi-file flag) is set. Mirror CompressDialog.cpp:1483-1495.
  if (ai.Flags_KeepName())
  {
    fileName = OriginalFileName;
  }
  else
  {
    if (!Info.KeepName)
    {
      const int dotPos = getExtDotPos(fileName);
      if (dotPos >= 0)
        fileName.DeleteFrom(dotPos);
    }
  }

  // SFX is out of scope (SFX excluded per the user); always the "else" branch.
  fileName.Add_Dot();
  UString ext = ai.GetMainExt();
  // G.7d: a HashHandler "archive" uses the chosen hash method's extension
  // (e.g. sha256), lower-cased. Mirror CompressDialog.cpp:1503-1512.
  if (ai.Flags_HashHandler())
  {
    UString estimatedName;
    getMethodSpec(estimatedName);
    if (!estimatedName.IsEmpty())
    {
      ext = estimatedName;
      ext.MakeLower_Ascii();
    }
  }
  fileName += ext;
  _archivePath->setEditText(UString_to_QStr(fileName));
}


// === SetParams mirror (CompressDialog.cpp 3244-3254) ========================
// Load the per-format remembered Parameters string into the edit. Empty when the
// format has no saved Options.
void QtCompressDialog::setParams()
{
  if (!ArcFormats || currentFormatIndex() < 0)
  {
    _params->setPlainText(QString());
    return;
  }
  const CArcInfoEx &ai = (*ArcFormats)[(unsigned)currentFormatIndex()];
  UString text;
  const int index = _regInfo.FindFormat(ai.Name);
  if (index >= 0)
    text = _regInfo.Formats[(unsigned)index].Options;
  _params->setPlainText(UString_to_QStr(text));
}


// === ShowOptionsString helpers : VERBATIM from CompressDialog.cpp 3327-3350 ==
// AddText_from_BoolPair: a CBoolPair option is shown only when overridden (bp.Def),
// suffixed with '-' when turned off. AddText_from_Bool1: a CBool1-style option is
// shown only when supported AND on. In the port the link/security CBool1s live as
// Info CBoolPairs whose Def==Supported (set by Set_Final_BoolPairs in applyToInfo),
// so the "Supported && Val" test becomes "Def && Val".
static void AddText_from_BoolPair(AString &s, const char *name, const CBoolPair &bp)
{
  if (bp.Def)
  {
    s.Add_OptSpaced(name);
    if (!bp.Val)
      s += "-";
  }
}

static void AddText_from_Bool1AsPair(AString &s, const char *name, const CBoolPair &bp)
{
  if (bp.Def && bp.Val)
    s.Add_OptSpaced(name);
}


// === ShowOptionsString mirror (CompressDialog.cpp 3353-3377) ================
// The one-line read-only summary of the effective tp/tm/tc/ta/-stl + SL/HL switches.
// The AS (AltStreams) / Sec (NtSecurity) entries are NT-only: their Info CBoolPairs
// are never Def on Linux (applyToInfo Init()s them), so the AddText_from_Bool1AsPair
// test drops them — matching the dropped-on-Linux scope.
void QtCompressDialog::showOptionsString()
{
  AString s;
  // fo.IsSet_TimePrec(): TimePrec set when != -1 (mirror CFormatOptions::IsSet_TimePrec).
  if (Info.TimePrec != (UInt32)(Int32)-1)
  {
    s.Add_OptSpaced("tp");
    s.Add_UInt32(Info.TimePrec);
  }
  AddText_from_BoolPair(s, "tm", Info.MTime);
  AddText_from_BoolPair(s, "tc", Info.CTime);
  AddText_from_BoolPair(s, "ta", Info.ATime);
  AddText_from_BoolPair(s, "-stl", Info.SetArcMTime);

  AddText_from_Bool1AsPair(s, "SL",  Info.SymLinks);
  AddText_from_Bool1AsPair(s, "HL",  Info.HardLinks);
  AddText_from_Bool1AsPair(s, "AS",  Info.AltStreams);
  AddText_from_Bool1AsPair(s, "Sec", Info.NtSecurity);

  _optionsString->setText(QString::fromLatin1(s.Ptr()));
}


// === SetParams / SetDeleteAfterCompressing headless hooks ===================
void QtCompressDialog::setParamsText(const UString &text)
{
  _params->setPlainText(UString_to_QStr(text));
}

void QtCompressDialog::setDeleteAfterCompressing(bool on)
{
  _deleteAfter->setChecked(on);
}

// G.7c headless hook: drive the editable archive-path edit text exactly as a user
// typing into m_ArchivePath. We do NOT set _inArcPathSync here, so the editTextChanged
// handler runs the real ArcPath_WasChanged auto-switch (the EFFECT under test).
void QtCompressDialog::setArchivePathText(const UString &text)
{
  _archivePath->setCurrentIndex(-1);
  _archivePath->setEditText(UString_to_QStr(text));
}


// === UpdatePasswordControl mirror (CompressDialog.cpp 565-583) ==============
void QtCompressDialog::onShowPasswordToggled()
{
  const bool show = isShowPasswordChecked();
  _password1->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
  _password2->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
  // When showing the password, the confirm field is hidden (mirror
  // _password2Control.Show_Bool(!showPassword)).
  _password2->setVisible(!show);
}


// === IDB_COMPRESS_OPTIONS mirror (CompressDialog.cpp:607-613) ================
// Open the COptionsDialog (QtCompressOptionsDialog) on the current Info; the
// sub-dialog reads/writes Info's time/link CBoolPairs + TimePrec in place. We sync
// the current format choice into Info first so the sub-dialog gates correctly
// (it reads ArcFormats[Info.FormatIndex].Flags_*). syncInfoFromWidgets()/onAccept()
// never touch these fields, so the values set here survive into QtCompressGUI().
void QtCompressDialog::onOptions()
{
  Info.FormatIndex = currentFormatIndex();
  QtCompressOptionsDialog dlg(this);
  dlg.ArcFormats = ArcFormats;
  dlg.loadFromInfo(Info);
  if (dlg.exec() == QDialog::Accepted)
  {
    dlg.applyToInfo(Info);
    // G.7f: the original re-runs ShowOptionsString after the Options sub-dialog
    // (CompressDialog.cpp:3413 OnButtonClicked IDB_COMPRESS_OPTIONS -> ShowOptionsString).
    showOptionsString();
  }
}


// k_DontSave_Exts mirror (CompressDialog.cpp:876-877): document-container extensions
// that the Compress Browse filter never offers as a save target (they are zip-family
// containers a user would not normally re-create from the Add dialog).
static const char * const k_DontSave_Exts =
  "xpi odt ods docx xlsx ";


// === AddFilter mirror (CompressDialog.cpp:862-873) ==========================
// Append a "Description (*.ext)" entry to BOTH the human-readable description list
// and the Qt name-filter list (one Qt "Name (*.ext)" line). filterFormatRow records
// the Format-combo row this entry maps to (-1 for the all-files trailer).
static void AddFilter_Qt(UStringVector &descriptions, QStringList &qtFilters,
    CRecordVector<int> &filterFormatRow, int formatRow,
    const UString &description, const UString &ext)
{
  UString mask("*.");
  mask += ext;
  UString desc = description;
  desc += " (";
  desc += mask;
  desc += ")";
  descriptions.Add(desc);
  qtFilters.append(UString_to_QStr(desc));
  filterFormatRow.Add(formatRow);
}


// === OnButtonSetArchive filter build (CompressDialog.cpp:888-952) ===========
unsigned QtCompressDialog::buildBrowseFilters(UStringVector &descriptions,
    QStringList &qtFilters, CRecordVector<int> &filterFormatRow) const
{
  descriptions.Clear();
  qtFilters.clear();
  filterFormatRow.Clear();

  if (!ArcFormats || !_format)
    return 0;

  // SFX is EXCLUDED per the user; always the non-SFX branch (CompressDialog.cpp:899).
  const unsigned numFormats = (unsigned)_format->count();

  // filters [0, ... numFormats - 1] correspond to items in the Format combo, in the
  // SAME order. For the "archive (all exts)" trailer we also accumulate every mask
  // and (for the description) each format's main ext (CompressDialog.cpp:905-949).
  UString desc;
  QStringList allMasks;
  CStringFinder finder;

  for (unsigned i = 0; i < numFormats; i++)
  {
    const CArcInfoEx &ai = (*ArcFormats)[(unsigned)_format->itemData((int)i).toInt()];
    UString fdesc = ai.Name;
    fdesc += " (";
    QString qmask;
    bool needSpace_desc = false;

    FOR_VECTOR (k, ai.Exts)
    {
      const UString &ext = ai.Exts[k].Ext;

      // k_DontSave_Exts (xpi/odt/ods/docx/xlsx): never offered as a save filter
      // (CompressDialog.cpp:923-924).
      if (finder.FindWord_In_LowCaseAsciiList_NoCase(k_DontSave_Exts, ext))
        continue;

      UString mask("*.");
      mask += ext;
      if (!qmask.isEmpty())
        qmask += QLatin1Char(' ');
      qmask += UString_to_QStr(mask);
      allMasks.append(UString_to_QStr(mask));

      if (needSpace_desc)
        fdesc.Add_Space();
      needSpace_desc = true;
      fdesc += ext;
    }
    fdesc += ")";

    descriptions.Add(fdesc);
    // A Qt name filter is "Name (*.a *.b ...)" — the format's display Name plus the
    // glob masks. If a format had ALL its exts in k_DontSave_Exts (so qmask is empty),
    // fall back to "*" so the entry stays a valid filter.
    if (qmask.isEmpty())
      qmask = QStringLiteral("*");
    qtFilters.append(UString_to_QStr(ai.Name) + QStringLiteral(" (") + qmask + QStringLiteral(")"));
    filterFormatRow.Add((int)i);

    // we use only main ext in desc to reduce the size of list (CompressDialog.cpp:934-937).
    if (i != 0)
      desc.Add_Space();
    desc += ai.GetMainExt();
  }

  // The "archive (all known exts)" filter (CompressDialog.cpp:940-949). Its label is
  // IDT_COMPRESS_ARCHIVE ("&Archive:") with ONLY the '&' mnemonic removed — exactly as
  // the original (line 944 does just RemoveChar(L'&')), so it reads "Archive: (...)".
  {
    UString aLabel = QStr_to_UString(FmLang(IDT_COMPRESS_ARCHIVE, QStringLiteral("&Archive:")));
    aLabel.RemoveChar(L'&');
    UString adesc = aLabel;
    adesc += " (";
    adesc += desc;
    adesc += ")";
    descriptions.Add(adesc);
    QString qall = allMasks.isEmpty() ? QStringLiteral("*") : allMasks.join(QLatin1Char(' '));
    qtFilters.append(UString_to_QStr(aLabel) + QStringLiteral(" (") + qall + QStringLiteral(")"));
    filterFormatRow.Add(-1);
  }

  // All Files (CompressDialog.cpp:952). LangString(IDS_OPEN_TYPE_ALL_FILES) == "All Files".
  AddFilter_Qt(descriptions, qtFilters, filterFormatRow, -1,
      QStr_to_UString(FmLang(IDS_OPEN_TYPE_ALL_FILES, QStringLiteral("All Files"))),
      UString("*"));

  return numFormats;
}


UStringVector QtCompressDialog::browseFilterStrings() const
{
  UStringVector descriptions;
  QStringList qtFilters;
  CRecordVector<int> filterFormatRow;
  buildBrowseFilters(descriptions, qtFilters, filterFormatRow);
  return descriptions;
}


// === OnButtonSetArchive mirror (CompressDialog.cpp OnButtonSetArchive) ======
void QtCompressDialog::onBrowse()
{
  const QString current = _archivePath->currentText();

  // Build the per-format Browse filter list (CompressDialog.cpp:888-952).
  UStringVector descriptions;
  QStringList qtFilters;
  CRecordVector<int> filterFormatRow;
  const unsigned numFormats = buildBrowseFilters(descriptions, qtFilters, filterFormatRow);

  // The pre-selected filter is the current Format row, mirroring filterIndex =
  // m_Format.GetCurSel() (CompressDialog.cpp:901). If nothing is selected, the
  // original falls back to the last (all-files) filter (953-954).
  int filterRow = _format ? _format->currentIndex() : -1;
  if (filterRow < 0)
    filterRow = (int)qtFilters.size() - 1;

  // G.1 : the original OnButtonSetArchive titles this picker with
  // LangString(IDS_COMPRESS_SET_ARCHIVE_BROWSE) == "Browse" (CompressDialog.rc:219).
  QFileDialog dlg(this, FmLang(IDS_COMPRESS_SET_ARCHIVE_BROWSE, QStringLiteral("Browse")),
      current);
  dlg.setAcceptMode(QFileDialog::AcceptSave); // bi.SaveMode = true (959)
  dlg.setFileMode(QFileDialog::AnyFile);
  dlg.setOption(QFileDialog::DontConfirmOverwrite, true);
  dlg.setNameFilters(qtFilters);
  if (filterRow >= 0 && filterRow < qtFilters.size())
    dlg.selectNameFilter(qtFilters.at(filterRow));

  if (dlg.exec() != QDialog::Accepted)
    return; // bi.BrowseForFile returned false (964-965)

  const QStringList chosen = dlg.selectedFiles();
  if (chosen.isEmpty())
    return;
  UString path = QStr_to_UString(chosen.first());

  // Which filter did the user confirm? Map the selected name-filter back to its row.
  const QString selFilter = dlg.selectedNameFilter();
  int selRow = qtFilters.indexOf(selFilter);

  // if ((unsigned)bi.FilterIndex < numFormats): the chosen filter is a specific format,
  // so try to set that format's extension on the path (CompressDialog.cpp:979-997).
  if (selRow >= 0 && (unsigned)selRow < numFormats && ArcFormats && _format)
  {
    bool needAddExt = true;
    const CArcInfoEx &ai = (*ArcFormats)[(unsigned)_format->itemData(selRow).toInt()];
    const int dotPos = getExtDotPos(path);
    if (dotPos >= 0)
    {
      const UString ext = path.Ptr(dotPos + 1);
      if (ai.FindExtension(ext) >= 0)
        needAddExt = false;
    }
    if (needAddExt)
    {
      if (path.IsEmpty() || path.Back() != L'.')
        path.Add_Dot();
      path += ai.GetMainExt();
    }
  }

  // SetArcPathFields(path) (CompressDialog.cpp:999): set the edit text. Guard the
  // programmatic write so the editTextChanged handler does not run the auto-switch
  // here — the explicit format re-selection below (or ArcPath_WasChanged) does it.
  {
    _inArcPathSync = true;
    _archivePath->setCurrentIndex(-1);
    _archivePath->setEditText(UString_to_QStr(path));
    _inArcPathSync = false;
  }

  // archive format was confirmed AND differs from the current Format combo selection:
  // re-select it (which fires onFormatChanged -> the full cascade) and return
  // (CompressDialog.cpp:1001-1009). Otherwise run ArcPath_WasChanged on the typed
  // name's extension (1011).
  if (selRow >= 0 && (unsigned)selRow < numFormats && _format
      && selRow != _format->currentIndex())
  {
    _format->setCurrentIndex(selRow); // fires onFormatChanged (== SetCurSel + FormatChanged)
    return;
  }

  arcPathWasChanged(path);
}


// === ArcPath_WasChanged mirror (CompressDialog.cpp:1263-1287) ===============
void QtCompressDialog::arcPathWasChanged(const UString &path)
{
  if (!ArcFormats || !_format)
    return;

  const int dotPos = getExtDotPos(path);
  if (dotPos < 0)
    return;
  const UString ext = path.Ptr(dotPos + 1);

  // If the extension already belongs to the current format, nothing to do (1270-1273).
  {
    const int cur = currentFormatIndex();
    if (cur >= 0)
    {
      const CArcInfoEx &ai = (*ArcFormats)[(unsigned)cur];
      if (ai.FindExtension(ext) >= 0)
        return;
    }
  }

  // Otherwise scan the Format combo for a format whose extension list contains `ext`;
  // the first match re-selects that format and re-runs FormatChanged (1275-1286).
  const unsigned count = (unsigned)_format->count();
  for (unsigned i = 0; i < count; i++)
  {
    const CArcInfoEx &ai = (*ArcFormats)[(unsigned)_format->itemData((int)i).toInt()];
    if (ai.FindExtension(ext) >= 0)
    {
      if ((int)i == _format->currentIndex())
        return; // already selected (defensive; the FindExtension check above caught it)
      // The combo's currentIndexChanged (onFormatChanged) rewrites the archive-path
      // text via setArchiveName; guard so that programmatic rewrite does not re-enter
      // this handler (mirrors the original, where SELCHANGE-only re-entry is impossible).
      _inArcPathSync = true;
      _format->setCurrentIndex((int)i); // SetCurSel + SaveOptionsInMem + FormatChanged(true)
      _inArcPathSync = false;
      return;
    }
  }
}


// === read-back half of OnOK (no validation/save) ===========================
// Mirrors the Info-population statements in OnOK (CompressDialog.cpp 1136-1178)
// for the C.3a fields. Shared by onAccept() and the headless harness path.
void QtCompressDialog::syncInfoFromWidgets()
{
  Info.FormatIndex = currentFormatIndex();
  Info.UpdateMode = (NCompressDialog::NUpdateMode::EEnum)_updateMode->currentData().toInt();
  Info.PathMode = (NWildcard::ECensorPathMode)_pathMode->currentData().toInt();
  if (_level->currentIndex() >= 0)
    Info.Level = (UInt32)_level->currentData().toInt();
  // C.3b-1 advanced fields (mirror OnOK 1140-1170). GetDictSpec/GetOrderSpec/
  // GetMethodSpec return the auto sentinels (-1 / empty) when the "* auto" row is
  // selected, so an all-auto dialog leaves these at their CInfo ctor defaults and
  // the engine derives everything from Level (== `7zz a -mx=N`).
  Info.Dict64 = getDictSpec();
  Info.Order = getOrderSpec();
  Info.OrderMode = getOrderMode();
  Info.Method = getMethodSpec();
  // C.3b-2 advanced fields (mirror OnOK 1144-1168). GetNumThreadsSpec/Get_MemUse_Spec/
  // GetBlockSizeSpec return the auto sentinels when the "* auto" rows are selected,
  // so an all-auto dialog leaves NumThreads/MemUsage at CInfo defaults and
  // SolidIsSpecified per the format (the engine then derives them from Level).
  Info.NumThreads = getNumThreadsSpec();
  Info.MemUsage.Clear();
  {
    const UString mus = getMemUseSpec();
    if (!mus.IsEmpty())
    {
      NCompression::CMemUse mu;
      mu.Parse(mus);
      if (mu.IsDefined)
        Info.MemUsage = mu;
    }
  }
  {
    // FormatChanged (CompressDialog.cpp 706-707) sets SolidIsSpecified to the
    // format's Solid_() flag; OnOK (1158-1168) then turns it off if the "* auto"
    // row is selected (solidLogSize == -1).
    Info.SolidIsSpecified = g_Formats[getStaticFormatIndex()].Solid_();
    const UInt32 solidLogSize = getBlockSizeSpec();
    Info.SolidBlockSize = 0;
    if (solidLogSize == (UInt32)(Int32)-1)
      Info.SolidIsSpecified = false;
    else if (solidLogSize > 0)
      Info.SolidBlockSize = (solidLogSize >= 64) ?
          (UInt64)(Int64)-1 :
          ((UInt64)1 << solidLogSize);
  }
  Info.EncryptionMethod = getEncryptionMethodSpec();
  Info.EncryptHeaders = _encryptHeaders->isChecked();
  if (ArcFormats && Info.FormatIndex >= 0)
    Info.EncryptHeadersIsAllowed = (*ArcFormats)[(unsigned)Info.FormatIndex].Is_7z();
  // G.7a: m_Params.GetText(Info.Options) (OnOK 1209). G.7b: Info.DeleteAfterCompressing
  // = IsButtonCheckedBool(IDX_COMPRESS_DEL) (OnOK 1175).
  Info.Options = QStr_to_UString(_params->toPlainText());
  Info.DeleteAfterCompressing = _deleteAfter->isChecked();
  Info.Password = QStr_to_UString(_password1->text());
  {
    UString s = QStr_to_UString(_archivePath->currentText());
    s.Trim();
    Info.ArcPath = s;
  }
  // C.6 (mirror OnOK 1211-1222, sans the interactive warnings the headless path
  // cannot show): parse the Volume field into Info.VolumeSizes. Empty == single
  // archive. A parse failure leaves VolumeSizes empty (the headless harness should
  // pass a well-formed size). The interactive onAccept() does the full check.
  {
    UString volumeString = QStr_to_UString(_volume->currentText());
    volumeString.Trim();
    Info.VolumeSizes.Clear();
    if (!volumeString.IsEmpty())
      ParseVolumeSizes(volumeString, Info.VolumeSizes);
  }
}


// === OnOK mirror (CompressDialog.cpp 1066-1252) =============================
void QtCompressDialog::onAccept()
{
  // --- password read + validation (CompressDialog.cpp 1068-1095) ------------
  Info.Password = QStr_to_UString(_password1->text());

  const CArcInfoEx &ai = (*ArcFormats)[(unsigned)currentFormatIndex()];
  const bool isZip = ai.Is_Zip();

  if (isZip)
  {
    // zip passwords must be ASCII (IsZipFormat path).
    bool ascii = true;
    for (unsigned i = 0; i < Info.Password.Len(); i++)
      if (Info.Password[i] >= 0x80) { ascii = false; break; }
    if (!ascii)
    {
      // G.1 : IDS_PASSWORD_USE_ASCII (CompressDialog.cpp:1073 ShowErrorMessageHwndRes).
      QMessageBox::critical(this, QString::fromLatin1("7-Zip"),
          FmLang(IDS_PASSWORD_USE_ASCII, tr("Password must consist of ASCII characters.")));
      return;
    }
    const UString method = getEncryptionMethodSpec();
    if (method.IsPrefixedBy_Ascii_NoCase("aes") && Info.Password.Len() > 99)
    {
      // G.1 : IDS_PASSWORD_TOO_LONG (CompressDialog.cpp:1081 ShowErrorMessageHwndRes).
      QMessageBox::critical(this, QString::fromLatin1("7-Zip"),
          FmLang(IDS_PASSWORD_TOO_LONG, tr("Too long password.")));
      return;
    }
  }

  // Password-match check (only when the confirm field is shown). Mirror
  // CompressDialog.cpp 1086-1095.
  if (!isShowPasswordChecked())
  {
    const UString password2 = QStr_to_UString(_password2->text());
    if (password2 != Info.Password)
    {
      // G.1 : IDS_PASSWORD_NOT_MATCH (CompressDialog.cpp:1092 ShowErrorMessageHwndRes).
      QMessageBox::critical(this, QString::fromLatin1("7-Zip"),
          FmLang(IDS_PASSWORD_NOT_MATCH, tr("Passwords do not match.")));
      return;
    }
  }

  // --- RAM gate (CompressDialog.cpp 1097-1119) ------------------------------
  // If the estimated compress memory exceeds the chosen MemUse limit, block accept
  // with the SetErrorMessage_MemUsage text (built against _ramSize). This is the
  // exact OnOK gate; memGateBlocks() reproduces GetMemoryUsage_DecompMem + the
  // memUsage > Get_MemUse_Bytes() test.
  {
    UString msg;
    if (memGateBlocks(msg))
    {
      QMessageBox::critical(this, QString::fromLatin1("7-Zip"), UString_to_QStr(msg));
      return;
    }
  }

  // --- archive path (CompressDialog.cpp 1124-1134) --------------------------
  UStringVector arcPaths;
  {
    UString s = QStr_to_UString(_archivePath->currentText());
    s.Trim();
    if (s.IsEmpty())
    {
      // G.1 : no original langID — CompressDialog.cpp uses the hardcoded wide literal
      // k_IncorrectPathMessage = L"Incorrect archive path" via ShowErrorMessage (NOT
      // ShowErrorMessageHwndRes), so this stays a literal.
      QMessageBox::critical(this, QString::fromLatin1("7-Zip"),
          QStringLiteral("Incorrect archive path."));
      return;
    }
    Info.ArcPath = s;
    addUniqueString(arcPaths, s);
  }

  // --- update / path mode (CompressDialog.cpp 1136-1137) --------------------
  Info.UpdateMode = (NCompressDialog::NUpdateMode::EEnum)_updateMode->currentData().toInt();
  Info.PathMode = (NWildcard::ECensorPathMode)_pathMode->currentData().toInt();

  // --- Level (CompressDialog.cpp 1139). ------------------------------------
  Info.Level = (UInt32)_level->currentData().toInt();

  // --- Method / Dictionary / Order (CompressDialog.cpp 1140-1170) ----------
  // GetDictSpec/GetOrderSpec/GetMethodSpec return the auto sentinels (-1 / empty)
  // for the "* auto" rows, so an all-auto dialog keeps these at CInfo defaults and
  // the engine derives method/dict/order from Level. NumThreads / SolidIsSpecified
  // / MemUsage remain at the ctor sentinels (the C.3b-2 surface).
  Info.Dict64 = getDictSpec();
  Info.Order = getOrderSpec();
  Info.OrderMode = getOrderMode();

  // --- Threads / Memory / Solid (CompressDialog.cpp 1144-1168) -------------
  Info.NumThreads = getNumThreadsSpec();

  Info.MemUsage.Clear();
  {
    const UString mus = getMemUseSpec();
    if (!mus.IsEmpty())
    {
      NCompression::CMemUse mu;
      mu.Parse(mus);
      if (mu.IsDefined)
        Info.MemUsage = mu;
    }
  }

  {
    // SolidIsSpecified was set to fi.Solid_() in FormatChanged (706-707); the EXACT
    // conversion at OnOK 1158-1168: -1 -> not specified; >=64 -> -1 (solid-all);
    // else 1<<logSize.
    Info.SolidIsSpecified = g_Formats[getStaticFormatIndex()].Solid_();
    const UInt32 solidLogSize = getBlockSizeSpec();
    Info.SolidBlockSize = 0;
    if (solidLogSize == (UInt32)(Int32)-1)
      Info.SolidIsSpecified = false;
    else if (solidLogSize > 0)
      Info.SolidBlockSize = (solidLogSize >= 64) ?
          (UInt64)(Int64)-1 :
          ((UInt64)1 << solidLogSize);
  }

  Info.Method = getMethodSpec();

  // --- encryption method + encrypt-headers (CompressDialog.cpp 1170-1178) ---
  Info.EncryptionMethod = getEncryptionMethodSpec();
  Info.FormatIndex = currentFormatIndex();
  Info.EncryptHeaders = _encryptHeaders->isChecked();

  // --- Delete-after + Parameters (CompressDialog.cpp 1175 / 1209) -----------
  Info.DeleteAfterCompressing = _deleteAfter->isChecked();
  Info.Options = QStr_to_UString(_params->toPlainText());

  // --- Volume / split (CompressDialog.cpp 1211-1235) ------------------------
  // m_Volume.GetText(volumeString); trim; ParseVolumeSizes -> Info.VolumeSizes.
  // An empty field == single archive (VolumeSizes empty). A parse failure shows the
  // IDS_INCORRECT_VOLUME_SIZE error and blocks accept; a too-small last volume
  // (< 100 KiB) raises the IDS_SPLIT_CONFIRM Yes/No/Cancel sanity check.
  {
    UString volumeString = QStr_to_UString(_volume->currentText());
    volumeString.Trim();
    Info.VolumeSizes.Clear();

    if (!volumeString.IsEmpty())
    {
      if (!ParseVolumeSizes(volumeString, Info.VolumeSizes))
      {
        // G.1 : IDS_INCORRECT_VOLUME_SIZE (CompressDialog.cpp:1220 ShowErrorMessageHwndRes).
        QMessageBox::critical(this, QString::fromLatin1("7-Zip"),
            FmLang(IDS_INCORRECT_VOLUME_SIZE, tr("Incorrect volume size")));
        return;
      }
      if (!Info.VolumeSizes.IsEmpty())
      {
        const UInt64 volumeSize = Info.VolumeSizes.Back();
        if (volumeSize < (100 << 10))
        {
          wchar_t s[32];
          ConvertUInt64ToString(volumeSize, s);
          // G.1 : MyFormatNew(IDS_SPLIT_CONFIRM, s) (CompressDialog.cpp:1230). The
          // single IDS_SPLIT_CONFIRM (7308) template carries the {0} placeholder for
          // the byte count (CompressDialog.rc:224 "Specified volume size: {0} bytes.\n
          // Are you sure you want to split archive into such volumes?"). Fetch the whole
          // (possibly translated) template, then substitute {0} — mirroring MyFormatNew,
          // exactly as fm/QtFileManagerWindow.cpp:779-780 does for IDS_N_SELECTED_ITEMS.
          QString msg = FmLang(IDS_SPLIT_CONFIRM, QStringLiteral(
              "Specified volume size: {0} bytes.\nAre you sure you want to split archive into such volumes?"))
              .replace(QStringLiteral("{0}"), QString::fromWCharArray(s));
          QMessageBox box(QMessageBox::Question, QString::fromLatin1("7-Zip"), msg,
              QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, this);
          // G.1 : standard Yes/No/Cancel buttons use langIDs 406/407/402 (kLangPairs,
          // LangUtils.cpp:66-67 + 402); English from the dialog-template literal.
          if (QAbstractButton *b = box.button(QMessageBox::Yes))
            b->setText(FmLang(406, QStringLiteral("Yes")));
          if (QAbstractButton *b = box.button(QMessageBox::No))
            b->setText(FmLang(407, QStringLiteral("No")));
          if (QAbstractButton *b = box.button(QMessageBox::Cancel))
            b->setText(FmLang(402, QStringLiteral("Cancel")));
          if (box.exec() != QMessageBox::Yes)
            return;
        }
      }
    }
  }

  // --- persist registry info (CompressDialog.cpp 1177-1249) -----------------
  _regInfo.EncryptHeaders = Info.EncryptHeaders;
  if (Info.FormatIndex >= 0)
    _regInfo.ArcType = ai.Name;
  _regInfo.ShowPassword = isShowPasswordChecked();
  _regInfo.Level = Info.Level;

  // B.9: persist the link/security CBoolPairs at the global CInfo level (mirror
  // SaveOptionsInMem 3358-3372 + OnOK 3799-3813). The Options sub-dialog has
  // already folded its choices into Info via applyToInfo().
  _regInfo.SymLinks      = Info.SymLinks;
  _regInfo.HardLinks     = Info.HardLinks;
  _regInfo.AltStreams    = Info.AltStreams;
  _regInfo.NtSecurity    = Info.NtSecurity;
  _regInfo.PreserveATime = Info.PreserveATime;

  // Per-format options: persist the chosen Level + Method + Dictionary + Order +
  // EncryptionMethod for this format (mirror SaveOptionsInMem). Method/Dict/Order
  // are stored at their *spec* (explicit-only) values so a later SetDictionary2 /
  // SetOrder2 picks them back up as the per-format default.
  {
    NCompressionQt::CFormatOptions &fo = _regInfo.Get_FormatOptions(ai.Name);
    // G.7a: persist the trimmed Parameters string (SaveOptionsInMem 3263-3269:
    // Info.Options.Trim(); fo.Options = Info.Options).
    Info.Options.Trim();
    fo.Options = Info.Options;
    fo.Level = Info.Level;
    fo.Method = Info.Method;
    if (Info.Dict64 != (UInt64)(Int64)-1)
      fo.Dictionary = (UInt32)Info.Dict64;
    if (Info.Order != (UInt32)(Int32)-1)
      fo.Order = Info.Order;
    fo.EncryptionMethod = Info.EncryptionMethod;
    // C.3b-2 (mirror SaveOptionsInMem 3314-3316): persist the chosen thread count /
    // solid block-size-log / mem-use spec at their *spec* (explicit-only) values, so
    // SetNumThreads2 / SetSolidBlockSize2 / SetMemUseCombo pick them back up.
    fo.NumThreads = getNumThreadsSpec();
    fo.BlockLogSize = getBlockSizeSpec();
    fo.MemUse = getMemUseSpec();
    // B.9: per-format time block (mirror OnOK 3808-3812 / SaveOptionsInMem).
    fo.TimePrec    = Info.TimePrec;
    fo.MTime       = Info.MTime;
    fo.CTime       = Info.CTime;
    fo.ATime       = Info.ATime;
    fo.SetArcMTime = Info.SetArcMTime;
  }

  // MRU history: chosen path first, then the existing entries (CompressDialog.cpp
  // 1241-1247).
  for (unsigned i = 0; i < _regInfo.ArcPaths.Size(); i++)
  {
    if (arcPaths.Size() >= kHistorySize)
      break;
    addUniqueString(arcPaths, _regInfo.ArcPaths[i]);
  }
  _regInfo.ArcPaths = arcPaths;

  _regInfo.Save();

  accept();
}
