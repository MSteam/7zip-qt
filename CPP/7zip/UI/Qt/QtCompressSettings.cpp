// QtCompressSettings.cpp
//
// QSettings-backed reimplementation of ZipRegistry.cpp's NCompression::CInfo /
// CFormatOptions Save()/Load(). The logic tracks the original; only the backing
// store (CKey over the Win32 registry) is swapped for QSettings.
//
// Mapping of the upstream value names (ZipRegistry.cpp, namespace NCompression):
//   kKeyName        "Compression"     -> QSettings group "Compression"
//   kLevel          "Level"           -> UInt32
//   kArchiver       "Archiver"        -> ArcType (string)
//   kShowPassword   "ShowPassword"    -> bool
//   kEncryptHeaders "EncryptHeaders"  -> bool
//   kArcHistory     "ArcHistory"      -> archive-path MRU (StringList)
//   kOptionsKeyName "Options"         -> subgroup; one child per FormatID with:
//       kMethod / kOptions / kEncryptionMethod / kMemUse (strings)
//       kLevel / kDictionary / kOrder / kBlockSize / kNumThreads (UInt32)
//       kTimePrec (UInt32) ; kMTime/kATime/kCTime/kSetArcMTime (CBoolPair)
//
// QSettings location: org "7-Zip", app "7-Zip" (same file as QtExtractSettings),
// IniFormat -> $XDG_CONFIG_HOME/7-Zip/7-Zip.conf. The verification harness points
// XDG_CONFIG_HOME at a temp dir and inspects the [Compression] group there.

#include "QtCompressSettings.h"

#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>

namespace {

const char * const kOrg   = "7-Zip";
const char * const kApp   = "7-Zip";
const char * const kGroup = "Compression";

// Value names — byte-identical to ZipRegistry.cpp's NCompression constants.
const char * const kArcHistory     = "ArcHistory";
const char * const kArchiver       = "Archiver";
const char * const kShowPassword   = "ShowPassword";
const char * const kEncryptHeaders = "EncryptHeaders";
const char * const kOptionsKeyName = "Options";

const char * const kLevel          = "Level";
const char * const kDictionary     = "Dictionary";
const char * const kOrder          = "Order";
const char * const kBlockSize      = "BlockSize";
const char * const kNumThreads     = "NumThreads";
const char * const kMethod         = "Method";
const char * const kOptions        = "Options";
const char * const kEncryptionMethod = "EncryptionMethod";
const char * const kMemUse         = "MemUse";

const char * const kTimePrec       = "TimePrec";
const char * const kMTime          = "MTime";
const char * const kATime          = "ATime";
const char * const kCTime          = "CTime";
const char * const kSetArcMTime    = "SetArcMTime";

QSettings MakeSettings()
{
  return QSettings(QSettings::IniFormat, QSettings::UserScope,
      QString::fromLatin1(kOrg), QString::fromLatin1(kApp));
}

static UString QStr_to_UString(const QString &s)
{
  UString u;
  const int n = s.size();
  for (int i = 0; i < n; i++)
    u += (wchar_t)s.at(i).unicode();
  return u;
}

static QString UString_to_QStr(const UString &u)
{
  QString s;
  for (unsigned i = 0; i < u.Len(); i++)
    s.append(QChar((char16_t)(unsigned)u[i]));
  return s;
}

// Mirror SetRegString(): empty value => delete (absent), else write.
static void Set_String(QSettings &st, const char *name, const UString &value)
{
  const QString key = QString::fromLatin1(name);
  if (value.IsEmpty())
    st.remove(key);
  else
    st.setValue(key, UString_to_QStr(value));
}

static void Get_String(QSettings &st, const char *name, UString &value)
{
  const QString key = QString::fromLatin1(name);
  if (st.contains(key))
    value = QStr_to_UString(st.value(key).toString());
  else
    value.Empty();
}

// Mirror Key_Set_UInt32(): only write when defined (!= -1), else delete. (The
// upstream Key_Set_UInt32 deletes the value when val == (UInt32)-1.)
static void Set_UInt32(QSettings &st, const char *name, UInt32 value)
{
  const QString key = QString::fromLatin1(name);
  if (value == (UInt32)(Int32)-1)
    st.remove(key);
  else
    st.setValue(key, (uint)value);
}

static void Get_UInt32(QSettings &st, const char *name, UInt32 &value)
{
  const QString key = QString::fromLatin1(name);
  if (st.contains(key))
  {
    bool ok = false;
    const uint v = st.value(key).toUInt(&ok);
    if (ok)
      value = (UInt32)v;
  }
}

// CBoolPair: write only if Def (mirror Key_Set_BoolPair_Delete_IfNotDef -> here
// we remove() when !Def so a stale value never lingers).
static void Set_BoolPair(QSettings &st, const char *name, const CBoolPair &b)
{
  const QString key = QString::fromLatin1(name);
  if (b.Def)
    st.setValue(key, b.Val);
  else
    st.remove(key);
}

static void Get_BoolPair(QSettings &st, const char *name, CBoolPair &b)
{
  const QString key = QString::fromLatin1(name);
  if (st.contains(key))
  {
    b.Val = st.value(key).toBool();
    b.Def = true;
  }
  else
  {
    b.Val = false;
    b.Def = false;
  }
}

} // namespace


