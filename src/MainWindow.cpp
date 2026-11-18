#include "MainWindow.h"
#include "AssemblyEditor.h"
#include "MemoryModel.h"
#include "RegisterModel.h"
#include "Theme.h"
#include "ProgramArguments.h"
#include <QSplitter>
#include <QTableView>
#include <QHeaderView>
#include <QToolBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QFile>
#include <QSaveFile>
#include <QCloseEvent>
#include <QAction>
#include <QDir>
#include <QToolButton>
#include <QTextBlock>
#include <QScrollBar>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QTimer>
#include <QLineEdit>
#include <QShortcut>
#include <QContextMenuEvent>
#include <QMenuBar>
#include <QMenu>
#include <QPlainTextEdit>
#include <QWheelEvent>
MainWindow::MainWindow() {
    setWindowTitle("ASM101 · Visual Assembly Debugger");
    resize(1400, 920);
    auto toolbar = addToolBar("Execution");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto add = [&](QString text, QString shortcut, auto callback) {
        auto a = toolbar->addAction(text, this, callback);
        a->setShortcut(QKeySequence(shortcut));
        return a;
    };
    add("Open", "Ctrl+O", [this] {
        open();
    });
    add("Save", "Ctrl+S", [this] {
        save();
    });
    toolbar->addSeparator();
    buildAction = add("Build", "F7", [this] {
        build();
    });
    startAction = add("Start", "F5", [this] {
        if (builtSource != editor->toPlainText()) {
            showError("Source changed. Build before starting.");
            return;
        }
        const auto arguments = parseProgramArguments(argumentsEdit->text());
        if (!arguments.error.isEmpty()) {
            showProgramArguments();
            statusBar()->showMessage(arguments.error);
            return;
        }
        cancelMove();
        active = true;
        haveFlags = false;
        stackScrollPending = true;
        stackWheelRemainder = stackPixelRemainder = 0;
        stdoutView->clear();
        stdoutDecoder = QStringDecoder(QStringDecoder::Utf8);
        registers->values.clear();
        registers->updateValues({});
        engine.start(executable, arguments.values);
        memory->resetSections();
        stackMemory->resetSections();
        stackTitle->setText("STACK · RSP — · scroll to explore");
        editor->setReadOnly(true);
        syncActions();
        statusBar()->showMessage("Starting GDB…");
    });
    stepAction = add("Step", "F10", [this] {
        stepInstruction();
    });
    stopAction = add("Stop", "Shift+F5", [this] {
        cancelMove();
        engine.stop();
        syncActions();
    });
    toolbar->widgetForAction(buildAction)->setObjectName("buildButton");
    toolbar->widgetForAction(stepAction)->setObjectName("stepButton");
    toolbar->widgetForAction(startAction)->setObjectName("startButton");
    toolbar->widgetForAction(stopAction)->setObjectName("stopButton");
    startButton = toolbar->widgetForAction(startAction);
    startButton->installEventFilter(this);
    // Disabled buttons route context-menu events to the toolbar before a build.
    toolbar->installEventFilter(this);
    startAction->setToolTip("Start (F5) · Right-click or F11 for program arguments");
    addToolBarBreak();
    argumentsBar = addToolBar("Program arguments");
    argumentsBar->setObjectName("programArgumentsBar");
    argumentsBar->setMovable(false);
    auto argumentsLabel = new QLabel("Arguments");
    argumentsBar->addWidget(argumentsLabel);
    argumentsEdit = new QLineEdit;
    argumentsEdit->setObjectName("programArgumentsEdit");
    argumentsEdit->setPlaceholderText("e.g. hello 42 \"two words\"");
    argumentsEdit->setClearButtonEnabled(true);
    argumentsEdit->setToolTip(
        "Arguments for the next Start. Separate with spaces; use single or double quotes "
        "to group text. Backslash escapes the next character except inside single quotes. "
        "Values are passed literally, without shell expansion. Empty quotes pass an empty "
        "argument.");
    argumentsEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    argumentsBar->addWidget(argumentsEdit);
    auto closeArguments = argumentsBar->addAction("Hide (F11)");
    connect(closeArguments, &QAction::triggered, argumentsBar, &QToolBar::hide);
    argumentsBar->hide();
    auto argumentsShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(argumentsShortcut, &QShortcut::activated, this, [this] {
        if (argumentsBar->isVisible())
            argumentsBar->hide();
        else
            showProgramArguments();
    });
    connect(argumentsEdit, &QLineEdit::returnPressed, argumentsBar, &QToolBar::hide);
    connect(argumentsEdit, &QLineEdit::textChanged, this, [this] {
        const auto arguments = parseProgramArguments(argumentsEdit->text());
        argumentsEdit->setAccessibleDescription(arguments.error);
        startAction->setToolTip(
            "Start (F5) · Right-click or F11 for program arguments\n" +
            (argumentsEdit->text().isEmpty() ? "No program arguments" : argumentsEdit->text()));
    });
    toolbar->addSeparator();
    position = new QLabel("RIP  —");
    toolbar->addWidget(position);
    auto horizontal = new QSplitter(Qt::Horizontal);
    auto left = new QSplitter(Qt::Vertical);
    left->setObjectName("leftSplitter");
    horizontal->addWidget(left);
    auto source = new QWidget;
    auto sl = new QVBoxLayout(source);
    sl->setContentsMargins(0, 0, 0, 0);
    sourceTitle = new QLabel("SOURCE CODE");
    sl->addWidget(sourceTitle);
    editor = new AssemblyEditor;
    sl->addWidget(editor);
    left->addWidget(source);
    auto regs = new QWidget;
    auto rl = new QVBoxLayout(regs);
    rl->setContentsMargins(0, 0, 0, 0);
    auto registerHeader = new QHBoxLayout;
    registerHeader->addWidget(new QLabel("REGISTERS"));
    registerHeader->addStretch();
    registerHeader->addWidget(new QLabel("Decimal:"));
    auto signedToggle = new QCheckBox("Unsigned");
    signedToggle->setObjectName("signedDecimalToggle");
    signedToggle->setToolTip("Toggle signed or unsigned decimal values for the register parts.");
    registerHeader->addWidget(signedToggle);
    rl->addLayout(registerHeader);
    auto rt = new QTableView;
    registerView = rt;
    registers = new RegisterModel(this);
    rt->setModel(registers);
    rt->verticalHeader()->hide();
    rt->horizontalHeader()->setStretchLastSection(true);
    rt->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    rt->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    rt->setColumnWidth(1, rt->fontMetrics().horizontalAdvance("0x00000000 0000 00 00") + 28);
    rt->verticalHeader()->setDefaultSectionSize(27);
    rt->setMinimumHeight(280);
    rt->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rt->setShowGrid(false);
    rt->setSelectionMode(QAbstractItemView::NoSelection);
    rl->addWidget(rt);
    connect(signedToggle, &QCheckBox::toggled, this, [this, signedToggle](bool enabled) {
        registers->setSignedDecimal(enabled);
        signedToggle->setText(enabled ? "Signed" : "Unsigned");
    });
    rl->addWidget(new QLabel("FLAGS"));
    flags = new QLabel("CF —    ZF —    SF —    OF —<br>RFLAGS —");
    flags->setTextFormat(Qt::RichText);
    flags->setWordWrap(true);
    flags->setMinimumHeight(65);
    rl->addWidget(flags);
    left->addWidget(regs);
    stdoutPanel = new QWidget;
    stdoutPanel->setObjectName("stdoutPanel");
    auto stdoutLayout = new QVBoxLayout(stdoutPanel);
    stdoutLayout->setContentsMargins(0, 0, 0, 0);
    auto stdoutHeader = new QHBoxLayout;
    stdoutHeader->addWidget(new QLabel("STDOUT"));
    stdoutHeader->addStretch();
    auto clearOutput = new QToolButton;
    clearOutput->setObjectName("clearStdoutButton");
    clearOutput->setText("Clear");
    stdoutHeader->addWidget(clearOutput);
    auto hideOutput = new QToolButton;
    hideOutput->setObjectName("hideStdoutButton");
    hideOutput->setText("Hide (F9)");
    stdoutHeader->addWidget(hideOutput);
    stdoutLayout->addLayout(stdoutHeader);
    stdoutView = new QPlainTextEdit;
    stdoutView->setObjectName("stdoutView");
    stdoutView->setReadOnly(true);
    stdoutView->setLineWrapMode(QPlainTextEdit::NoWrap);
    stdoutView->setMaximumBlockCount(5000);
    stdoutView->setPlaceholderText("Program stdout appears here.");
    stdoutView->setMinimumHeight(60);
    stdoutView->setToolTip("Program standard output · retained after exit · cleared on Start");
    stdoutLayout->addWidget(stdoutView);
    left->addWidget(stdoutPanel);
    auto stdoutToggle = new QAction("Stdout", this);
    stdoutToggle->setObjectName("stdoutToggle");
    stdoutToggle->setCheckable(true);
    stdoutToggle->setChecked(true);
    stdoutToggle->setShortcut(QKeySequence(Qt::Key_F9));
    menuBar()->addMenu("&View")->addAction(stdoutToggle);
    connect(stdoutToggle, &QAction::toggled, this, [this](bool visible) {
        cancelMove();
        syncActions();
        stdoutPanel->setVisible(visible);
    });
    connect(hideOutput, &QToolButton::clicked, this, [stdoutToggle] {
        stdoutToggle->setChecked(false);
    });
    connect(clearOutput, &QToolButton::clicked, stdoutView, &QPlainTextEdit::clear);
    connect(&engine, &DebuggerEngine::standardOutput, this, &MainWindow::appendStandardOutput);
    auto mem = new QWidget;
    auto ml = new QVBoxLayout(mem);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->addWidget(new QLabel("RAM · PROCESS MEMORY"));
    memoryLegend = new QLabel;
    memoryLegend->setObjectName("memoryLegend");
    memoryLegend->setTextFormat(Qt::RichText);
    memoryLegend->setTextInteractionFlags(Qt::TextBrowserInteraction);
    memoryLegend->setToolTip("Click a section to show or hide it.");
    memoryLegend->setWordWrap(true);
    ml->addWidget(memoryLegend);
    memorySplitter = new QSplitter(Qt::Vertical);
    memorySplitter->setObjectName("memorySplitter");
    memorySplitter->setChildrenCollapsible(false);
    ml->addWidget(memorySplitter, 1);
    auto ramPanel = new QWidget;
    auto ramLayout = new QVBoxLayout(ramPanel);
    ramLayout->setContentsMargins(0, 0, 0, 0);
    memorySplitter->addWidget(ramPanel);
    ram = new QTableView;
    ram->setObjectName("ramView");
    memory = new MemoryModel(&engine, this);
    ram->setModel(memory);
    ram->setItemDelegate(new MemoryDelegate(ram));
    ram->verticalHeader()->hide();
    ram->verticalHeader()->setDefaultSectionSize(28);
    connect(memory, &QAbstractItemModel::modelReset, this, [this] {
        updateTextLayout();
    });
    ram->horizontalHeader()->setStretchLastSection(true);
    ram->setColumnWidth(0, 310);
    ram->setShowGrid(false);
    ram->setSelectionMode(QAbstractItemView::NoSelection);
    ramLayout->addWidget(ram, 1);
    auto memoryHint =
        new QLabel("Click a byte · hex ↔ ASCII     Yellow outline · next instruction\n"
                   "Green underline · changed byte     ?? · unreadable");
    memoryHint->setWordWrap(true);
    ramLayout->addWidget(memoryHint);
    stackPanel = new QWidget;
    auto stackLayout = new QVBoxLayout(stackPanel);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    memorySplitter->addWidget(stackPanel);
    memorySplitter->handle(1)->setToolTip("Drag to resize RAM and stack");
    memorySplitter->setSizes({350, 350});
    stackTitle = new QLabel("STACK · RSP — · scroll to explore");
    stackTitle->setObjectName("stackTitle");
    stackTitle->setWordWrap(true);
    stackTitle->setTextFormat(Qt::RichText);
    stackTitle->setTextInteractionFlags(Qt::TextBrowserInteraction);
    stackTitle->setToolTip("Click stack (RSP) or base (RBP) to center that pointer.\n"
                           "Scroll one row per wheel notch or scrollbar arrow click.\n"
                           "Drag the divider above this panel to resize it.");
    connect(stackTitle, &QLabel::linkActivated, this, &MainWindow::focusStackPointer);
    stackLayout->addWidget(stackTitle);
    stackView = new QTableView;
    stackView->setObjectName("stackView");
    stackMemory = new MemoryModel(&engine, this, MemoryModel::Stack);
    stackView->setModel(stackMemory);
    stackView->setItemDelegate(new MemoryDelegate(stackView));
    stackView->verticalHeader()->hide();
    stackView->verticalHeader()->setDefaultSectionSize(28);
    stackView->horizontalHeader()->setStretchLastSection(true);
    stackView->setColumnWidth(
        0,
        stackView->fontMetrics().horizontalAdvance("0x0000000000000000 – 0x0000000000000000") + 20);
    stackView->setShowGrid(false);
    stackView->setSelectionMode(QAbstractItemView::NoSelection);
    stackView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    stackView->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    stackView->verticalScrollBar()->setSingleStep(1);
    stackView->viewport()->installEventFilter(this);
    stackView->verticalScrollBar()->installEventFilter(this);
    stackView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    stackLayout->addWidget(stackView, 1);
    connect(stackMemory, &QAbstractItemModel::dataChanged, this, [this] {
        updateStackLayout();
    });
    connect(stackMemory, &QAbstractItemModel::modelReset, this, [this] {
        updateStackLayout();
    });
    connect(memoryLegend, &QLabel::linkActivated, this, [this](const QString &section) {
        cancelMove();
        if (section == "stack") {
            stackVisible = !stackVisible;
            stackTitle->setVisible(stackVisible);
            stackView->setVisible(stackVisible);
            stackPanel->setVisible(stackVisible);
        } else {
            memory->setSectionVisible(section, !memory->sectionVisible(section));
        }
        updateMemoryLegend();
        syncActions();
    });
    horizontal->addWidget(mem);
    horizontal->setSizes({640, 760});
    left->setSizes({350, 390, 140});
    setCentralWidget(horizontal);
    moveOverlay = new MoveOverlay(this);
    auto cancelVisibleMove = [this] {
        if (moveOverlay->isVisible()) {
            cancelMove();
            syncActions();
        }
    };
    connect(horizontal, &QSplitter::splitterMoved, this, cancelVisibleMove);
    connect(left, &QSplitter::splitterMoved, this, cancelVisibleMove);
    connect(memorySplitter, &QSplitter::splitterMoved, this, cancelVisibleMove);
    for (auto table : {ram, registerView, stackView}) {
        connect(table->verticalScrollBar(), &QScrollBar::valueChanged, this, cancelVisibleMove);
        connect(table->horizontalScrollBar(), &QScrollBar::valueChanged, this, cancelVisibleMove);
    }

    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, cancelVisibleMove);
    connect(editor->horizontalScrollBar(), &QScrollBar::valueChanged, this, cancelVisibleMove);
    connect(&engine, &DebuggerEngine::snapshot, this, &MainWindow::updateSnapshot);
    connect(&engine, &DebuggerEngine::running, this, [this] {
        statusBar()->showMessage("Running");
        syncActions();
    });
    connect(&engine, &DebuggerEngine::ended, this, [this] {
        cancelMove();
        active = false;
        editor->setReadOnly(false);
        editor->setExecutionLine(0);
        statusBar()->showMessage("Session ended");
        syncActions();
    });
    connect(&engine, &DebuggerEngine::error, this, [this](QString s) {
        cancelMove();
        showError(s);
        engine.stop();
    });
    connect(&builder, &QProcess::errorOccurred, this, [this](auto) {
        building = false;
        editor->setReadOnly(false);
        showError(builder.errorString());
        syncActions();
    });
    connect(editor, &QPlainTextEdit::textChanged, this, [this] {
        syncActions();
    });
    const QString installedExample = QDir(QCoreApplication::applicationDirPath())
                                         .filePath("../share/asm101/examples/example.asm");
    const QString examplePath =
        QFileInfo::exists(installedExample) ? installedExample : QString(EXAMPLE_PATH);
    QFile f(examplePath);
    if (f.open(QIODevice::ReadOnly)) {
        editor->setPlainText(QString::fromUtf8(f.readAll()));
        sourcePath = QFileInfo(examplePath).isWritable() ? examplePath : QString();
        editor->document()->setModified(false);
        sourceTitle->setText("SOURCE CODE · " + QFileInfo(examplePath).fileName());
    }
    auto authorship = new QLabel("Luis Pinto · @xsoulp · 2026");
    authorship->setObjectName("authorship");
    statusBar()->addPermanentWidget(authorship);
    position->setMinimumWidth(0);
    position->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto themeToggle = new QToolButton;
    themeToggle->setObjectName("themeToggle");
    themeToggle->setText("Light mode");
    themeToggle->setAccessibleName("Light mode");
    themeToggle->setToolTip("Toggle light mode for classroom projection or return to dark mode.");
    themeToggle->setCheckable(true);
    themeToggle->setChecked(lightTheme());
    toolbar->addWidget(themeToggle);
    zoomToggle = new QToolButton;
    zoomToggle->setObjectName("zoomToggle");
    zoomToggle->setText("Double font size");
    zoomToggle->setAccessibleName("Double font size");
    zoomToggle->setToolButtonStyle(Qt::ToolButtonIconOnly);
    zoomToggle->setIconSize(QSize(24, 24));
    zoomToggle->setCheckable(true);
    zoomToggle->setChecked(qApp->property("doubleText").toBool());
    toolbar->addWidget(zoomToggle);
    connect(zoomToggle, &QToolButton::toggled, this, [this](bool doubled) {
        cancelMove();
        applyTextSize(*qApp, doubled);
        refreshAppearance();
        syncActions();
    });
    connect(themeToggle, &QToolButton::toggled, this, [this](bool light) {
        cancelMove();
        applyTheme(*qApp, light);
        refreshAppearance();
        syncActions();
    });
    refreshAppearance();
    statusBar()->showMessage("Ready · Build F7 → Start F5 → Step F10");
    syncActions();
}
void MainWindow::syncActions() {
    buildAction->setEnabled(!active && !building);
    startAction->setEnabled(!active && !building && !executable.isEmpty() &&
                            builtSource == editor->toPlainText());
    stepAction->setEnabled(active && engine.paused() && !moveBusy);
    stopAction->setEnabled(active);
    argumentsEdit->setReadOnly(active || building);
}
void MainWindow::showProgramArguments() {
    cancelMove();
    syncActions();
    argumentsBar->show();
    argumentsEdit->setFocus(Qt::ShortcutFocusReason);
    argumentsEdit->selectAll();
}
bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (stackView && event->type() == QEvent::Wheel &&
        (watched == stackView->viewport() || watched == stackView->verticalScrollBar())) {
        auto wheel = static_cast<QWheelEvent *>(event);
        int steps = 0;
        if (wheel->angleDelta().y()) {
            stackWheelRemainder += wheel->angleDelta().y();
            steps = stackWheelRemainder / 120;
            stackWheelRemainder %= 120;
        } else if (wheel->pixelDelta().y()) {
            stackPixelRemainder += wheel->pixelDelta().y();
            const int rowHeight = stackView->verticalHeader()->defaultSectionSize();
            steps = stackPixelRemainder / rowHeight;
            stackPixelRemainder %= rowHeight;
        } else {
            return QMainWindow::eventFilter(watched, event);
        }
        auto scroll = stackView->verticalScrollBar();
        scroll->setValue(scroll->value() - steps);
        wheel->accept();
        return true;
    }
    if (event->type() == QEvent::ContextMenu) {
        auto context = static_cast<QContextMenuEvent *>(event);
        auto widget = qobject_cast<QWidget *>(watched);
        if (watched == startButton ||
            (widget && widget == startButton->parentWidget() &&
             startButton->rect().contains(startButton->mapFromGlobal(context->globalPos())))) {
            showProgramArguments();
            context->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
void MainWindow::showError(QString text) {
    statusBar()->showMessage(text);
    QMessageBox::warning(this, "ASM101", text);
}
void MainWindow::appendStandardOutput(const QByteArray &data) {
    const QString text = stdoutDecoder(data);
    if (text.isEmpty())
        return;
    auto scroll = stdoutView->verticalScrollBar();
    const bool follow = scroll->value() == scroll->maximum();
    QTextCursor cursor(stdoutView->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    // Bound long single-line output as well as the number of retained lines.
    const int excess = stdoutView->document()->characterCount() - 1 - 1024 * 1024;
    if (excess > 0) {
        cursor.movePosition(QTextCursor::Start);
        int end = excess;
        if (stdoutView->document()->characterAt(end).isLowSurrogate())
            ++end;
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    if (follow)
        scroll->setValue(scroll->maximum());
}
bool MainWindow::save() {
    if (sourcePath.isEmpty())
        sourcePath = QFileDialog::getSaveFileName(
            this, "Save assembly", QDir::home().filePath("example.asm"), "NASM source (*.asm)");
    if (sourcePath.isEmpty())
        return false;
    QSaveFile f(sourcePath);
    if (!f.open(QIODevice::WriteOnly) || f.write(editor->toPlainText().toUtf8()) < 0 ||
        !f.commit()) {
        showError(f.errorString());
        return false;
    }
    editor->document()->setModified(false);
    sourceTitle->setText("SOURCE CODE · " + QFileInfo(sourcePath).fileName());
    return true;
}
bool MainWindow::confirmDiscard() {
    if (!editor->document()->isModified())
        return true;
    auto r = QMessageBox::question(this, "Unsaved source", "Save changes to the assembly source?",
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    return r == QMessageBox::Discard || (r == QMessageBox::Save && save());
}
void MainWindow::open() {
    if (active || building)
        return;
    if (!confirmDiscard())
        return;
    QString p = QFileDialog::getOpenFileName(this, "Open assembly", {}, "NASM source (*.asm)");
    if (p.isEmpty())
        return;
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) {
        showError(f.errorString());
        return;
    }
    sourcePath = p;
    editor->setPlainText(QString::fromUtf8(f.readAll()));
    editor->document()->setModified(false);
    editor->setExecutionLine(0);
    executable.clear();
    sourceTitle->setText("SOURCE CODE · " + QFileInfo(p).fileName());
    syncActions();
}
void MainWindow::build() {
    if (!save())
        return;
    executable.clear();
    building = true;
    syncActions();
    statusBar()->showMessage("Assembling…");
    QString obj = buildDir.filePath("program.o"), target = buildDir.filePath("program");
    builtSource = editor->toPlainText();
    editor->setReadOnly(true);
    disconnect(&builder, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, nullptr);
    connect(&builder, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, obj, target](int code, QProcess::ExitStatus status) {
                if (code || status != QProcess::NormalExit) {
                    building = false;
                    editor->setReadOnly(false);
                    showError(QString::fromUtf8(builder.readAllStandardError()));
                    syncActions();
                    return;
                }
                disconnect(&builder, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                           this, nullptr);
                connect(&builder, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                        [this, target](int code, QProcess::ExitStatus status) {
                            building = false;
                            editor->setReadOnly(false);
                            if (code || status != QProcess::NormalExit)
                                showError(QString::fromUtf8(builder.readAllStandardError()));
                            else {
                                executable = target;
                                statusBar()->showMessage("Build succeeded · Start F5");
                            }
                            syncActions();
                        });
                statusBar()->showMessage("Linking…");
                builder.start("ld", {obj, "-o", target});
            });
    builder.setWorkingDirectory(QFileInfo(sourcePath).absolutePath());
    builder.start("nasm", {"-f", "elf64", "-g", "-F", "dwarf", sourcePath, "-o", obj});
}
void MainWindow::updateSnapshot() {
    registers->updateValues(engine.registers);
    editor->setExecutionLine(engine.line);
    position->setText(QString("RIP  0x%1   %2 (%3 bytes)")
                          .arg(engine.rip, 16, 16, QChar('0'))
                          .arg(engine.instruction.toHtmlEscaped())
                          .arg(engine.instructionSize));
    changedFlags = haveFlags ? (oldFlags ^ engine.flags) : 0;
    oldFlags = engine.flags;
    haveFlags = true;
    updateFlagDisplay();
    memory->refresh();
    stackMemory->refresh();
    animateMove();
    syncActions();
    statusBar()->showMessage("Stopped · next machine instruction ready · F10 to step");
}
void MainWindow::closeEvent(QCloseEvent *e) {
    if (!confirmDiscard()) {
        e->ignore();
        return;
    }
    cancelMove();
    engine.stop();
    builder.kill();
    e->accept();
}

void MainWindow::cancelMove() {
    ++moveEpoch;
    pendingMove.reset();
    moveBusy = false;
    if (moveOverlay)
        moveOverlay->cancel();
}

static std::optional<quint64> readLittleEndian(const QByteArray &data, int bits) {
    if (data.size() != bits / 8)
        return {};
    quint64 value = 0;
    for (int n = 0; n < data.size(); ++n)
        value |= quint64(uchar(data[n])) << (8 * n);
    return value;
}

void MainWindow::stepInstruction() {
    if (!active || !engine.paused() || moveBusy)
        return;
    pendingMove = decodeMov(engine.instruction, engine.registers, engine.rip,
                            engine.instructionSize, engine.line);
    if (!pendingMove)
        pendingMove = decodeAdd(engine.instruction, engine.registers, engine.rip,
                                engine.instructionSize, engine.line);
    if (!pendingMove)
        pendingMove = decodePop(engine.instruction, engine.registers, engine.rip,
                                engine.instructionSize, engine.line);
    if (!pendingMove)
        pendingMove = decodePush(engine.instruction, engine.registers, engine.rip,
                                 engine.instructionSize, engine.line);
    moveBusy = pendingMove.has_value();
    syncActions();
    // Read sources before PUSH/POP can change RSP or overwrite aliased memory.
    if (pendingMove && ((pendingMove->source.kind == MoveOperand::Memory &&
                         pendingMove->operation != MoveTransfer::Mov) ||
                        (pendingMove->destination.kind == MoveOperand::Memory &&
                         pendingMove->operation == MoveTransfer::Add))) {
        const bool sourceMemory = pendingMove->source.kind == MoveOperand::Memory;
        const auto operand = sourceMemory ? pendingMove->source : pendingMove->destination;
        const int epoch = moveEpoch;
        engine.readMemory(operand.address, operand.bits / 8,
                          [this, epoch, sourceMemory, bits = operand.bits](QByteArray data) {
                              if (epoch != moveEpoch || !active || !engine.paused())
                                  return;
                              const auto value = readLittleEndian(data, bits);
                              if (!value) {
                                  pendingMove.reset();
                                  moveBusy = false;
                              } else if (sourceMemory)
                                  pendingMove->sourceValue = value;
                              else
                                  pendingMove->destinationValue = value;
                              engine.step();
                              syncActions();
                          });
    } else {
        engine.step();
        syncActions();
    }
}

std::optional<QPoint> MainWindow::operandPoint(const MoveOperand &operand, int line) const {
    if (operand.kind == MoveOperand::Immediate) {
        const auto block = editor->document()->findBlockByNumber(line - 1);
        if (!block.isValid())
            return {};
        const QString source = block.text().section(';', 0, 0);
        int first = source.lastIndexOf(',') + 1;
        while (first < source.size() && source[first].isSpace())
            ++first;
        int last = source.size();
        while (last > first && source[last - 1].isSpace())
            --last;
        QTextCursor cursor(block);
        cursor.setPosition(block.position() + first);
        const QRect begin = editor->cursorRect(cursor);
        cursor.setPosition(block.position() + qMax(first, last));
        const QRect end = editor->cursorRect(cursor);
        const QPoint point((begin.center().x() + end.center().x()) / 2, begin.center().y());
        if (!editor->viewport()->rect().contains(point))
            return {};
        return editor->viewport()->mapTo(const_cast<MainWindow *>(this), point);
    }
    if (operand.kind == MoveOperand::Memory) {
        MemoryModel *model = memory;
        QTableView *view = ram;
        if (!model->validAddress(operand.address)) {
            model = stackMemory;
            view = stackView;
        }
        const int row = model->rowForAddress(operand.address);
        if (row < 0 || !view->isVisible() || !model->validAddress(operand.address))
            return {};
        const QRect rect = view->visualRect(model->index(row, 1));
        const int width = MemoryDelegate::byteWidth(view->fontMetrics(), rect.width());
        const int offset = int(operand.address - model->address(row));
        const int label = model->hasSymbols(row) ? view->fontMetrics().height() + 2 : 0;
        const int footer = MemoryDelegate::pointerLabelHeight(model, row, view->font());
        const QPoint point(rect.x() + 10 + offset * width + width / 2,
                           rect.y() + label + (rect.height() - label - footer) / 2);
        if (!view->viewport()->rect().contains(point))
            return {};
        return view->viewport()->mapTo(const_cast<MainWindow *>(this), point);
    }
    const int row = registers->names.indexOf(operand.reg);
    if (row < 0)
        return {};
    const QModelIndex index = registers->index(row, 1);
    const QRect rect = registerView->visualRect(index);
    const QString value = registers->data(index).toString();
    QList<int> digits;
    for (int n = 2; n < value.size(); ++n)
        if (value[n] != ' ')
            digits.append(n);
    if (digits.size() != 16)
        return {};
    const int first = digits[16 - (operand.shift + operand.bits) / 4];
    const int last = digits[15 - operand.shift / 4] + 1;
    const auto metrics = registerView->fontMetrics();
    const int x = rect.x() + 3 +
                  (metrics.horizontalAdvance(value.left(first)) +
                   metrics.horizontalAdvance(value.left(last))) /
                      2;
    const QPoint point(x, rect.center().y());
    if (!registerView->viewport()->rect().contains(point))
        return {};
    return registerView->viewport()->mapTo(const_cast<MainWindow *>(this), point);
}

void MainWindow::animateMove() {
    if (!pendingMove)
        return;
    const MoveTransfer move = *pendingMove;
    pendingMove.reset();
    if (engine.rip != move.rip + move.instructionSize) {
        moveBusy = false; // A faulting instruction did not complete.
        return;
    }
    const int epoch = moveEpoch;
    auto display = [this, move, epoch](std::optional<quint64> actual) {
        if (epoch != moveEpoch)
            return;
        std::optional<quint64> expected = move.sourceValue;
        const bool addition = move.operation == MoveTransfer::Add;
        if (addition) {
            if (!move.sourceValue || !move.destinationValue)
                expected.reset();
            else
                expected =
                    (*move.destinationValue + *move.sourceValue) & moveMask(move.destination.bits);
        }
        if (!actual || (addition && !expected) || (expected && *expected != *actual)) {
            moveBusy = false;
            syncActions();
            return;
        }
        if (move.operation == MoveTransfer::Push || move.operation == MoveTransfer::Pop) {
            // Bring the participating registers/bytes into view before locating endpoints.
            for (const auto &operand : {move.source, move.destination}) {
                if (operand.kind == MoveOperand::Register) {
                    const int row = registers->names.indexOf(operand.reg);
                    if (row >= 0)
                        registerView->scrollTo(registers->index(row, 1));
                } else if (operand.kind == MoveOperand::Memory) {
                    MemoryModel *model =
                        memory->validAddress(operand.address) ? memory : stackMemory;
                    QTableView *view = model == memory ? ram : stackView;
                    const int row = model->rowForAddress(operand.address);
                    if (row >= 0 && view->isVisible())
                        view->scrollTo(model->index(row, 1));
                }
            }
        }
        const auto from = operandPoint(move.source, move.sourceLine);
        const auto to = operandPoint(move.destination, move.sourceLine);
        if (!from || !to) {
            moveBusy = false; // Preserve scrolling; animate only visible endpoints.
            syncActions();
            return;
        }
        const quint64 flyingValue = addition ? *move.sourceValue : *actual;
        const QString value = (addition ? "+ " : "") +
                              QString("0x%1")
                                  .arg(flyingValue, move.destination.bits / 4, 16, QChar('0'))
                                  .toUpper()
                                  .replace("0X", "0x");
        moveOverlay->start(
            value, *from, *to,
            [this, epoch] {
                if (epoch != moveEpoch)
                    return;
                moveBusy = false;
                syncActions();
            },
            addition                               ? "ADD"
            : move.operation == MoveTransfer::Pop  ? "POP"
            : move.operation == MoveTransfer::Push ? "PUSH"
                                                   : "MOV");
    };
    if (move.destination.kind == MoveOperand::Register) {
        if (!engine.registers.contains(move.destination.reg))
            display({});
        else
            display(moveRegisterValue(move.destination, engine.registers));
    } else {
        engine.readMemory(move.destination.address, move.destination.bits / 8,
                          [display, bits = move.destination.bits](QByteArray data) {
                              display(readLittleEndian(data, bits));
                          });
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (moveOverlay && moveBusy) {
        cancelMove();
        syncActions();
    }
}

void MainWindow::updateFlagDisplay() {
    if (!haveFlags) {
        flags->setText("CF —    ZF —    SF —    OF —<br>RFLAGS —");
        return;
    }
    QString html;
    const QStringList names = {"CF", "ZF", "SF", "OF"};
    const int bits[] = {0, 6, 7, 11};
    for (int i = 0; i < 4; ++i) {
        const bool changed = changedFlags & (quint64(1) << bits[i]);
        const auto color =
            changed ? themeColor("#64f0a0", "#006b35") : themeColor("#cad9e8", "#17212b");
        html += QString("<span style='color:%1'>%2  %3</span>　")
                    .arg(color.name(), names[i])
                    .arg((oldFlags >> bits[i]) & 1);
    }
    html += QString("<br>RFLAGS  0x%1").arg(oldFlags, 16, 16, QChar('0'));
    flags->setText(html);
}

void MainWindow::updateMemoryLegend() {
    const QStringList sections = {".text", ".rodata", ".data", ".bss", "stack"};
    const QColor colors[] = {themeColor("#72aef5", "#0756a3"), themeColor("#c291df", "#7e268b"),
                             themeColor("#70cba2", "#216b39"), themeColor("#d8b15e", "#805500"),
                             themeColor("#f29b86", "#a74329")};
    QStringList entries;
    for (int i = 0; i < sections.size(); ++i) {
        const auto &section = sections[i];
        const bool visible = section == "stack" ? stackVisible : memory->sectionVisible(section);
        entries << QString(
                       "<a href='%1' title='%2 %1' style='color:%3; text-decoration:%4'>%5 %1</a>")
                       .arg(section, visible ? "Hide" : "Show", colors[i].name(),
                            visible ? "none" : "line-through", visible ? "■" : "□");
    }
    memoryLegend->setText(entries.join("　"));
}

void MainWindow::refreshAppearance() {
    updateMemoryLegend();
    updateFlagDisplay();
    editor->refreshTheme();
    updateTextLayout();
    QPixmap magnifier(48, 48);
    magnifier.setDevicePixelRatio(2);
    magnifier.fill(Qt::transparent);
    QPainter painter(&magnifier);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(themeColor("#d2deeb", "#17212b"), 2, Qt::SolidLine, Qt::RoundCap));
    painter.drawEllipse(QRectF(3, 3, 13, 13));
    painter.drawLine(QPointF(15, 15), QPointF(22, 22));
    painter.drawLine(QPointF(6, 9.5), QPointF(13, 9.5));
    if (!zoomToggle->isChecked())
        painter.drawLine(QPointF(9.5, 6), QPointF(9.5, 13));
    painter.end();
    zoomToggle->setIcon(QIcon(magnifier));
    zoomToggle->setToolTip(zoomToggle->isChecked() ? "Restore normal font size (100%)"
                                                   : "Double font size for projection (200%)");
    for (auto table : {ram, stackView, registerView})
        table->viewport()->update();
}

void MainWindow::updateTextLayout() {
    centralWidget()->setFont(qApp->font());
    editor->refreshFontLayout();
    const auto registerMetrics = registerView->fontMetrics();
    registerView->verticalHeader()->setDefaultSectionSize(qMax(27, registerMetrics.height() + 8));
    registerView->setColumnWidth(1,
                                 registerMetrics.horizontalAdvance("0x00000000 0000 00 00") + 28);
    registerView->setColumnWidth(
        2, registerMetrics.horizontalAdvance("AX:-32768  AH:-128  AL:-128") + 20);
    registerView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    for (auto table : {ram, stackView}) {
        const auto metrics = table->fontMetrics();
        table->verticalHeader()->setDefaultSectionSize(qMax(28, metrics.height() + 8));
        table->setColumnWidth(1, MemoryDelegate::byteWidth(metrics, 0) * 8 + 20);
        table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    ram->setColumnWidth(
        0, qMax(310, ram->fontMetrics().horizontalAdvance("0x00000000 – 0x00000000") + 20));
    for (int row : memory->annotatedRows())
        ram->setRowHeight(row, ram->fontMetrics().height() * 2 + 8);
    stackView->setColumnWidth(
        0,
        stackView->fontMetrics().horizontalAdvance("0x0000000000000000 – 0x0000000000000000") + 20);
    updateStackLayout();
    ram->setMinimumHeight(3 * ram->verticalHeader()->defaultSectionSize() +
                          ram->horizontalHeader()->sizeHint().height());
    QTimer::singleShot(0, editor, [this] {
        editor->ensureCursorVisible();
    });
}

void MainWindow::updateStackLayout() {
    if (engine.registers.contains("rsp")) {
        const auto stackColor = themeColor("#ead55e", "#916000").name();
        const auto baseColor = themeColor("#b9cce0", "#344a60").name();
        stackTitle->setText(QString("STACK · <a href='rsp' style='color:%1'>stack (RSP) 0x%2</a>"
                                    " · <a href='rbp' style='color:%3'>base (RBP) 0x%4</a>")
                                .arg(stackColor)
                                .arg(engine.registers.value("rsp"), 16, 16, QChar('0'))
                                .arg(baseColor)
                                .arg(engine.registers.value("rbp"), 16, 16, QChar('0')));
    }
    const int rowHeight = stackView->verticalHeader()->defaultSectionSize();
    QSet<int> pointerRows;
    for (const auto &name : {QString("rsp"), QString("rbp")}) {
        if (!engine.registers.contains(name))
            continue;
        const int row = stackMemory->rowForAddress(engine.registers.value(name));
        if (row >= 0)
            pointerRows.insert(row);
    }
    const auto affected = stackPointerRows | pointerRows;
    for (int row : affected) {
        if (row < 0 || row >= stackMemory->rowCount())
            continue;
        const int height =
            rowHeight + MemoryDelegate::pointerLabelHeight(stackMemory, row, stackView->font());
        if (stackView->rowHeight(row) != height)
            stackView->setRowHeight(row, height);
    }
    stackPointerRows = pointerRows;
    const int chrome = stackView->horizontalHeader()->sizeHint().height() +
                       2 * stackView->frameWidth() +
                       stackView->horizontalScrollBar()->sizeHint().height();
    stackView->setMinimumHeight(3 * rowHeight + chrome);
    const auto rsp = engine.registers.constFind("rsp");
    if (rsp != engine.registers.cend() &&
        (stackScrollPending || !lastStackPointer || *lastStackPointer != rsp.value())) {
        lastStackPointer = rsp.value();
        if (stackMemory->rowForAddress(rsp.value()) < 0)
            return;
        stackScrollPending = false;
        // Apply row/footer geometry before positioning, and before starting any
        // transfer animation. A delayed scroll would cancel that animation.
        focusStackPointer("rsp");
    }
}
void MainWindow::focusStackPointer(const QString &name) {
    const auto pointer = engine.registers.constFind(name);
    if (pointer == engine.registers.cend())
        return;
    const int row = stackMemory->rowForAddress(pointer.value());
    if (row < 0) {
        statusBar()->showMessage(name.toUpper() + " is outside the displayed stack range.");
        return;
    }
    stackView->doItemsLayout();
    stackView->scrollTo(stackMemory->index(row, 1), QAbstractItemView::PositionAtCenter);
}
