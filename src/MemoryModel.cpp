#include "MemoryModel.h"
#include <QPainter>
#include <QTimer>
#include <QHelpEvent>
#include <QToolTip>
#include <QAbstractItemView>
#include <climits>
MemoryModel::MemoryModel(DebuggerEngine *e,QObject *p,Mode mode):QAbstractTableModel(p),engine(e),mode(mode){}
int MemoryModel::rowCount(const QModelIndex &p) const{return p.isValid()?0:(mode==Stack?10:rows);}
quint64 MemoryModel::address(int row)const{if(mode==Stack)return haveStack&&row>=0&&row<10?stackBase+quint64(row)*8:~quint64(0);for(auto s:spans)if(row>=s.first && quint64(row-s.first)<s.rows)return s.base+quint64(row-s.first)*8;return ~quint64(0);}
int MemoryModel::rowForAddress(quint64 address) const {
    if (mode == Stack) return validAddress(address) ? int((address - stackBase) / 8) : -1;
    for (const auto &span : spans)
        if (address >= span.base && (address - span.base) / 8 < span.rows)
            return span.first + int((address - span.base) / 8);
    return -1;
}
bool MemoryModel::validAddress(quint64 a)const{if(mode==Stack)return haveStack&&a>=stackBase&&a-stackBase<80;for(auto s:engine->sections)if(a>=s.address&&a-s.address<s.size)return true;return false;}
QColor MemoryModel::sectionColor(quint64 a)const{if(mode==Stack)return QColor("#213140");for(auto s:engine->sections)if(a>=s.address&&a-s.address<s.size){if(s.name==".text")return QColor("#142e4d");if(s.name==".rodata")return QColor("#30203e");if(s.name==".data")return QColor("#123c32");return QColor("#443719");}return QColor("#101820");}
void MemoryModel::resetSections(){if(mode==Stack){beginResetModel();++generation;haveStack=false;stackBytes.clear();modified.clear();endResetModel();return;}beginResetModel();++generation;cache.clear();requested.clear();modified.clear();spans.clear();rows=0;for(auto s:engine->sections){quint64 base=s.address&~quint64(7),end=(s.address+s.size+7)&~quint64(7);if(!spans.isEmpty()){auto &last=spans.last();quint64 prevEnd=last.base+last.rows*8;if(base<=prevEnd){int extra=int(qMin<quint64>((end>prevEnd?(end-prevEnd)/8:0),INT_MAX-rows));last.rows+=extra;rows+=extra;continue;}}if(rows>0)++rows;quint64 n=qMin<quint64>((end-base)/8,INT_MAX-rows);spans.append({base,n,rows});rows+=int(n);}endResetModel();}
void MemoryModel::fetch(quint64 page,bool compare){
    if(requested.contains(page)||!engine->paused())return;
    requested.insert(page); int epoch=generation;
    // Split reads at ELF boundaries so inaccessible gaps are never invented as zeros.
    for(auto s:engine->sections){quint64 a=qMax(page,s.address),end=qMin(page+256,s.address+s.size);if(a>=end)continue;
        engine->readMemory(a,int(end-a),[this,page,a,end,epoch,compare](QByteArray b){if(epoch!=generation)return;QByteArray old=cache.value(page);QByteArray &v=cache[page];if(v.isEmpty())v=QByteArray(512,char(0));for(quint64 addr=a;addr<end;++addr){if(addr-page>=256)break;v[256+int(addr-page)]=0;}for(int i=0;i<int(b.size());++i){int off=int(a-page)+i;if(compare&&old.size()==512&&old[256+off]&&old[off]!=b[i])modified.insert(a+i);v[off]=b[i];v[256+off]=1;}while(cache.size()>128){auto it=cache.begin();if(it.key()==page)++it;requested.remove(it.key());cache.erase(it);}if(rows)emit dataChanged(index(0,0),index(rows-1,1));});
    }
}
int MemoryModel::byte(quint64 a)const{if(mode==Stack){auto it=stackBytes.constFind(a);return it==stackBytes.cend()?-1:int(it.value());}if(!validAddress(a))return -1;quint64 page=a&~quint64(255);if(!cache.contains(page)&&!requested.contains(page))QTimer::singleShot(0,const_cast<MemoryModel*>(this),[this,page]{const_cast<MemoryModel*>(this)->fetch(page);});auto it=cache.constFind(page);int off=int(a-page);return it!=cache.cend()&&it->size()==512&&(*it)[256+off]?uchar((*it)[off]):-1;}
void MemoryModel::refresh(){if(mode==Stack){refreshStack();return;}modified.clear();requested.clear();auto pages=cache.keys(); // Retain a bounded working set across steps.
    while(cache.size()>128)cache.erase(cache.begin());
    for(auto p:pages)if(cache.contains(p))fetch(p,true);
    if(rows)emit dataChanged(index(0,0),index(rows-1,1));
}
QVariant MemoryModel::data(const QModelIndex &i,int role)const{quint64 a=address(i.row());if(role==Qt::DisplayRole){if(a==~quint64(0))return mode==Stack?QString("—"):QString("…");if(i.column()==0)return QString("0x%1 – 0x%2").arg(a,8,16,QChar('0')).arg(a+7,8,16,QChar('0')).toUpper().replace("0X","0x");QStringList values;for(int n=0;n<8;++n){int b=byte(a+n);values<< (b<0?QString("??"):QString("%1").arg(b,2,16,QChar('0')).toUpper());}return values.join(' ');}return {};}
QVariant MemoryModel::headerData(int s,Qt::Orientation o,int role)const{if(o==Qt::Horizontal&&role==Qt::DisplayRole)return s==0?"Address Range":"Values · 8 bytes";return {};}
QStringList MemoryModel::symbolsAt(quint64 a) const {
    if (mode == Stack) return {};
    return engine->dataSymbols.value(a);
}

