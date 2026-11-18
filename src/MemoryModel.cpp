#include "MemoryModel.h"
#include "Theme.h"
#include <QPainter>
#include <QTimer>
#include <QHelpEvent>
#include <QToolTip>
#include <QAbstractItemView>
#include <QFont>
#include <QMouseEvent>
#include <climits>
MemoryModel::MemoryModel(DebuggerEngine *e, QObject *p, Mode mode)
    : QAbstractTableModel(p), engine(e), mode(mode) {}
int MemoryModel::rowCount(const QModelIndex &p) const {
    return p.isValid() ? 0 : (mode == Stack && !haveStack ? 10 : rows);
}
quint64 MemoryModel::address(int row) const {
    if (mode == Stack)
        return haveStack && row >= 0 && row < rows ? stackBase + quint64(row) * 8 : ~quint64(0);
    for (auto s : spans)
        if (row >= s.first && quint64(row - s.first) < s.rows)
            return s.base + quint64(row - s.first) * 8;
    return ~quint64(0);
}
int MemoryModel::rowForAddress(quint64 address) const {
    if (mode == Stack)
        return validAddress(address) ? int((address - stackBase) / 8) : -1;
    for (const auto &span : spans)
        if (address >= span.base && (address - span.base) / 8 < span.rows)
            return span.first + int((address - span.base) / 8);
    return -1;
}
bool MemoryModel::validAddress(quint64 a) const {
    if (mode == Stack)
        return haveStack && a >= stackBase && (a - stackBase) / 8 < quint64(rows);
    for (auto s : engine->sections)
        if (sectionVisible(s.name) && a >= s.address && a - s.address < s.size)
            return true;
    return false;
}
QColor MemoryModel::sectionColor(quint64 a) const {
    if (mode == Stack)
        return themeColor("#482b29", "#fbe3dd");
    for (auto s : engine->sections)
        if (a >= s.address && a - s.address < s.size) {
            if (s.name == ".text")
                return themeColor("#142e4d", "#dceaff");
            if (s.name == ".rodata")
                return themeColor("#30203e", "#eee0f5");
            if (s.name == ".data")
                return themeColor("#123c32", "#d9f0e2");
            return themeColor("#443719", "#fff0c9");
        }
    return themeColor("#101820", "#ffffff");
}
void MemoryModel::resetSections() {
    asciiBytes.clear();
    if (mode == Stack) {
        beginResetModel();
        ++generation;
        haveStack = false;
        rows = 0;
        cache.clear();
        requested.clear();
        modified.clear();
        endResetModel();
        return;
    }
    beginResetModel();
    ++generation;
    cache.clear();
    requested.clear();
    modified.clear();
    rebuildSpans();
    endResetModel();
}
bool MemoryModel::sectionVisible(const QString &name) const {
    return !hiddenSections.contains(name);
}
void MemoryModel::setSectionVisible(const QString &name, bool visible) {
    if (mode != Sections || sectionVisible(name) == visible)
        return;
    beginResetModel();
    if (visible)
        hiddenSections.remove(name);
    else
        hiddenSections.insert(name);
    rebuildSpans();
    endResetModel();
}
void MemoryModel::rebuildSpans() {
    spans.clear();
    rows = 0;
    for (auto s : engine->sections) {
        if (!sectionVisible(s.name))
            continue;
        quint64 base = s.address & ~quint64(7), end = (s.address + s.size + 7) & ~quint64(7);
        if (!spans.isEmpty()) {
            auto &last = spans.last();
            quint64 prevEnd = last.base + last.rows * 8;
            if (base <= prevEnd) {
                int extra =
                    int(qMin<quint64>((end > prevEnd ? (end - prevEnd) / 8 : 0), INT_MAX - rows));
                last.rows += extra;
                rows += extra;
                continue;
            }
        }
        if (rows > 0)
            ++rows;
        quint64 n = qMin<quint64>((end - base) / 8, INT_MAX - rows);
        spans.append({base, n, rows});
        rows += int(n);
    }
}
void MemoryModel::fetch(quint64 page, bool compare) {
    if (requested.contains(page) || !engine->paused())
        return;
    if (mode == Stack && !validAddress(page) && !validAddress(page + 255))
        return;
    requested.insert(page);
    if (mode == Stack) {
        fetchStack(page, compare);
        return;
    }
    int epoch = generation;
    // Split reads at ELF boundaries so inaccessible gaps are never invented as zeros.
    for (auto s : engine->sections) {
        quint64 a = qMax(page, s.address), end = qMin(page + 256, s.address + s.size);
        if (a >= end)
            continue;
        engine->readMemory(a, int(end - a), [this, page, a, end, epoch, compare](QByteArray b) {
            if (epoch != generation)
                return;
            QByteArray old = cache.value(page);
            QByteArray &v = cache[page];
            if (v.isEmpty())
                v = QByteArray(512, char(0));
            for (quint64 addr = a; addr < end; ++addr) {
                if (addr - page >= 256)
                    break;
                v[256 + int(addr - page)] = 0;
            }
            for (int i = 0; i < int(b.size()); ++i) {
                int off = int(a - page) + i;
                if (compare && old.size() == 512 && old[256 + off] && old[off] != b[i])
                    modified.insert(a + i);
                v[off] = b[i];
                v[256 + off] = 1;
            }
            while (cache.size() > 128) {
                auto it = cache.begin();
                if (it.key() == page)
                    ++it;
                requested.remove(it.key());
                cache.erase(it);
            }
            if (rows)
                emit dataChanged(index(0, 0), index(rows - 1, 1));
        });
    }
}
int MemoryModel::byte(quint64 a) const {
    if (!validAddress(a))
        return -1;
    quint64 page = a & ~quint64(255);
    if (!cache.contains(page) && !requested.contains(page))
        QTimer::singleShot(0, const_cast<MemoryModel *>(this), [this, page] {
            const_cast<MemoryModel *>(this)->fetch(page);
        });
    auto it = cache.constFind(page);
    int off = int(a - page);
    return it != cache.cend() && it->size() == 512 && (*it)[256 + off] ? uchar((*it)[off]) : -1;
}

