// QtHashMethodsDialog.h
//
// Small algorithm-selection dialog for the Qt hash app. There is no single
// original "choose methods" dialog (the FileManager hash command takes its
// method from the chosen context-menu entry); this dialog offers the SAME set
// the original Explorer context menu offers (CPP/7zip/UI/Explorer/ContextMenu.cpp
// g_HashCommands): CRC32, CRC64, XXH64, MD5, SHA1, SHA256, SHA384, SHA512,
// SHA3-256, BLAKE2sp, and "*" (All).
//
// The checked method names populate CHashOptions.Methods (UStringVector). The
// default checked set is CRC32 — the engine's k_DefaultHashMethod (HashCalc.cpp).

#ifndef ZIP7_INC_QT_HASH_METHODS_DIALOG_H
#define ZIP7_INC_QT_HASH_METHODS_DIALOG_H

#include <QtWidgets/QDialog>

#include "../../../Common/MyString.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
QT_END_NAMESPACE

#include <vector>

class QtHashMethodsDialog : public QDialog
{
  Q_OBJECT
public:
  explicit QtHashMethodsDialog(QWidget *parent = nullptr);

  // The checked method names, in the canonical g_HashCommands order, as the
  // engine expects them (CHashOptions.Methods entries).
  UStringVector SelectedMethods() const;

  // Preselect a method by its engine name (case-insensitive). Returns true if
  // it matched one of the offered entries.
  bool SetChecked(const char *methodName, bool checked);

private:
  std::vector<QCheckBox *> _checks;
};

#endif
