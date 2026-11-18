#pragma once
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QSet>
#include "DebuggerEngine.h"
class MemoryModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Mode { Sections, Stack };
    explicit MemoryModel(DebuggerEngine *engine, QObject *parent = nullptr, Mode mode = Sections);
    int rowCount(const QModelIndex &p = {}) const override;
    int columnCount(const QModelIndex & = {}) const override {
        return 2;
    }
    QVariant data(const QModelIndex &, int role = Qt::DisplayRole) const override;
    QVariant headerData(int, Qt::Orientation, int) const override;
    void resetSections();
    bool sectionVisible(const QString &name) const;
    void setSectionVisible(const QString &name, bool visible);
    void refresh();
    quint64 address(int row) const;
    int rowForAddress(quint64 address) const;
    QColor sectionColor(quint64) const;
    bool validAddress(quint64) const;
    int byte(quint64) const;
    QString byteText(quint64) const;
    QString byteTooltip(quint64) const;
    bool toggleAscii(quint64);
    bool showsAscii(quint64 address) const {
        return asciiBytes.contains(address);
    }
    QStringList symbolsAt(quint64 address) const;
    QStringList pointerLabels(quint64 address) const;
    QList<int> annotatedRows() const;
    bool hasSymbols(int row) const;
    QString symbolTooltip(quint64 address) const;
    bool changed(quint64 a) const {
        return modified.contains(a);
    }
    bool current(quint64 a) const {
        return a >= engine->rip && a < engine->rip + engine->instructionSize;
    }

private:
    struct Span {
        quint64 base, rows;
        int first;
    };
    QList<Span> spans;
    int rows = 0, generation = 0;
    DebuggerEngine *engine;
    Mode mode;
    quint64 stackBase = 0;
    bool haveStack = false;
    void refreshStack();
    void fetchStack(quint64 page, bool compare);
    mutable QMap<quint64, QByteArray> cache;
    mutable QSet<quint64> requested;
    QSet<quint64> modified;
    QSet<QString> hiddenSections;
    QSet<quint64> asciiBytes;
    void rebuildSpans();
    void fetch(quint64 page, bool compare = false);
};
class MemoryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static int byteWidth(const QFontMetrics &metrics, int columnWidth);
    static QFont pointerLabelFont(const QFont &font);
    static int pointerLabelHeight(const MemoryModel *model, int row, const QFont &font);
    void paint(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const override;
    bool editorEvent(QEvent *, QAbstractItemModel *, const QStyleOptionViewItem &,
                     const QModelIndex &) override;
    bool helpEvent(QHelpEvent *, QAbstractItemView *, const QStyleOptionViewItem &,
                   const QModelIndex &) override;
};
