// QtFmSettings.cpp
// ----------------------------------------------------------------------------
// See QtFmSettings.h. QSettings identity / bridges mirror QtFavorites.cpp +
// QtExtractSettings.cpp verbatim (org/app "7-Zip"/"7-Zip", IniFormat, UserScope,
// element-wise 32-bit-wchar-correct QString<->UString conversion).
// ----------------------------------------------------------------------------

#include "QtFmSettings.h"
#include "../QtLang.h"   // P.2 : QtLang_LoadFile

#include "../../../../Common/StringConvert.h"   // GetUnicodeString ($VISUAL/$EDITOR)

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>   // P.2 : applicationDirPath (Lang dir)
#include <QtCore/QFileInfo>          // G.1j : exists() probe for locale candidates
#include <QtCore/QLocale>            // G.1j : OS-locale auto-detect
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QStringList>        // G.1j : uiLanguages()
#include <QtCore/QVariant>
#include <cstdlib>   // getenv

namespace {

const char * const kOrg = "7-Zip";
const char * const kApp = "7-Zip";

// Group [Options] holds the CFmSettings bool subset + WorkDir + DeleteToTrash.
const char * const kOptGroup = "Options";
// Group [FM] mirrors HKCU\Software\7-Zip\FM for the Editor command.
const char * const kFmGroup = "FM";
// Group [Tools] is OWNED by QtFavorites; we touch only its DiffCommand key.
const char * const kToolsGroup = "Tools";

// Value names — byte-mirror the upstream CFmSettings names (RegistryUtils.cpp:
// 27-35) where they exist.
const char * const kShowDots = "ShowDots";
const char * const kShowRealFileIcons = "ShowRealFileIcons";
const char * const kFullRow = "FullRow";
const char * const kShowGrid = "ShowGrid";
const char * const kSingleClick = "SingleClick";
const char * const kAlternativeSelection = "AlternativeSelection";
const char * const kAlternatingColors = "AlternatingColors";
const char * const kWorkDirUseSystemTemp = "WorkDirUseSystemTemp";
const char * const kWorkDirMode = "WorkDirMode";   // G.9c : NWorkDir::NMode 0/1/2
const char * const kWorkDirPath = "WorkDirPath";
const char * const kDeleteToTrash = "DeleteToTrash";
const char * const kEditor = "Editor";        // [FM] Editor (orig "Editor")
const char * const kDiffCommand = "DiffCommand"; // [Tools] (orig FM\Diff)
const char * const kDiffDefault = "meld";
// P.2 : [Options] Lang — mirrors HKCU\Software\7-Zip\Lang (RegistryUtils.cpp:20
// kLangValueName=L"Lang"). Empty = English; otherwise a Lang/*.txt basename.
const char * const kLang = "Lang";

// G.4a : view-settings persistence groups/keys (ViewSettings.cpp analogues).
//   [ListView] holds one sub-key per list-type (a child group named after the type)
//   carrying that type's header "State" blob + "Cols" visibility list — the
//   CListViewInfo-per-type serialization (kCulumnsKeyName HKCU\...\FM\Columns).
//   [Panels] holds the per-panel "ListMode<i>" (CListMode kListMode) + "Path<i>"
//   (SavePanelPath kPanelPathValueName "PanelPath"). We use "Path<i>"/"ListMode<i>"
//   under one [Panels] group, the Qt-idiomatic shape of the same per-panel values.
const char * const kListViewGroup = "ListView";
const char * const kStateKey = "State";       // QHeaderView::saveState() blob
const char * const kColsKey = "Cols";         // "<propid>:<0|1>;..." visibility set
const char * const kPanelsGroup = "Panels";
const char * const kListModePrefix = "ListMode"; // + panel index
const char * const kPanelPathPrefix = "Path";    // + panel index (SavePanelPath)
const char * const kNumPanelsKey = "NumPanels";  // G.4g : AppState NumPanels (1/2)

// G.4d : [FM] "FolderHistory" — byte-mirrors kFolderHistoryValueName under
// REG_PATH_FM = HKCU\Software\7-Zip\FM (ViewSettings.cpp:30). A QStringList of the
// recently-visited folders, most-recent-first (CFolderHistory).
const char * const kFolderHistory = "FolderHistory";

QSettings MakeSettings()
{
  return QSettings(QSettings::IniFormat, QSettings::UserScope,
      QString::fromLatin1(kOrg), QString::fromLatin1(kApp));
}

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

bool GetBool(QSettings &st, const char *name, bool def)
{
  return st.value(QString::fromLatin1(name), def).toBool();
}

} // namespace


