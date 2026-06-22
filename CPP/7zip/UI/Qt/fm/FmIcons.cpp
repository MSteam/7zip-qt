// FmIcons.cpp
// ----------------------------------------------------------------------------
// See FmIcons.h.
// ----------------------------------------------------------------------------

#include "FmIcons.h"

#include <QtCore/QString>
#include <QtGui/QPixmap>

namespace FmIcons
{

QImage applyMagentaMask(const QImage &src)
{
  if (src.isNull())
    return src;

  // Mirror imageList.AddMasked(bmp, RGB(255,0,255)): the key colour becomes the
  // transparent (masked-out) pixels. We need an alpha channel for that, so
  // convert to ARGB32 first (the bmps load as RGB32 / indexed).
  QImage img = src.convertToFormat(QImage::Format_ARGB32);

  const QRgb key = qRgb(kMaskR, kMaskG, kMaskB);          // 0xFFFF00FF
  const QRgb transparent = qRgba(0, 0, 0, 0);

  const int h = img.height();
  const int w = img.width();
  for (int y = 0; y < h; ++y)
  {
    QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
    for (int x = 0; x < w; ++x)
    {
      // Compare on the opaque RGB (ignore any incoming alpha); the bmps are
      // opaque so the key pixels are exactly qRgb(255,0,255).
      if ((line[x] | 0xFF000000u) == (key | 0xFF000000u))
        line[x] = transparent;
    }
  }
  return img;
}

// HiDPI LIMITATION (P.3, documented — intentionally NOT fixed): these toolbar
// assets are the ORIGINAL Win32 7-Zip bitmaps at FIXED pixel sizes — X.bmp large
// = 48x36, X2.bmp small = 24x24 (the App.cpp large?Bitmap:Bitmap2 pairing). They
// are raster, magenta-masked, and carry no @2x/devicePixelRatio variants, so on a
// HiDPI (scale>1) display Qt upscales them and they look soft. Per the port
// decision we KEEP the originals (1:1 fidelity). The scalable-asset option, if
// crisp HiDPI is later wanted: ship the per-button source as SVG (or supply @2x
// PNG variants + QIcon::addFile with QImage::setDevicePixelRatio) and load via
// QIcon::fromTheme / a QIconEngine instead of the fixed bmp. No asset change here.
static const char *bmpAlias(Button b, bool large)
{
  // X.bmp == large (48x36), X2.bmp == small (24x24): the App.cpp
  // `large ? BitmapResID : Bitmap2ResID` pairing, as the qrc aliases.
  switch (b)
  {
    case Button::Add:     return large ? "Add.bmp"     : "Add2.bmp";
    case Button::Extract: return large ? "Extract.bmp" : "Extract2.bmp";
    case Button::Test:    return large ? "Test.bmp"    : "Test2.bmp";
    case Button::Copy:    return large ? "Copy.bmp"    : "Copy2.bmp";
    case Button::Move:    return large ? "Move.bmp"    : "Move2.bmp";
    case Button::Delete:  return large ? "Delete.bmp"  : "Delete2.bmp";
    case Button::Info:    return large ? "Info.bmp"    : "Info2.bmp";
  }
  return "Add.bmp";
}

QString bitmapResourcePath(Button b, bool large)
{
  return QStringLiteral(":/fm/") + QLatin1String(bmpAlias(b, large));
}

QIcon toolbarIcon(Button b, bool large)
{
  const QString path = bitmapResourcePath(b, large);
  QImage img(path);
  if (img.isNull())
    return QIcon(); // missing/unusable asset -> null icon (caller reports)

  const QImage masked = applyMagentaMask(img);
  QIcon icon(QPixmap::fromImage(masked));
  return icon;
}

QIcon windowIcon()
{
  // resource.rc: IDI_ICON ICON "FM.ico". Qt's .ico reader yields a
  // multi-resolution QIcon directly.
  return QIcon(QStringLiteral(":/fm/FM.ico"));
}

QIcon aboutLogo()
{
  return QIcon(QStringLiteral(":/fm/7zipLogo.ico"));
}

} // namespace FmIcons
