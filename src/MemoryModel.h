#pragma once
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QSet>
#include "DebuggerEngine.h"
class MemoryModel:public QAbstractTableModel {
    Q_OBJECT
public:
    enum Mode { Sections, Stack };
    explicit MemoryModel(DebuggerEngine *engine,QObject *parent=nullptr, Mode mode=Sections);
    int rowCount(const QModelIndex &p={}) const override;
    int columnCount(const QModelIndex & ={}) const override {return 2;}
    QVariant data(const QModelIndex &,int role=Qt::DisplayRole) const override;
    QVariant headerData(int,Qt::Orientation,int) const override;
    void resetSections(); void refresh();
    quint64 address(int row) const;
    int rowForAddress(quint64 address) const;
    QColor sectionColor(quint64) const;
    bool validAddress(quint64) const;
    int byte(quint64) const;
    QStringList symbolsAt(quint64 address) const;
    QList<int> annotatedRows() const;
    bool hasSymbols(int row) const;
    QString symbolTooltip(quint64 address) const;
    bool changed(quint64 a) const {return modified.contains(a);}
    bool current(quint64 a) const {return a>=engine->rip && a<engine->rip+engine->instructionSize;}
private:
    struct Span {quint64 base,rows; int first;}; QList<Span> spans; int rows=0,generation=0;
    DebuggerEngine *engine;
    Mode mode;
    quint64 stackBase = 0;
    bool haveStack = false;
    QMap<quint64, uchar> stackBytes;
    void refreshStack();
    mutable QMap<quint64,QByteArray> cache;
    mutable QSet<quint64> requested;
    QSet<quint64> modified;
    void fetch(quint64 page,bool compare=false);
};
class MemoryDelegate:public QStyledItemDelegate {
public: using QStyledItemDelegate::QStyledItemDelegate;
    static int byteWidth(const QFontMetrics &metrics, int columnWidth);
    void paint(QPainter *,const QStyleOptionViewItem &,const QModelIndex &) const override;
    bool helpEvent(QHelpEvent *, QAbstractItemView *, const QStyleOptionViewItem &, const QModelIndex &) override;
};
