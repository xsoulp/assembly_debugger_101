#include "MovAnimation.h"
#include "Theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

quint64 moveMask(int bits) {
    return bits == 64 ? ~quint64(0) : (quint64(1) << bits) - 1;
}

quint64 moveRegisterValue(const MoveOperand &operand, const QMap<QString, quint64> &registers) {
    return (registers.value(operand.reg) >> operand.shift) & moveMask(operand.bits);
}

static std::optional<MoveOperand> registerOperand(const QString &name) {
    const QList<QStringList> families = {
        {"rax", "eax", "ax", "al", "ah"}, {"rbx", "ebx", "bx", "bl", "bh"},
        {"rcx", "ecx", "cx", "cl", "ch"}, {"rdx", "edx", "dx", "dl", "dh"},
        {"rsi", "esi", "si", "sil"}, {"rdi", "edi", "di", "dil"},
        {"rbp", "ebp", "bp", "bpl"}, {"rsp", "esp", "sp", "spl"}
    };
    const int widths[] = {64, 32, 16, 8, 8};
    for (const auto &family : families) {
        const int index = family.indexOf(name);
        if (index >= 0) {
            MoveOperand result{MoveOperand::Register};
            result.reg = family.first();
            result.bits = widths[index];
            result.shift = index == 4 ? 8 : 0;
            return result;
        }
    }
    return {};
}

static std::optional<quint64> number(const QString &text) {
    bool ok = false;
    quint64 value = text.toULongLong(&ok, 0);
    if (!ok && text.startsWith('-')) value = quint64(text.toLongLong(&ok, 0));
    return ok ? std::optional<quint64>(value) : std::nullopt;
}

static std::optional<quint64> effectiveAddress(QString expression,
        const QMap<QString, quint64> &registers, quint64 nextRip) {
    expression.remove(' ');
    // The restricted Intel address grammar: base + index * scale + displacement.
    const QRegularExpression term("([+-]?)([a-z0-9]+)(?:\\*([1248]))?");
    int offset = 0;
    quint64 address = 0;
    while (offset < expression.size()) {
        const auto match = term.match(expression, offset, QRegularExpression::NormalMatch,
                                      QRegularExpression::AnchorAtOffsetMatchOption);
        if (!match.hasMatch() || (offset > 0 && match.captured(1).isEmpty())) return {};
        const QString atom = match.captured(2);
        std::optional<quint64> value;
        if (atom == "rip") value = nextRip;
        else if (auto reg = registerOperand(atom); reg && reg->bits == 64 && registers.contains(reg->reg))
            value = moveRegisterValue(*reg, registers);
        else value = number(atom);
        if (!value) return {};
        const quint64 scale = match.captured(3).isEmpty() ? 1 : match.captured(3).toUInt();
        if (match.captured(1) == "-") address -= *value * scale;
        else address += *value * scale;
        offset = match.capturedEnd();
    }
    return offset ? std::optional<quint64>(address) : std::nullopt;
}

static std::optional<MoveOperand> operand(QString text,
        const QMap<QString, quint64> &registers, quint64 nextRip) {
    text = text.trimmed();
    if (auto reg = registerOperand(text)) {
        if (!registers.contains(reg->reg)) return {};
        return reg;
    }
    static const QRegularExpression memory("^(byte|word|dword|qword) ptr (?:ds:)?(.+)$");
    const auto match = memory.match(text);
    if (match.hasMatch()) {
        MoveOperand result{MoveOperand::Memory};
        const QStringList sizes = {"byte", "word", "dword", "qword"};
        result.bits = 8 << sizes.indexOf(match.captured(1));
        QString address = match.captured(2).trimmed();
        if (address.startsWith('[') && address.endsWith(']'))
            address = address.mid(1, address.size() - 2);
        const auto resolved = effectiveAddress(address, registers, nextRip);
        if (!resolved) return {};
        result.address = *resolved;
        return result;
    }
    if (auto value = number(text)) {
        MoveOperand result{MoveOperand::Immediate};
        result.immediate = *value;
        return result;
    }
    return {};
}

