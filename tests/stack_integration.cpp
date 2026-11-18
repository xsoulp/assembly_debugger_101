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
#include <QScrollBar>
#include <QSplitter>
#include <QMessageBox>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QStyleOptionSlider>
#include <QStyle>
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
        return condition();
    };
    auto action = [&](const QString &name) -> QAction * {
        for (auto a : window.findChildren<QAction *>())
            if (a->text() == name)
                return a;
        return nullptr;
    };
    auto view = window.findChild<QTableView *>("stackView");
    require(view, "Missing stack panel");
    auto model = dynamic_cast<MemoryModel *>(view->model());
    RegisterModel *registers = nullptr;
    for (auto table : window.findChildren<QTableView *>())
        if (auto m = dynamic_cast<RegisterModel *>(table->model()))
            registers = m;
    require(model && registers && model->rowCount() == 10 && model->columnCount() == 2,
            "Stack must have ten rows and two logical columns");
    require(model->byte(0) == -1, "Initial stack contains fabricated bytes");
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
    quint64 initialRsp = registers->values["rsp"];
    int initialRow = model->rowForAddress(initialRsp);
    auto loaded = [&] {
        return model->address(initialRow) == initialRsp && model->byte(initialRsp - 72) >= 0 &&
               model->byte(model->address(initialRow) + 7) >= 0;
    };
    require(wait(loaded), "Stack bytes did not load");
    require(initialRow > 9 && model->rowCount() > initialRow + 100,
            "Stack has no scrollable addresses above and below the initial RSP");
    require(view->verticalScrollBar()->maximum() > 0, "Stack vertical scrollbar has no range");
    auto requireFocused = [&] {
        const int row = model->rowForAddress(registers->values.value("rsp"));
        const QRect rect = view->visualRect(model->index(row, 1));
        require(rect.top() >= 0 && rect.bottom() < view->viewport()->height(),
                "Current RSP byte or its labels are clipped outside the viewport");
        require(qAbs(rect.center().y() - view->viewport()->rect().center().y()) <= rect.height(),
                "Stack viewer is not centered on the current RSP");
    };
    requireFocused();
    require(view->verticalScrollMode() == QAbstractItemView::ScrollPerItem &&
                view->verticalScrollBar()->singleStep() == 1,
            "Stack scrolling must advance one row at a time");
    auto wheel = [&](QWidget *target, int delta) {
        const auto position = target->rect().center();
        QWheelEvent event(position, target->mapToGlobal(position), QPoint(), QPoint(0, delta),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(target, &event);
    };
    auto scrollbar = view->verticalScrollBar();
    const int firstVisible = view->rowAt(0), initialScroll = scrollbar->value();
    wheel(view->viewport(), -120);
    require(scrollbar->value() == initialScroll + 1 && view->rowAt(0) == firstVisible + 1,
            "One wheel notch moved more than one stack row");
    wheel(view->viewport(), 120);
    require(scrollbar->value() == initialScroll, "Wheel up did not restore the preceding row");
    wheel(scrollbar, -120);
    require(scrollbar->value() == initialScroll + 1,
            "Wheel over the scrollbar moved more than one row");
    wheel(scrollbar, 120);
    QStyleOptionSlider scrollbarStyle;
    scrollbarStyle.initFrom(scrollbar);
    scrollbarStyle.orientation = Qt::Vertical;
    scrollbarStyle.minimum = scrollbar->minimum();
    scrollbarStyle.maximum = scrollbar->maximum();
    scrollbarStyle.sliderPosition = scrollbar->value();
    scrollbarStyle.sliderValue = scrollbar->value();
    scrollbarStyle.pageStep = scrollbar->pageStep();
    scrollbarStyle.singleStep = 1;
    const auto downArrow = scrollbar->style()->subControlRect(
        QStyle::CC_ScrollBar, &scrollbarStyle, QStyle::SC_ScrollBarAddLine, scrollbar);
    QTest::mouseClick(scrollbar, Qt::LeftButton, Qt::NoModifier, downArrow.center());
    require(scrollbar->value() == initialScroll + 1,
            "One scrollbar arrow click moved more than one stack row");
    scrollbar->setValue(initialScroll);
    auto title = window.findChild<QLabel *>("stackTitle");
    require(title && title->text().contains("stack (RSP)") && title->text().contains("base (RBP)"),
            "Missing pointer labels in the stack heading");
    auto splitter = window.findChild<QSplitter *>("memorySplitter");
    require(splitter && splitter->count() == 2, "RAM and stack lack a resize divider");
    require(view->minimumHeight() < view->maximumHeight(), "Stack height is still fixed");
    const int initialHeight = view->height();
    const auto originalSizes = splitter->sizes();
    splitter->setSizes({originalSizes[0] + 70, originalSizes[1] - 70});
    QTest::qWait(30);
    require(view->height() < initialHeight, "Dragging the divider cannot shrink the stack");
    const int smallerHeight = view->height();
    splitter->setSizes(originalSizes);
    QTest::qWait(30);
    require(view->height() > smallerHeight, "Dragging the divider cannot enlarge the stack");
    view->scrollTo(model->index(initialRow + 64, 0), QAbstractItemView::PositionAtTop);
    QTest::qWait(30);
    const int upperRow = view->rowAt(0);
    require(upperRow > initialRow && model->address(upperRow) > initialRsp,
            "Scrolling down did not reveal higher stack addresses");
    require(wait([&] {
                return model->byte(initialRsp + 512) >= 0;
            }),
            "Higher stack addresses were not fetched on demand");
    const int scrolledPosition = view->verticalScrollBar()->value();
    require(model->data(model->index(initialRow, 0), Qt::ForegroundRole).value<QColor>() ==
                QColor("#ead55e"),
            "RSP address is not gold");
    require(model->sectionColor(initialRsp) == QColor("#482b29"), "Missing stack color");
    auto themeToggle = window.findChild<QToolButton *>("themeToggle");
    themeToggle->click();
    require(view->verticalScrollBar()->value() == scrolledPosition,
            "Theme change reset the stack scroll position");
    require(view->height() > smallerHeight, "Theme change reset stack panel size");
    auto zoomToggle = window.findChild<QToolButton *>("zoomToggle");
    zoomToggle->click();
    require(view->minimumHeight() < view->maximumHeight() &&
                view->verticalScrollBar()->maximum() > 0,
            "Stack became fixed or unscrollable with larger text");
    zoomToggle->click();
    // Visit the end of the virtual range, including unmapped memory above the stack.
    view->scrollTo(model->index(model->rowCount() - 1, 0), QAbstractItemView::PositionAtBottom);
    QTest::qWait(100);
    require(model->byte(model->address(model->rowCount() - 1)) == -1,
            "Unreadable higher stack memory was fabricated");
    view->scrollTo(model->index(initialRow, 0), QAbstractItemView::PositionAtBottom);
    QTest::qWait(30);
    require(model->data(model->index(initialRow, 0), Qt::ForegroundRole).value<QColor>() ==
                    QColor("#916000") &&
                model->sectionColor(initialRsp) == QColor("#fbe3dd"),
            "Stack colors do not adapt to the light theme");
    themeToggle->click();
    for (int row = initialRow - 9; row <= initialRow; ++row) {
        require(model->address(row) == initialRsp - (initialRow - row) * 8,
                "Incorrect stack row address");
        require(model->rowForAddress(model->address(row)) == row &&
                    model->rowForAddress(model->address(row) + 7) == row,
                "Incorrect stack address lookup");
        require(model->data(model->index(row, 1)).toString().split(' ').size() == 8,
                "Incorrect byte count");
    }
    require(view->visualRect(model->index(initialRow, 1)).bottom() < view->viewport()->height(),
            "Tenth stack row is clipped");
    const auto originalBottom = model->data(model->index(initialRow, 1)).toString();
    auto overlay = window.findChild<QWidget *>("movOverlay");
    require(overlay, "Missing operand animation overlay");
    auto hasFooterColor = [&](int row, QColor expected) {
        const QRect rect = view->visualRect(model->index(row, 1));
        const int footer = MemoryDelegate::pointerLabelHeight(model, row, view->font());
        const QImage rendered = view->viewport()->grab().toImage();
        const qreal scale = rendered.devicePixelRatio();
        const QRect area = QRect(rect.x(), rect.bottom() - footer + 1, rect.width(), footer)
                               .intersected(view->viewport()->rect());
        for (int y = int(area.top() * scale); y <= int(area.bottom() * scale); ++y)
            for (int x = int(area.left() * scale); x <= int(area.right() * scale); ++x) {
                const auto color = rendered.pixelColor(x, y);
                if (qAbs(color.red() - expected.red()) < 20 &&
                    qAbs(color.green() - expected.green()) < 20 &&
                    qAbs(color.blue() - expected.blue()) < 20)
                    return true;
            }
        return false;
    };
    for (int step = 1; step <= 12; ++step) {
        const int panelHeight = view->height();
        if (step == 1)
            view->scrollTo(model->index(initialRow + 64, 0), QAbstractItemView::PositionAtTop);
        const int inspectedPosition = scrollbar->value();
        action("Step")->trigger();
        if (step >= 6 && step <= 8) {
            require(wait([&] {
                        return overlay->isVisible();
                    }),
                    qPrintable(
                        QString("Stack-to-register animation did not start at step %1").arg(step)));
            require(!action("Step")->isEnabled(), "Step enabled during stack animation");
            require(overlay->property("value").toString() ==
                        (step == 7 ? "0x77AA" : "0x11223344556677AA"),
                    "Stack animation displayed an incorrect value");
            require(overlay->property("operation").toString() == (step == 8 ? "POP" : "MOV"),
                    "Incorrect stack animation operation");
            const QPoint from =
                view->viewport()->mapFrom(&window, overlay->property("source").toPoint());
            const QRect source = view->visualRect(model->index(initialRow - 1, 1));
            require(source.contains(from) &&
                        from.x() <
                            source.x() + 10 +
                                MemoryDelegate::byteWidth(view->fontMetrics(), source.width()),
                    "Animation did not start at the original stack byte");
            require(overlay->property("source").toPoint().x() >
                        overlay->property("destination").toPoint().x(),
                    "Stack-to-register animation moved in the wrong direction");
            if (step == 8) {
                require(registers->values["rsp"] == initialRsp,
                        "POP animation started before RSP was updated");
                QTest::qWait(150);
                window.grab().save("/tmp/asm101-stack-pop.png");
            }
        }
        require(wait([&] {
                    return action("Step")->isEnabled();
                }),
                "Step failed");
        require(wait(loaded), "Stack refresh failed");
        require(view->height() == panelHeight, "Stepping resized the stack panel");
        for (int row = initialRow - 9; row <= initialRow; ++row)
            require(model->address(row) == initialRsp - (initialRow - row) * 8,
                    "Stack addresses moved during execution");
        if (step == 2) {
            requireFocused();
            require(hasFooterColor(initialRow, QColor("#ead55e")),
                    "The stack label is not visibly rendered below the RSP byte");
            require(hasFooterColor(initialRow, QColor("#b9cce0")),
                    "The base label is not visibly rendered below the RBP byte");
            window.grab().save("/tmp/asm101-pointer-labels.png");
            require(
                model->data(model->index(initialRow, 0), Qt::FontRole).value<QFont>().underline() &&
                    model->data(model->index(initialRow, 0), Qt::ForegroundRole).value<QColor>() ==
                        QColor("#ead55e"),
                "RSP and RBP styles did not combine on the same address");
            require(model->data(model->index(initialRow, 0), Qt::ToolTipRole)
                        .toString()
                        .contains("RBP"),
                    "Missing pointer tooltip");
        }
        if (step == 1) {
            require(scrollbar->value() == inspectedPosition,
                    "An unchanged RSP interrupted manual stack inspection");
            QMetaObject::invokeMethod(title, "linkActivated", Qt::DirectConnection,
                                      Q_ARG(QString, QString("rsp")));
            requireFocused();
        }
        if (step == 9) {
            require(registers->values["rsp"] == 1, "RSP was not changed to the test value");
            require(model->byte(1) == -1, "Out-of-range stack byte was fabricated");
            for (int row = initialRow - 9; row <= initialRow; ++row)
                require(!model->data(model->index(row, 0), Qt::ForegroundRole).isValid(),
                        "Out-of-range RSP highlighted an unrelated address");
        }
        if (step == 4) {
            requireFocused();
            require(registers->values["rsp"] == initialRsp - 8, "PUSH did not decrement RSP");
            require(model->data(model->index(initialRow - 1, 1)).toString() ==
                        "88 77 66 55 44 33 22 11",
                    "Wrong PUSH bytes");
            require(
                model->data(model->index(initialRow - 1, 0), Qt::ForegroundRole).value<QColor>() ==
                        QColor("#ead55e") &&
                    !model->data(model->index(initialRow, 0), Qt::ForegroundRole).isValid() &&
                    model->data(model->index(initialRow, 0), Qt::FontRole)
                        .value<QFont>()
                        .underline(),
                "Pointer styles did not follow RSP and RBP independently");
        }
        if (step == 5) {
            require(model->byte(initialRsp - 8) == 0xaa && model->changed(initialRsp - 8) &&
                        !model->changed(initialRsp - 7),
                    "Wrong stack write or changed-byte highlight");
            window.grab().save("/tmp/asm101-stack.png");
        }
        if (step == 8) {
            requireFocused();
            require(registers->values["rsp"] == initialRsp &&
                        registers->values["rbx"] == 0x11223344556677aaULL,
                    "POP did not restore RSP and read actual bytes");
            require(model->data(model->index(initialRow, 1)).toString() == originalBottom,
                    "Initial stack row changed after POP");
            require(model->byte(initialRsp - 8) == 0xaa,
                    "Popped bytes disappeared from the fixed stack view");
        }
    }
    action("Step")->trigger();
    require(wait([&] {
                return action("Start")->isEnabled();
            }),
            "Exit failed");
    action("Start")->trigger();
    require(wait([&] {
                return action("Step")->isEnabled();
            }),
            "Restart failed");
    initialRsp = registers->values["rsp"];
    initialRow = model->rowForAddress(initialRsp);
    require(wait(loaded), "Stack did not reset on restart");
    require(!model->changed(model->address(0)), "Stack retained old modification highlights");
    action("Stop")->trigger();
    require(wait([&] {
                return action("Start")->isEnabled();
            }),
            "Stop failed");
    qInfo() << "PASS: scrollable/resizable stack, initial RSP visible, fixed addresses, PUSH/POP, "
               "stack-to-register MOV/POP animations, writes, restart";
    return 0;
}
