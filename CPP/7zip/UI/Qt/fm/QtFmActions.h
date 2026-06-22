// QtFmActions.h
// ----------------------------------------------------------------------------
// Milestone B.3 : the bridge from the file-manager shell's toolbar/menu actions
// to the existing Milestone-C operation flows (QtExtractGUI / QtUpdateGUI /
// QtHashGUI).
//
// Rather than re-implement the option wiring each flow needs, this mirrors
// main_7zqt.cpp's Run7zqt() exactly: it builds a 7-Zip COMMAND VECTOR (the same
// argv the real `7zz`/`7zG` would receive), runs it through the REAL command-line
// parser (CArcCmdLineParser / CArcCmdLineOptions), and dispatches by command
// group to the same Qt flow. So the FM's Add/Extract/Test/CRC buttons produce the
// identical engine behaviour as the unified 7zqt binary — the CPanel operations
// (App.cpp Extract / CompressFiles / Test / CalcCRC) translated to the Linux/Qt
// command path.
//
// The shell composes the command from the focused panel's selection:
//   Add     -> { "a", <archiveName>, <file>... }              (+ "-ad" to show dialog)
//   Extract -> { "x", "-o<destDir>", <archive> }              (+ "-ad" to show dialog)
//   Test    -> { "t", <archive> }
//   CRC     -> { "h", "-scrc<METHOD>", <file>... }
//
// RunFmCommand() returns the flow's exit code (0 == success); it is meant to be
// called on the GUI thread (the flows show their own modal progress dialog).
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_ACTIONS_H
#define ZIP7_INC_QT_FM_ACTIONS_H

#include <QtCore/QtGlobal>

#include "../../../../Common/MyString.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

// Run a fully-formed 7-Zip command (argv[1..], e.g. {"a", arc, file...}) through
// the real parser + the Qt operation flows. `parent` is the dialog parent.
// `headless` suppresses the interactive dialogs (offscreen/scripted path).
// Returns the flow exit code (0 == success).
int RunFmCommand(const UStringVector &commandStrings, bool headless, QWidget *parent);

#endif