static std::optional<MoveTransfer> decodeBinary(const QString &instruction,
        const QMap<QString, quint64> &registers, quint64 rip, int size, int line, bool addition) {
    // Decode the instruction GDB actually disassembled, including operand widths.
    QString text = instruction.toLower().section('#', 0, 0).trimmed();
    text.remove(QRegularExpression("<[^>]*>"));
    const auto match = QRegularExpression(addition ? "^add\\s+(.+),\\s*(.+)$" : "^mov\\s+(.+),\\s*(.+)$").match(text);
    if (!match.hasMatch() || size <= 0) return {};
    auto destination = operand(match.captured(1), registers, rip + size);
    auto source = operand(match.captured(2), registers, rip + size);
    if (!source || !destination || destination->kind == MoveOperand::Immediate ||
        (source->kind == MoveOperand::Memory && destination->kind == MoveOperand::Memory)) return {};
    if (source->kind == MoveOperand::Immediate) source->bits = destination->bits;
    if (source->bits != destination->bits) return {};
    MoveTransfer transfer{*source, *destination, rip, size, line, {}};
    if (source->kind == MoveOperand::Register) transfer.sourceValue = moveRegisterValue(*source, registers);
    else if (source->kind == MoveOperand::Immediate)
        transfer.sourceValue = source->immediate & moveMask(destination->bits);
    transfer.addition = addition;
    if (addition && destination->kind == MoveOperand::Register)
        transfer.destinationValue = moveRegisterValue(*destination, registers);
    return transfer;
}

std::optional<MoveTransfer> decodeMov(const QString &instruction,
        const QMap<QString, quint64> &registers, quint64 rip, int size, int line) {
    return decodeBinary(instruction, registers, rip, size, line, false);
}

std::optional<MoveTransfer> decodeAdd(const QString &instruction,
        const QMap<QString, quint64> &registers, quint64 rip, int size, int line) {
    return decodeBinary(instruction, registers, rip, size, line, true);
}

MoveOverlay::MoveOverlay(QWidget *parent) : QWidget(parent) {
    setObjectName("movOverlay");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
    animation.setDuration(450);
    animation.setStartValue(0.0);
    animation.setEndValue(1.0);
    animation.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&animation, &QVariantAnimation::valueChanged, this, [this](QVariant v) {
        progress = v.toReal();
        update();
    });
    connect(&animation, &QVariantAnimation::finished, this, [this] {
        hide();
        auto callback = std::move(finished);
        if (callback) callback();
    });
}

void MoveOverlay::start(const QString &text, QPoint from, QPoint to, std::function<void()> callback, const QString &operation) {
    cancel();
    source = from;
    destination = to;
    value = text;
    finished = std::move(callback);
    progress = 0;
    setProperty("value", value);
    setProperty("source", source);
    setProperty("destination", destination);
    setProperty("operation", operation);
    setAccessibleName(operation + " " + value);
    setGeometry(parentWidget()->rect());
    show();
    raise();
    animation.start();
}

void MoveOverlay::cancel() {
    animation.stop();
    finished = {};
    hide();
}

void MoveOverlay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF from(source), to(destination);
    const QPointF bend = (from + to) / 2 + QPointF(0, -45);
    QPainterPath path(from);
    path.quadTo(bend, to);
    auto trail = themeColor("#63d2e7", "#00617a");
    trail.setAlpha(lightTheme() ? 200 : 130);
    painter.setPen(QPen(trail, 2, Qt::DashLine));
    painter.drawPath(path);
    painter.setPen(QPen(themeColor("#70dcec", "#00617a"), 2));
    painter.drawEllipse(from, 9, 9);
    painter.setPen(QPen(themeColor("#f4d65e", "#916000"), 2));
    painter.drawEllipse(to, 11, 11);
    const qreal t = progress;
    QPointF center = (1-t)*(1-t)*from + 2*(1-t)*t*bend + t*t*to;
    const QSizeF size(fontMetrics().horizontalAdvance(value) + 24, fontMetrics().height() + 14);
    center.setX(qBound(size.width()/2, center.x(), width()-size.width()/2));
    center.setY(qBound(size.height()/2, center.y(), height()-size.height()/2));
    const QRectF box(center - QPointF(size.width()/2, size.height()/2), size);
    painter.setBrush(themeColor("#16394a", "#e0f2fa"));
    painter.setPen(QPen(themeColor("#70dcec", "#00617a"), 2));
    painter.drawRoundedRect(box, 7, 7);
    painter.setPen(themeColor("#ffffff", "#102d40"));
    painter.drawText(box, Qt::AlignCenter, value);
}