namespace QtFmSettings {

void CInfo::Load()
{
  QSettings st = MakeSettings();

  st.beginGroup(QString::fromLatin1(kOptGroup));
  // CFmSettings bools — defaults all false (RegistryUtils.cpp:136-149).
  ShowDots             = GetBool(st, kShowDots, false);
  // G.9d : default TRUE (the port has always shown per-format icons; see CInfo).
  ShowRealFileIcons    = GetBool(st, kShowRealFileIcons, true);
  FullRow              = GetBool(st, kFullRow, false);
  ShowGrid             = GetBool(st, kShowGrid, false);
  SingleClick          = GetBool(st, kSingleClick, false);
  AlternativeSelection = GetBool(st, kAlternativeSelection, false);
  // Qt-side view tweak (default on, matching QtPanel.cpp:216).
  AlternatingColors    = GetBool(st, kAlternatingColors, true);
  // WorkDir overlay — default kSystem (use-system-temp), empty path.
  WorkDirUseSystemTemp = GetBool(st, kWorkDirUseSystemTemp, true);
  // G.9c : the three-way mode (kSystem=0/kCurrent=1/kSpecified=2). When no
  // WorkDirMode value is stored (an older session wrote only the bool), MIGRATE
  // from WorkDirUseSystemTemp: true -> kSystem(0), false -> kSpecified(2). The
  // bool can never express kCurrent, so migration only yields 0 or 2 — exactly the
  // two states the pre-G.9c UI could persist. A corrupt out-of-range value is
  // clamped to kSystem (the data-safe default, FoldersPage CheckRadioButton uses
  // the same bounded index).
  {
    const int def = WorkDirUseSystemTemp ? 0 : 2;
    int m = st.value(QString::fromLatin1(kWorkDirMode), def).toInt();
    if (m < 0 || m > 2)
      m = 0;
    WorkDirMode = m;
    // Keep the bool consistent with the (possibly migrated) mode for any caller
    // still reading it (the agent overlay + the headless WorkDirUseSystemTemp hook).
    WorkDirUseSystemTemp = (WorkDirMode == 0);
  }
  WorkDirPath          = QStr_to_UString(
      st.value(QString::fromLatin1(kWorkDirPath)).toString());
  // DeleteToTrash — data-safe default true.
  DeleteToTrash        = GetBool(st, kDeleteToTrash, true);
  // P.2 Lang — default empty (English).
  LangName             = QStr_to_UString(
      st.value(QString::fromLatin1(kLang)).toString());
  st.endGroup();

  st.beginGroup(QString::fromLatin1(kFmGroup));
  EditorPath = QStr_to_UString(
      st.value(QString::fromLatin1(kEditor)).toString());
  st.endGroup();

  // Diff lives in [Tools] (owned by QtFavorites); empty here means "default".
  st.beginGroup(QString::fromLatin1(kToolsGroup));
  DiffCommand = QStr_to_UString(
      st.value(QString::fromLatin1(kDiffCommand)).toString());
  st.endGroup();
}

void CInfo::Save() const
{
  QSettings st = MakeSettings();

  st.beginGroup(QString::fromLatin1(kOptGroup));
  st.setValue(QString::fromLatin1(kShowDots), ShowDots);
  st.setValue(QString::fromLatin1(kShowRealFileIcons), ShowRealFileIcons);
  st.setValue(QString::fromLatin1(kFullRow), FullRow);
  st.setValue(QString::fromLatin1(kShowGrid), ShowGrid);
  st.setValue(QString::fromLatin1(kSingleClick), SingleClick);
  st.setValue(QString::fromLatin1(kAlternativeSelection), AlternativeSelection);
  st.setValue(QString::fromLatin1(kAlternatingColors), AlternatingColors);
  // G.9c : persist BOTH the three-way mode and the legacy bool (kept in sync:
  // the bool == (Mode==kSystem), so an older reader still sees System-vs-not).
  st.setValue(QString::fromLatin1(kWorkDirUseSystemTemp), (WorkDirMode == 0));
  st.setValue(QString::fromLatin1(kWorkDirMode), WorkDirMode);
  st.setValue(QString::fromLatin1(kWorkDirPath), UString_to_QStr(WorkDirPath));
  st.setValue(QString::fromLatin1(kDeleteToTrash), DeleteToTrash);
  st.setValue(QString::fromLatin1(kLang), UString_to_QStr(LangName));
  st.endGroup();

  st.beginGroup(QString::fromLatin1(kFmGroup));
  st.setValue(QString::fromLatin1(kEditor), UString_to_QStr(EditorPath));
  st.endGroup();

  // Write Diff back into the SAME [Tools] DiffCommand key QtFavorites reads, so
  // doDiff() stays consistent with the Options Editor tab.
  st.beginGroup(QString::fromLatin1(kToolsGroup));
  st.setValue(QString::fromLatin1(kDiffCommand), UString_to_QStr(DiffCommand));
  st.endGroup();

  st.sync();
}

// ---------------------------------------------------------------------------
// G.4a : per-list-type view-settings persistence.
// ---------------------------------------------------------------------------

namespace {

// A type id ("7-Zip.zip", "FSFolder", ...) becomes a QSettings group name. QSettings
// (and the IniFormat) treat '/' and '\' as group separators and a leading-'.'
// awkwardly, so reduce the id to a safe single token: keep [A-Za-z0-9._-], map any
// other byte to '_'. The mapping is total and stable (same id -> same key), which is
// all persistence needs; collisions across the small fixed set of 7-Zip type ids do
// not occur in practice (they differ in the kept characters).
QString SanitizeTypeKey(const UString &typeKey)
{
  const QString in = UString_to_QStr(typeKey);
  QString out;
  out.reserve(in.size());
  for (const QChar ch : in)
  {
    const ushort u = ch.unicode();
    const bool ok = (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z')
                 || (u >= 'a' && u <= 'z') || u == '.' || u == '_' || u == '-';
    out.append(ok ? ch : QLatin1Char('_'));
  }
  if (out.isEmpty())
    out = QStringLiteral("fs");
  return out;
}

} // namespace

UString ListTypeKey(const UString &folderTypeId)
{
  // Empty or a plain filesystem folder => the shared "fs" key (CPanel allows an
  // empty _typeIDString; we give it a stable name so the FS layout persists).
  if (folderTypeId.IsEmpty()
      || folderTypeId.IsEqualTo("FSFolder")
      || folderTypeId.IsEqualTo("AltStreamsFolder")
      || folderTypeId.IsEqualTo("FSDrives"))
    return UString(L"fs");
  return folderTypeId;
}

void SaveListViewState(const UString &typeKey, const QByteArray &state)
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kListViewGroup));
  st.beginGroup(SanitizeTypeKey(typeKey));
  st.setValue(QString::fromLatin1(kStateKey), state);
  st.endGroup();
  st.endGroup();
  st.sync();
}

