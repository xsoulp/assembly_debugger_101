#include "MainWindow.h"
#include "MemoryModel.h"
#include "RegisterModel.h"
#include "Theme.h"
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <functional>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    applyTheme(app);
    MainWindow window;
    window.show();
    auto require = [](bool ok, const QString &message) {
        if (!ok) {
            qCritical().noquote() << message;
            std::exit(1);
        }
    };
    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        for (auto widget : app.topLevelWidgets())
            if (auto box = qobject_cast<QMessageBox *>(widget))
                require(false, box->text());
    });
    watchdog.start(100);
    auto wait = [&](std::function<bool()> condition) {
        QElapsedTimer timer;
        timer.start();
        while (!condition() && timer.elapsed() < 5000)
            QTest::qWait(10);
        return condition();
    };
    auto action = [&](const QString &name) -> QAction * {
        for (auto a : window.findChildren<QAction *>())
            if (a->text() == name)
                return a;
        return nullptr;
    };
    auto stack = window.findChild<QTableView *>("stackView");
    auto ram = window.findChild<QTableView *>("ramView");
    auto stackModel = static_cast<MemoryModel *>(stack->model());
    auto memory = static_cast<MemoryModel *>(ram->model());
    RegisterModel *registers = nullptr;
    QTableView *registerView = nullptr;
    for (auto table : window.findChildren<QTableView *>())
        if (auto model = dynamic_cast<RegisterModel *>(table->model())) {
            registers = model;
            registerView = table;
        }
    auto overlay = window.findChild<QWidget *>("movOverlay");
    require(registers && overlay, "Missing register view or overlay");
    action("Build")->trigger();
    require(wait([&] {
                return action("Start")->isEnabled();
            }),
            "Build failed");
    action("Start")->trigger();
    require(wait([&] {
                return action("Step")->isEnabled();
            }),
            "Start failed");
    const quint64 rsp = registers->values["rsp"];
    require(wait([&] {
                return stackModel->byte(rsp - 8) >= 0;
            }),
            "Stack did not load");
    QMap<QString, quint64> symbols;
    for (int row : memory->annotatedRows())
        for (int n = 0; n < 8; ++n)
            for (const auto &name : memory->symbolsAt(memory->address(row) + n))
                symbols[name] = memory->address(row) + n;
    require(symbols.size() == 4, "Missing memory operands");

    struct Endpoint {
        QString reg;
        quint64 address = 0;
    };
    auto reg = [](const QString &name) {
        return Endpoint{name};
    };
    auto mem = [](quint64 address) {
        return Endpoint{{}, address};
    };
    auto checkEndpoint = [&](Endpoint endpoint, const QPoint &point) {
        if (!endpoint.reg.isEmpty()) {
            const int row = registers->names.indexOf(endpoint.reg);
            return registerView->visualRect(registers->index(row, 1))
                .contains(registerView->viewport()->mapFrom(&window, point));
        }
        const bool inRam = memory->validAddress(endpoint.address);
        auto model = inRam ? memory : stackModel;
        auto view = inRam ? ram : stack;
        const int row = model->rowForAddress(endpoint.address);
        const QRect rect = view->visualRect(model->index(row, 1));
        const int width = MemoryDelegate::byteWidth(view->fontMetrics(), rect.width());
        const int x = rect.x() + 10 + int(endpoint.address - model->address(row)) * width;
        const QPoint local = view->viewport()->mapFrom(&window, point);
        return rect.contains(local) && local.x() >= x && local.x() < x + width;
    };
    int stepNumber = 0;
    auto step = [&](const QString &operation = {}, quint64 value = 0, int bits = 64,
                    Endpoint from = {}, Endpoint to = {}, quint64 expectedRsp = 0,
                    bool immediate = false) {
        ++stepNumber;
        action("Step")->trigger();
        if (!operation.isEmpty()) {
            require(wait([&] {
                        return overlay->isVisible();
                    }),
                    QString("Animation missing at step %1 (%2)").arg(stepNumber).arg(operation));
            require(overlay->property("operation").toString() == operation, "Wrong operation");
            const QString text =
                QString("0x%1").arg(value, bits / 4, 16, QChar('0')).toUpper().replace("0X", "0x");
            require(overlay->property("value").toString() == text,
                    QString("Wrong value at step %1: expected %2").arg(stepNumber).arg(text));
            require(checkEndpoint(from, overlay->property("source").toPoint()) &&
                        checkEndpoint(to, overlay->property("destination").toPoint()),
                    QString("Wrong endpoints at step %1").arg(stepNumber));
            require(!action("Step")->isEnabled(), "Step enabled during animation");
            require(registers->values["rsp"] == expectedRsp, "Incorrect RSP after transfer");
        }
        bool sawOverlay = false;
        require(wait([&] {
                    sawOverlay |= overlay->isVisible();
                    return action("Step")->isEnabled();
                }),
                "Step did not finish");
        if (immediate) {
            require(!sawOverlay, "Immediate PUSH displayed an overlay");
            require(registers->changed.contains("rsp"), "Immediate PUSH did not highlight RSP");
            const quint64 address = registers->values["rsp"];
            require(wait([&] {
                        return stackModel->changed(address);
                    }),
                    "Immediate PUSH did not highlight the changed stack byte");
        }
    };
    constexpr quint64 value = 0x1122334455667788ULL;
    step(); // mov rbp, rsp
    step(); // mov rax, value
    step("PUSH", 0x7788, 16, reg("rax"), mem(rsp - 2), rsp - 2);
    step("POP", 0x7788, 16, mem(rsp - 2), reg("rbx"), rsp);
    step("PUSH", value, 64, reg("rax"), mem(rsp - 8), rsp - 8);
    step("POP", value, 64, mem(rsp - 8), reg("rdx"), rsp);
    step("PUSH", 0x83a5, 16, mem(symbols["word_value"]), mem(rsp - 2), rsp - 2);
    step("POP", 0x83a5, 16, mem(rsp - 2), mem(symbols["word_out"]), rsp);
    step("PUSH", 0xfedcba9876543210ULL, 64, mem(symbols["qword_value"]), mem(rsp - 8), rsp - 8);
    step("POP", 0xfedcba9876543210ULL, 64, mem(rsp - 8), mem(symbols["qword_out"]), rsp);
    step("PUSH", value, 64, reg("rax"), mem(rsp - 8), rsp - 8);
    step("PUSH", value, 64, mem(rsp - 8), mem(rsp - 16), rsp - 16);
    step("POP", value, 64, mem(rsp - 16), reg("rcx"), rsp - 8);
    step("POP", value, 64, mem(rsp - 8), reg("rdx"), rsp);
    step("PUSH", value, 64, reg("rax"), mem(rsp - 8), rsp - 8);
    step("PUSH", 0x7788, 64, reg("rbx"), mem(rsp - 16), rsp - 16);
    step("POP", 0x7788, 64, mem(rsp - 16), mem(rsp - 8), rsp - 8);
    step("POP", 0x7788, 64, mem(rsp - 8), reg("rcx"), rsp);
    step("PUSH", rsp, 64, reg("rsp"), mem(rsp - 8), rsp - 8);
    step("POP", rsp, 64, mem(rsp - 8), reg("rsp"), rsp);
    step("PUSH", rsp & 0xffff, 16, reg("rsp"), mem(rsp - 2), rsp - 2);
    step("POP", rsp & 0xffff, 16, mem(rsp - 2), reg("rsp"), rsp);
    step(); // mov r15, value
    step("PUSH", 0xaabbccddeeff1234ULL, 64, reg("r15"), mem(rsp - 8), rsp - 8);
    step("POP", 0xaabbccddeeff1234ULL, 64, mem(rsp - 8), reg("r8"), rsp);
    step("PUSH", 0x1234, 16, reg("r8"), mem(rsp - 2), rsp - 2);
    step("POP", 0x1234, 16, mem(rsp - 2), reg("r15"), rsp);
    step("PUSH", 0x7788, 16, reg("rax"), mem(rsp - 2), rsp - 2);
    step("POP", 0x7788, 16, mem(rsp - 2), reg("rbx"), rsp);
    step({}, 0, 16, {}, {}, rsp - 2, true); // push word -7
    step("POP", 0xfff9, 16, mem(rsp - 2), reg("rax"), rsp);
    step({}, 0, 64, {}, {}, rsp - 8, true); // push dword -1
    step("POP", ~quint64(0), 64, mem(rsp - 8), reg("rcx"), rsp);
    step({}, 0, 64, {}, {}, rsp - 8, true); // push byte 5
    step("POP", 5, 64, mem(rsp - 8), reg("rdx"), rsp);
    action("Stop")->trigger();
    require(wait([&] {
                return action("Start")->isEnabled();
            }),
            "Stop failed");
    qInfo() << "PASS: word/qword PUSH/POP, registers, memory, RSP addressing, extended registers, "
               "and immediate highlights without overlays";
}
