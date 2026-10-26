#pragma once
#include <QPlainTextEdit>
class AssemblyEditor:public QPlainTextEdit {
public:
    explicit AssemblyEditor(QWidget *parent=nullptr);
    void setExecutionLine(int line);
    void paintGutter(QPaintEvent *event);
    int gutterWidth()const;
protected: void resizeEvent(QResizeEvent *)override;
private: QWidget *gutter; int executionLine=0;
};
