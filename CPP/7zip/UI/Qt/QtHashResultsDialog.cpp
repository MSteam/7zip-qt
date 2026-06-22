// QtHashResultsDialog.cpp
//
// Mirror of GUI/HashGUI.cpp's ShowHashResults(CPropNameValPairs, HWND): a
// modal two-column list of the hash result pairs.

#include "QtHashResultsDialog.h"

#include "QtLang.h"
#include "../FileManager/resourceGui.h"  // IDS_CHECKSUM_INFORMATION
#include "../FileManager/resource.h"      // IDS_COPY

#include <QtGui/QClipboard>
#include <QtGui/QFontDatabase>
#include <QtGui/QGuiApplication>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QVBoxLayout>


static QString US2Q(const UString &s)
{
  return QString::fromWCharArray(s.Ptr(), (int)s.Len());
}

QtHashResultsDialog::QtHashResultsDialog(const CPropNameValPairs &pairs, QWidget *parent)
  : QDialog(parent)
{
  // LangString(IDS_CHECKSUM_INFORMATION) in the original (HashGUI.cpp:321).
  setWindowTitle(FmLang(IDS_CHECKSUM_INFORMATION, QStringLiteral("Checksum information")));
  setModal(true);
  resize(640, 360);

  QVBoxLayout *layout = new QVBoxLayout(this);

  _table = new QTableWidget(this);
  _table->setColumnCount(2);
  // The original CListViewDialog (ListViewDialog.cpp:70/96) leaves the two
  // column headers blank (pszText = NULL) — no IDS_ exists for "Property"/"Value",
  // so these stay plain literals (no langID to invent).
  _table->setHorizontalHeaderLabels(QStringList()
      << QStringLiteral("Property") << QStringLiteral("Value"));
  _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  _table->setSelectionBehavior(QAbstractItemView::SelectRows);
  _table->verticalHeader()->setVisible(false);
  _table->horizontalHeader()->setStretchLastSection(true);

  // Digest values are fixed-width hex; show the Value column monospaced so the
  // columns line up like the console output does.
  const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

  _table->setRowCount((int)pairs.Size());
  for (unsigned i = 0; i < pairs.Size(); i++)
  {
    const CProperty &p = pairs[i];
    QTableWidgetItem *nameItem = new QTableWidgetItem(US2Q(p.Name));
    QTableWidgetItem *valItem  = new QTableWidgetItem(US2Q(p.Value));
    valItem->setFont(mono);
    _table->setItem((int)i, 0, nameItem);
    _table->setItem((int)i, 1, valItem);
  }
  _table->resizeColumnToContents(0);

  layout->addWidget(_table);

  QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
  // OK uses langID 401 (kLangPairs, LangUtils.cpp); Copy reuses the FM's
  // IDS_COPY = 6000 (resource.rc:204 "Copy"). English from the inline literals.
  if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok))
    ok->setText(FmLang(401, QStringLiteral("OK")));
  QPushButton *copyButton = buttons->addButton(
      FmLang(IDS_COPY, QStringLiteral("Copy")), QDialogButtonBox::ActionRole);
  connect(copyButton, &QPushButton::clicked, this, &QtHashResultsDialog::onCopy);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  layout->addWidget(buttons);
}

// onCopy : build the same "<Name>: <Value>\n" listing AddHashBundleRes(UString&)
// produces and put it on the clipboard.
void QtHashResultsDialog::onCopy()
{
  QString text;
  const int rows = _table->rowCount();
  for (int i = 0; i < rows; i++)
  {
    const QTableWidgetItem *name = _table->item(i, 0);
    const QTableWidgetItem *val  = _table->item(i, 1);
    text += name ? name->text() : QString();
    text += QStringLiteral(": ");
    text += val ? val->text() : QString();
    text += QLatin1Char('\n');
  }
  QGuiApplication::clipboard()->setText(text);
}