static QString asciiText(int value) {
    if (value == 0)
        return "\\0";
    if (value == 9)
        return "\\t";
    if (value == 10)
        return "\\n";
    if (value == 13)
        return "\\r";
    if (value == 32)
        return QString(QChar(0x2420)); // Visible space.
    if (value >= 33 && value <= 126)
        return QString(QChar(value));
    return QString(QChar(0x00b7)); // Control bytes and values outside ASCII.
}

QString MemoryModel::byteText(quint64 address) const {
    const int value = byte(address);
    if (value < 0)
        return "??";
    return showsAscii(address) ? asciiText(value)
                               : QString("%1").arg(value, 2, 16, QChar('0')).toUpper();
}

QString MemoryModel::byteTooltip(quint64 address) const {
    const int value = byte(address);
    if (value < 0)
        return {};
    QString ascii;
    if (value == 32)
        ascii = "SPACE";
    else if (value == 127)
        ascii = "DEL (control character)";
    else if (value >= 128)
        ascii = "outside ASCII (0–127)";
    else if (value < 32 && value != 0 && value != 9 && value != 10 && value != 13)
        ascii = "control character";
    else
        ascii = asciiText(value);
    QString text = QString("0x%1 · hex %2 · decimal %3 · ASCII %4\nClick to show %5.")
                       .arg(address, 16, 16, QChar('0'))
                       .arg(QString("%1").arg(value, 2, 16, QChar('0')).toUpper())
                       .arg(value)
                       .arg(ascii, showsAscii(address) ? "hex" : "ASCII");
    if (!symbolsAt(address).isEmpty())
        text.prepend(symbolsAt(address).join(" / ") + '\n');
    return text;
}

