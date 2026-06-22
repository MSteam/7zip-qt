// QtFavorites.cpp
// ----------------------------------------------------------------------------
// See QtFavorites.h. The 10-slot list is stored as a QStringList under
// [Favorites] FolderShortcuts; the QStr<->UString bridges are the element-wise
// 32-bit-wchar_t-correct ones copied from QtExtractSettings.cpp (NOT
// toStdWString round-trips, per the tree's convention).
// ----------------------------------------------------------------------------

#include "QtFavorites.h"

#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>

namespace {

const char * const kOrg = "7-Zip";
const char * const kApp = "7-Zip";
const char * const kFavGroup = "Favorites";
const char * const kFolderShortcuts = "FolderShortcuts"; // mirror ViewSettings.cpp:31
const char * const kToolsGroup = "Tools";
const char * const kDiffCommand = "DiffCommand";         // mirror RegistryUtils "Diff"
const char * const kDiffDefault = "meld";

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

// Load the 10-slot list (grow/clamp to kNumSlots).
QStringList LoadSlots()
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kFavGroup));
  QStringList list = st.value(QString::fromLatin1(kFolderShortcuts)).toStringList();
  st.endGroup();
  while (list.size() < QtFavorites::kNumSlots)
    list << QString();
  while (list.size() > QtFavorites::kNumSlots)
    list.removeLast();
  return list;
}

void SaveSlots(const QStringList &list)
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kFavGroup));
  st.setValue(QString::fromLatin1(kFolderShortcuts), list);
  st.endGroup();
  st.sync();
}

} // namespace


namespace QtFavorites {

UString GetString(int index)
{
  if (index < 0 || index >= kNumSlots)
    return UString();
  const QStringList list = LoadSlots();
  return QStr_to_UString(list.at(index));
}

void SetString(int index, const UString &s)
{
  if (index < 0 || index >= kNumSlots)
    return;
  QStringList list = LoadSlots();
  list[index] = UString_to_QStr(s);
  SaveSlots(list);
}

int AddNext(const UString &path)
{
  QStringList list = LoadSlots();
  int slot = -1;
  for (int i = 0; i < kNumSlots; i++)
    if (list.at(i).isEmpty()) { slot = i; break; }
  if (slot < 0)
    slot = 0;
  list[slot] = UString_to_QStr(path);
  SaveSlots(list);
  return slot;
}

UString GetDiffCommand()
{
  QSettings st = MakeSettings();
  st.beginGroup(QString::fromLatin1(kToolsGroup));
  QString cmd = st.value(QString::fromLatin1(kDiffCommand)).toString();
  st.endGroup();
  if (cmd.trimmed().isEmpty())
    cmd = QString::fromLatin1(kDiffDefault);
  return QStr_to_UString(cmd);
}

} // namespace QtFavorites
