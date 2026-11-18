#include "MemoryModel.h"
#include "Theme.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QHelpEvent>
#include <QScrollBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QToolTip>
#include <functional>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    applyTheme(app);
    auto require = [](bool ok, const char *message) {
        if (!ok) {
            qCritical() << message;
            std::exit(1);
        }
    };
    auto wait = [&](std::function<bool()> condition) {
        QElapsedTimer timer;
        timer.start();
        while (!condition() && timer.elapsed() < 7000)
            QTest::qWait(10);
        require(condition(), "Timed out waiting for debugger state or actual memory");
    };
    QTemporaryDir directory;
    const auto object = directory.filePath("ascii.o"), executable = directory.filePath("ascii");
    require(QProcess::execute(
                "nasm", {"-f", "elf64", "-g", "-F", "dwarf", EXAMPLE_PATH, "-o", object}) == 0 &&
                QProcess::execute("ld", {object, "-o", executable}) == 0,
            "Could not build ASCII fixture");
    DebuggerEngine engine;
    QObject::connect(&engine, &DebuggerEngine::error, [&](const QString &text) {
        qCritical() << text;
        std::exit(1);
    });
    MemoryModel model(&engine);
    QTableView view;
    view.resize(1000, 450);
    view.setModel(&model);
    view.setItemDelegate(new MemoryDelegate(&view));
    view.setSelectionMode(QAbstractItemView::NoSelection);
    view.setEditTriggers(QAbstractItemView::NoEditTriggers);
    view.verticalHeader()->hide();
    view.horizontalHeader()->setStretchLastSection(true);
    view.setColumnWidth(0, 360);
    view.show();
    engine.start(executable);
    wait([&] {
        return engine.paused();
    });
    model.resetSections();
    quint64 address = 0;
    for (auto it = engine.dataSymbols.cbegin(); it != engine.dataSymbols.cend(); ++it)
        if (it.value().contains("text"))
            address = it.key();
    require(address != 0, "Missing data symbol");
    auto layout = [&] {
        view.setFont(app.font());
        view.verticalHeader()->setDefaultSectionSize(view.fontMetrics().height() + 8);
        for (int row : model.annotatedRows())
            view.setRowHeight(row, view.fontMetrics().height() * 2 + 8);
        view.setColumnWidth(1, MemoryDelegate::byteWidth(view.fontMetrics(), 0) * 8 + 20);
        view.scrollTo(model.index(model.rowForAddress(address), 1));
        QTest::qWait(30);
    };
    layout();
    wait([&] {
        return model.byte(address) == 'A' && model.byte(address + 7) == '<';
    });
    auto bytePoint = [&](quint64 a) {
        const int row = model.rowForAddress(a);
        const QRect rect = view.visualRect(model.index(row, 1));
        const int width = MemoryDelegate::byteWidth(view.fontMetrics(), rect.width());
        const int header = model.hasSymbols(row) ? view.fontMetrics().height() + 2 : 0;
        return QPoint(rect.x() + 10 + int(a - model.address(row)) * width + width / 2,
                      rect.y() + header + (rect.height() - header) / 2);
    };
    auto click = [&](quint64 a) {
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, bytePoint(a));
    };
    const auto registers = engine.registers;
    const auto rip = engine.rip;
    click(address);
    require(model.showsAscii(address) && model.byteText(address) == "A" &&
                model.byteText(address + 1) == "42",
            "Click did not toggle just the addressed byte to ASCII");
    require(model.data(model.index(model.rowForAddress(address), 1)).toString().startsWith("A 42"),
            "DisplayRole differs from the painted ASCII representation");
    require(engine.registers == registers && engine.rip == rip && model.byte(address) == 'A',
            "Representation toggle changed the real process");
    QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier, bytePoint(address));
    require(model.showsAscii(address), "Right-click toggled a byte");
    click(address);
    require(!model.showsAscii(address) && model.byteText(address) == "41",
            "Second click did not restore hex");
    click(address);
    const QStringList characters = {"A",
                                    "B",
                                    QString(QChar(0x2420)),
                                    "\\n",
                                    "\\0",
                                    QString(QChar(0x00b7)),
                                    QString(QChar(0x00b7)),
                                    "<"};
    for (int i = 1; i < 8; ++i)
        click(address + i);
    for (int i = 0; i < 8; ++i)
        require(model.byteText(address + i) == characters[i],
                "Incorrect ASCII/control representation");
    require(model.byteTooltip(address + 5).contains("DEL") &&
                model.byteTooltip(address + 6).contains("outside ASCII"),
            "Control/non-ASCII byte tooltip is misleading");
    const auto rect = view.visualRect(model.index(model.rowForAddress(address), 1));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(rect.x() + 15, rect.y() + 3));
    require(model.showsAscii(address), "Clicking a symbol label toggled the byte below it");
    const QRect addressRect = view.visualRect(model.index(model.rowForAddress(address), 0));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, addressRect.center());
    require(model.showsAscii(address), "Clicking an address toggled byte representation");
    const auto point = bytePoint(address);
    QHelpEvent hover(QEvent::ToolTip, point, view.viewport()->mapToGlobal(point));
    QApplication::sendEvent(view.viewport(), &hover);
    require(QToolTip::text().contains("hex 41") && QToolTip::text().contains("decimal 65") &&
                QToolTip::text().contains("ASCII A"),
            "Byte tooltip lost the numeric or ASCII value");
    QToolTip::hideText();
    applyTheme(app, true);
    layout();
    require(model.byteText(address) == "A", "Theme change reset ASCII mode");
    click(address);
    require(model.byteText(address) == "41", "Byte hit-testing failed in light mode");
    view.resize(700, 450);
    applyTextSize(app, true);
    layout();
    require(view.horizontalScrollBar()->value() > 0,
            "Magnified-byte test must exercise horizontal scrolling");
    click(address);
    require(model.byteText(address) == "A",
            "Byte hit-testing failed with magnification or horizontal scrolling");
    applyTextSize(app, false);
    applyTheme(app, false);
    view.resize(1000, 450);
    layout();
    model.setSectionVisible(".data", false);
    model.setSectionVisible(".data", true);
    layout();
    require(model.showsAscii(address) && model.byteText(address) == "A",
            "Section filter reset the byte's mode");
    engine.step();
    wait([&] {
        return engine.paused();
    });
    model.refresh();
    wait([&] {
        return model.byte(address) == 'Z';
    });
    require(model.byteText(address) == "Z" && model.showsAscii(address) && model.changed(address),
            "Stepping did not preserve ASCII mode or the actual changed-byte highlight");
    require(!model.toggleAscii(0) && model.byteText(0) == "??",
            "Unreadable memory was converted to ASCII");
    bool ended = false;
    QObject::connect(&engine, &DebuggerEngine::ended, [&] {
        ended = true;
    });
    engine.stop();
    wait([&] {
        return ended;
    });
    engine.start(executable);
    wait([&] {
        return engine.paused();
    });
    model.resetSections();
    layout();
    wait([&] {
        return model.byte(address) == 'A';
    });
    require(!model.showsAscii(address) && model.byteText(address) == "41",
            "New run retained old byte display choices");
    ended = false;
    engine.stop();
    wait([&] {
        return ended;
    });
    qInfo() << "PASS: per-byte click hex/ASCII toggle, real memory unchanged, controls, tooltip, "
               "themes, zoom, filters, stepping and reset";
}
