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
#include <functional>
int main(int argc,char **argv){QApplication app(argc,argv);applyTheme(app);MainWindow w;w.show();QTimer watchdog;QObject::connect(&watchdog,&QTimer::timeout,[&]{for(auto widget:app.topLevelWidgets())if(auto box=qobject_cast<QMessageBox*>(widget)){qCritical()<<"Unexpected dialog:"<<box->text();std::exit(1);}});watchdog.start(100);
 auto wait=[&](std::function<bool()> predicate){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<7000)QTest::qWait(10);return predicate();};
 auto action=[&](QString name){for(auto a:w.findChildren<QAction*>())if(a->text()==name)return a;return static_cast<QAction*>(nullptr);};
 auto require=[](bool ok,const char *message){if(!ok){qCritical()<<message;std::exit(1);}};
 AssemblyEditor *editor=nullptr;for(auto e:w.findChildren<QPlainTextEdit*>())if(auto a=dynamic_cast<AssemblyEditor*>(e))editor=a;
 RegisterModel *regs=nullptr;MemoryModel *memory=nullptr;for(auto t:w.findChildren<QTableView*>()){if(auto m=dynamic_cast<RegisterModel*>(t->model()))regs=m;if(t->objectName()=="ramView")if(auto m=dynamic_cast<MemoryModel*>(t->model()))memory=m;}
 require(editor&&regs&&memory,"Missing panels");require(editor->toPlainText().contains("value dq 10"),"Missing example");
 action("Build")->trigger();require(wait([&]{return action("Start")->isEnabled();}),"Build failed");
 action("Start")->trigger();require(wait([&]{return action("Step")->isEnabled();}),"Start failed");
 require(!editor->extraSelections().isEmpty(),"Missing source highlight");require(memory->rowCount()>0,"Missing memory rows");
 quint64 data=0;for(int row=0;row<memory->rowCount();++row){quint64 a=memory->address(row);if(memory->sectionColor(a)==QColor("#123c32")){data=a;break;}}
 require(data!=0,"Missing .data");require(wait([&]{return memory->byte(data)==10;}),"Missing initial memory");
 for(int step=1;step<=3;++step){action("Step")->trigger();require(wait([&]{return action("Step")->isEnabled();}),"Step failed");if(step==1||step==2){require(regs->values["rax"]==quint64(step==1?10:15),"Wrong register");require(regs->changed.contains("rax")&&regs->changed.contains("rip"),"Missing changed register highlights");}}
 require(wait([&]{return memory->byte(data)==15;}),"Memory write missing");require(memory->changed(data)&&!memory->changed(data+1),"Wrong byte highlights");require(!regs->changed.contains("rax"),"Register highlight persisted past next step");
 require(memory->current(regs->values["rip"]),"Missing instruction outline");w.grab().save("/tmp/asm101-test.png");
 for(int step=4;step<=6;++step){action("Step")->trigger();require(wait([&]{return step<6?action("Step")->isEnabled():action("Start")->isEnabled();}),"Step to exit failed");}
 action("Start")->trigger();require(wait([&]{return action("Step")->isEnabled();}),"Restart failed");require(wait([&]{return memory->byte(data)==10;}),"Restart did not reset memory");action("Stop")->trigger();require(wait([&]{return action("Start")->isEnabled();}),"Stop failed");
 qInfo()<<"PASS: GUI build/start/step, source arrow, register and byte highlights, step to exit, restart, stop";return 0;}