QByteArray ReadListViewState(const UString &typeKey)
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kListViewGroup));
  st.beginGroup(SanitizeTypeKey(typeKey));
  const QByteArray v = st.value(QString::fromLatin1(kStateKey)).toByteArray();
  st.endGroup();
  st.endGroup();
  return v;
}

void SaveColumnVisible(const UString &typeKey,
    const QVector<QPair<PROPID, bool>> &cols)
{
  // Flatten to "<propid>:<0|1>;..." (a single string value, like CListViewInfo
  // packs all columns into one binary blob under one value name).
  QString flat;
  for (const QPair<PROPID, bool> &kv : cols)
  {
    flat += QString::number((uint)kv.first);
    flat += QLatin1Char(':');
    flat += kv.second ? QLatin1Char('1') : QLatin1Char('0');
    flat += QLatin1Char(';');
  }
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kListViewGroup));
  st.beginGroup(SanitizeTypeKey(typeKey));
  st.setValue(QString::fromLatin1(kColsKey), flat);
  st.endGroup();
  st.endGroup();
  st.sync();
}

QVector<QPair<PROPID, bool>> ReadColumnVisible(const UString &typeKey)
{
  QVector<QPair<PROPID, bool>> out;
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kListViewGroup));
  st.beginGroup(SanitizeTypeKey(typeKey));
  const QString flat = st.value(QString::fromLatin1(kColsKey)).toString();
  st.endGroup();
  st.endGroup();
  if (flat.isEmpty())
    return out;
  const QStringList items = flat.split(QLatin1Char(';'), Qt::SkipEmptyParts);
  for (const QString &item : items)
  {
    const int colon = item.indexOf(QLatin1Char(':'));
    if (colon <= 0)
      continue;
    bool ok = false;
    const uint propID = item.left(colon).toUInt(&ok);
    if (!ok)
      continue;
    const bool vis = (item.mid(colon + 1) != QLatin1String("0"));
    out.push_back(qMakePair((PROPID)propID, vis));
  }
  return out;
}

