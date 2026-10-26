#include "DebuggerEngine.h"
#include "MemoryModel.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <QDebug>
int main(int argc,char **argv){QCoreApplication app(argc,argv);QTemporaryDir dir;QString source=dir.filePath("edge.asm"),obj=dir.filePath("edge.o"),exe=dir.filePath("edge");QFile f(source);if(!f.open(QIODevice::WriteOnly))return 1;f.write("section .rodata\nmessage db 0x41\nsection .data\nvalue db 7\nsection .bss\nbuffer resq 1\nsection .text\nglobal _start\n_start:\nxor rax, rax\ntimes 2 inc rax\nmov [buffer], rax\nmov rax, 60\nxor rdi, rdi\nsyscall\n");f.close();if(QProcess::execute("nasm",{"-f","elf64","-g","-F","dwarf",source,"-o",obj})||QProcess::execute("ld",{obj,"-o",exe}))return 1;
DebuggerEngine e;MemoryModel model(&e);e.sections={{".text",0x1001,2},{".rodata",0x1003,1},{".data",0x1004,1},{".bss",0x1005,1}};model.resetSections();if(model.rowCount()!=1||model.address(0)!=0x1000||model.validAddress(0x1000)||model.sectionColor(0x1002)==model.sectionColor(0x1003)||model.sectionColor(0x1004)==model.sectionColor(0x1005))return 2;
int stops=0,lastLine=0,previousSize=0;quint64 previousRip=0,bss=0;auto fail=[&](QString s){qCritical()<<s;app.exit(1);};QObject::connect(&e,&DebuggerEngine::error,&app,fail);
QObject::connect(&e,&DebuggerEngine::snapshot,&app,[&]{if(stops&&e.rip!=previousRip+previousSize){fail("Not exactly one instruction");return;}previousRip=e.rip;previousSize=e.instructionSize;
if(stops==0){if(e.sections.size()!=4){fail("Missing ELF sections");return;}for(auto s:e.sections)if(s.name==".bss")bss=s.address;}
qInfo()<<"stop"<<stops<<"line"<<e.line<<"rip"<<Qt::hex<<e.rip;
if(stops>=1&&stops<=3&&e.registers["rax"]!=quint64(stops-1)){fail("Wrong repeated instruction state");return;}
if(stops==1&&!(e.flags&64)){fail("XOR did not set ZF");return;}if(stops==2&&(e.flags&64)){fail("INC did not clear ZF");return;}if(stops==2&&lastLine!=e.line){fail("Expected two instructions on one source line");return;}lastLine=e.line;
e.readMemory(bss,8,[&](QByteArray b){if(b.size()!=8||uchar(b[0])!=(stops>=4?2:0)){fail("Wrong BSS memory");return;}++stops;e.step();});});
QObject::connect(&e,&DebuggerEngine::ended,&app,[&]{if(stops==7){qInfo()<<"PASS: instruction stepping within one source line, actual flags, BSS, four ELF sections, per-byte boundary colors";app.exit(0);}else fail("Early termination");});QTimer::singleShot(20000,&app,[&]{fail("Timeout");});e.start(exe);return app.exec();}
