#include "MainWindow.h"
#include "MemoryModel.h"
#include "RegisterModel.h"
#include "Theme.h"
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLabel>
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
    auto view = window.findChild<QTableView *>("stackView");
    require(view, "Missing stack panel");
    auto model = dynamic_cast<MemoryModel *>(view->model());
    RegisterModel *registers = nullptr;
    for (auto table : window.findChildren<QTableView *>())
        if (auto m = dynamic_cast<RegisterModel *>(table->model())) registers = m;
    require(model && registers && model->rowCount() == 10 && model->columnCount() == 2,
            "Stack must have ten rows and two logical columns");
    require(model->byte(0) == -1, "Initial stack contains fabricated bytes");
    action("Build")->trigger();
    require(wait([&] { return action("Start")->isEnabled(); }), "Build failed");
    action("Start")->trigger();
    require(wait([&] { return action("Step")->isEnabled(); }), "Start failed");
    quint64 initialRsp = registers->values["rsp"];
    auto loaded = [&] { return model->address(9) == initialRsp && model->byte(model->address(0)) >= 0 && model->byte(model->address(9) + 7) >= 0; };
    require(wait(loaded), "Stack bytes did not load");
    for (int row = 0; row < 10; ++row) {
        require(model->address(row) == initialRsp - (9 - row) * 8, "Incorrect stack row address");
        require(model->rowForAddress(model->address(row)) == row && model->rowForAddress(model->address(row) + 7) == row,
                "Incorrect stack address lookup");
        require(model->data(model->index(row, 1)).toString().split(' ').size() == 8, "Incorrect byte count");
    }
    require(view->visualRect(model->index(9, 1)).bottom() < view->viewport()->height(),
            "Tenth stack row is clipped");
    const auto originalBottom = model->data(model->index(9, 1)).toString();
    for (int step = 1; step <= 9; ++step) {
        action("Step")->trigger();
        require(wait([&] { return action("Step")->isEnabled(); }), "Step failed");
        require(wait(loaded), "Stack refresh failed");
        for (int row = 0; row < 10; ++row)
            require(model->address(row) == initialRsp - (9 - row) * 8, "Stack addresses moved during execution");
        if (step == 6) {
            require(registers->values["rsp"] == 1, "RSP was not changed to the test value");
            require(model->byte(1) == -1, "Out-of-range stack byte was fabricated");
        }
        if (step == 3) {
            require(registers->values["rsp"] == initialRsp - 8, "PUSH did not decrement RSP");
            require(model->data(model->index(8, 1)).toString() == "88 77 66 55 44 33 22 11", "Wrong PUSH bytes");
        }
        if (step == 4) {
            require(model->byte(initialRsp - 8) == 0xaa && model->changed(initialRsp - 8) && !model->changed(initialRsp - 7),
                    "Wrong stack write or changed-byte highlight");
            window.grab().save("/tmp/asm101-stack.png");
        }
        if (step == 5) {
            require(registers->values["rsp"] == initialRsp && registers->values["rbx"] == 0x11223344556677aaULL,
                    "POP did not restore RSP and read actual bytes");
            require(model->data(model->index(9, 1)).toString() == originalBottom, "Initial stack row changed after POP");
            require(model->byte(initialRsp - 8) == 0xaa, "Popped bytes disappeared from the fixed stack view");
        }
    }
    action("Step")->trigger();
    require(wait([&] { return action("Start")->isEnabled(); }), "Exit failed");
    action("Start")->trigger();
    require(wait([&] { return action("Step")->isEnabled(); }), "Restart failed");
    initialRsp = registers->values["rsp"];
    require(wait(loaded), "Stack did not reset on restart");
    require(!model->changed(model->address(0)), "Stack retained old modification highlights");
    action("Stop")->trigger();
    require(wait([&] { return action("Start")->isEnabled(); }), "Stop failed");
    qInfo() << "PASS: ten visible stack rows, initial RSP at bottom, fixed addresses, PUSH/POP, writes, restart";
    return 0;
}
