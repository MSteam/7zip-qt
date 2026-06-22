// FormatIcons.h
// ----------------------------------------------------------------------------
// Milestone B.6 : per-format archive file icons.
//
// Maps a file extension ("7z", "zip", "tgz", ...) to the ORIGINAL 7-Zip
// per-format archive icon (CPP/7zip/Archive/Icons/<name>.ico). The mapping is
// NOT invented: it is a verbatim lift of the two tables in
//   CPP/7zip/Bundles/Format7zF/resource.rc
// that the original 7-Zip ships and that the FileManager itself consumes:
//   * lines 6-32  : icon-resource-id (0..26) -> "../../Archive/Icons/<name>.ico"
//   * line 38     : STRINGTABLE id 100, the ext:id association string the FM's
//                   CCodecIcons::LoadIcons() parses (split on space, then ':').
// Composing the two halves yields the canonical extension -> icon-basename map.
//
// On Linux the engine's runtime version of this table is intentionally EMPTY
// (NWindows::MyLoadString is stubbed in agent/AgentLinuxCompat.cpp), so we
// transcribe the resource.rc literal here. This is mirroring a real source 1:1,
// not a guess. The 27 .ico are embedded BY IN-TREE PATH via fm_resources.qrc
// (additive /fm/fmt prefix) and loaded with QIcon(":/fm/fmt/<name>.ico"),
// exactly as FmIcons loads FM.ico — no redraw, no theme substitute.
//
// Anything not in the table (or whose .ico is somehow missing) returns a null
// QIcon, and the model keeps its generic theme fallback (_fileIcon).
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_FORMAT_ICONS_H
#define ZIP7_INC_QT_FM_FORMAT_ICONS_H

#include <QtGui/QIcon>

class QString;

namespace FormatIcons
{
  // `ext` is a bare extension WITHOUT the dot ("7z", "zip", "tgz"). Case is
  // normalized to lower-case internally. Returns the stock per-format archive
  // icon if `ext` maps to one of the 27 icons, else a null QIcon (caller falls
  // back to the generic file icon). Cached; safe to call per cell.
  QIcon iconForExtension(const QString &ext);
}

#endif
