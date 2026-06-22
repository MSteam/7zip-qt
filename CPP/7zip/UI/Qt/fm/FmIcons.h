// FmIcons.h
// ----------------------------------------------------------------------------
// Milestone B.3 : load the ORIGINAL 7-Zip File Manager image assets (embedded
// via fm_resources.qrc from their in-tree CPP/7zip/UI/FileManager/ paths) and
// turn them into Qt objects, reproducing EXACTLY the magenta-mask transparency
// the original App.cpp applies.
//
// The original (App.cpp AddButton):
//     HBITMAP b = LoadBitmap(... X.bmp / X2.bmp ...);
//     imageList.AddMasked(b, RGB(255, 0, 255));   // magenta -> transparent
//
// We mirror this verbatim: load the .bmp into a QImage, convert to ARGB32, set
// every qRgb(255,0,255) pixel fully transparent, wrap as a QPixmap/QIcon. NO
// theme icons, NO redrawing — the stock bitmap pixels are used as-is except the
// magenta key colour is made transparent.
//
// "Toolbar buttons" enum mirrors g_ArchiveButtons + g_StandardButtons (App.cpp).
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_ICONS_H
#define ZIP7_INC_QT_FM_ICONS_H

#include <QtGui/QIcon>
#include <QtGui/QImage>

class QString;

namespace FmIcons
{
  // The magenta transparency key — RGB(255,0,255) — exactly the value passed to
  // imageList.AddMasked in App.cpp::AddButton.
  static const int kMaskR = 255;
  static const int kMaskG = 0;
  static const int kMaskB = 255;

  // Which toolbar button (== a g_ArchiveButtons / g_StandardButtons entry).
  enum class Button
  {
    Add,      // IDB_ADD     / IDB_ADD2
    Extract,  // IDB_EXTRACT / IDB_EXTRACT2
    Test,     // IDB_TEST    / IDB_TEST2
    Copy,     // IDB_COPY    / IDB_COPY2
    Move,     // IDB_MOVE    / IDB_MOVE2
    Delete,   // IDB_DELETE  / IDB_DELETE2
    Info      // IDB_INFO    / IDB_INFO2  (Properties)
  };

  // Apply the magenta -> transparent mask to a freshly loaded QImage, mirroring
  // imageList.AddMasked(bmp, RGB(255,0,255)). Returns an ARGB32 image with the
  // key colour made fully transparent. Exposed for the headless self-check.
  QImage applyMagentaMask(const QImage &src);

  // Load one toolbar bitmap from the qrc (":/fm/Add.bmp" etc.), apply the
  // magenta mask, and return it as a QIcon. `large` selects X.bmp (48x36) vs
  // X2.bmp (24x24), exactly like App.cpp's `large ? BitmapResID : Bitmap2ResID`.
  QIcon toolbarIcon(Button b, bool large);

  // The qrc resource path (":/fm/<name>") for a button's bitmap, for the
  // self-check that wants to load the raw bmp and verify the mask.
  QString bitmapResourcePath(Button b, bool large);

  // The window / application icon (resource.rc IDI_ICON -> FM.ico), loaded from
  // the qrc (":/fm/FM.ico"). Multi-resolution .ico -> QIcon directly.
  QIcon windowIcon();

  // The About-dialog logo (7zipLogo.ico), loaded from the qrc.
  QIcon aboutLogo();
}

#endif