bool MemoryModel::toggleAscii(quint64 address) {
    if (!validAddress(address) || byte(address) < 0)
        return false;
    if (asciiBytes.contains(address))
        asciiBytes.remove(address);
    else
        asciiBytes.insert(address);
    const int row = rowForAddress(address);
    emit dataChanged(index(row, 1), index(row, 1), {Qt::DisplayRole});
    return true;
}
void MemoryModel::refresh() {
    if (mode == Stack) {
        refreshStack();
        return;
    }
    modified.clear();
    requested.clear();
    auto pages = cache.keys(); // Retain a bounded working set across steps.
    while (cache.size() > 128)
        cache.erase(cache.begin());
    for (auto p : pages)
        if (cache.contains(p))
            fetch(p, true);
    if (rows)
        emit dataChanged(index(0, 0), index(rows - 1, 1));
}
QVariant MemoryModel::data(const QModelIndex &i, int role) const {
    if (!i.isValid())
        return {};
    quint64 a = address(i.row());
    if (mode == Stack && haveStack && i.column() == 0) {
        const auto contains = [this, a](const QString &name) {
            const auto it = engine->registers.constFind(name);
            return it != engine->registers.cend() && it.value() >= a && it.value() - a < 8;
        };
        if (role == Qt::ForegroundRole && contains("rsp"))
            return themeColor("#ead55e", "#916000");
        if (role == Qt::FontRole && contains("rbp")) {
            QFont font;
            font.setUnderline(true);
            return font;
        }
        if (role == Qt::ToolTipRole) {
            QStringList pointers;
            for (const auto &name : {QString("rsp"), QString("rbp")})
                if (contains(name))
                    pointers << QString("%1 · 0x%2")
                                    .arg(name.toUpper())
                                    .arg(engine->registers.value(name), 16, 16, QChar('0'));
            return pointers.join('\n');
        }
    }
    if (role == Qt::DisplayRole) {
        if (a == ~quint64(0))
            return mode == Stack ? QString("—") : QString("…");
        if (i.column() == 0)
            return QString("0x%1 – 0x%2")
                .arg(a, 8, 16, QChar('0'))
                .arg(a + 7, 8, 16, QChar('0'))
                .toUpper()
                .replace("0X", "0x");
        QStringList values;
        for (int n = 0; n < 8; ++n) {
            if (!validAddress(a + n)) {
                values << "  ";
                continue;
            }
            values << byteText(a + n);
        }
        return values.join(' ');
    }
    return {};
}
QVariant MemoryModel::headerData(int s, Qt::Orientation o, int role) const {
    if (o == Qt::Horizontal && role == Qt::DisplayRole)
        return s == 0 ? "Address Range" : "Values · 8 bytes";
    return {};
}
QStringList MemoryModel::symbolsAt(quint64 a) const {
    if (mode == Stack || !validAddress(a))
        return {};
    return engine->dataSymbols.value(a);
}

QStringList MemoryModel::pointerLabels(quint64 a) const {
    QStringList labels;
    if (mode != Stack || !validAddress(a))
        return labels;
    if (engine->registers.contains("rbp") && engine->registers.value("rbp") == a)
        labels << "base";
    if (engine->registers.contains("rsp") && engine->registers.value("rsp") == a)
        labels << "stack";
    return labels;
}

QList<int> MemoryModel::annotatedRows() const {
    if (mode == Stack)
        return {};
    QSet<int> result;
    for (auto it = engine->dataSymbols.cbegin(); it != engine->dataSymbols.cend(); ++it)
        if (validAddress(it.key()))
            for (const auto &span : spans)
                if (it.key() >= span.base && (it.key() - span.base) / 8 < span.rows) {
                    result.insert(span.first + int((it.key() - span.base) / 8));
                    break;
                }
    return result.values();
}

bool MemoryModel::hasSymbols(int row) const {
    if (mode == Stack)
        return false;
    const quint64 a = address(row);
    if (a == ~quint64(0))
        return false;
    for (auto it = engine->dataSymbols.lowerBound(a);
         it != engine->dataSymbols.cend() && it.key() - a < 8; ++it)
        if (validAddress(it.key()))
            return true;
    return false;
}

QString MemoryModel::symbolTooltip(quint64 a) const {
    return symbolsAt(a).join(" / ") + QString(" · 0x%1").arg(a, 16, 16, QChar('0'));
}

int MemoryDelegate::byteWidth(const QFontMetrics &metrics, int columnWidth) {
    // Spread the eight bytes across the available column, keeping room for labels.
    const int minimum = qMax(40, metrics.horizontalAdvance("00 ") + 4);
    return qMax(minimum, (columnWidth - 20) / 8);
}

QFont MemoryDelegate::pointerLabelFont(const QFont &font) {
    QFont label = font;
    label.setPointSizeF(qMax(9.0, font.pointSizeF() * 0.65));
    label.setBold(true);
    label.setUnderline(false);
    return label;
}

