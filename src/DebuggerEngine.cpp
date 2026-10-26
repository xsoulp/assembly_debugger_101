#include "DebuggerEngine.h"
#include <QFile>
#include <elf.h>
#include <cstring>
DebuggerEngine::DebuggerEngine(QObject *p):QObject(p) {
    connect(&process,&QProcess::readyReadStandardOutput,this,&DebuggerEngine::consume);
    connect(&process,&QProcess::errorOccurred,this,[this](auto){ if(!terminating) emit error(process.errorString()); });
    connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this]{ready=false; pending.clear(); emit ended();});
}
DebuggerEngine::~DebuggerEngine(){ terminating=true; process.kill(); process.waitForFinished(1000); }
void DebuggerEngine::command(QString c,std::function<void(QString,Mi)> cb,bool reportError) {
    int t=++token; pending[t]=[this,cb,reportError](QString result,Mi m){if(result=="error"&&reportError&&!terminating)emit error(m["msg"].text);if(cb)cb(result,m);}; process.write(QString::number(t).toUtf8()+c.toUtf8()+"\n");
}
void DebuggerEngine::start(const QString &path) {
    if(process.state()!=QProcess::NotRunning) { emit error("Stop the current session first."); return; }
    loadSections(path); registers.clear(); registerNames.clear(); ready=false; terminating=false; buffer.clear();
    connect(&process,&QProcess::started,this,[this,path]{
        command("-gdb-set pagination off"); command("-gdb-set disassembly-flavor intel"); command("-gdb-set confirm off");
        command("-gdb-set mi-async on");
        command("-file-exec-and-symbols "+miQuote(path),[this](QString result,Mi){
            if(result!="done") return;
            command("-data-list-register-names",[this](QString result,Mi m){
                if(result!="done") return;
                for(auto v:m["register-names"].items) registerNames<<v.text;
                command("-break-insert -t _start",[this](QString result,Mi){ if(result=="done") command("-exec-run"); });
            });
        });
    },Qt::SingleShotConnection);
    process.start("gdb",{"--quiet","--nx","--interpreter=mi2"});
}
void DebuggerEngine::consume() {
    buffer+=process.readAllStandardOutput();
    while(buffer.contains('\n')) {
        int e=buffer.indexOf('\n'); QString s=QString::fromUtf8(buffer.left(e)).trimmed(); buffer.remove(0,e+1);
        int p=0; while(p<s.size() && s[p].isDigit()) ++p;
        if(p>=s.size()) continue;
        QChar kind=s[p++]; int comma=s.indexOf(',',p); QString result=comma<0?s.mid(p):s.mid(p,comma-p);
        Mi m=comma<0?Mi{}:Mi::results(s.mid(comma+1));
        if(kind=='^') {
            auto cb=pending.take(s.left(p-1).toInt());
            if(cb) cb(result,m);
        } else if(kind=='*' && result=="running") {ready=false; emit running();}
        else if(kind=='*' && result=="stopped") {
            if(m["reason"].text.startsWith("exited")) {ready=false; command("-gdb-exit");}
            else if(!terminating) refresh(m);
        }
    }
}
void DebuggerEngine::refresh(const Mi &record) {
    ready=false; line=record["frame"]["line"].text.toInt();
    command("-data-list-register-values x",[this](QString result,Mi m){
        if(result!="done") return;
        QMap<QString,quint64> next;
        for(auto r:m["register-values"].items) {int i=r["number"].text.toInt(); if(i<registerNames.size()) {bool ok; quint64 v=r["value"].text.toULongLong(&ok,0); if(ok) next[registerNames[i]]=v;}}
        registers=next; rip=next.value("rip"); flags=next.value("eflags",next.value("rflags"));
        command(QString("-data-disassemble -s %1 -e %2 -- 2").arg(rip).arg(rip+16),[this](QString result,Mi m){
            instructionSize=0; instruction.clear();
            if(result=="done" && !m["asm_insns"].items.isEmpty()) {auto i=m["asm_insns"].items.first(); instruction=i["inst"].text; instructionSize=QByteArray::fromHex(i["opcodes"].text.toLatin1().replace(" ","")).size();}
            ready=true; emit snapshot();
        });
    });
}
void DebuggerEngine::step(){if(ready){ready=false; command("-exec-step-instruction");}}
void DebuggerEngine::stop(){terminating=true;ready=false;if(process.state()!=QProcess::NotRunning) {process.kill();} }
void DebuggerEngine::readMemory(quint64 a,int count,std::function<void(QByteArray)> cb){
    if(!ready) {cb({});return;}
    command(QString("-data-read-memory-bytes %1 %2").arg(a).arg(count),[cb](QString result,Mi m){ QByteArray data; if(result=="done") for(auto block:m["memory"].items) data+=QByteArray::fromHex(block["contents"].text.toLatin1()); cb(data); },false);
}
void DebuggerEngine::loadSections(const QString &path){
    sections.clear(); dataSymbols.clear(); QFile f(path); if(!f.open(QIODevice::ReadOnly)) return; QByteArray b=f.readAll();
    if(b.size()<qsizetype(sizeof(Elf64_Ehdr))) return;
    Elf64_Ehdr h; memcpy(&h,b.data(),sizeof h);
    if(memcmp(h.e_ident,ELFMAG,SELFMAG)||h.e_ident[EI_CLASS]!=ELFCLASS64||h.e_type!=ET_EXEC||h.e_shentsize!=sizeof(Elf64_Shdr)||h.e_shstrndx>=h.e_shnum) return;
    if(h.e_shoff>b.size() || quint64(h.e_shnum)*sizeof(Elf64_Shdr)>quint64(b.size())-h.e_shoff) return;
    auto sh=[&](int i){Elf64_Shdr s;memcpy(&s,b.data()+h.e_shoff+i*sizeof s,sizeof s);return s;};
    auto names=sh(h.e_shstrndx); if(names.sh_offset>quint64(b.size())||names.sh_size>quint64(b.size())-names.sh_offset) return;
    for(int i=0;i<h.e_shnum;++i){auto s=sh(i);if(s.sh_name>=names.sh_size)continue; auto raw=b.mid(names.sh_offset+s.sh_name,names.sh_size-s.sh_name);QString name=QString::fromLatin1(raw.left(raw.indexOf('\0')));if(QStringList{".text",".rodata",".data",".bss"}.contains(name)&&s.sh_size)sections.append({name,s.sh_addr,s.sh_size});}
    // NASM variables are often STT_NOTYPE, so use their ELF section rather than
    // relying on STT_OBJECT or guessing addresses from source text.
    auto inFile = [&](quint64 offset, quint64 size) {
        return offset <= quint64(b.size()) && size <= quint64(b.size()) - offset;
    };
    auto stringAt = [&](const Elf64_Shdr &table, quint32 offset) -> QString {
        if (!inFile(table.sh_offset, table.sh_size) || offset >= table.sh_size)
            return {};
        const char *start = b.constData() + table.sh_offset + offset;
        const auto length = table.sh_size - offset;
        const char *end = static_cast<const char *>(memchr(start, 0, length));
        return end ? QString::fromUtf8(start, end - start) : QString();
    };
    for (int i = 0; i < h.e_shnum; ++i) {
        const auto table = sh(i);
        if (table.sh_type != SHT_SYMTAB || table.sh_entsize != sizeof(Elf64_Sym) ||
            table.sh_link >= h.e_shnum || !inFile(table.sh_offset, table.sh_size))
            continue;
        const auto strings = sh(table.sh_link);
        for (quint64 offset = 0; offset + sizeof(Elf64_Sym) <= table.sh_size;
             offset += sizeof(Elf64_Sym)) {
            Elf64_Sym symbol;
            memcpy(&symbol, b.constData() + table.sh_offset + offset, sizeof symbol);
            const int type = ELF64_ST_TYPE(symbol.st_info);
            if ((type != STT_NOTYPE && type != STT_OBJECT) ||
                symbol.st_shndx == SHN_UNDEF || symbol.st_shndx >= h.e_shnum)
                continue;
            const QString sectionName = stringAt(names, sh(symbol.st_shndx).sh_name);
            if (sectionName != ".data" && sectionName != ".rodata" && sectionName != ".bss")
                continue;
            const auto section = sh(symbol.st_shndx);
            if (symbol.st_value < section.sh_addr ||
                symbol.st_value - section.sh_addr >= section.sh_size)
                continue;
            const QString name = stringAt(strings, symbol.st_name);
            // ld's boundary markers are not user variables.
            if (name.isEmpty() || name == "__bss_start" || name == "_edata" || name == "_end")
                continue;
            auto &aliases = dataSymbols[symbol.st_value];
            if (!aliases.contains(name))
                aliases.append(name);
        }
    }
    std::sort(sections.begin(),sections.end(),[](auto a,auto b){return a.address<b.address;});
}