void SaveListMode(int panelIndex, int mode)
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kPanelsGroup));
  st.setValue(QString::fromLatin1(kListModePrefix) + QString::number(panelIndex), mode);
  st.endGroup();
  st.sync();
}

int ReadListMode(int panelIndex, int def)
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kPanelsGroup));
  const int v = st.value(QString::fromLatin1(kListModePrefix) + QString::number(panelIndex),
      def).toInt();
  st.endGroup();
  if (v < 0 || v > 3)   // SetListViewMode clamps; reject a corrupt stored value
    return def;
  return v;
}

void SavePanelPath(int panelIndex, const UString &path)
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kPanelsGroup));
  st.setValue(QString::fromLatin1(kPanelPathPrefix) + QString::number(panelIndex),
      UString_to_QStr(path));
  st.endGroup();
  st.sync();
}

bool ReadPanelPath(int panelIndex, UString &path)
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kPanelsGroup));
  const QVariant v = st.value(QString::fromLatin1(kPanelPathPrefix) + QString::number(panelIndex));
  st.endGroup();
  if (!v.isValid())
    return false;
  const QString s = v.toString();
  if (s.isEmpty())
    return false;
  path = QStr_to_UString(s);
  return true;
}

void SaveNumPanels(int numPanels)
{
  // App.h NumPanels persisted (CApp::Save). Only 1 and 2 are meaningful
  // (kNumPanelsMax == 2); clamp anything else to the upstream default of 2.
  if (numPanels != 1 && numPanels != 2)
    numPanels = 2;
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kPanelsGroup));
  st.setValue(QString::fromLatin1(kNumPanelsKey), numPanels);
  st.endGroup();
  st.sync();
}

int ReadNumPanels(int def)
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kPanelsGroup));
  const int v = st.value(QString::fromLatin1(kNumPanelsKey), def).toInt();
  st.endGroup();
  if (v != 1 && v != 2)   // reject a corrupt stored value (CApp::Read clamps)
    return def;
  return v;
}

void SaveFolderHistory(const UStringVector &folders)
{
  // ViewSettings.cpp SaveFolderHistory -> SaveStringList(kFolderHistoryValueName).
  // QSettings serializes a QStringList natively; one value under [FM] FolderHistory.
  QStringList list;
  FOR_VECTOR (i, folders)
    list.push_back(UString_to_QStr(folders[i]));
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kFmGroup));
  st.setValue(QString::fromLatin1(kFolderHistory), list);
  st.endGroup();
  st.sync();
}

