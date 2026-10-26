#pragma once
#include <QApplication>
#include <QFontDatabase>
inline void applyTheme(QApplication &app){app.setStyle("Fusion");QFont font=QFontDatabase::systemFont(QFontDatabase::FixedFont);font.setPointSize(11);app.setFont(font);app.setStyleSheet(R"(
QWidget { background:#0d161e; color:#d2deeb; }
QMainWindow { background:#080f16; }
QToolBar { spacing:8px; padding:10px; border-bottom:1px solid #2b4152; }
QToolButton { background:#233747; padding:9px 16px; border:1px solid #345166; border-radius:4px; }
QToolButton#buildButton,QToolButton#stepButton { background:#0757b7; }
QToolButton#startButton { background:#086b46; }
QToolButton#stopButton { background:#b9283e; }
QToolButton:hover { background:#164f88; }
QToolButton:disabled,QToolButton#buildButton:disabled,QToolButton#stepButton:disabled,QToolButton#startButton:disabled,QToolButton#stopButton:disabled { color:#596a78; background:#15222e; }
QLabel { padding:8px; }
QPlainTextEdit,QTableView { border:1px solid #2a4357; selection-background-color:#344f6c; }
QHeaderView::section { background:#192a36; color:#9cbbd6; border:0; padding:6px; text-align:left; }
QSplitter::handle { background:#080f16; width:10px; height:10px; }
QStatusBar { color:#88a3ba; border-top:1px solid #2a4357; }
QScrollBar:vertical { background:#0e1922; width:12px; }
QScrollBar::handle:vertical { background:#344b5e; min-height:24px; border-radius:4px; }
)");}
