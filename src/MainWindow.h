#pragma once
#include <QMainWindow>
#include <QTemporaryDir>
#include <QStringConverter>
#include "DebuggerEngine.h"
#include "MovAnimation.h"
class AssemblyEditor;
class MemoryModel;
class RegisterModel;
class QLabel;
class QTableView;
class QToolButton;
class QLineEdit;
class QToolBar;
class QSplitter;
class QPlainTextEdit;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

private:
    DebuggerEngine engine;
    QProcess builder;
    QTemporaryDir buildDir;
    AssemblyEditor *editor;
    MemoryModel *memory;
    MemoryModel *stackMemory;
    QTableView *stackView = nullptr;
    QLabel *stackTitle;
    QWidget *stackPanel;
    QSplitter *memorySplitter;
    QSet<int> stackPointerRows;
    bool stackScrollPending = false;
    std::optional<quint64> lastStackPointer;
    int stackWheelRemainder = 0;
    int stackPixelRemainder = 0;
    void focusStackPointer(const QString &name);
    QWidget *stdoutPanel;
    QPlainTextEdit *stdoutView;
    QStringDecoder stdoutDecoder{QStringDecoder::Utf8};
    void appendStandardOutput(const QByteArray &data);
    RegisterModel *registers;
    QTableView *ram;
    QTableView *registerView;
    MoveOverlay *moveOverlay = nullptr;
    std::optional<MoveTransfer> pendingMove;
    bool moveBusy = false;
    int moveEpoch = 0;
    void stepInstruction();
    void animateMove();
    void cancelMove();
    void refreshAppearance();
    void updateMemoryLegend();
    void updateTextLayout();
    void updateStackLayout();
    void updateFlagDisplay();
    std::optional<QPoint> operandPoint(const MoveOperand &operand, int line) const;
    QLabel *flags, *position, *sourceTitle, *memoryLegend;
    QAction *buildAction, *startAction, *stepAction, *stopAction;
    QToolButton *zoomToggle;
    QToolBar *argumentsBar;
    QLineEdit *argumentsEdit;
    QWidget *startButton;
    void showProgramArguments();
    QString sourcePath, executable, builtSource;
    bool active = false, building = false;
    bool stackVisible = true;
    quint64 oldFlags = 0, changedFlags = 0;
    bool haveFlags = false;
    bool save();
    bool confirmDiscard();
    void open();
    void build();
    void syncActions();
    void showError(QString);
    void updateSnapshot();
};
