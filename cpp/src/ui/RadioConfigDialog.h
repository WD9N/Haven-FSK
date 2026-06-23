#pragma once
#include <QDialog>
#include <QRadioButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QGroupBox>
#include <QPushButton>

// RadioConfigDialog — radio control method, connection settings, and
// Connect/Disconnect controls. Opened directly from the Radio menu bar item.

class RadioConfigDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RadioConfigDialog(QWidget* parent = nullptr);

    // Call after construction to reflect current connection state
    void setConnected(bool connected);

signals:
    void configChanged();       // settings saved
    void connectRequested();    // operator clicked Connect Rig
    void disconnectRequested(); // operator clicked Disconnect Rig

private slots:
    void onConnect();
    void onDisconnect();
    void onSave();
    void onMethodChanged();

private:
    void setupUi();
    void loadSettings();
    void saveSettings();

    QRadioButton* m_radioNone    {nullptr};
    QRadioButton* m_radioRigctld {nullptr};
    QRadioButton* m_radioTCI     {nullptr};

    QGroupBox*  m_rigctldGroup {nullptr};
    QLineEdit*  m_rigctldHost  {nullptr};
    QSpinBox*   m_rigctldPort  {nullptr};

    QGroupBox*  m_tciGroup     {nullptr};
    QLineEdit*  m_tciHost      {nullptr};
    QSpinBox*   m_tciPort      {nullptr};

    QSpinBox*    m_pttLeadMs    {nullptr};
    QSpinBox*    m_txTailMs    {nullptr};

    QPushButton* m_connectBtn    {nullptr};
    QPushButton* m_disconnectBtn {nullptr};

    bool m_isConnected {false};
};