namespace NCompressionQt {

int CInfo::FindFormat(const UString &name) const
{
  FOR_VECTOR (i, Formats)
    if (name.IsEqualTo_NoCase(Formats[i].FormatID))
      return (int)i;
  return -1;
}

CFormatOptions &CInfo::Get_FormatOptions(const UString &name)
{
  const int idx = FindFormat(name);
  if (idx >= 0)
    return Formats[(unsigned)idx];
  CFormatOptions fo;
  fo.FormatID = name;
  const unsigned added = Formats.Add(fo);
  return Formats[added];
}


// Mirror of NCompression::CInfo::Save() (ZipRegistry.cpp lines 255-305).
void CInfo::Save() const
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kGroup));

  Set_BoolPair(st, "Security",      NtSecurity);
  Set_BoolPair(st, "AltStreams",    AltStreams);
  Set_BoolPair(st, "HardLinks",     HardLinks);
  Set_BoolPair(st, "SymLinks",      SymLinks);
  Set_BoolPair(st, "PreserveATime", PreserveATime);

  st.setValue(QString::fromLatin1(kShowPassword), ShowPassword);
  st.setValue(QString::fromLatin1(kLevel), (uint)Level);
  st.setValue(QString::fromLatin1(kArchiver), UString_to_QStr(ArcType));
  st.setValue(QString::fromLatin1(kEncryptHeaders), EncryptHeaders);

  // ArcHistory: full replace of the MRU list (upstream does RecurseDeleteKey +
  // SetValue_Strings). QStringList round-trips through QSettings as a multi-value.
  {
    QStringList list;
    for (unsigned i = 0; i < ArcPaths.Size(); i++)
      list << UString_to_QStr(ArcPaths[i]);
    st.setValue(QString::fromLatin1(kArcHistory), list);
  }
  st.endGroup();

  // Options subgroup: one child group per FormatID (upstream RecurseDeleteKey +
  // recreate). We clear the whole subtree, then write each format's block.
  st.beginGroup(QString::fromLatin1(kGroup));
  st.beginGroup(QString::fromLatin1(kOptionsKeyName));
  st.remove(QString()); // wipe stale per-format keys (== RecurseDeleteKey Options)
  for (unsigned i = 0; i < Formats.Size(); i++)
  {
    const CFormatOptions &fo = Formats[i];
    st.beginGroup(UString_to_QStr(fo.FormatID));

    Set_String(st, kMethod,           fo.Method);
    Set_String(st, kOptions,          fo.Options);
    Set_String(st, kEncryptionMethod, fo.EncryptionMethod);
    Set_String(st, kMemUse,           fo.MemUse);

    Set_UInt32(st, kLevel,      fo.Level);
    Set_UInt32(st, kDictionary, fo.Dictionary);
    Set_UInt32(st, kOrder,      fo.Order);
    Set_UInt32(st, kBlockSize,  fo.BlockLogSize);
    Set_UInt32(st, kNumThreads, fo.NumThreads);

    Set_UInt32(st, kTimePrec,   fo.TimePrec);
    Set_BoolPair(st, kMTime,       fo.MTime);
    Set_BoolPair(st, kATime,       fo.ATime);
    Set_BoolPair(st, kCTime,       fo.CTime);
    Set_BoolPair(st, kSetArcMTime, fo.SetArcMTime);

    st.endGroup();
  }
  st.endGroup();
  st.endGroup();
  st.sync();
}


