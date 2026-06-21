#include "MainWindow.h"
#include "dsp/Constants.h"
#include <QVBoxLayout>
#include <QWidget>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString("%1 v%2 — %3")
        .arg(HavenFSK::APP_NAME)
        .arg(HavenFSK::APP_VERSION)
        .arg(HavenFSK::EMISSION_DESIG));
    resize(900, 700);
    setupUi();
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *layout  = new QVBoxLayout(central);

    m_statusLabel = new QLabel(
        QString("HAVEN-FSK v%1 — C++ rewrite in progress\n"
                "16-tone MFSK | 500 Hz BW | ~62 bps | %2")
            .arg(HavenFSK::APP_VERSION)
            .arg(HavenFSK::EMISSION_DESIG),
        central);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel);

    setCentralWidget(central);
    statusBar()->showMessage("Ready");
}