QList<int> MemoryModel::annotatedRows() const {
    if (mode == Stack) return {};
    QSet<int> result;
    for (auto it = engine->dataSymbols.cbegin(); it != engine->dataSymbols.cend(); ++it)
        for (const auto &span : spans)
            if (it.key() >= span.base && (it.key() - span.base) / 8 < span.rows) {
                result.insert(span.first + int((it.key() - span.base) / 8));
                break;
            }
    return result.values();
}

bool MemoryModel::hasSymbols(int row) const {
    if (mode == Stack) return false;
    const quint64 a = address(row);
    if (a == ~quint64(0)) return false;
    auto it = engine->dataSymbols.lowerBound(a);
    return it != engine->dataSymbols.cend() && it.key() - a < 8;
}

QString MemoryModel::symbolTooltip(quint64 a) const {
    return symbolsAt(a).join(" / ") + QString(" · 0x%1").arg(a, 16, 16, QChar('0'));
}

int MemoryDelegate::byteWidth(const QFontMetrics &metrics, int columnWidth) {
    // Spread the eight bytes across the available column, keeping room for labels.
    const int minimum = qMax(34, metrics.horizontalAdvance("00 ") + 4);
    return qMax(minimum, (columnWidth - 20) / 8);
}

void MemoryDelegate::paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const {
    const auto m = static_cast<const MemoryModel *>(i.model());
    const quint64 a = m->address(i.row());
    if (a == ~quint64(0)) {
        QStyledItemDelegate::paint(p, o, i);
        return;
    }
    p->save();
    p->setClipRect(o.rect);
    p->fillRect(o.rect, QColor("#101820"));
    p->setFont(o.font);
    const bool annotated = m->hasSymbols(i.row());
    const int labelHeight = annotated ? o.fontMetrics.height() + 2 : 0;
    if (i.column() == 0) {
        p->setPen(QColor("#99b6d6"));
        p->drawText(o.rect.adjusted(10, labelHeight, 0, 0), Qt::AlignVCenter, m->data(i).toString());
    } else {
        const int width = byteWidth(o.fontMetrics, o.rect.width());
        const int x = o.rect.x() + 10;
        for (int n = 0; n < 8; ++n) {
            const QRect r(x + n * width, o.rect.y() + labelHeight + 1,
                          width - 2, o.rect.height() - labelHeight - 2);
            p->fillRect(r, m->sectionColor(a + n));
            if (m->current(a + n)) {
                p->setPen(QColor("#ead55e"));
                p->drawRect(r.adjusted(0, 0, -1, -1));
            }
            p->setFont(o.font);
            p->setPen(m->changed(a + n) ? QColor("#64f0a0") : QColor("#e0e8f1"));
            const int b = m->byte(a + n);
            p->drawText(r, Qt::AlignCenter, b < 0 ? "??" : QString("%1").arg(b, 2, 16, QChar('0')).toUpper());
            if (m->changed(a + n))
                p->fillRect(r.adjusted(2, r.height() - 3, -2, 0), QColor("#64f0a0"));

            const auto names = m->symbolsAt(a + n);
            if (!names.isEmpty()) {
                int next = n + 1;
                while (next < 8 && m->symbolsAt(a + next).isEmpty()) ++next;
                QFont labelFont = o.font;
                if (labelFont.pointSizeF() > 1) labelFont.setPointSizeF(labelFont.pointSizeF() - 1);
                p->setFont(labelFont);
                p->setPen(QColor("#a8d8c3"));
                const QRect label(x + n * width + 2, o.rect.y(), (next - n) * width - 4, labelHeight);
                p->drawText(label, Qt::AlignLeft | Qt::AlignVCenter,
                            QFontMetrics(labelFont).elidedText(names.join(" / "), Qt::ElideRight, label.width()));
            }
        }
    }
    p->restore();
}

