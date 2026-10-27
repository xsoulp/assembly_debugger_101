#pragma once
#include <QMainWindow>
#include <QTemporaryDir>
#include "DebuggerEngine.h"
#include "MovAnimation.h"
class AssemblyEditor; class MemoryModel; class RegisterModel; class QLabel; class QTableView;
class MainWindow:public QMainWindow {
    Q_OBJECT
public: MainWindow();
protected:void closeEvent(QCloseEvent *)override;
    void resizeEvent(QResizeEvent *) override;
private:
    DebuggerEngine engine; QProcess builder; QTemporaryDir buildDir;
    AssemblyEditor *editor; MemoryModel *memory; MemoryModel *stackMemory; QTableView *stackView; QLabel *stackTitle; RegisterModel *registers; QTableView *ram; QTableView *registerView;
    MoveOverlay *moveOverlay = nullptr;
    std::optional<MoveTransfer> pendingMove;
    bool moveBusy = false;
    int moveEpoch = 0;
    void stepInstruction();
    void animateMove();
    void cancelMove();
    void refreshAppearance();
    void updateFlagDisplay();
    std::optional<QPoint> operandPoint(const MoveOperand &operand, int line) const;
    QLabel *flags,*position,*sourceTitle,*memoryLegend; QAction *buildAction,*startAction,*stepAction,*stopAction;
    QString sourcePath,executable,builtSource; bool active=false,building=false; quint64 oldFlags=0,changedFlags=0;bool haveFlags=false;
    bool save(); bool confirmDiscard(); void open(); void build(); void syncActions(); void showError(QString); void updateSnapshot();
};