UStringVector ReadFolderHistory()
{
  UStringVector out;
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kFmGroup));
  const QStringList list = st.value(QString::fromLatin1(kFolderHistory)).toStringList();
  st.endGroup();
  for (const QString &s : list)
    out.Add(QStr_to_UString(s));
  return out;
}

bool GetDeleteToTrash()
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kOptGroup));
  const bool v = GetBool(st, kDeleteToTrash, true);
  st.endGroup();
  return v;
}

UString GetEditorCommand()
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kFmGroup));
  const QString ed = st.value(QString::fromLatin1(kEditor)).toString();
  st.endGroup();
  if (!ed.trimmed().isEmpty())
    return QStr_to_UString(ed);

  // Empty [FM] Editor -> preserve today's behaviour: $VISUAL then $EDITOR.
  if (const char *v = getenv("VISUAL"))
    if (v[0])
      return GetUnicodeString(v);
  if (const char *e = getenv("EDITOR"))
    if (e[0])
      return GetUnicodeString(e);
  return UString();   // caller falls through to xdg-open
}

// P.2 : the Lang dir — <exe-dir>/Lang/  (GetLangDirPrefix equivalent,
// LangUtils.cpp:33-36). $7ZIP_LANG_DIR overrides (for an XDG install / tests).
UString GetLangDirPrefix()
{
  if (const char *env = getenv("7ZIP_LANG_DIR"))
    if (env[0])
    {
      UString d = GetUnicodeString(env);
      if (!d.IsEmpty() && d.Back() != L'/')
        d += L'/';
      return d;
    }
  UString d = QStr_to_UString(QCoreApplication::applicationDirPath());
  if (!d.IsEmpty() && d.Back() != L'/')
    d += L'/';
  d += L"Lang/";
  return d;
}

UString ResolveLangPath(const UString &langName)
{
  // Empty or the forced-English sentinel "-" => no file (English).
  if (langName.IsEmpty() || langName == L"-")
    return UString();

  // An absolute path or any name containing '/' is taken verbatim (test hook).
  if (langName[0] == L'/' || langName.Find(L'/') >= 0)
  {
    UString p = langName;
    if (p.ReverseFind_Dot() < 0)
      p += L".txt";
    return p;
  }

  // Otherwise it is a basename under the Lang dir; append ".txt" if extensionless
  // (mirrors ReloadLang, LangUtils.cpp:320-330).
  UString p = GetLangDirPrefix();
  p += langName;
  if (langName.ReverseFind_Dot() < 0)
    p += L".txt";
  return p;
}

