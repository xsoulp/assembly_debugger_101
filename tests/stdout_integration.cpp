#include "MainWindow.h"
#include "Theme.h"
#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
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
        require(condition(), "Timed out waiting for program output or debugger state");
    };
    const QByteArray expected = "Hello \xE2\x82\xAC\n^done\n*stopped,reason=\"exited-normally\"\n"
                                "last bytes without newline";
    QTemporaryDir dir;
    const auto object = dir.filePath("stdout.o"), executable = dir.filePath("stdout program");
    require(QProcess::execute(
                "nasm", {"-f", "elf64", "-g", "-F", "dwarf", EXAMPLE_PATH, "-o", object}) == 0 &&
                QProcess::execute("ld", {object, "-o", executable}) == 0,
            "Could not build stdout fixture");
    DebuggerEngine engine;
    QByteArray output;
    bool ended = false;
    QObject::connect(&engine, &DebuggerEngine::standardOutput, [&](const QByteArray &bytes) {
        output += bytes;
    });
    QObject::connect(&engine, &DebuggerEngine::error, [&](const QString &message) {
        require(false, message);
    });
    QObject::connect(&engine, &DebuggerEngine::ended, [&] {
        ended = true;
    });
    for (int run = 0; run < 2; ++run) {
        output.clear();
        ended = false;
        engine.start(executable);
        wait([&] {
            return engine.paused();
        });
        require(output.isEmpty(), "Output from the previous session leaked into a new run");
        int steps = 0, writes = 0;
        while (!ended && steps++ < 40) {
            const bool writing = engine.instruction == "syscall" &&
                                 engine.registers.value("rax") == 1 &&
                                 engine.registers.value("rdi") == 1;
            engine.step();
            wait([&] {
                return engine.paused() || ended;
            });
            if (writing) {
                ++writes;
                if (writes == 1)
                    wait([&] {
                        return output == "Hello ";
                    });
                if (writes == 2)
                    wait([&] {
                        return output == QByteArray("Hello \xE2");
                    });
            }
        }
        require(ended && output == expected,
                "Stdout lost bytes, captured stderr, or confused output with MI records");
    }

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
    auto view = window.findChild<QPlainTextEdit *>("stdoutView");
    auto panel = window.findChild<QWidget *>("stdoutPanel");
    auto toggle = window.findChild<QAction *>("stdoutToggle");
    auto left = window.findChild<QSplitter *>("leftSplitter");
    require(view && panel && toggle && left && left->indexOf(panel) == 2,
            "Missing stdout panel at the bottom left");
    require(view->isReadOnly() && !panel->isHidden(),
            "Stdout should initially be visible and read-only");
    require(left->sizes().size() == 3 && view->minimumHeight() < view->maximumHeight(),
            "Stdout panel is not resizable");
    window.activateWindow();
    wait([&] {
        return window.isActiveWindow();
    });
    QTest::keyClick(&window, Qt::Key_F9);
    require(panel->isHidden() && !toggle->isChecked(), "F9 did not hide stdout");
    action("Build")->trigger();
    wait([&] {
        return action("Start")->isEnabled();
    });
    action("Start")->trigger();
    wait([&] {
        return action("Step")->isEnabled();
    });
    for (int step = 0; step < 5; ++step) {
        action("Step")->trigger();
        wait([&] {
            return action("Step")->isEnabled();
        });
    }
    wait([&] {
        return view->toPlainText() == "Hello ";
    });
    require(panel->isHidden(), "Output unexpectedly reopened the hidden panel");
    QTest::keyClick(&window, Qt::Key_F9);
    require(!panel->isHidden() && view->toPlainText() == "Hello ",
            "F9 did not restore captured output");
    for (int step = 5; step < 33; ++step) {
        action("Step")->trigger();
        wait([&] {
            return step == 32 ? action("Start")->isEnabled() : action("Step")->isEnabled();
        });
        if (step == 9)
            require(view->toPlainText() == "Hello ",
                    "Incomplete UTF-8 produced a replacement character");
    }
    wait([&] {
        return view->toPlainText() == QString::fromUtf8(expected);
    });
    require(!view->toPlainText().contains(QChar::ReplacementCharacter),
            "UTF-8 split across writes was corrupted");
    auto theme = window.findChild<QToolButton *>("themeToggle");
    theme->click();
    require(view->palette().color(QPalette::Base).lightness() > 230 &&
                view->toPlainText() == QString::fromUtf8(expected),
            "Light theme lost output or contrast");
    theme->click();
    auto zoom = window.findChild<QToolButton *>("zoomToggle");
    const auto normalSize = view->font().pointSizeF();
    zoom->click();
    require(view->font().pointSizeF() == normalSize * 2,
            "Output does not follow text magnification");
    zoom->click();
    window.findChild<QToolButton *>("hideStdoutButton")->click();
    require(panel->isHidden() && !toggle->isChecked(),
            "Hide button did not update the View toggle");
    toggle->trigger();
    require(!panel->isHidden() && view->toPlainText() == QString::fromUtf8(expected),
            "View menu lost output");
    action("Start")->trigger();
    wait([&] {
        return action("Step")->isEnabled();
    });
    require(view->toPlainText().isEmpty(), "Start did not clear previous output");
    for (int step = 0; step < 5; ++step) {
        action("Step")->trigger();
        wait([&] {
            return action("Step")->isEnabled();
        });
    }
    wait([&] {
        return view->toPlainText() == "Hello ";
    });
    action("Stop")->trigger();
    wait([&] {
        return action("Start")->isEnabled();
    });
    require(view->toPlainText() == "Hello ", "Stop discarded captured stdout");
    window.findChild<QToolButton *>("clearStdoutButton")->click();
    require(view->toPlainText().isEmpty(), "Clear did not empty stdout");
    qInfo() << "PASS: stdout pipe isolation, streaming, UTF-8, no newline, exit/stop/restart, "
               "hidden capture, F9, View, themes and zoom";
}
