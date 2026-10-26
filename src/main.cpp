// Author: Luis Pinto (@xsoulp), 2026.
#include "MainWindow.h"
#include "Theme.h"
#include <QApplication>
#include <QIcon>
#include <QFontDatabase>
int main(int argc,char **argv){QApplication app(argc,argv);app.setApplicationName("ASM101");app.setApplicationVersion(ASM101_VERSION);app.setDesktopFileName("asm101");app.setWindowIcon(QIcon(":/icons/asm101.svg"));applyTheme(app);MainWindow w;w.show();return app.exec();}
