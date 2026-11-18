#include "MainWindow.h"
#include "ProgramArguments.h"
#include "RegisterModel.h"
#include "Theme.h"
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QtEndian>
#include <functional>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    applyTheme(app);
    auto require = [](bool ok, const QString &message) {
        if (!ok) {
            qCritical().noquote() << message;
            std::exit(1);
        }
    };
    auto wait = [&](std::function<bool()> condition) {
        QElapsedTimer timer;
        timer.start();
        while (!condition() && timer.elapsed() < 7000)
            QTest::qWait(10);
        require(condition(), "Timed out waiting for debugger/UI state");
    };
    const QString text =
        R"(alpha "two words" 'single quoted' "" '' escaped\ space "say \"hi\"" 'C:\temp' prefix"suffix" '$HOME' '*')";
    const QStringList expected = {
        "alpha",      "two words", "single quoted", "",      "", "escaped space",
        "say \"hi\"", "C:\\temp",  "prefixsuffix",  "$HOME", "*"};
    const auto parsed = parseProgramArguments(text);
    require(parsed.error.isEmpty() && parsed.values == expected,
            "Argument parsing lost quotes or escapes");
    require(parseProgramArguments(" \t ").values.isEmpty(), "Blank text produced arguments");
    for (const auto &invalid :
         {QString("'unfinished"), QString("\"unfinished"), QString("trailing\\"),
          QString("line\nbreak"), QString("line\rbreak"), QString(QChar('\0'))})
        require(!parseProgramArguments(invalid).error.isEmpty(),
                "Invalid argument text was accepted");

    QTemporaryDir dir;
    const auto executable = dir.filePath("program with spaces");
    const auto object = dir.filePath("program.o");
    require(QProcess::execute(
                "nasm", {"-f", "elf64", "-g", "-F", "dwarf", EXAMPLE_PATH, "-o", object}) == 0 &&
                QProcess::execute("ld", {object, "-o", executable}) == 0,
            "Could not assemble argument fixture");
    DebuggerEngine engine;
    QObject::connect(&engine, &DebuggerEngine::error, [&](const QString &message) {
        require(false, message);
    });
    auto read = [&](quint64 address, int count) {
        QByteArray result;
        bool done = false;
        engine.readMemory(address, count, [&](QByteArray bytes) {
            result = bytes;
            done = true;
        });
        wait([&] {
            return done;
        });
        require(result.size() == count, "Could not read actual argv memory");
        return result;
    };
    const auto marker = dir.filePath("shell-must-not-run");
    const QList<QStringList> cases = {
        expected + QStringList{"single'quote", "literal\\slash", "$(touch " + marker + ")",
                               "a;b|c&d>e<f", "Olá 世界"},
        {},
        {"", "hello world", "-flag", "--", "#literal"},
    };
    for (const auto &arguments : cases) {
        engine.start(executable, arguments);
        wait([&] {
            return engine.paused();
        });
        const auto rsp = engine.registers.value("rsp");
        const auto table = read(rsp, int((arguments.size() + 3) * 8));
        const auto word = [&](int index) {
            return qFromLittleEndian<quint64>(table.constData() + index * 8);
        };
        require(word(0) == quint64(arguments.size() + 1),
                "Incorrect argc on the real process stack");
        const auto all = QStringList{executable} + arguments;
        for (int i = 0; i < all.size(); ++i) {
            const auto bytes = all[i].toUtf8() + '\0';
            require(read(word(i + 1), bytes.size()) == bytes,
                    "argv differs at argument " + QString::number(i));
        }
        require(word(arguments.size() + 2) == 0, "argv must end in a NULL pointer");
        bool ended = false;
        auto connection = QObject::connect(&engine, &DebuggerEngine::ended, [&] {
            ended = true;
        });
        engine.stop();
        wait([&] {
            return ended;
        });
        QObject::disconnect(connection);
    }
    require(!QFileInfo::exists(marker), "Argument text executed a shell command");

    MainWindow window;
    window.show();
    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        for (auto widget : app.topLevelWidgets())
            if (auto box = qobject_cast<QMessageBox *>(widget))
                require(false, box->text());
    });
    watchdog.start(100);
    auto action = [&](const QString &name) -> QAction * {
        for (auto a : window.findChildren<QAction *>())
            if (a->text() == name)
                return a;
        require(false, "Missing action " + name);
        return nullptr;
    };
    auto bar = window.findChild<QToolBar *>("programArgumentsBar");
    auto field = window.findChild<QLineEdit *>("programArgumentsEdit");
    auto button = window.findChild<QWidget *>("startButton");
    require(bar && field && button && bar->isHidden(),
            "Arguments field should initially be hidden");
    window.activateWindow();
    wait([&] {
        return window.isActiveWindow();
    });
    QTest::keyClick(&window, Qt::Key_F11);
    require(!bar->isHidden() && field->hasFocus(), "F11 did not open/focus arguments");
    field->setText("\"two words\" second");
    QTest::keyClick(field, Qt::Key_Return);
    require(bar->isHidden(), "Enter did not hide the arguments field");
    // Disabled Start still allows configuration before the source is built.
    auto toolbar = qobject_cast<QToolBar *>(button->parentWidget());
    require(toolbar, "Missing execution toolbar");
    auto global = button->mapToGlobal(button->rect().center());
    QContextMenuEvent disabledContext(QContextMenuEvent::Mouse, toolbar->mapFromGlobal(global),
                                      global);
    QApplication::sendEvent(toolbar, &disabledContext);
    require(!bar->isHidden(), "Right-click on disabled Start did not reveal arguments");
    action("Build")->trigger();
    wait([&] {
        return action("Start")->isEnabled();
    });
    bar->hide();
    global = button->mapToGlobal(button->rect().center());
    QContextMenuEvent context(QContextMenuEvent::Mouse, button->rect().center(), global);
    QApplication::sendEvent(button, &context);
    require(!bar->isHidden() && field->text() == "\"two words\" second",
            "Right-click lost arguments");
    RegisterModel *registers = nullptr;
    for (auto view : window.findChildren<QTableView *>())
        if (auto model = dynamic_cast<RegisterModel *>(view->model()))
            registers = model;
    require(registers, "Missing register model");
    action("Start")->trigger();
    wait([&] {
        return action("Step")->isEnabled();
    });
    require(field->isReadOnly(), "Arguments must be locked during the current run");
    for (int step = 0; step < 3; ++step) {
        action("Step")->trigger();
        wait([&] {
            return action("Step")->isEnabled();
        });
    }
    require(registers->values.value("rax") == 3 &&
                registers->values.value("rdx") == qFromLittleEndian<quint64>("two word"),
            "Start did not pass the GUI's arguments to the actual program");
    action("Stop")->trigger();
    wait([&] {
        return action("Start")->isEnabled();
    });
    require(!field->isReadOnly(), "Arguments remained locked after Stop");
    field->setText("'unclosed");
    action("Start")->trigger();
    require(action("Start")->isEnabled() && !action("Stop")->isEnabled() && !bar->isHidden(),
            "Invalid quotes started a process");
    field->clear();
    action("Start")->trigger();
    wait([&] {
        return action("Step")->isEnabled();
    });
    action("Step")->trigger();
    wait([&] {
        return action("Step")->isEnabled();
    });
    require(registers->values.value("rax") == 1, "Clearing the field retained previous arguments");
    action("Stop")->trigger();
    wait([&] {
        return action("Start")->isEnabled();
    });
    qInfo() << "PASS: literal argc/argv, quoting, empty arguments, Unicode, restart/clear, F11, "
               "right-click and GUI launch";
}
