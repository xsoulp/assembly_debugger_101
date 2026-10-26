#pragma once
#include <QAbstractTableModel>
#include <QSet>
class RegisterModel:public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;
    QStringList names={"rax","rbx","rcx","rdx","rsi","rdi","rbp","rsp","rip"};
    QMap<QString,quint64> values; QSet<QString> changed;
    int rowCount(const QModelIndex &p={})const override{return p.isValid()?0:9;}
    int columnCount(const QModelIndex & ={})const override{return 3;}
    QVariant data(const QModelIndex &,int role=Qt::DisplayRole)const override;
    QVariant headerData(int,Qt::Orientation,int)const override;
    void updateValues(QMap<QString,quint64> next);
    void setSignedDecimal(bool enabled);
private:
    bool signedDecimal = false;
};
