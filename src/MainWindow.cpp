#include "MainWindow.h"
#include "AssemblyEditor.h"
#include "MemoryModel.h"
#include "RegisterModel.h"
#include "Theme.h"
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
MainWindow::MainWindow(){
    setWindowTitle("ASM101 · Visual Assembly Debugger");resize(1400,920);
    auto toolbar=addToolBar("Execution");toolbar->setMovable(false);toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto add=[&](QString text,QString shortcut,auto callback){auto a=toolbar->addAction(text,this,callback);a->setShortcut(QKeySequence(shortcut));return a;};
    add("Open","Ctrl+O",[this]{open();});add("Save","Ctrl+S",[this]{save();});toolbar->addSeparator();
    buildAction=add("Build","F7",[this]{build();});startAction=add("Start","F5",[this]{
        if(builtSource!=editor->toPlainText()){showError("Source changed. Build before starting.");return;}
        cancelMove();active=true;haveFlags=false;registers->values.clear();registers->updateValues({});engine.start(executable);memory->resetSections();stackMemory->resetSections();stackTitle->setText("STACK · RSP — · 10 × 8 B");editor->setReadOnly(true);syncActions();statusBar()->showMessage("Starting GDB…");
    });
    stepAction=add("Step","F10",[this]{stepInstruction();});stopAction=add("Stop","Shift+F5",[this]{cancelMove();engine.stop();syncActions();});toolbar->widgetForAction(buildAction)->setObjectName("buildButton");toolbar->widgetForAction(stepAction)->setObjectName("stepButton");toolbar->widgetForAction(startAction)->setObjectName("startButton");toolbar->widgetForAction(stopAction)->setObjectName("stopButton");toolbar->addSeparator();position=new QLabel("RIP  —");toolbar->addWidget(position);
    auto horizontal=new QSplitter(Qt::Horizontal);auto left=new QSplitter(Qt::Vertical);horizontal->addWidget(left);
    auto source=new QWidget;auto sl=new QVBoxLayout(source);sl->setContentsMargins(0,0,0,0);sourceTitle=new QLabel("SOURCE CODE");sl->addWidget(sourceTitle);editor=new AssemblyEditor;sl->addWidget(editor);left->addWidget(source);
    auto regs=new QWidget;auto rl=new QVBoxLayout(regs);rl->setContentsMargins(0,0,0,0);auto registerHeader = new QHBoxLayout;
    registerHeader->addWidget(new QLabel("REGISTERS"));
    registerHeader->addStretch();
    registerHeader->addWidget(new QLabel("Decimal:"));
    auto signedToggle = new QCheckBox("Unsigned");
    signedToggle->setObjectName("signedDecimalToggle");
    signedToggle->setToolTip("Toggle signed or unsigned decimal values for the register parts.");
    registerHeader->addWidget(signedToggle);
    rl->addLayout(registerHeader);
    auto rt=new QTableView;registerView=rt;registers=new RegisterModel(this);rt->setModel(registers);rt->verticalHeader()->hide();rt->horizontalHeader()->setStretchLastSection(true);rt->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);rt->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Fixed);
    rt->setColumnWidth(1, rt->fontMetrics().horizontalAdvance("0x00000000 0000 00 00") + 28);rt->verticalHeader()->setDefaultSectionSize(27);rt->setMinimumHeight(280);rt->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);rt->setShowGrid(false);rt->setSelectionMode(QAbstractItemView::NoSelection);rl->addWidget(rt);
    connect(signedToggle, &QCheckBox::toggled, this, [this, signedToggle](bool enabled) {
        registers->setSignedDecimal(enabled);
        signedToggle->setText(enabled ? "Signed" : "Unsigned");
    });
    rl->addWidget(new QLabel("FLAGS"));flags=new QLabel("CF —    ZF —    SF —    OF —<br>RFLAGS —");flags->setTextFormat(Qt::RichText);flags->setWordWrap(true);flags->setMinimumHeight(65);rl->addWidget(flags);left->addWidget(regs);
    auto mem=new QWidget;auto ml=new QVBoxLayout(mem);ml->setContentsMargins(0,0,0,0);ml->addWidget(new QLabel("RAM · PROCESS MEMORY"));memoryLegend=new QLabel;memoryLegend->setWordWrap(true);ml->addWidget(memoryLegend);ram=new QTableView;ram->setObjectName("ramView");memory=new MemoryModel(&engine,this);ram->setModel(memory);ram->setItemDelegate(new MemoryDelegate(ram));ram->verticalHeader()->hide();ram->verticalHeader()->setDefaultSectionSize(28);connect(memory,&QAbstractItemModel::modelReset,this,[this]{
        updateTextLayout();
    });ram->horizontalHeader()->setStretchLastSection(true);ram->setColumnWidth(0,310);ram->setShowGrid(false);ram->setSelectionMode(QAbstractItemView::NoSelection);ml->addWidget(ram);auto memoryHint = new QLabel("Yellow outline · next instruction     Green underline · changed byte     ?? · unreadable");memoryHint->setWordWrap(true);ml->addWidget(memoryHint);
    stackTitle = new QLabel("STACK · RSP — · 10 × 8 B");
    stackTitle->setObjectName("stackTitle");
    stackTitle->setWordWrap(true);
    ml->addWidget(stackTitle);
    stackView = new QTableView;
    stackView->setObjectName("stackView");
    stackMemory = new MemoryModel(&engine, this, MemoryModel::Stack);
    stackView->setModel(stackMemory);
    stackView->setItemDelegate(new MemoryDelegate(stackView));
    stackView->verticalHeader()->hide();
    stackView->verticalHeader()->setDefaultSectionSize(28);
    stackView->horizontalHeader()->setStretchLastSection(true);
    stackView->setColumnWidth(0, stackView->fontMetrics().horizontalAdvance("0x0000000000000000 – 0x0000000000000000") + 20);
    stackView->setShowGrid(false);
    stackView->setSelectionMode(QAbstractItemView::NoSelection);
    stackView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    stackView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    stackView->setFixedHeight(10 * 28 + stackView->horizontalHeader()->sizeHint().height() + 2 * stackView->frameWidth());
    ml->addWidget(stackView);
    horizontal->addWidget(mem);horizontal->setSizes({640,760});left->setSizes({470,390});setCentralWidget(horizontal);
    moveOverlay=new MoveOverlay(this);
    auto cancelVisibleMove = [this] {
        if (moveOverlay->isVisible()) { cancelMove(); syncActions(); }
    };
    connect(horizontal, &QSplitter::splitterMoved, this, cancelVisibleMove);
    connect(left, &QSplitter::splitterMoved, this, cancelVisibleMove);
    for (auto table : {ram, registerView, stackView}) {
        connect(table->verticalScrollBar(), &QScrollBar::valueChanged, this, cancelVisibleMove);
        connect(table->horizontalScrollBar(), &QScrollBar::valueChanged, this, cancelVisibleMove);
    }

    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, cancelVisibleMove);
    connect(editor->horizontalScrollBar(), &QScrollBar::valueChanged, this, cancelVisibleMove);
    connect(&engine,&DebuggerEngine::snapshot,this,&MainWindow::updateSnapshot);
    connect(&engine,&DebuggerEngine::running,this,[this]{statusBar()->showMessage("Running");syncActions();});
    connect(&engine,&DebuggerEngine::ended,this,[this]{cancelMove();active=false;editor->setReadOnly(false);editor->setExecutionLine(0);statusBar()->showMessage("Session ended");syncActions();});
    connect(&engine,&DebuggerEngine::error,this,[this](QString s){cancelMove();showError(s);engine.stop();});
    connect(&builder,&QProcess::errorOccurred,this,[this](auto){building=false;editor->setReadOnly(false);showError(builder.errorString());syncActions();});
    connect(editor,&QPlainTextEdit::textChanged,this,[this]{syncActions();});
    QFile f(EXAMPLE_PATH);if(f.open(QIODevice::ReadOnly)){editor->setPlainText(QString::fromUtf8(f.readAll()));sourcePath=QFileInfo(EXAMPLE_PATH).isWritable()?QString(EXAMPLE_PATH):QString();editor->document()->setModified(false);sourceTitle->setText("SOURCE CODE · "+QFileInfo(EXAMPLE_PATH).fileName());}
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
    statusBar()->showMessage("Ready · Build F7 → Start F5 → Step F10");syncActions();
}
void MainWindow::syncActions(){buildAction->setEnabled(!active&&!building);startAction->setEnabled(!active&&!building&&!executable.isEmpty()&&builtSource==editor->toPlainText());stepAction->setEnabled(active&&engine.paused()&&!moveBusy);stopAction->setEnabled(active);}
void MainWindow::showError(QString text){statusBar()->showMessage(text);QMessageBox::warning(this,"ASM101",text);}
bool MainWindow::save(){if(sourcePath.isEmpty())sourcePath=QFileDialog::getSaveFileName(this,"Save assembly",QDir::home().filePath("example.asm"),"NASM source (*.asm)");if(sourcePath.isEmpty())return false;QSaveFile f(sourcePath);if(!f.open(QIODevice::WriteOnly)||f.write(editor->toPlainText().toUtf8())<0||!f.commit()){showError(f.errorString());return false;}editor->document()->setModified(false);sourceTitle->setText("SOURCE CODE · "+QFileInfo(sourcePath).fileName());return true;}
bool MainWindow::confirmDiscard(){if(!editor->document()->isModified())return true;auto r=QMessageBox::question(this,"Unsaved source","Save changes to the assembly source?",QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel);return r==QMessageBox::Discard||(r==QMessageBox::Save&&save());}
void MainWindow::open(){if(active||building)return;if(!confirmDiscard())return;QString p=QFileDialog::getOpenFileName(this,"Open assembly",{},"NASM source (*.asm)");if(p.isEmpty())return;QFile f(p);if(!f.open(QIODevice::ReadOnly)){showError(f.errorString());return;}sourcePath=p;editor->setPlainText(QString::fromUtf8(f.readAll()));editor->document()->setModified(false);editor->setExecutionLine(0);executable.clear();sourceTitle->setText("SOURCE CODE · "+QFileInfo(p).fileName());syncActions();}
void MainWindow::build(){
    if(!save())return;executable.clear();building=true;syncActions();statusBar()->showMessage("Assembling…");
    QString obj=buildDir.filePath("program.o"),target=buildDir.filePath("program");builtSource=editor->toPlainText();editor->setReadOnly(true);
    disconnect(&builder,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,nullptr);
    connect(&builder,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,obj,target](int code,QProcess::ExitStatus status){
        if(code||status!=QProcess::NormalExit){building=false;editor->setReadOnly(false);showError(QString::fromUtf8(builder.readAllStandardError()));syncActions();return;}
        disconnect(&builder,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,nullptr);
        connect(&builder,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,target](int code,QProcess::ExitStatus status){building=false;editor->setReadOnly(false);if(code||status!=QProcess::NormalExit)showError(QString::fromUtf8(builder.readAllStandardError()));else{executable=target;statusBar()->showMessage("Build succeeded · Start F5");}syncActions();});
        statusBar()->showMessage("Linking…");builder.start("ld",{obj,"-o",target});
    });
    builder.setWorkingDirectory(QFileInfo(sourcePath).absolutePath());builder.start("nasm",{"-f","elf64","-g","-F","dwarf",sourcePath,"-o",obj});
}
void MainWindow::updateSnapshot(){registers->updateValues(engine.registers);editor->setExecutionLine(engine.line);position->setText(QString("RIP  0x%1   %2 (%3 bytes)").arg(engine.rip,16,16,QChar('0')).arg(engine.instruction.toHtmlEscaped()).arg(engine.instructionSize));changedFlags=haveFlags?(oldFlags^engine.flags):0;oldFlags=engine.flags;haveFlags=true;updateFlagDisplay();memory->refresh();stackMemory->refresh();stackTitle->setText(QString("STACK · RSP 0x%1 · 10 × 8 B").arg(engine.registers.value("rsp"),16,16,QChar('0')));animateMove();syncActions();statusBar()->showMessage("Stopped · next machine instruction ready · F10 to step");}
void MainWindow::closeEvent(QCloseEvent *e){if(!confirmDiscard()){e->ignore();return;}cancelMove();engine.stop();builder.kill();e->accept();}

