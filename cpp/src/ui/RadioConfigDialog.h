#pragma once
#include <QDialog>
#include <QRadioButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>

// RadioConfigDialog — radio control method, connection settings, and
// Connect/Disconnect controls. Opened directly from the Radio menu bar item.

class RadioConfigDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RadioConfigDialog(QWidget* parent = nullptr);

    // Call after construction to reflect current connection state
    void setConnected(bool connected);

public slots:
    // Live feedback while the dialog is open — wire the active
    // RadioInterface's connected()/connectFailed() signals to these
    // (see MainWindow::onOpenRadioConfig()) so a failed attempt is
    // visible right where the operator is looking, not just in the main
    // window's status bar behind this modal dialog.
    void onConnectSucceeded();
    void onConnectFailed(const QString& reason);

signals:
    void configChanged();       // settings saved
    void connectRequested();    // operator clicked Connect Rig
    void disconnectRequested(); // operator clicked Disconnect Rig

private slots:
    void onConnect();
    void onDisconnect();
    void onSave();
    void onMethodChanged();
    void onRefreshHamlibPorts();

private:
    void setupUi();
    void loadSettings();
    void saveSettings();

    QRadioButton* m_radioNone    {nullptr};
    QRadioButton* m_radioRigctld {nullptr};
    QRadioButton* m_radioTCI     {nullptr};
    QRadioButton* m_radioHamlib  {nullptr};

    QGroupBox*  m_rigctldGroup {nullptr};
    QLineEdit*  m_rigctldHost  {nullptr};
    QSpinBox*   m_rigctldPort  {nullptr};

    QGroupBox*  m_tciGroup     {nullptr};
    QLineEdit*  m_tciHost      {nullptr};
    QSpinBox*   m_tciPort      {nullptr};

    // Direct Hamlib linking (CAT over USB/serial, no external rigctld)
    QGroupBox*   m_hamlibGroup        {nullptr};
    QComboBox*   m_hamlibRigModel     {nullptr};  // searchable, itemData = numeric model ID
    QComboBox*   m_hamlibPort         {nullptr};  // itemData = QSerialPortInfo::portName()
    QPushButton* m_hamlibRefreshPorts {nullptr};
    QComboBox*   m_hamlibBaud         {nullptr};

    QSpinBox*    m_pttLeadMs    {nullptr};
    QSpinBox*    m_txTailMs    {nullptr};

    QCheckBox*   m_setModeOnConnect  {nullptr};
    QLineEdit*   m_connectModeString {nullptr};

    QPushButton* m_connectBtn    {nullptr};
    QPushButton* m_disconnectBtn {nullptr};
    QLabel*      m_connectStatusLabel {nullptr};

    bool m_isConnected {false};
};
