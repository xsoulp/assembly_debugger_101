#include "DebuggerEngine.h"
#include "MemoryModel.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>
#include <QDebug>
#include <cstdlib>
int main(int argc,char **argv){QCoreApplication app(argc,argv);if(argc<2)return 1;QTemporaryDir dir;QString obj=dir.filePath("p.o"),exe=dir.filePath("p");if(QProcess::execute("nasm",{"-f","elf64","-g","-F","dwarf",QString::fromLocal8Bit(argv[1]),"-o",obj})||QProcess::execute("ld",{obj,"-o",exe}))return 2;
DebuggerEngine e;int step=0;quint64 data=0,previousRip=0;int previousSize=0;auto fail=[&](QString s){qCritical()<<s;app.exit(1);};
QObject::connect(&e,&DebuggerEngine::error,&app,fail);
QObject::connect(&e,&DebuggerEngine::snapshot,&app,[&]{
    if(e.line<=0||!e.rip||e.instructionSize<=0||!e.registers.contains("rsp")||!e.registers.contains("eflags")){fail("Incomplete actual state");return;}
    if(step==0){for(auto s:e.sections)if(s.name==".data")data=s.address;if(!data){fail("Missing data section");return;}}
    if(step>0&&e.rip!=previousRip+previousSize){fail("Instruction did not advance");return;}previousRip=e.rip;previousSize=e.instructionSize;
    if((step==1&&e.registers["rax"]!=10)||(step==2&&e.registers["rax"]!=15)){fail("Wrong RAX");return;}
    if(step==5&&(!(e.flags&(1<<6))||e.registers["rdi"]!=0)){fail("Incorrect XOR flags or RDI");return;}
    e.readMemory(data,8,[&](QByteArray b){if(b.size()!=8||uchar(b[0])!=(step>=3?15:10)){fail("Wrong memory after step "+QString::number(step));return;}qInfo()<<"Verified stop"<<step<<"RIP"<<Qt::hex<<e.rip<<"RAX"<<e.registers["rax"]<<"data"<<b.toHex();++step;e.step();});
});
QObject::connect(&e,&DebuggerEngine::ended,&app,[&]{if(step==6){qInfo()<<"PASS: entry, six instruction steps, registers, flags, memory, termination";app.exit(0);}else fail("Unexpected termination at "+QString::number(step));});
QTimer::singleShot(20000,&app,[&]{fail("Timed out");});e.start(exe);return app.exec();}
