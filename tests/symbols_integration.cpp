#include "MainWindow.h"
#include "MemoryModel.h"
#include "RegisterModel.h"
#include "Theme.h"
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QHelpEvent>
#include <QHeaderView>
#include <QMessageBox>
#include <QLabel>
#include <QCheckBox>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QToolTip>
#include <functional>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    applyTheme(app);
    MainWindow window;
    window.show();
    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        for (auto widget : app.topLevelWidgets())
            if (auto box = qobject_cast<QMessageBox *>(widget)) {
                qCritical() << box->text();
                std::exit(1);
            }
    });
    watchdog.start(100);
    auto require = [](bool ok, const char *message) {
        if (!ok) { qCritical() << message; std::exit(1); }
    };
    auto wait = [&](std::function<bool()> condition) {
        QElapsedTimer timer;
        timer.start();
        while (!condition() && timer.elapsed() < 7000) QTest::qWait(10);
        return condition();
    };
    auto action = [&](const QString &name) -> QAction * {
        for (auto a : window.findChildren<QAction *>())
            if (a->text() == name) return a;
        return nullptr;
    };
    // Addressing and widths come from GDB's Intel disassembly and the stopped registers.
    const QMap<QString, quint64> registers = {{"rax", 0x1122334455667788ULL}, {"rbx", 0x402000}, {"rcx", 3}};
    const auto indexed = decodeMov("mov al,BYTE PTR [rbx+rcx*2-0x1]", registers, 0x401000, 4, 1);
    require(indexed && indexed->source.address == 0x402005 && indexed->destination.bits == 8,
            "Incorrect indexed effective address");
    const auto highByte = decodeMov("mov BYTE PTR ds:0x402008,ah", registers, 0x401000, 6, 1);
    require(highByte && highByte->sourceValue == 0x77, "Incorrect AH source byte");
    const auto relative = decodeMov("mov eax,DWORD PTR [rip+0xffa] # 0x402000", registers, 0x401000, 6, 1);
    require(relative && relative->source.address == 0x402000 && relative->destination.bits == 32,
            "Incorrect RIP-relative address");
    const auto negative = decodeMov("mov eax,0xffffffff", registers, 0x401000, 5, 1);
    require(negative && negative->sourceValue == 0xffffffffULL, "Incorrect immediate width");
    require(!decodeMov("mov rax,QWORD PTR fs:0x28", registers, 0x401000, 9, 1) &&
            !decodeMov("add al,0x1", registers, 0x401000, 2, 1), "Unsupported instruction was animated");

    MemoryModel *memory = nullptr;
    QTableView *ram = nullptr;
    RegisterModel *registerModel = nullptr;
    for (auto table : window.findChildren<QTableView *>()) {
        if (auto model = dynamic_cast<RegisterModel *>(table->model())) registerModel = model;
        if (auto model = dynamic_cast<MemoryModel *>(table->model()); model && table->objectName() == "ramView") {
            memory = model;
            ram = table;
        }
    }
    require(memory && ram && registerModel, "Missing debugger panels");
    auto authorship = window.findChild<QLabel *>("authorship");
    require(authorship && authorship->isVisible() && authorship->text() == "Luis Pinto · @xsoulp · 2026",
            "Missing authorship signature");
    RegisterModel parts;
    parts.updateValues({{"rax", 0xffffffffffffffffULL}, {"rbx", 0x100}, {"rcx", 0x1234}, {"rdx", 0xabcd}});
    require(parts.data(parts.index(0, 2)).toString() == "AX:65535   AH:255 AL:255" &&
            parts.data(parts.index(1, 2)).toString() == "BX:256   BH:1 BL:0" &&
            parts.data(parts.index(2, 2)).toString() == "CX:4660   CH:18 CL:52" &&
            parts.data(parts.index(3, 2)).toString() == "DX:43981   DH:171 DL:205",
            "Incorrect unsigned decimal register parts");
    parts.setSignedDecimal(true);
    require(parts.data(parts.index(0, 2)).toString() == "AX:-1   AH:-1 AL:-1" &&
            parts.data(parts.index(1, 2)).toString() == "BX:256   BH:1 BL:0" &&
            parts.data(parts.index(3, 2)).toString() == "DX:-21555   DH:-85 DL:-51",
            "Incorrect signed byte or word interpretation");
    parts.updateValues({{"rax", 0x800080}, {"rbx", 0x8000}});
    require(parts.data(parts.index(0, 2)).toString() == "AX:128   AH:0 AL:-128" &&
            parts.data(parts.index(1, 2)).toString() == "BX:-32768   BH:-128 BL:0",
            "Incorrect signed minimum boundaries");
    action("Build")->trigger();
    require(wait([&] { return action("Start")->isEnabled(); }), "Build failed");
    action("Start")->trigger();
    require(wait([&] { return action("Step")->isEnabled(); }), "Start failed");

    QMap<QString, quint64> addresses;
    for (int row : memory->annotatedRows()) {
        require(ram->rowHeight(row) > ram->verticalHeader()->defaultSectionSize(), "No room for labels");
        for (int n = 0; n < 8; ++n) {
            const auto a = memory->address(row) + n;
            for (const auto &name : memory->symbolsAt(a)) addresses[name] = a;
        }
    }
    require(addresses.size() == 4 && addresses.contains("v1") && addresses.contains("v2") &&
            addresses.contains("v3") && addresses.contains("resultado"), "Missing or spurious variable names");
    require(addresses["v2"] == addresses["v1"] + 1 && addresses["v3"] == addresses["v1"] + 2,
            "Names do not match consecutive variable bytes");
    require(wait([&] {
        return memory->byte(addresses["v1"]) == 0x28 && memory->byte(addresses["v2"]) == 0x64 &&
               memory->byte(addresses["v3"]) == 0x50 && memory->byte(addresses["resultado"]) == 0;
    }), "Incorrect actual variable memory");

    int row = -1;
    for (int r : memory->annotatedRows())
        if (addresses["v2"] >= memory->address(r) && addresses["v2"] - memory->address(r) < 8) row = r;
    require(row >= 0, "Missing v2 row");
    ram->scrollTo(memory->index(row, 1));
    QTest::qWait(30);
    const QRect rect = ram->visualRect(memory->index(row, 1));
    const int width = MemoryDelegate::byteWidth(ram->fontMetrics(), rect.width());
    const QPoint point(rect.x() + 10 + int(addresses["v2"] - memory->address(row)) * width + 5,
                       rect.y() + 5);
    QHelpEvent help(QEvent::ToolTip, point, ram->viewport()->mapToGlobal(point));
    QApplication::sendEvent(ram->viewport(), &help);
    require(QToolTip::text().contains("v2") &&
            QToolTip::text().contains(QString("0x%1").arg(addresses["v2"], 16, 16, QChar('0'))),
            "Tooltip does not identify hovered symbol and exact address");
    QToolTip::hideText();
    window.grab().save("/tmp/asm101-variables.png");

    auto overlay = window.findChild<QWidget *>("movOverlay");
    require(overlay, "Missing MOV overlay");
    for (int step = 0; step < 7; ++step) {
        action("Step")->trigger();
        const QString expected = step == 1 ? "0x28" : step == 2 ? "+ 0x64" : step == 3 ? "+ 0x50" : step == 4 ? "0x03" : step == 6 ? "0x49" : "";
        if (!expected.isEmpty()) {
            require(wait([&] { return overlay->isVisible(); }), "Operand animation did not start");
            require(!action("Step")->isEnabled(), "Step enabled during animation");
            require(overlay->property("value").toString() == expected, "Animated value differs from actual operand");
            require(overlay->property("operation").toString() == ((step == 2 || step == 3) ? "ADD" : "MOV"),
                    "Wrong animated operation");
            const QPoint from = overlay->property("source").toPoint();
            const QPoint to = overlay->property("destination").toPoint();
            require(step >= 1 && step <= 3 ? from.x() > to.x() : step == 6 ? from.x() < to.x() : to.y() > from.y(),
                    "Animation moves in the wrong direction");
            if (step == 1) {
                const QPoint original = overlay->property("source").toPoint();
                action("Step")->trigger(); // A disabled action must not execute another instruction.
                QTest::qWait(180);
                require(overlay->isVisible() && overlay->property("source").toPoint() == original,
                        "Reentrant step altered active animation");
                window.grab().save("/tmp/asm101-mov.png");
            }
        }
        if (step == 2) {
            QTest::qWait(180);
            window.grab().save("/tmp/asm101-add.png");
        }
        require(wait([&] { return action("Step")->isEnabled(); }), "Step failed");
        require(!overlay->isVisible(), "Overlay left visible after completion");
        if (step == 2)
            require(registerModel->data(registerModel->index(0, 2)).toString().startsWith("AX:140   AH:0 AL:140"),
                    "ADD result does not match decimal register parts");
        if (step == 2) {
            auto toggle = window.findChild<QCheckBox *>("signedDecimalToggle");
            require(toggle && !toggle->isChecked(), "Missing unsigned/signed toggle");
            const auto actualRegisters = registerModel->values;
            const QString originalHex = registerModel->data(registerModel->index(0, 1)).toString();
            toggle->click();
            require(toggle->text() == "Signed" &&
                    registerModel->data(registerModel->index(0, 2)).toString() == "AX:140   AH:0 AL:-116",
                    "Signed toggle did not reinterpret AL");
            require(registerModel->values == actualRegisters &&
                    registerModel->data(registerModel->index(0, 1)).toString() == originalHex,
                    "Decimal toggle changed the actual register or hexadecimal display");
            window.grab().save("/tmp/asm101-signed.png");
            toggle->click();
            require(toggle->text() == "Unsigned" &&
                    registerModel->data(registerModel->index(0, 2)).toString() == "AX:140   AH:0 AL:140",
                    "Unsigned toggle did not restore decimal display");
        }
    }
    require(wait([&] { return memory->byte(addresses["resultado"]) == 0x49; }), "Missing division result");
    require(registerModel->data(registerModel->index(0, 2)).toString() == "AX:329   AH:1 AL:73",
            "Incorrect decimal AL/AH/AX after DIV");
    require(registerModel->data(registerModel->index(1, 2)).toString() == "BX:3   BH:0 BL:3",
            "Incorrect decimal BL/BH/BX");
    require(memory->changed(addresses["resultado"]) &&
            memory->symbolsAt(addresses["resultado"]).contains("resultado"), "Label or changed-byte highlight lost");
    action("Stop")->trigger();
    require(wait([&] { return action("Start")->isEnabled(); }), "Stop failed");
    action("Start")->trigger();
    require(wait([&] { return action("Step")->isEnabled(); }), "Restart failed");
    require(memory->symbolsAt(addresses["v1"]) == QStringList{"v1"}, "Duplicate label after restart");
    action("Step")->trigger(); // xor ax, ax
    require(wait([&] { return action("Step")->isEnabled(); }), "Restarted step failed");
    action("Step")->trigger(); // mov al, [v1]
    require(wait([&] { return overlay->isVisible(); }), "Restarted animation missing");
    action("Stop")->trigger();
    require(!overlay->isVisible(), "Stop did not cancel animation");
    require(wait([&] { return action("Start")->isEnabled(); }), "Stop failed");
    qInfo() << "PASS: variable labels, tooltips, actual MOV/ADD operands and direction, decimal register parts, authorship, step locking, stop during animation, restart";
    return 0;
}