void MainWindow::cancelMove() {
    ++moveEpoch;
    pendingMove.reset();
    moveBusy = false;
    if (moveOverlay) moveOverlay->cancel();
}

static std::optional<quint64> readLittleEndian(const QByteArray &data, int bits) {
    if (data.size() != bits / 8) return {};
    quint64 value = 0;
    for (int n = 0; n < data.size(); ++n) value |= quint64(uchar(data[n])) << (8 * n);
    return value;
}

void MainWindow::stepInstruction() {
    if (!active || !engine.paused() || moveBusy) return;
    pendingMove = decodeMov(engine.instruction, engine.registers, engine.rip,
                            engine.instructionSize, engine.line);
    if (!pendingMove)
        pendingMove = decodeAdd(engine.instruction, engine.registers, engine.rip,
                                engine.instructionSize, engine.line);
    moveBusy = pendingMove.has_value();
    syncActions();
    // ADD needs both original operands, including any memory operand, before execution.
    if (pendingMove && pendingMove->addition &&
        (pendingMove->source.kind == MoveOperand::Memory || pendingMove->destination.kind == MoveOperand::Memory)) {
        const bool sourceMemory = pendingMove->source.kind == MoveOperand::Memory;
        const auto operand = sourceMemory ? pendingMove->source : pendingMove->destination;
        const int epoch = moveEpoch;
        engine.readMemory(operand.address, operand.bits / 8,
                          [this, epoch, sourceMemory, bits = operand.bits](QByteArray data) {
            if (epoch != moveEpoch || !active || !engine.paused()) return;
            const auto value = readLittleEndian(data, bits);
            if (!value) { pendingMove.reset(); moveBusy = false; }
            else if (sourceMemory) pendingMove->sourceValue = value;
            else pendingMove->destinationValue = value;
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
        if (!block.isValid()) return {};
        const QString source = block.text().section(';', 0, 0);
        int first = source.lastIndexOf(',') + 1;
        while (first < source.size() && source[first].isSpace()) ++first;
        int last = source.size();
        while (last > first && source[last - 1].isSpace()) --last;
        QTextCursor cursor(block);
        cursor.setPosition(block.position() + first);
        const QRect begin = editor->cursorRect(cursor);
        cursor.setPosition(block.position() + qMax(first, last));
        const QRect end = editor->cursorRect(cursor);
        const QPoint point((begin.center().x() + end.center().x()) / 2, begin.center().y());
        if (!editor->viewport()->rect().contains(point)) return {};
        return editor->viewport()->mapTo(const_cast<MainWindow *>(this), point);
    }
    if (operand.kind == MoveOperand::Memory) {
        MemoryModel *model = memory;
        QTableView *view = ram;
        if (!model->validAddress(operand.address)) { model = stackMemory; view = stackView; }
        const int row = model->rowForAddress(operand.address);
        if (row < 0 || !model->validAddress(operand.address)) return {};
        const QRect rect = view->visualRect(model->index(row, 1));
        const int width = MemoryDelegate::byteWidth(view->fontMetrics(), rect.width());
        const int offset = int(operand.address - model->address(row));
        const int label = model->hasSymbols(row) ? view->fontMetrics().height() + 2 : 0;
        const QPoint point(rect.x() + 10 + offset * width + width / 2,
                           rect.y() + label + (rect.height() - label) / 2);
        if (!view->viewport()->rect().contains(point)) return {};
        return view->viewport()->mapTo(const_cast<MainWindow *>(this), point);
    }
    const int row = registers->names.indexOf(operand.reg);
    if (row < 0) return {};
    const QModelIndex index = registers->index(row, 1);
    const QRect rect = registerView->visualRect(index);
    const QString value = registers->data(index).toString();
    QList<int> digits;
    for (int n = 2; n < value.size(); ++n)
        if (value[n] != ' ') digits.append(n);
    if (digits.size() != 16) return {};
    const int first = digits[16 - (operand.shift + operand.bits) / 4];
    const int last = digits[15 - operand.shift / 4] + 1;
    const auto metrics = registerView->fontMetrics();
    const int x = rect.x() + 3 + (metrics.horizontalAdvance(value.left(first)) +
                                  metrics.horizontalAdvance(value.left(last))) / 2;
    const QPoint point(x, rect.center().y());
    if (!registerView->viewport()->rect().contains(point)) return {};
    return registerView->viewport()->mapTo(const_cast<MainWindow *>(this), point);
}

void MainWindow::animateMove() {
    if (!pendingMove) return;
    const MoveTransfer move = *pendingMove;
    pendingMove.reset();
    if (engine.rip != move.rip + move.instructionSize) {
        moveBusy = false; // A faulting instruction did not complete.
        return;
    }
    const int epoch = moveEpoch;
    auto display = [this, move, epoch](std::optional<quint64> actual) {
        if (epoch != moveEpoch) return;
        std::optional<quint64> expected = move.sourceValue;
        if (move.addition) {
            if (!move.sourceValue || !move.destinationValue) expected.reset();
            else expected = (*move.destinationValue + *move.sourceValue) & moveMask(move.destination.bits);
        }
        if (!actual || (move.addition && !expected) || (expected && *expected != *actual)) {
            moveBusy = false;
            syncActions();
            return;
        }
        const auto from = operandPoint(move.source, move.sourceLine);
        const auto to = operandPoint(move.destination, move.sourceLine);
        if (!from || !to) {
            moveBusy = false; // Preserve scrolling; animate only visible endpoints.
            syncActions();
            return;
        }
        const quint64 flyingValue = move.addition ? *move.sourceValue : *actual;
        const QString value = (move.addition ? "+ " : "") +
            QString("0x%1").arg(flyingValue, move.destination.bits / 4, 16, QChar('0')).toUpper().replace("0X", "0x");
        moveOverlay->start(value, *from, *to, [this, epoch] {
            if (epoch != moveEpoch) return;
            moveBusy = false;
            syncActions();
        }, move.addition ? "ADD" : "MOV");
    };
    if (move.destination.kind == MoveOperand::Register) {
        if (!engine.registers.contains(move.destination.reg)) display({});
        else display(moveRegisterValue(move.destination, engine.registers));
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
        const auto color = changed ? themeColor("#64f0a0", "#006b35") : themeColor("#cad9e8", "#17212b");
        html += QString("<span style='color:%1'>%2  %3</span>　")
            .arg(color.name(), names[i]).arg((oldFlags >> bits[i]) & 1);
    }
    html += QString("<br>RFLAGS  0x%1").arg(oldFlags, 16, 16, QChar('0'));
    flags->setText(html);
}

void MainWindow::refreshAppearance() {
    memoryLegend->setText(QString("<span style='color:%1'>■ .text</span>　<span style='color:%2'>■ .rodata</span>　<span style='color:%3'>■ .data</span>　<span style='color:%4'>■ .bss</span>")
        .arg(themeColor("#72aef5", "#0756a3").name(), themeColor("#c291df", "#7e268b").name(),
             themeColor("#70cba2", "#216b39").name(), themeColor("#d8b15e", "#805500").name()));
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
    if (!zoomToggle->isChecked()) painter.drawLine(QPointF(9.5, 6), QPointF(9.5, 13));
    painter.end();
    zoomToggle->setIcon(QIcon(magnifier));
    zoomToggle->setToolTip(zoomToggle->isChecked() ? "Restore normal font size (100%)" : "Double font size for projection (200%)");
    for (auto table : {ram, stackView, registerView})
        table->viewport()->update();
}

void MainWindow::updateTextLayout() {
    centralWidget()->setFont(qApp->font());
    editor->refreshFontLayout();
    const auto registerMetrics = registerView->fontMetrics();
    registerView->verticalHeader()->setDefaultSectionSize(qMax(27, registerMetrics.height() + 8));
    registerView->setColumnWidth(1, registerMetrics.horizontalAdvance("0x00000000 0000 00 00") + 28);
    registerView->setColumnWidth(2, registerMetrics.horizontalAdvance("AX:-32768  AH:-128  AL:-128") + 20);
    registerView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    for (auto table : {ram, stackView}) {
        const auto metrics = table->fontMetrics();
        table->verticalHeader()->setDefaultSectionSize(qMax(28, metrics.height() + 8));
        table->setColumnWidth(1, MemoryDelegate::byteWidth(metrics, 0) * 8 + 20);
        table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    ram->setColumnWidth(0, qMax(310, ram->fontMetrics().horizontalAdvance("0x00000000 – 0x00000000") + 20));
    for (int row : memory->annotatedRows())
        ram->setRowHeight(row, ram->fontMetrics().height() * 2 + 8);
    stackView->setColumnWidth(0, stackView->fontMetrics().horizontalAdvance("0x0000000000000000 – 0x0000000000000000") + 20);
    const int rowHeight = stackView->verticalHeader()->defaultSectionSize();
    const int chrome = stackView->horizontalHeader()->sizeHint().height() + 2 * stackView->frameWidth()
                     + stackView->horizontalScrollBar()->sizeHint().height();
    const int fullHeight = 10 * rowHeight + chrome;
    if (qApp->property("doubleText").toBool()) {
        stackView->setMinimumHeight(3 * rowHeight + chrome);
        stackView->setMaximumHeight(fullHeight);
        stackView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    } else {
        stackView->setFixedHeight(fullHeight);
        stackView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    ram->setMinimumHeight(3 * ram->verticalHeader()->defaultSectionSize() + ram->horizontalHeader()->sizeHint().height());
    QTimer::singleShot(0, editor, [this] { editor->ensureCursorVisible(); });
}