// G.1j : OS-locale auto-detect — the Linux mirror of LangUtils.cpp
// OpenDefaultLang()/Lang_GetShortNames_for_DefaultLang() (~:242-306). The original
// derives short basenames from the Windows LANGID via the kLangs sub-tag table
// (LangUtils.cpp:169-181), then tries "<dir>/<sub-variant>.txt" (the country
// SUBLANG, e.g. zh-cn / pt-br / sr-spc) FIRST, then "<dir>/<primary>.txt" (the bare
// language, e.g. zh / pt / de). That sub-then-primary ORDER and the lowercase
// "<lang>-<country>" basename shape (the kLangs '-cn'/'-tw'/'-br' entries) are
// exactly what we reproduce here from QLocale. The Lang corpus basenames confirm
// the shape: zh-cn, zh-tw, pt-br, sr-spc, sr-spl alongside bare de/fr/ru/...
//
// We do NOT invent locale names: every candidate is mechanically derived from
// QLocale::system() (its uiLanguages() preference list + name()), normalized to
// the original's lowercase '-' form. Country-specific candidates precede the bare
// language, matching the original's subLang-first probe.
static void CollectDefaultLangCandidates(QStringList &out)
{
  const QLocale sys = QLocale::system();

  // uiLanguages() is the OS preference list, most-specific first, e.g.
  //   "de-DE" -> ["de-DE", "de"]            (variant then bare)
  //   "zh-CN" -> ["zh-Hans-CN","zh-CN","zh"]
  //   "pt-BR" -> ["pt-BR", "pt"]
  // We translate each tag to a Lang/*.txt basename. A tag's separator may be '-'
  // or '_'; the original joins prime+sub with '-' and lowercases — so we lowercase
  // and rewrite '_' to '-'. For a 3-part tag like "zh-Hans-CN" we also offer the
  // lang+last-subtag form ("zh-cn"), which is the kLangs sub shape the corpus uses.
  const QStringList ui = sys.uiLanguages();
  for (const QString &tagIn : ui)
  {
    QString tag = tagIn;
    tag.replace(QLatin1Char('_'), QLatin1Char('-'));
    tag = tag.toLower();
    if (tag.isEmpty())
      continue;
    const QStringList parts = tag.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
      continue;
    const QString &lang = parts.first();
    // Country-specific variant first (mirrors subLang-first), then bare language.
    if (parts.size() >= 2)
    {
      // lang + last subtag (the country), e.g. zh-Hans-CN -> "zh-cn", de-DE -> "de-de".
      const QString variant = lang + QLatin1Char('-') + parts.last();
      if (!out.contains(variant))
        out.append(variant);
      // Also the full normalized tag as-is (e.g. "zh-hans-cn"), in case a corpus
      // ever ships that exact basename. Cheap; the exists() probe gates it.
      if (!out.contains(tag) && tag != variant)
        out.append(tag);
    }
    if (!out.contains(lang))
      out.append(lang);
  }

  // QLocale::name() is the canonical "<lang>_<COUNTRY>" (e.g. "de_DE","zh_CN").
  // Add its '-'-joined lowercase variant + bare language as a final backstop, in
  // case uiLanguages() came back as just "C"/"en" but name() is more specific.
  QString nm = sys.name();           // e.g. "pt_BR"
  nm.replace(QLatin1Char('_'), QLatin1Char('-'));
  nm = nm.toLower();
  if (!nm.isEmpty())
  {
    const QStringList parts = nm.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (!parts.isEmpty())
    {
      if (parts.size() >= 2 && !out.contains(nm))
        out.append(nm);             // "pt-br"
      const QString &lang = parts.first();
      if (!out.contains(lang))
        out.append(lang);           // "pt"
    }
  }
}

// G.1j : try each OS-locale candidate's <dir>/<base>.txt in order; load the first
// that exists AND opens (CLang sig / id-0 guard). Mirrors OpenDefaultLang(): walk
// the derived names, attempt LangOpen, stop on success. Returns true if loaded.
static bool LoadDefaultLangFromLocale()
{
  QStringList cands;
  CollectDefaultLangCandidates(cands);

  const UString dirPrefix = GetLangDirPrefix();
  const QString qDir = UString_to_QStr(dirPrefix);

  for (const QString &base : cands)
  {
    if (base.isEmpty())
      continue;
    const QString full = qDir + base + QStringLiteral(".txt");
    // Probe existence first (the original only LangOpen()s a path it built; a
    // missing file just falls through to the next candidate).
    if (!QFileInfo(full).isFile())
      continue;
    if (QtLang_LoadFile(us2fs(QStr_to_UString(full))))
      return true;                  // first openable match wins (sub-then-primary)
  }
  return false;                     // none matched => English
}

bool StartupLoadLang()
{
  CInfo s;
  s.Load();

  // An explicitly-stored choice is AUTHORITATIVE (mirrors ReloadLang: a non-empty
  // g_LangID short-circuits OpenDefaultLang). The "-" sentinel = forced English.
  if (!s.LangName.IsEmpty())
  {
    if (s.LangName == L"-")
      return false;                       // forced English; no auto-detect
    const UString path = ResolveLangPath(s.LangName);
    if (path.IsEmpty())
      return false;                       // English
    return QtLang_LoadFile(us2fs(path));  // bad sig / id-0 / missing => English
  }

  // No stored Lang (the upstream empty-g_LangID case) => OS-locale auto-detect.
  return LoadDefaultLangFromLocale();
}

} // namespace QtFmSettings
