// Author: Luis Pinto (@xsoulp), 2026.
#include "MainWindow.h"
#include "Theme.h"
#include <QApplication>
#include <QFontDatabase>
int main(int argc,char **argv){QApplication app(argc,argv);app.setApplicationName("ASM101");app.setApplicationVersion(ASM101_VERSION);applyTheme(app);MainWindow w;w.show();return app.exec();}
