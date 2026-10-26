#pragma once
#include <QString>
#include <QMap>
#include <QList>
// Recursive MI grammar: tuples, lists, named results and escaped C strings.
struct Mi {
    QString text; QMap<QString,Mi> fields; QList<Mi> items;
    Mi operator[](const QString &key) const { return fields.value(key); }
    static Mi parse(const QString &s, int &p) {
        Mi n;
        if(p>=s.size()) return n;
        if(s[p]=='"') {
            ++p;
            while(p<s.size() && s[p]!='"') {
                QChar c=s[p++];
                if(c=='\\' && p<s.size()) { c=s[p++]; if(c=='n') c='\n'; else if(c=='t') c='\t'; else if(c=='r') c='\r'; else if(c>='0' && c<='7') { int v=c.unicode()-'0'; for(int i=0;i<2 && p<s.size() && s[p]>='0' && s[p]<='7';++i) v=v*8+s[p++].unicode()-'0'; c=QChar(v); } }
                n.text+=c;
            }
            ++p; return n;
        }
        if(s[p]=='{' || s[p]=='[') {
            QChar end=s[p++]=='{'?'}':']';
            while(p<s.size() && s[p]!=end) {
                int start=p;
                while(p<s.size() && (s[p].isLetterOrNumber() || s[p]=='-' || s[p]=='_')) ++p;
                if(p<s.size() && s[p]=='=') { QString key=s.mid(start,p-start); ++p; Mi v=parse(s,p); n.fields[key]=v; n.items.append(v); }
                else { p=start; n.items.append(parse(s,p)); }
                if(p<s.size() && s[p]==',') ++p; else break;
            }
            if(p<s.size()) ++p;
            return n;
        }
        int start=p; while(p<s.size() && s[p]!=',' && s[p]!=']' && s[p]!='}') ++p;
        n.text=s.mid(start,p-start); return n;
    }
    static Mi results(QString s) { s="{"+s+"}"; int p=0; return parse(s,p); }
};
inline QString miQuote(QString s) { s.replace("\\","\\\\"); s.replace("\"","\\\""); s.replace("\n","\\n"); return '"'+s+'"'; }
