#pragma once
#include <QMap>
#include <QPoint>
#include <QVariantAnimation>
#include <QWidget>
#include <functional>
#include <optional>

struct MoveOperand {
    enum Kind { Register, Memory, Immediate } kind;
    QString reg;
    int bits = 0;
    int shift = 0;
    quint64 address = 0;
    quint64 immediate = 0;
};

struct MoveTransfer {
    MoveOperand source, destination;
    quint64 rip = 0;
    int instructionSize = 0;
    int sourceLine = 0;
    std::optional<quint64> sourceValue;
    std::optional<quint64> destinationValue;
    bool addition = false;
};

std::optional<MoveTransfer> decodeMov(const QString &instruction,
                                    const QMap<QString, quint64> &registers,
                                    quint64 rip, int instructionSize, int line);
std::optional<MoveTransfer> decodeAdd(const QString &instruction,
                                    const QMap<QString, quint64> &registers,
                                    quint64 rip, int instructionSize, int line);
quint64 moveMask(int bits);
quint64 moveRegisterValue(const MoveOperand &operand, const QMap<QString, quint64> &registers);

class MoveOverlay : public QWidget {
public:
    explicit MoveOverlay(QWidget *parent);
    void start(const QString &value, QPoint source, QPoint destination, std::function<void()> finished, const QString &operation = "MOV");
    void cancel();
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QVariantAnimation animation;
    QPoint source, destination;
    QString value;
    qreal progress = 0;
    std::function<void()> finished;
};
