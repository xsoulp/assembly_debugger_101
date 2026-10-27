#include "RegisterModel.h"
#include "Theme.h"
#include <QColor>

QVariant RegisterModel::data(const QModelIndex &index, int role) const {
    const QString name = names[index.row()];
    if (role == Qt::DisplayRole) {
        if (index.column() == 0)
            return name.toUpper();
        if (index.column() == 2) {
            if (index.row() >= 4) return QString();
            if (!values.contains(name)) return QString("—");
            const QString family = name.mid(1, 1).toUpper();
            const quint64 value = values[name];
            auto decimal = [this](quint64 raw, int bits) -> qint64 {
                const qint64 part = qint64(raw & ((quint64(1) << bits) - 1));
                return signedDecimal && (part & (qint64(1) << (bits - 1)))
                    ? part - (qint64(1) << bits) : part;
            };
            // Reserve five word digits plus a sign; byte fields also include room for a sign.
            return QString("%1X:%2  %1H:%3  %1L:%4")
                .arg(family).arg(decimal(value, 16), 6)
                .arg(decimal(value >> 8, 8), 4).arg(decimal(value, 8), 4);
        }
        if (!values.contains(name))
            return QString("—");

        QString hex = QString("%1").arg(values[name], 16, 16, QChar('0')).toUpper();
        if (name == "rax" || name == "rbx" || name == "rcx" || name == "rdx") {
            // Separate the low 8, 16 and 32 bits, counting from the right.
            hex.insert(14, ' ');
            hex.insert(12, ' ');
            hex.insert(8, ' ');
        }
        return "0x" + hex;
    }
    if (role == Qt::ForegroundRole)
        return (name == "rip" ? themeColor("#f4d65e", "#775100") : themeColor("#c9d9e9", "#17212b"));
    if (role == Qt::BackgroundRole && changed.contains(name))
        return themeColor("#194630", "#d5efdf");
    return {};
}

QVariant RegisterModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        const QStringList headers = {"Register", "Value (hex)", signedDecimal ? "Parts (signed dec)" : "Parts (unsigned dec)"};
        return headers.value(section);
    }
    if (orientation == Qt::Horizontal && role == Qt::ToolTipRole && section == 2)
        return signedDecimal
            ? QString("Signed decimal: low/high byte (−128–127), low word (−32768–32767).")
            : QString("Unsigned decimal: low/high byte (0–255), low word (0–65535).");
    return {};
}

void RegisterModel::updateValues(QMap<QString, quint64> next) {
    changed.clear();
    for (auto name : names)
        if (values.contains(name) && values[name] != next[name])
            changed.insert(name);
    values = next;
    emit dataChanged(index(0, 0), index(8, 2));
}

void RegisterModel::setSignedDecimal(bool enabled) {
    if (signedDecimal == enabled) return;
    signedDecimal = enabled;
    emit dataChanged(index(0, 2), index(3, 2), {Qt::DisplayRole});
    emit headerDataChanged(Qt::Horizontal, 2, 2);
}