int MemoryDelegate::pointerLabelHeight(const MemoryModel *model, int row, const QFont &font) {
    int lines = 0;
    const quint64 base = model->address(row);
    if (base == ~quint64(0))
        return 0;
    for (int n = 0; n < 8; ++n)
        lines = qMax(lines, int(model->pointerLabels(base + n).size()));
    return lines ? lines * (QFontMetrics(pointerLabelFont(font)).height() + 1) + 2 : 0;
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
    p->fillRect(o.rect, themeColor("#101820", "#ffffff"));
    p->setFont(o.font);
    const bool annotated = m->hasSymbols(i.row());
    const int labelHeight = annotated ? o.fontMetrics.height() + 2 : 0;
    const int footerHeight = pointerLabelHeight(m, i.row(), o.font);
    if (i.column() == 0) {
        const auto foreground = m->data(i, Qt::ForegroundRole);
        p->setPen(foreground.isValid() ? foreground.value<QColor>()
                                       : themeColor("#99b6d6", "#344a60"));
        if (m->data(i, Qt::FontRole).value<QFont>().underline()) {
            QFont font = o.font;
            font.setUnderline(true);
            p->setFont(font);
        }
        p->drawText(o.rect.adjusted(10, labelHeight, 0, -footerHeight), Qt::AlignVCenter,
                    m->data(i).toString());
    } else {
        const int width = byteWidth(o.fontMetrics, o.rect.width());
        const int x = o.rect.x() + 10;
        for (int n = 0; n < 8; ++n) {
            if (!m->validAddress(a + n))
                continue;
            const QRect r(x + n * width, o.rect.y() + labelHeight + 1, width - 2,
                          o.rect.height() - labelHeight - footerHeight - 2);
            p->fillRect(r, m->sectionColor(a + n));
            if (m->current(a + n)) {
                p->setPen(themeColor("#ead55e", "#916000"));
                p->drawRect(r.adjusted(0, 0, -1, -1));
            }
            p->setFont(o.font);
            p->setPen(m->changed(a + n) ? themeColor("#64f0a0", "#006b35")
                                        : themeColor("#e0e8f1", "#17212b"));
            p->drawText(r, Qt::AlignCenter, m->byteText(a + n));
            if (m->changed(a + n))
                p->fillRect(r.adjusted(2, r.height() - 3, -2, 0), themeColor("#64f0a0", "#006b35"));

            const auto pointers = m->pointerLabels(a + n);
            if (!pointers.isEmpty()) {
                const QFont font = pointerLabelFont(o.font);
                const int lineHeight = QFontMetrics(font).height() + 1;
                p->setFont(font);
                for (int line = 0; line < pointers.size(); ++line) {
                    p->setPen(pointers[line] == "stack" ? themeColor("#ead55e", "#916000")
                                                        : themeColor("#b9cce0", "#344a60"));
                    const QRect label(r.x(), r.bottom() + 2 + line * lineHeight, r.width(),
                                      lineHeight);
                    p->drawText(label, Qt::AlignHCenter | Qt::AlignVCenter, pointers[line]);
                }
            }

            const auto names = m->symbolsAt(a + n);
            if (!names.isEmpty()) {
                int next = n + 1;
                while (next < 8 && m->symbolsAt(a + next).isEmpty())
                    ++next;
                QFont labelFont = o.font;
                if (labelFont.pointSizeF() > 1)
                    labelFont.setPointSizeF(labelFont.pointSizeF() - 1);
                p->setFont(labelFont);
                p->setPen(themeColor("#a8d8c3", "#205e40"));
                const QRect label(x + n * width + 2, o.rect.y(), (next - n) * width - 4,
                                  labelHeight);
                p->drawText(label, Qt::AlignLeft | Qt::AlignVCenter,
                            QFontMetrics(labelFont).elidedText(names.join(" / "), Qt::ElideRight,
                                                               label.width()));
            }
        }
    }
    p->restore();
}

