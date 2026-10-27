#include "MainWindow.h"
#include "Theme.h"
#include "AssemblyEditor.h"
#include "MemoryModel.h"
#include "RegisterModel.h"
#include <QApplication>
#include <QAction>
#include <QTest>
#include <QElapsedTimer>
#include <QTableView>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>
#include <QToolButton>
#include <QHeaderView>
#include <functional>
int main(int argc,char **argv){QApplication app(argc,argv);applyTheme(app);MainWindow w;w.show();QTimer watchdog;QObject::connect(&watchdog,&QTimer::timeout,[&]{for(auto widget:app.topLevelWidgets())if(auto box=qobject_cast<QMessageBox*>(widget)){qCritical()<<"Unexpected dialog:"<<box->text();std::exit(1);}});watchdog.start(100);
 auto wait=[&](std::function<bool()> predicate){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<7000)QTest::qWait(10);return predicate();};
 auto action=[&](QString name){for(auto a:w.findChildren<QAction*>())if(a->text()==name)return a;return static_cast<QAction*>(nullptr);};
 auto require=[](bool ok,const char *message){if(!ok){qCritical()<<message;std::exit(1);}};
 AssemblyEditor *editor=nullptr;for(auto e:w.findChildren<QPlainTextEdit*>())if(auto a=dynamic_cast<AssemblyEditor*>(e))editor=a;
 RegisterModel *regs=nullptr;MemoryModel *memory=nullptr;for(auto t:w.findChildren<QTableView*>()){if(auto m=dynamic_cast<RegisterModel*>(t->model()))regs=m;if(t->objectName()=="ramView")if(auto m=dynamic_cast<MemoryModel*>(t->model()))memory=m;}
 require(editor&&regs&&memory,"Missing panels");require(editor->toPlainText().contains("value dq 10"),"Missing example");
 auto themeToggle=w.findChild<QToolButton *>("themeToggle");
 require(themeToggle&&themeToggle->isVisible()&&!themeToggle->isChecked(),"Missing corner theme toggle");
 const QString source=editor->toPlainText();
 const bool modified=editor->document()->isModified();
 auto zoomToggle=w.findChild<QToolButton *>("zoomToggle");
 require(zoomToggle&&zoomToggle->isVisible()&&!zoomToggle->icon().isNull(),"Missing corner magnifier button");
 const auto normalFont=editor->font().pointSizeF();
 const int normalGutter=editor->gutterWidth();
 zoomToggle->click();QTest::qWait(30);
 require(editor->font().pointSizeF()==normalFont*2&&editor->gutterWidth()>normalGutter,"Magnifier did not double editor text and gutter");
 require(zoomToggle->isVisible()&&themeToggle->isVisible(),"Corner controls disappeared at double font size");
 zoomToggle->click();QTest::qWait(30);
 require(editor->font().pointSizeF()==normalFont,"Magnifier did not restore normal size");
 themeToggle->click();QTest::qWait(30);
 require(editor->palette().color(QPalette::Base).lightness()>230,"Editor did not switch to a white background");
 require(editor->toPlainText()==source&&editor->document()->isModified()==modified,"Theme toggle changed source text");
 themeToggle->click();QTest::qWait(30);
 require(editor->palette().color(QPalette::Base).lightness()<60,"Dark theme did not restore");
 action("Build")->trigger();require(wait([&]{return action("Start")->isEnabled();}),"Build failed");
 action("Start")->trigger();require(wait([&]{return action("Step")->isEnabled();}),"Start failed");
 require(!editor->extraSelections().isEmpty(),"Missing source highlight");require(memory->rowCount()>0,"Missing memory rows");
 quint64 data=0;for(int row=0;row<memory->rowCount();++row){quint64 a=memory->address(row);if(memory->sectionColor(a)==QColor("#123c32")){data=a;break;}}
 require(data!=0,"Missing .data");require(wait([&]{return memory->byte(data)==10;}),"Missing initial memory");
 for(int step=1;step<=3;++step){action("Step")->trigger();require(wait([&]{return action("Step")->isEnabled();}),"Step failed");if(step==1||step==2){require(regs->values["rax"]==quint64(step==1?10:15),"Wrong register");require(regs->changed.contains("rax")&&regs->changed.contains("rip"),"Missing changed register highlights");}}
 require(wait([&]{return memory->byte(data)==15;}),"Memory write missing");require(memory->changed(data)&&!memory->changed(data+1),"Wrong byte highlights");require(!regs->changed.contains("rax"),"Register highlight persisted past next step");
 require(memory->current(regs->values["rip"]),"Missing instruction outline");w.grab().save("/tmp/asm101-test.png");
 const auto actualRegisters=regs->values;
 const auto darkSection=memory->sectionColor(data);
 const auto darkHighlight=editor->extraSelections().first().format.background().color();
 themeToggle->click();QTest::qWait(30);
 require(regs->values==actualRegisters&&memory->byte(data)==15&&memory->changed(data),"Theme toggle changed debugger state");
 require(memory->sectionColor(data).lightness()>180,"Memory section did not switch to light colors");
 require(regs->data(regs->index(0,1),Qt::ForegroundRole).value<QColor>().lightness()<100,"Register text is not dark in light mode");
 require(editor->extraSelections().first().format.background().color().lightness()>180,"Execution line did not switch to a light highlight");
 require(w.rect().contains(themeToggle->mapTo(&w,themeToggle->rect().topRight())),"Corner toggle is outside the window");
 w.grab().save("/tmp/asm101-light.png");
 zoomToggle->click();QTest::qWait(30);
 require(editor->font().pointSizeF()==normalFont*2&&editor->toPlainText()==source,"Magnification altered source or used the wrong scale");
 require(regs->values==actualRegisters&&memory->byte(data)==15&&memory->changed(data),"Magnifier changed debugger state");
 require(editor->viewport()->rect().intersects(editor->cursorRect()),"Magnifier scrolled the current instruction out of view");
 for(auto table:w.findChildren<QTableView *>()) {
     require(table->font().pointSizeF()==normalFont*2,"Table text did not double");
     require(table->rowHeight(0)>table->fontMetrics().height(),"Enlarged table row clips text");
     if(table->model()==regs)
         require(table->columnWidth(2)>=table->fontMetrics().horizontalAdvance("AX:-32768  AH:-128  AL:-128"),"Enlarged register parts are clipped");
     else
         require(table->columnWidth(1)>=MemoryDelegate::byteWidth(table->fontMetrics(),0)*8,"Enlarged memory bytes are clipped");
 }
 require(zoomToggle->isVisible()&&w.rect().contains(zoomToggle->mapTo(&w,zoomToggle->rect().topRight())),"Magnifier is outside the window");
 w.grab().save("/tmp/asm101-light-zoom.png");
 themeToggle->click();QTest::qWait(30);
 require(editor->font().pointSizeF()==normalFont*2,"Theme switch reset magnification");
 zoomToggle->click();QTest::qWait(30);
 require(editor->font().pointSizeF()==normalFont,"Normal font size did not restore after a theme switch");
 require(memory->sectionColor(data)==darkSection&&editor->extraSelections().first().format.background().color()==darkHighlight,"Dark debugger colors did not restore");
 for(int step=4;step<=6;++step){action("Step")->trigger();require(wait([&]{return step<6?action("Step")->isEnabled():action("Start")->isEnabled();}),"Step to exit failed");}
 action("Start")->trigger();require(wait([&]{return action("Step")->isEnabled();}),"Restart failed");require(wait([&]{return memory->byte(data)==10;}),"Restart did not reset memory");action("Stop")->trigger();require(wait([&]{return action("Start")->isEnabled();}),"Stop failed");
 qInfo()<<"PASS: GUI build/start/step, source arrow, register and byte highlights, step to exit, restart, stop";return 0;}
