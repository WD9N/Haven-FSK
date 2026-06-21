#include <QApplication>
#include <QMessageBox>
#include "ui/MainWindow.h"
#include "dsp/FecSelfTest.h"

int main(int argc, char *argv[])
{
#ifdef QT_DEBUG
    if (!HavenFSK::runFecSelfTest()) {
        return 1;
    }
#endif

    QApplication app(argc, argv);

    app.setApplicationName("HAVEN-FSK");
    app.setApplicationVersion("0.2.0");
    app.setOrganizationName("WD9N");

    MainWindow w;
    w.show();

    return app.exec();
}
