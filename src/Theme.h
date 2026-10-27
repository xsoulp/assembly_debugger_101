#pragma once
#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QVariant>

inline bool lightTheme() {
    const auto app = QCoreApplication::instance();
    return app && app->property("lightTheme").toBool();
}
inline QColor themeColor(const char *dark, const char *light) {
    return QColor(lightTheme() ? light : dark);
}

inline void applyTextSize(QApplication &app, bool doubled) {
    app.setProperty("doubleText", doubled);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(doubled ? 22 : 11);
    app.setFont(font);
    // Refresh fonts cached by Qt's application stylesheet.
    if (!app.styleSheet().isEmpty()) app.setStyleSheet(app.styleSheet());
}

inline void applyTheme(QApplication &app, bool light = false) {
    app.setProperty("lightTheme", light);
    app.setStyle("Fusion");
    applyTextSize(app, app.property("doubleText").toBool());
    QPalette palette;
    palette.setColor(QPalette::Window, themeColor("#0d161e", "#ffffff"));
    palette.setColor(QPalette::Base, themeColor("#0d161e", "#ffffff"));
    palette.setColor(QPalette::AlternateBase, themeColor("#192a36", "#f0f3f7"));
    palette.setColor(QPalette::WindowText, themeColor("#d2deeb", "#17212b"));
    palette.setColor(QPalette::Text, themeColor("#d2deeb", "#17212b"));
    palette.setColor(QPalette::Button, themeColor("#233747", "#e8edf3"));
    palette.setColor(QPalette::ButtonText, themeColor("#d2deeb", "#17212b"));
    palette.setColor(QPalette::Highlight, themeColor("#344f6c", "#c7ddfa"));
    palette.setColor(QPalette::HighlightedText, themeColor("#ffffff", "#10243b"));
    palette.setColor(QPalette::ToolTipBase, themeColor("#192a36", "#ffffff"));
    palette.setColor(QPalette::ToolTipText, themeColor("#d2deeb", "#17212b"));
    palette.setColor(QPalette::Disabled, QPalette::Text, themeColor("#708496", "#687480"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, themeColor("#708496", "#687480"));
    app.setPalette(palette);
    const QString common = R"(
QToolBar,QToolBar QWidget,QStatusBar,QStatusBar QWidget { font-size:11pt; }
QToolBar { spacing:8px; padding:10px; }
QToolButton { padding:9px 16px; border-radius:4px; }
QToolButton#buildButton,QToolButton#stepButton { background:#0757b7; color:#ffffff; }
QToolButton#startButton { background:#086b46; color:#ffffff; }
QToolButton#stopButton { background:#b9283e; color:#ffffff; }
QLabel { padding:8px; }
QHeaderView::section { border:0; padding:6px; text-align:left; }
QSplitter::handle { width:10px; height:10px; }
QScrollBar:vertical { width:12px; }
QScrollBar::handle:vertical { min-height:24px; border-radius:4px; }
)";
    app.setStyleSheet(common + (light ? QString(R"(
QWidget { background:#ffffff; color:#17212b; }
QMainWindow { background:#e3e9ef; }
QToolBar { border-bottom:1px solid #a3b1c0; }
QToolButton { background:#e8edf3; border:1px solid #8798aa; }
QToolButton:hover { background:#cfdfef; color:#10243b; }
QToolButton#themeToggle:checked,QToolButton#zoomToggle:checked { background:#c7ddfa; border:1px solid #0757b7; color:#10243b; }
QToolButton:disabled,QToolButton#buildButton:disabled,QToolButton#stepButton:disabled,QToolButton#startButton:disabled,QToolButton#stopButton:disabled { color:#687480; background:#e9edf1; }
QPlainTextEdit,QTableView { border:1px solid #a3b1c0; selection-background-color:#c7ddfa; selection-color:#10243b; }
QHeaderView::section { background:#e8edf3; color:#243c53; }
QSplitter::handle { background:#e3e9ef; }
QStatusBar { color:#344a60; border-top:1px solid #a3b1c0; }
QScrollBar:vertical { background:#edf1f5; }
QScrollBar::handle:vertical { background:#91a1b1; }
QToolTip { background:#ffffff; color:#17212b; border:1px solid #8798aa; }
)") : QString(R"(
QWidget { background:#0d161e; color:#d2deeb; }
QMainWindow { background:#080f16; }
QToolBar { border-bottom:1px solid #2b4152; }
QToolButton { background:#233747; border:1px solid #345166; }
QToolButton:hover { background:#164f88; }
QToolButton#zoomToggle:checked { background:#344f6c; border:1px solid #72aef5; }
QToolButton:disabled,QToolButton#buildButton:disabled,QToolButton#stepButton:disabled,QToolButton#startButton:disabled,QToolButton#stopButton:disabled { color:#596a78; background:#15222e; }
QPlainTextEdit,QTableView { border:1px solid #2a4357; selection-background-color:#344f6c; }
QHeaderView::section { background:#192a36; color:#9cbbd6; }
QSplitter::handle { background:#080f16; }
QStatusBar { color:#88a3ba; border-top:1px solid #2a4357; }
QScrollBar:vertical { background:#0e1922; }
QScrollBar::handle:vertical { background:#344b5e; }
)")));
}
