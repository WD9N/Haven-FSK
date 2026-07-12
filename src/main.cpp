#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <QDebug>
#include <QIcon>
#include <QStandardPaths>
#include <QDir>
#include "dsp/Constants.h"
#include "dsp/DspLog.h"
#include "dsp/WeakSignalBench.h"
#include "ui/MainWindow.h"
#include <cstring>
#include <cstdlib>

// Self-tests: run automatically in Debug builds, and in ANY build via
// --self-test (headless CI entry point — see .github/workflows/ci.yml).
#include "dsp/FecSelfTest.h"
#include "dsp/FrameSelfTest.h"
#include "dsp/MfskLoopbackSelfTest.h"
#include "audio/AudioSelfTest.h"
#include "dsp/psk/Psk31SelfTest.h"

static QFile     g_logFile;
static QMutex    g_logMutex;
static int       g_debugLinesSinceFlush = 0;

static void messageHandler(QtMsgType type, const QMessageLogContext&,
                            const QString& msg)
{
    const char* level =
        (type == QtWarningMsg)  ? "WARN " :
        (type == QtCriticalMsg) ? "CRIT " :
        (type == QtFatalMsg)    ? "FATAL" : "DEBUG";

    QString line = QDateTime::currentDateTime()
                       .toString("hh:mm:ss.zzz") +
                   " [" + level + "] " + msg + "\n";

    QMutexLocker lk(&g_logMutex);
    if (g_logFile.isOpen()) {
        g_logFile.write(line.toUtf8());
        // flush() forces a real disk sync -- doing that on every single
        // DEBUG line was expensive enough to compete with the main
        // thread's real-time job of pulling audio chunks off the OS
        // buffer promptly (AudioEngine's RX path has no dedicated
        // thread — see DECISIONS.md). MfskModem alone can log 100+
        // DEBUG lines during a single message's frame collection.
        // WARN/CRIT/FATAL are rare and worth persisting immediately;
        // DEBUG is buffered and flushed periodically instead (plus an
        // unconditional flush on app exit — see main(), below).
        if (type != QtDebugMsg) {
            g_logFile.flush();
            g_debugLinesSinceFlush = 0;
        } else if (++g_debugLinesSinceFlush >= 50) {
            g_logFile.flush();
            g_debugLinesSinceFlush = 0;
        }
    }

    // Also write to stderr so Qt Creator output pane still works
    fputs(qPrintable(line), stderr);
}

int main(int argc, char* argv[]) {
    // Headless weak-signal benchmark — pure DSP, no Qt needed, so it
    // runs before any Qt setup and exits. See WeakSignalBench.h.
    // Usage: HavenFSK.exe --bench [trialsPerPoint]
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--bench") == 0) {
            int trials = (i + 1 < argc) ? std::atoi(argv[i + 1]) : 0;
            return HavenFSK::runWeakSignalBench(trials > 0 ? trials : 10)
                       ? 0 : 1;
        }
        if (std::strcmp(argv[i], "--bench-sync") == 0) {
            int trials = (i + 1 < argc) ? std::atoi(argv[i + 1]) : 0;
            return HavenFSK::runSyncThresholdStudy(trials > 0 ? trials : 10)
                       ? 0 : 1;
        }
    }

    // Select best audio backend per platform BEFORE QApplication.
    // Must precede any Qt object construction.
#ifdef Q_OS_WIN
    // Windows Media Foundation gives better large-buffer management than
    // raw WASAPI for 3-4 second TX audio (handles QBuffer pull mode cleanly)
    qputenv("QT_MULTIMEDIA_PREFERRED_PLUGINS", "windowsmediafoundation");
#elif defined(Q_OS_LINUX) && defined(__arm__)
    // Raspberry Pi: force ALSA for direct hardware access
    qputenv("QT_MULTIMEDIA_PREFERRED_PLUGINS", "alsa");
