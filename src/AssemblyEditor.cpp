#include "AssemblyEditor.h"
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QPainter>
#include <QTextBlock>
class Highlighter:public QSyntaxHighlighter{
public:using QSyntaxHighlighter::QSyntaxHighlighter;
void highlightBlock(const QString &s)override{
    auto apply=[&](QString pattern,QColor color){QTextCharFormat f;f.setForeground(color);auto it=QRegularExpression(pattern,QRegularExpression::CaseInsensitiveOption).globalMatch(s);while(it.hasNext()){auto m=it.next();setFormat(m.capturedStart(),m.capturedLength(),f);}};
    apply("\\b(section|global|mov|add|sub|xor|syscall|lea|cmp|jmp|je|jne|push|pop|call|ret|inc|dec|and|or|test|db|dq|resb)\\b",QColor("#62b7ff"));
    apply("\\b(r(ax|bx|cx|dx|si|di|bp|sp|ip)|r[89]|r1[0-5])\\b",QColor("#d4e6f4"));
    apply("\\b(0x[0-9a-f]+|[0-9]+)\\b",QColor("#f5bb79"));apply("[._a-zA-Z][\\w.]*:",QColor("#df94e8"));apply(";.*",QColor("#66bd88"));
}};
class Gutter:public QWidget {AssemblyEditor *e;public:Gutter(AssemblyEditor *e):QWidget(e),e(e){}void paintEvent(QPaintEvent *p)override{e->paintGutter(p);}};
AssemblyEditor::AssemblyEditor(QWidget *p):QPlainTextEdit(p){gutter=new Gutter(this);new Highlighter(document());setLineWrapMode(NoWrap);setTabStopDistance(fontMetrics().horizontalAdvance(' ')*4);connect(this,&QPlainTextEdit::blockCountChanged,this,[this]{setViewportMargins(gutterWidth(),0,0,0);});connect(this,&QPlainTextEdit::updateRequest,this,[this](QRect,int){gutter->update();});setViewportMargins(gutterWidth(),0,0,0);}
int AssemblyEditor::gutterWidth()const{return 32+fontMetrics().horizontalAdvance(QString::number(blockCount()));}
void AssemblyEditor::resizeEvent(QResizeEvent *e){QPlainTextEdit::resizeEvent(e);gutter->setGeometry(0,0,gutterWidth(),height());}
void AssemblyEditor::setExecutionLine(int line){executionLine=line;QList<QTextEdit::ExtraSelection> extra;if(line>0&&line<=blockCount()){QTextEdit::ExtraSelection s;s.cursor=QTextCursor(document()->findBlockByNumber(line-1));s.format.setBackground(QColor("#50481b"));s.format.setProperty(QTextFormat::FullWidthSelection,true);extra<<s;setTextCursor(s.cursor);ensureCursorVisible();}setExtraSelections(extra);gutter->update();}
void AssemblyEditor::paintGutter(QPaintEvent *){QPainter p(gutter);p.fillRect(gutter->rect(),QColor("#141e27"));QTextBlock b=firstVisibleBlock();while(b.isValid()){int y=qRound(blockBoundingGeometry(b).translated(contentOffset()).top());if(y>height())break;p.setPen(b.blockNumber()+1==executionLine?QColor("#ffe35c"):QColor("#708496"));p.drawText(0,y,gutterWidth()-8,fontMetrics().height(),Qt::AlignRight,QString::number(b.blockNumber()+1));if(b.blockNumber()+1==executionLine)p.drawText(1,y,20,fontMetrics().height(),Qt::AlignCenter,"➜");b=b.next();}}
