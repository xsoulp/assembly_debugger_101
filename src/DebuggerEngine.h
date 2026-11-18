#pragma once
#include <QObject>
#include <QProcess>
#include <QMap>
#include <functional>
#include <memory>
#include "Mi.h"
class QSocketNotifier;
class QTemporaryDir;
struct Section {
    QString name;
    quint64 address = 0, size = 0;
};
class DebuggerEngine : public QObject {
    Q_OBJECT
public:
    explicit DebuggerEngine(QObject *parent = nullptr);
    ~DebuggerEngine();
    void start(const QString &executable, const QStringList &arguments = {});
    void step();
    void stop();
    void readMemory(quint64 address, int count, std::function<void(QByteArray)> callback);
    bool paused() const {
        return ready;
    }
    QList<Section> sections;
    QMap<quint64, QStringList> dataSymbols;
    QMap<QString, quint64> registers;
    quint64 rip = 0, flags = 0;
    int line = 0, instructionSize = 0;
    QString instruction;
signals:
    void snapshot();
    void running();
    void ended();
    void error(QString message);
    void standardOutput(const QByteArray &data);

private:
    QProcess process;
    QByteArray buffer;
    int token = 0;
    bool ready = false, terminating = false;
    QMap<int, std::function<void(QString, Mi)>> pending;
    QStringList registerNames;
    std::unique_ptr<QTemporaryDir> outputDirectory;
    QSocketNotifier *outputNotifier = nullptr;
    int outputFd = -1;
    bool prepareOutput();
    void readOutput();
    void closeOutput();
    void command(QString command, std::function<void(QString, Mi)> callback = {},
                 bool reportError = true);
    void consume();
    void refresh(const Mi &record);
    void loadSections(const QString &path);
};