bool MemoryDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                 const QStyleOptionViewItem &option, const QModelIndex &index) {
    if (event->type() != QEvent::MouseButtonRelease || index.column() != 1)
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    auto mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton)
        return false;
    auto memory = static_cast<MemoryModel *>(model);
    const auto base = memory->address(index.row());
    if (base == ~quint64(0))
        return false;
    const int width = byteWidth(option.fontMetrics, option.rect.width());
    const int x = mouse->position().toPoint().x() - option.rect.x() - 10;
    const int offset = x / width;
    if (x < 0 || offset >= 8)
        return false;
    const int header = memory->hasSymbols(index.row()) ? option.fontMetrics.height() + 2 : 0;
    const int footer = pointerLabelHeight(memory, index.row(), option.font);
    const QRect byteRect(option.rect.x() + 10 + offset * width, option.rect.y() + header + 1,
                         width - 2, option.rect.height() - header - footer - 2);
    return byteRect.contains(mouse->position().toPoint()) && memory->toggleAscii(base + offset);
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
        const bool symbolHeader =
            model->hasSymbols(index.row()) &&
            event->pos().y() < option.rect.y() + option.fontMetrics.height() + 2;
        if (symbolHeader)
            while (symbolByte > 0 && model->symbolsAt(base + symbolByte).isEmpty())
                --symbolByte;
        if (symbolHeader && !model->symbolsAt(base + symbolByte).isEmpty()) {
            QToolTip::showText(event->globalPos(),
                               model->symbolTooltip(base + symbolByte).toHtmlEscaped(), view);
            return true;
        }
        const int header = model->hasSymbols(index.row()) ? option.fontMetrics.height() + 2 : 0;
        const int footer = pointerLabelHeight(model, index.row(), option.font);
        if (event->pos().y() >= option.rect.y() + header + 1 &&
            event->pos().y() < option.rect.bottom() - footer) {
            const auto text = model->byteTooltip(base + byte);
            if (!text.isEmpty()) {
                QToolTip::showText(event->globalPos(),
                                   "<qt>" + text.toHtmlEscaped().replace('\n', "<br>") + "</qt>",
                                   view);
                return true;
            }
        }
    }
    QToolTip::hideText();
    return true;
}

void MemoryModel::refreshStack() {
    if (!engine->paused() || !engine->registers.contains("rsp"))
        return;
    ++generation;
    requested.clear();
    modified.clear();
    if (!haveStack) {
        beginResetModel();
        // A virtual 128 KiB window, loaded only as it is visited. Keep addresses
        // stable during the run so scrolling and PUSH/POP can be compared in place.
        const quint64 rsp = engine->registers["rsp"] & ~quint64(7);
        stackBase = rsp >= 65536 ? rsp - 65536 : 0;
        const quint64 last = rsp <= ~quint64(0) - 65535 ? rsp + 65535 : ~quint64(0);
        rows = int((last - stackBase) / 8 + 1);
        haveStack = true;
        endResetModel();
    }
    const auto pages = cache.keys();
    for (auto page : pages)
        if (cache.contains(page))
            fetch(page, true);
    const quint64 rsp = engine->registers["rsp"];
    if (validAddress(rsp))
        fetch(rsp & ~quint64(255), true);
    emit dataChanged(index(0, 0), index(rows - 1, 1));
}

void MemoryModel::fetchStack(quint64 page, bool compare) {
    const int epoch = generation;
    const auto previous = cache.value(page);
    // Reset validity while refreshing: unreadable memory must not retain old values.
    cache[page] = QByteArray(512, char(0));
    auto install = [this, previous, page, epoch, compare](quint64 address, const QByteArray &data) {
        if (epoch != generation)
            return;
        auto &bytes = cache[page];
        if (bytes.isEmpty())
            bytes = QByteArray(512, char(0));
        for (int n = 0; n < data.size(); ++n) {
            const quint64 a = address + n;
            if (a < page || a - page >= 256)
                break;
            if (!validAddress(a))
                continue;
            const int offset = int(a - page);
            const uchar value = uchar(data[n]);
            if (compare && previous.size() == 512 && previous[256 + offset] &&
                uchar(previous[offset]) != value)
                modified.insert(a);
            bytes[offset] = char(value);
            bytes[256 + offset] = 1;
        }
        while (cache.size() > 128) {
            auto it = cache.begin();
            if (it.key() == page)
                ++it;
            requested.remove(it.key());
            cache.erase(it);
        }
        const int first = rowForAddress(qMax(page, stackBase));
        const int last = rowForAddress(qMin(page + 255, stackBase + quint64(rows) * 8 - 1));
        if (first >= 0 && last >= first)
            emit dataChanged(index(first, 0), index(last, 1));
    };
    // Retry one row at a time when a page crosses an unreadable mapping boundary.
    engine->readMemory(page, 256, [this, page, epoch, install](QByteArray data) {
        if (epoch != generation)
            return;
        if (data.size() == 256) {
            install(page, data);
            return;
        }
        for (int row = 0; row < 32; ++row) {
            const quint64 address = page + row * 8;
            if (!validAddress(address))
                continue;
            engine->readMemory(address, 8, [install, address](QByteArray bytes) {
                install(address, bytes);
            });
        }
    });
}