// Mirror of NCompression::CInfo::Load() (ZipRegistry.cpp lines 307-374).
void CInfo::Load()
{
  ArcPaths.Clear();
  Formats.Clear();

  Level = 5;
  ArcType = L"7z";
  ShowPassword = false;
  EncryptHeaders = false;

  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kGroup));

  Get_BoolPair(st, "Security",      NtSecurity);
  Get_BoolPair(st, "AltStreams",    AltStreams);
  Get_BoolPair(st, "HardLinks",     HardLinks);
  Get_BoolPair(st, "SymLinks",      SymLinks);
  Get_BoolPair(st, "PreserveATime", PreserveATime);

  // ArcHistory MRU.
  {
    const QString key = QString::fromLatin1(kArcHistory);
    if (st.contains(key))
    {
      const QStringList list = st.value(key).toStringList();
      for (const QString &s : list)
        ArcPaths.Add(QStr_to_UString(s));
    }
  }

  // Per-format Options blocks.
  {
    st.beginGroup(QString::fromLatin1(kOptionsKeyName));
    const QStringList formatIDs = st.childGroups();
    for (const QString &fid : formatIDs)
    {
      st.beginGroup(fid);
      CFormatOptions fo;
      fo.FormatID = QStr_to_UString(fid);

      Get_String(st, kMethod,           fo.Method);
      Get_String(st, kOptions,          fo.Options);
      Get_String(st, kEncryptionMethod, fo.EncryptionMethod);
      Get_String(st, kMemUse,           fo.MemUse);

      Get_UInt32(st, kLevel,      fo.Level);
      Get_UInt32(st, kDictionary, fo.Dictionary);
      Get_UInt32(st, kOrder,      fo.Order);
      Get_UInt32(st, kBlockSize,  fo.BlockLogSize);
      Get_UInt32(st, kNumThreads, fo.NumThreads);

      Get_UInt32(st, kTimePrec,   fo.TimePrec);
      Get_BoolPair(st, kMTime,       fo.MTime);
      Get_BoolPair(st, kATime,       fo.ATime);
      Get_BoolPair(st, kCTime,       fo.CTime);
      Get_BoolPair(st, kSetArcMTime, fo.SetArcMTime);

      Formats.Add(fo);
      st.endGroup();
    }
    st.endGroup();
  }

  {
    const QString key = QString::fromLatin1(kArchiver);
    if (st.contains(key))
      ArcType = QStr_to_UString(st.value(key).toString());
  }
  {
    const QString key = QString::fromLatin1(kLevel);
    if (st.contains(key))
    {
      bool ok = false;
      const uint v = st.value(key).toUInt(&ok);
      if (ok)
        Level = (UInt32)v;
    }
  }
  {
    const QString key = QString::fromLatin1(kShowPassword);
    if (st.contains(key))
      ShowPassword = st.value(key).toBool();
  }
  {
    const QString key = QString::fromLatin1(kEncryptHeaders);
    if (st.contains(key))
      EncryptHeaders = st.value(key).toBool();
  }

  st.endGroup();
}

} // namespace NCompressionQt