bool MemoryDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                              const QStyleOptionViewItem &option, const QModelIndex &index) {
    const auto model = static_cast<const MemoryModel *>(index.model());
    if (event->type() != QEvent::ToolTip || index.column() != 1)
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    const quint64 base = model->address(index.row());
    const int offset = event->pos().x() - option.rect.x() - 10;
    const int byte = offset / byteWidth(option.fontMetrics, option.rect.width());
    if (base != ~quint64(0) && offset >= 0 && byte < 8) {
        int symbolByte = byte;
        if (model->hasSymbols(index.row()) &&
            event->pos().y() < option.rect.y() + option.fontMetrics.height() + 2)
            while (symbolByte > 0 && model->symbolsAt(base + symbolByte).isEmpty()) --symbolByte;
        if (!model->symbolsAt(base + symbolByte).isEmpty()) {
            QToolTip::showText(event->globalPos(), model->symbolTooltip(base + symbolByte).toHtmlEscaped(), view);
            return true;
        }
    }
    QToolTip::hideText();
    return true;
}

void MemoryModel::refreshStack() {
    if (!engine->paused() || !engine->registers.contains("rsp")) return;
    const auto previous = stackBytes;
    if (!haveStack) {
        // Keep the initial RSP on the bottom row as the stack grows upward in the view.
        stackBase = engine->registers["rsp"] - 9 * 8;
        haveStack = true;
    }
    stackBytes.clear();
    modified.clear();
    const int epoch = ++generation;
    const quint64 base = stackBase;
    emit dataChanged(index(0, 0), index(9, 1));
    auto install = [this, previous, epoch](quint64 address, const QByteArray &data) {
        if (epoch != generation) return;
        for (int n = 0; n < data.size(); ++n) {
            const quint64 a = address + n;
            if (!validAddress(a)) break;
            const uchar value = uchar(data[n]);
            if (previous.contains(a) && previous[a] != value) modified.insert(a);
            stackBytes[a] = value;
        }
        emit dataChanged(index(0, 0), index(9, 1));
    };
    // Normally a single 80-byte read. Retry by row if the block straddles unreadable memory.
    engine->readMemory(base, 80, [this, base, epoch, install](QByteArray data) {
        if (epoch != generation) return;
        if (data.size() == 80) { install(base, data); return; }
        for (int row = 0; row < 10; ++row) {
            const quint64 address = base + row * 8;
            engine->readMemory(address, 8, [install, address](QByteArray bytes) { install(address, bytes); });
        }
    });
}