#endif
// Linux desktop: no override — Qt auto-selects PulseAudio/PipeWire

    QApplication app(argc, argv);

    // Window/taskbar icon on all platforms (embedded via haven_fsk.qrc).
    // The exe's Explorer icon comes separately from resources/haven_fsk.rc.
    // Multiple sizes so Windows picks a crisp one per context instead of
    // scaling a single bitmap.
    {
        QIcon appIcon;
        for (int size : {16, 32, 48, 64, 128, 256})
            appIcon.addFile(QString(":/icons/Haven_fsk_%1.png").arg(size));
        app.setWindowIcon(appIcon);
    }

    // Open rolling log file next to the executable (portable convention),
    // truncated on each launch so it never grows unbounded. Falls back to
    // the per-user AppData folder when the exe dir isn't writable (e.g.
    // the app was unzipped into Program Files) — attempting the open is
    // the reliable writability test on Windows.
    // Must come AFTER QApplication construction: applicationDirPath()
    // returns an empty string (with a warning) before the app object
    // exists, which pointed the log at the drive root where open()
    // silently fails — so no haven_debug.log was ever written.
    const QIODevice::OpenMode logMode =
        QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text;
    g_logFile.setFileName(
        QCoreApplication::applicationDirPath() + "/haven_debug.log");
    if (!g_logFile.open(logMode)) {
        const QString dataDir = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        g_logFile.setFileName(dataDir + "/haven_debug.log");
        if (!g_logFile.open(logMode))
            fputs("haven_debug.log could not be opened — logging to stderr only\n",
                  stderr);
    }
    qInstallMessageHandler(messageHandler);
    if (g_logFile.isOpen())
        qDebug() << "Debug log:" << g_logFile.fileName();

    // Route the Qt-free DSP layer's logging (DspLog shim, ADR-124) into
    // the same qDebug/qWarning stream as everything else, so DSP lines
    // land in haven_debug.log with the usual timestamps and flushing.
    HavenFSK::setDspLogSink([](HavenFSK::DspLogLevel level,
                               const std::string& msg) {
        if (level == HavenFSK::DspLogLevel::Warning)
            qWarning().noquote() << QString::fromStdString(msg);
        else
            qDebug().noquote() << QString::fromStdString(msg);
    });

    // Buffered DEBUG lines (see messageHandler) could otherwise leave the
    // last <50 lines unflushed at clean shutdown.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        QMutexLocker lk(&g_logMutex);
        if (g_logFile.isOpen()) g_logFile.flush();
    });

    QApplication::setOrganizationName("WD9N");
    QApplication::setOrganizationDomain("github.com/WD9N");
    QApplication::setApplicationName("HAVEN-FSK");
    QApplication::setApplicationVersion(HavenFSK::APP_VERSION);

    // --self-test: run the DSP suite headlessly and exit — works in any
    // build config (CI runs Release), no GUI, no audio devices needed.
    // The audio self-test is deliberately excluded: CI runners have no
    // audio hardware, and its device-enumeration check is meaningless
    // there. Debug builds still run the full suite (audio included)
    // before showing the GUI, as always.
    const bool selfTestOnly = app.arguments().contains("--self-test");
    if (selfTestOnly) {
        bool ok = HavenFSK::runFecSelfTest()
               && HavenFSK::runFrameSelfTest()
               && HavenFSK::runMfskLoopbackSelfTest()
               && HavenFSK::runPsk31SelfTest();
        return ok ? 0 : 1;
    }

#ifdef QT_DEBUG
    if (!HavenFSK::runFecSelfTest())            return 1;
    if (!HavenFSK::runFrameSelfTest())          return 1;
    if (!HavenFSK::runMfskLoopbackSelfTest())   return 1;
    if (!HavenFSK::runPsk31SelfTest())          return 1;
    if (!HavenFSK::runAudioSelfTest())          return 1;
#endif

    MainWindow window;
    window.show();

    return app.exec();
}
