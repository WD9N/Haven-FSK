#include <QApplication>
#include "dsp/Constants.h"
#include "ui/MainWindow.h"

#ifdef QT_DEBUG
#include "dsp/FecSelfTest.h"
#include "dsp/FrameSelfTest.h"
#include "audio/AudioSelfTest.h"
#endif

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
