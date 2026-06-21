#include <QApplication>
#include <QMessageBox>
#include "ui/MainWindow.h"
#include "dsp/Constants.h"
#include "dsp/FecSelfTest.h"
#include "dsp/FrameSelfTest.h"
#include "audio/AudioSelfTest.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setOrganizationName("WD9N");
    QApplication::setOrganizationDomain("github.com/WD9N");
    QApplication::setApplicationName("HAVEN-FSK");
    QApplication::setApplicationVersion(HavenFSK::APP_VERSION);

#ifdef QT_DEBUG
    if (!HavenFSK::runFecSelfTest())    return 1;
    if (!HavenFSK::runFrameSelfTest())  return 1;
    if (!HavenFSK::runAudioSelfTest())  return 1;
#endif

    MainWindow w;
    w.show();

    return app.exec();
}
