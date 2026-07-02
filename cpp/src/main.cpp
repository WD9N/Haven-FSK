#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include "dsp/Constants.h"
#include "ui/MainWindow.h"

#ifdef QT_DEBUG
#include "dsp/FecSelfTest.h"
#include "dsp/FrameSelfTest.h"
#include "audio/AudioSelfTest.h"
#include "dsp/psk/Psk31SelfTest.h"
#endif

static QFile     g_logFile;
static QMutex    g_logMutex;

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
        g_logFile.flush();
    }

    // Also write to stderr so Qt Creator output pane still works
    fputs(qPrintable(line), stderr);
}

int main(int argc, char* argv[]) {
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

    // Open rolling log file next to the executable.
    // Truncated on each launch so it never grows unbounded.
    g_logFile.setFileName(
        QCoreApplication::applicationDirPath() + "/haven_debug.log");
    g_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    qInstallMessageHandler(messageHandler);

    QApplication app(argc, argv);

    QApplication::setOrganizationName("WD9N");
    QApplication::setOrganizationDomain("github.com/WD9N");
    QApplication::setApplicationName("HAVEN-FSK");
    QApplication::setApplicationVersion(HavenFSK::APP_VERSION);

#ifdef QT_DEBUG
    if (!HavenFSK::runFecSelfTest())   return 1;
    if (!HavenFSK::runFrameSelfTest()) return 1;
    if (!HavenFSK::runAudioSelfTest()) return 1;
#endif

    MainWindow window;
    window.show();

    return app.exec();
}
