// QtExtractSettings.cpp
//
// QSettings-backed reimplementation of ZipRegistry.cpp's NExtract::CInfo
// Save()/Load(). The logic below tracks the original line-for-line; only the
// backing store (CKey over the Win32 registry) is swapped for QSettings.
//
// Mapping of the upstream value names (ZipRegistry.cpp, namespace NExtract):
//   kKeyName       "Extraction"   -> QSettings group "Extraction"
//   kExtractMode   "ExtractMode"  -> only written when PathMode_Force
//   kOverwriteMode "OverwriteMode"-> only written when OverwriteMode_Force
//   kSplitDest     "SplitDest"    -> CBoolPair, written iff Def
//   kElimDup       "ElimDup"      -> CBoolPair, written iff Def
//   kNtSecur       "Security"     -> CBoolPair (NtSecurity), written iff Def
//   kShowPassword  "ShowPassword" -> CBoolPair, written iff Def
//   kPathHistory   "PathHistory"  -> output-path MRU (StringList)
//
// QSettings location: org "7-Zip", app "7-Zip". On Linux with the default
// IniFormat scope this resolves to $XDG_CONFIG_HOME/7-Zip/7-Zip.conf (honored by
// the verification harness via a temp XDG_CONFIG_HOME).

#include "QtExtractSettings.h"

#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>

namespace {

// QSettings identity. Mirrors HKCU\Software\7-Zip (org/app) + the "Extraction"
// subkey (kKeyName). Forced to IniFormat so the store is a single inspectable
// file under XDG_CONFIG_HOME, matching the upstream "one registry hive" model
// and what the verification step inspects.
const char * const kOrg   = "7-Zip";
const char * const kApp   = "7-Zip";
const char * const kGroup = "Extraction";

// Value names — kept byte-identical to ZipRegistry.cpp's NExtract constants.
const char * const kExtractMode   = "ExtractMode";
const char * const kOverwriteMode = "OverwriteMode";
const char * const kShowPassword  = "ShowPassword";
const char * const kPathHistory   = "PathHistory";
const char * const kSplitDest     = "SplitDest";
const char * const kElimDup       = "ElimDup";
const char * const kNtSecur       = "Security";
// G.9a : the extract memory-usage GB limit (ZipRegistry.cpp kMemLimit="MemLimit").
const char * const kMemLimit      = "MemLimit";

QSettings MakeSettings()
{
  return QSettings(QSettings::IniFormat, QSettings::UserScope,
      QString::fromLatin1(kOrg), QString::fromLatin1(kApp));
}

static UString QStr_to_UString(const QString &s)
{
  // QString is UTF-16; UString is wchar_t. Build element-by-element to stay
  // correct on platforms where wchar_t is 32-bit (Linux), like the rest of the
  // tree's QString<->UString bridging.
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

// CBoolPair writer: mirror Key_Set_BoolPair() — write only if Def (otherwise the
// value is left absent, exactly like the registry path which never writes it).
static void Set_BoolPair(QSettings &st, const char *name, const CBoolPair &b)
{
  if (b.Def)
    st.setValue(QString::fromLatin1(name), b.Val);
}

// CBoolPair reader, Val defaults to false: mirror Key_Get_BoolPair().
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

// CBoolPair reader, Val defaults to true: mirror Key_Get_BoolPair_true().
static void Get_BoolPair_true(QSettings &st, const char *name, CBoolPair &b)
{
  const QString key = QString::fromLatin1(name);
  if (st.contains(key))
  {
    b.Val = st.value(key).toBool();
    b.Def = true;
  }
  else
  {
    b.Val = true;
    b.Def = false;
  }
}

} // namespace


namespace NExtractQt {

// Mirror of NExtract::CInfo::Save() (ZipRegistry.cpp lines ~103-122).
void CInfo::Save() const
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kGroup));

  // PathMode / OverwriteMode are only persisted when "forced" (the user picked a
  // non-default value), exactly like the original. When not forced we remove any
  // stale value so a later Load() falls back to the default — this matches the
  // registry behavior where the value simply would not have been (re)written and
  // RecurseDeleteKey is not used for these scalars (we use remove() to keep the
  // "not forced => default on next load" invariant under a persistent ini file).
  if (PathMode_Force)
    st.setValue(QString::fromLatin1(kExtractMode), (uint)(UInt32)PathMode);
  else
    st.remove(QString::fromLatin1(kExtractMode));

  if (OverwriteMode_Force)
    st.setValue(QString::fromLatin1(kOverwriteMode), (uint)(UInt32)OverwriteMode);
  else
    st.remove(QString::fromLatin1(kOverwriteMode));

  Set_BoolPair(st, kSplitDest, SplitDest);
  Set_BoolPair(st, kElimDup, ElimDup);
  Set_BoolPair(st, kNtSecur, NtSecurity);
  Set_BoolPair(st, kShowPassword, ShowPassword);

  // kPathHistory: upstream does RecurseDeleteKey + SetValue_Strings — i.e. a full
  // replace of the MRU list. QStringList round-trips through QSettings as a
  // multi-value, which is the natural equivalent.
  {
    QStringList list;
    for (unsigned i = 0; i < Paths.Size(); i++)
      list << UString_to_QStr(Paths[i]);
    st.setValue(QString::fromLatin1(kPathHistory), list);
  }

  st.endGroup();
  st.sync();
}

