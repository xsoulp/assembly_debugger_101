// Record the real ASM101 widgets and GDB session; captions are composed separately.
#include "MainWindow.h"
#include "AssemblyEditor.h"
#include "MemoryModel.h"
#include "RegisterModel.h"
#include "Theme.h"
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QSplitter>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <functional>

static void require(bool ok, const QString &message) {
    if (!ok) {
        qCritical().noquote() << message;
        std::exit(1);
    }
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    require(argc == 2, "Usage: record_demo OUTPUT.mp4");
    applyTheme(app);
    MainWindow window;
    window.resize(1824, 792);
    window.show();
    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        for (auto widget : app.topLevelWidgets())
            if (auto box = qobject_cast<QMessageBox *>(widget))
                require(false, box->text());
    });
    watchdog.start(100);
    auto wait = [&](const std::function<bool()> &condition) {
        QElapsedTimer timer;
        timer.start();
        while (!condition() && timer.elapsed() < 10000)
            QTest::qWait(10);
        return condition();
    };
    auto action = [&](const QString &text) -> QAction * {
        for (auto a : window.findChildren<QAction *>())
            if (a->text() == text)
                return a;
        require(false, "Missing action: " + text);
        return nullptr;
    };
    RegisterModel *registers = nullptr;
    QTableView *registerView = nullptr;
    for (auto table : window.findChildren<QTableView *>())
        if (auto model = dynamic_cast<RegisterModel *>(table->model())) {
            registers = model;
            registerView = table;
        }
    require(registers, "Missing registers");
    // Fit the source and the six relevant registers without scrolling mid-recording.
    registerView->setMinimumHeight(204);
    for (int row = 0; row < registers->names.size(); ++row)
        registerView->setRowHidden(row, !QStringList{"rax", "rbx", "rcx", "rdx", "rsp", "rip"}
                                           .contains(registers->names[row]));
    for (auto splitter : window.findChildren<QSplitter *>())
        splitter->setSizes(splitter->orientation() == Qt::Horizontal ? QList<int>{800, 1014}
                                                                    : QList<int>{345, 345});
    action("Build")->trigger();
    require(wait([&] { return action("Start")->isEnabled(); }), "Build did not finish");
    action("Start")->trigger();
    require(wait([&] { return action("Step")->isEnabled(); }), "GDB did not stop at _start");
    // Use the app's own section filter to keep both variables visible above the stack.
    QMetaObject::invokeMethod(window.findChild<QLabel *>("memoryLegend"), "linkActivated",
                              Qt::DirectConnection, Q_ARG(QString, QString(".text")));
    auto ram = window.findChild<QTableView *>("ramView");
    auto memory = static_cast<MemoryModel *>(ram->model());
    auto stack = static_cast<MemoryModel *>(window.findChild<QTableView *>("stackView")->model());
    quint64 valueAddress = 0, resultAddress = 0;
    for (int row : memory->annotatedRows())
        for (int n = 0; n < 8; ++n) {
            const auto address = memory->address(row) + n;
            if (memory->symbolsAt(address).contains("value")) valueAddress = address;
            if (memory->symbolsAt(address).contains("result")) resultAddress = address;
        }
    require(valueAddress && resultAddress, "Missing demo variables");
    const quint64 initialRsp = registers->values.value("rsp");
    require(wait([&] { return memory->byte(valueAddress) == 10 && stack->byte(initialRsp - 8) >= 0; }),
            "Memory did not load");
    QTest::qWait(200);

    const QString output = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    QProcess encoder;
    encoder.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    encoder.start("ffmpeg", {"-hide_banner", "-loglevel", "warning", "-y", "-f", "rawvideo",
        "-pixel_format", "bgra", "-video_size", "1920x1080", "-framerate", "30", "-i", "pipe:0",
        "-an", "-c:v", "libx264", "-preset", "fast", "-crf", "18", "-pix_fmt", "yuv420p",
        "-movflags", "+faststart", "-frames:v", "450", output});
    require(encoder.waitForStarted(), "Could not start ffmpeg");

    struct Beat { int frame; QString label, code, description; };
    const QList<Beat> beats = {
        {24, "01  READ A VARIABLE", "mov rax, [value]", "value = 10  →  RAX = 10"},
        {69, "02  UPDATE A REGISTER", "add rax, 5", "RAX: 10 + 5 = 15  (0x0F)"},
        {114, "03  WRITE A VARIABLE", "mov [value], rax", "RAX = 15  →  value = 15"},
        {159, "04  COPY BETWEEN REGISTERS", "mov rbx, rax", "RAX = 15  →  RBX = 15"},
        {204, "05  WRITE THE STACK", "push rbx", "RBX = 15  →  [RSP] = 15  ·  RSP − 8"},
        {249, "06  READ THE STACK", "mov rcx, [rsp]", "[RSP] = 15  →  RCX = 15"},
        {294, "07  POP INTO A REGISTER", "pop rdx", "[RSP] = 15  →  RDX = 15  ·  RSP + 8"},
        {339, "08  SAVE THE RESULT", "mov [result], rdx", "RDX = 15  →  result = 15"}
    };
    int beat = -1;
    int animationFrames = 0;
    const auto overlay = window.findChild<QWidget *>("movOverlay");
    for (int frame = 0; frame < 450; ++frame) {
        QElapsedTimer pacing;
        pacing.start();
        if (beat + 1 < beats.size() && frame == beats[beat + 1].frame) {
            require(action("Step")->isEnabled(), "Previous instruction is still running");
            if (beat >= 0) {
                require(registers->values.value("rax") == (beat == 0 ? 10u : 15u), "Wrong RAX");
                if (beat >= 2) require(memory->byte(valueAddress) == 15, "Variable write failed");
                if (beat >= 4) require(stack->byte(initialRsp - 8) == 15, "Stack write failed");
                if (beat >= 5) require(registers->values.value("rcx") == 15, "Stack read failed");
                if (beat >= 6) require(registers->values.value("rdx") == 15 &&
                    registers->values.value("rsp") == initialRsp, "POP failed to restore RSP");
            }
            ++beat;
            qInfo().noquote() << beats[beat].label << "·" << beats[beat].code;
            action("Step")->trigger();
        }
        QApplication::processEvents();
        if (overlay && overlay->isVisible()) ++animationFrames;
        QImage image(1920, 1080, QImage::Format_ARGB32);
        image.fill(QColor("#071018"));
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        auto text = [&](QRect rect, const QString &value, int size, QColor color,
                        bool bold = false, bool mono = false, int align = Qt::AlignLeft | Qt::AlignVCenter) {
            QFont font(mono ? "DejaVu Sans Mono" : "DejaVu Sans");
            font.setPixelSize(size);
            font.setBold(bold);
            p.setFont(font);
            p.setPen(color);
            p.drawText(rect, align, value);
        };
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#72aef5"));
        p.drawRoundedRect(QRectF(48, 31, 7, 46), 3, 3);
        text({72, 22, 220, 62}, "ASM101", 42, QColor("#f2f7ff"), true);
        text({310, 28, 1100, 54}, "See assembly. Follow every value.", 32, QColor("#b4c9dc"));
        text({1500, 33, 372, 44}, "REAL x86-64 · STEP BY STEP", 17, QColor("#72aef5"), true,
             false, Qt::AlignRight | Qt::AlignVCenter);
        p.setBrush(QColor("#122230"));
        p.drawRoundedRect(QRectF(42, 106, 1836, 804), 12, 12);
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(QRectF(48, 112, 1824, 792), 7, 7);
        p.setClipPath(clip);
        p.drawPixmap(QRect(48, 112, 1824, 792), window.grab());
        p.restore();
        QString label = "VARIABLES  →  REGISTERS  →  STACK";
        QString code = "Start with 10. Follow it to 15.";
        QString description = "A tiny NASM program. A real GDB session.";
        if (beat >= 0) {
            label = beats[beat].label;
            code = beats[beat].code;
            description = beats[beat].description;
        }
        const bool closing = frame >= 390;
        if (closing) {
            label = "WATCH THE DATA MOVE";
            code = "Variables. Registers. Stack.";
            description = "value = 15  ·  result = 15  ·  stack pointer restored";
        }
        text({48, 926, 1000, 30}, label, 18, QColor("#70cba2"), true);
        text({48, 958, 1050, 50}, code, 31, QColor("#f2f7ff"), true, !closing && beat >= 0);
        text({48, 1010, 1230, 34}, description, 22, QColor("#b4c9dc"));
        text({1290, 953, 582, 36}, "github.com/xsoulp", 23, QColor("#c4d9ef"), true,
             false, Qt::AlignRight | Qt::AlignVCenter);
        text({1230, 991, 642, 34}, "assembly_debugger_101", 21, QColor("#72aef5"), false,
             true, Qt::AlignRight | Qt::AlignVCenter);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#192a36"));
        p.drawRect(48, 1060, 1824, 3);
        p.setBrush(QColor("#70cba2"));
        p.drawRect(QRectF(48, 1060, 1824.0 * (frame + 1) / 450.0, 3));
        p.end();
        if (frame == 225) require(image.save(QFileInfo(output).dir().filePath("asm101-linkedin-cover.png")), "Could not save cover");
        const auto bytes = reinterpret_cast<const char *>(image.constBits());
        require(encoder.write(bytes, image.sizeInBytes()) == image.sizeInBytes(), "Could not write frame");
        while (encoder.bytesToWrite() > 0)
            require(encoder.waitForBytesWritten(30000), "ffmpeg stopped accepting frames");
        QTest::qWait(qMax(1, 33 - int(pacing.elapsed())));
    }
    require(wait([&] { return action("Step")->isEnabled() && memory->byte(resultAddress) == 15; }),
            "Final result is incorrect");
    require(animationFrames >= 40, "Transfer animations were not captured");
    encoder.closeWriteChannel();
    require(encoder.waitForFinished(30000) && encoder.exitCode() == 0, "Video encoding failed");
    action("Stop")->trigger();
    require(wait([&] { return action("Start")->isEnabled(); }), "Debugger did not stop");
    qInfo() << "Saved 450 frames / 15 seconds; animation frames:" << animationFrames;
}
