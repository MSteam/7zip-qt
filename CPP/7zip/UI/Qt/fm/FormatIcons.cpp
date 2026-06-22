// FormatIcons.cpp
// ----------------------------------------------------------------------------
// See FormatIcons.h.
// ----------------------------------------------------------------------------

#include "FormatIcons.h"

#include <QtCore/QHash>
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace FormatIcons
{

// --- table (A) : icon-resource-id -> .ico basename ---------------------------
// VERBATIM from CPP/7zip/Bundles/Format7zF/resource.rc lines 6-32 (the numbered
// `<id> ICON "../../Archive/Icons/<name>.ico"` statements). Indexed by id 0..26;
// the qrc aliases these basenames under ":/fm/fmt/".
static const char *const kIconBasenameById[] = {
  /*  0 */ "7z.ico",
  /*  1 */ "zip.ico",
  /*  2 */ "bz2.ico",
  /*  3 */ "rar.ico",
  /*  4 */ "arj.ico",
  /*  5 */ "z.ico",
  /*  6 */ "lzh.ico",
  /*  7 */ "cab.ico",
  /*  8 */ "iso.ico",
  /*  9 */ "split.ico",
  /* 10 */ "rpm.ico",
  /* 11 */ "deb.ico",
  /* 12 */ "cpio.ico",
  /* 13 */ "tar.ico",
  /* 14 */ "gz.ico",
  /* 15 */ "wim.ico",
  /* 16 */ "lzma.ico",
  /* 17 */ "dmg.ico",
  /* 18 */ "hfs.ico",
  /* 19 */ "xar.ico",
  /* 20 */ "vhd.ico",
  /* 21 */ "fat.ico",
  /* 22 */ "ntfs.ico",
  /* 23 */ "xz.ico",
  /* 24 */ "squashfs.ico",
  /* 25 */ "apfs.ico",
  /* 26 */ "zst.ico",
};
static const int kNumIcons =
    (int)(sizeof(kIconBasenameById) / sizeof(kIconBasenameById[0]));

// --- table (B) : the ext:id association string -------------------------------
// VERBATIM from CPP/7zip/Bundles/Format7zF/resource.rc line 38 (STRINGTABLE id
// 100). Parsed exactly as CCodecIcons::LoadIcons() does it: split on spaces,
// then split each token on ':' into (ext, iconIndex).
static const char *const kExtIdAssoc =
    "7z:0 zip:1 rar:3 001:9 cab:7 iso:8 xz:23 txz:23 lzma:16 tar:13 cpio:12 "
    "bz2:2 bzip2:2 tbz2:2 tbz:2 gz:14 gzip:14 tgz:14 tpz:14 zst:26 tzst:26 "
    "z:5 taz:5 lzh:6 lha:6 rpm:10 deb:11 arj:4 vhd:20 vhdx:20 wim:15 swm:15 "
    "esd:15 fat:21 ntfs:22 dmg:17 hfs:18 xar:19 squashfs:24 apfs:25";

// ext (lower-case) -> .ico basename, built once from (A) composed with (B).
static QHash<QString, QString> &extToBasename()
{
  static QHash<QString, QString> map;
  static bool built = false;
  if (!built)
  {
    built = true;
    // Same parse as CCodecIcons::LoadIcons: SplitString on ' ', then on ':'.
    const QStringList tokens =
        QString::fromLatin1(kExtIdAssoc).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &tok : tokens)
    {
      const int colon = tok.indexOf(QLatin1Char(':'));
      if (colon <= 0)
        continue;
      const QString ext = tok.left(colon).toLower();
      bool ok = false;
      const int id = tok.mid(colon + 1).toInt(&ok);
      if (!ok || id < 0 || id >= kNumIcons)
        continue;
      map.insert(ext, QString::fromLatin1(kIconBasenameById[id]));
    }
  }
  return map;
}

QIcon iconForExtension(const QString &ext)
{
  if (ext.isEmpty())
    return QIcon();

  const QString key = ext.toLower();
  const QHash<QString, QString> &m = extToBasename();
  const auto it = m.constFind(key);
  if (it == m.constEnd())
    return QIcon();

  // basename -> QIcon cache. Loaded from the embedded stock .ico (:/fm/fmt/...),
  // exactly as FmIcons::windowIcon() loads FM.ico — Qt's ico plugin yields a
  // multi-resolution QIcon. A truly-missing asset stays null (model falls back).
  static QHash<QString, QIcon> iconCache;
  const QString &basename = it.value();
  const auto cached = iconCache.constFind(basename);
  if (cached != iconCache.constEnd())
    return cached.value();

  QIcon icon(QStringLiteral(":/fm/fmt/") + basename);
  iconCache.insert(basename, icon);
  return icon;
}

} // namespace FormatIcons