// Mirror of NExtract::CInfo::Load() (ZipRegistry.cpp lines ~140-175).
void CInfo::Load()
{
  // Defaults — identical to the original Load() prologue.
  PathMode = NExtract::NPathMode::kCurPaths;
  PathMode_Force = false;
  OverwriteMode = NExtract::NOverwriteMode::kAsk;
  OverwriteMode_Force = false;

  SplitDest.Val = true;
  SplitDest.Def = false;

  ElimDup.Init();
  NtSecurity.Init();
  ShowPassword.Init();

  Paths.Clear();

  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kGroup));

  // PathHistory MRU.
  {
    const QString key = QString::fromLatin1(kPathHistory);
    if (st.contains(key))
    {
      const QStringList list = st.value(key).toStringList();
      for (const QString &s : list)
        Paths.Add(QStr_to_UString(s));
    }
  }

  // ExtractMode (PathMode), bounded by kAbsPaths like the original.
  {
    const QString key = QString::fromLatin1(kExtractMode);
    bool ok = false;
    const uint v = st.value(key, (uint)0).toUInt(&ok);
    if (ok && st.contains(key) && v <= (uint)NExtract::NPathMode::kAbsPaths)
    {
      PathMode = (NExtract::NPathMode::EEnum)v;
      PathMode_Force = true;
    }
  }

  // OverwriteMode, bounded by kRenameExisting like the original.
  {
    const QString key = QString::fromLatin1(kOverwriteMode);
    bool ok = false;
    const uint v = st.value(key, (uint)0).toUInt(&ok);
    if (ok && st.contains(key) && v <= (uint)NExtract::NOverwriteMode::kRenameExisting)
    {
      OverwriteMode = (NExtract::NOverwriteMode::EEnum)v;
      OverwriteMode_Force = true;
    }
  }

  Get_BoolPair_true(st, kSplitDest, SplitDest);
  Get_BoolPair(st, kElimDup, ElimDup);
  Get_BoolPair(st, kNtSecur, NtSecurity);
  Get_BoolPair(st, kShowPassword, ShowPassword);

  st.endGroup();
}

// G.9a : mirror NExtract::Save_LimitGB (ZipRegistry.cpp:132-138). Persist the GB
// limit under [Extraction] "MemLimit". A value of (UInt32)-1 ("no configured
// limit", the CSettingsPage::OnApply "checkbox unticked" case) is written too, so
// a later Read_LimitGB sees the same not-forced state the original registry path
// would (Key_Set_UInt32 stores whatever it is handed).
void Save_LimitGB(UInt32 limit_GB)
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kGroup));
  st.setValue(QString::fromLatin1(kMemLimit), (uint)limit_GB);
  st.endGroup();
  st.sync();
}

// G.9a : mirror NExtract::Read_LimitGB (ZipRegistry.cpp:188-196). Default
// (UInt32)-1 ("not found") when no value is stored, exactly like the original
// (which leaves v = (UInt32)(Int32)-1 if GetValue_UInt32_IfOk doesn't fire).
UInt32 Read_LimitGB()
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kGroup));
  UInt32 v = (UInt32)(Int32)-1;
  const QString key = QString::fromLatin1(kMemLimit);
  if (st.contains(key))
  {
    bool ok = false;
    const uint stored = st.value(key).toUInt(&ok);
    if (ok)
      v = (UInt32)stored;
  }
  st.endGroup();
  return v;
}

} // namespace NExtractQt
