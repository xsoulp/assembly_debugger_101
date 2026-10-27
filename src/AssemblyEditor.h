#pragma once
#include <QPlainTextEdit>
class QSyntaxHighlighter;
class AssemblyEditor:public QPlainTextEdit {
public:
    explicit AssemblyEditor(QWidget *parent=nullptr);
    void setExecutionLine(int line);
    void paintGutter(QPaintEvent *event);
    int gutterWidth()const;
    void refreshTheme();
    void refreshFontLayout();
protected: void resizeEvent(QResizeEvent *)override;
private: QWidget *gutter; QSyntaxHighlighter *highlighter; int executionLine=0;
};
