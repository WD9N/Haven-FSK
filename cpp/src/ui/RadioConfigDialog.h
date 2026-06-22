#pragma once
#include <QDialog>
#include <QRadioButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QGroupBox>
#include <QLabel>

// RadioConfigDialog — radio control method and connection settings.
// Opened from Radio → Configure... on the main menu bar.
// Settings persisted in QSettings under radio/* keys.
// Emits configChanged() on OK/Apply so MainWindow reconnects.

class RadioConfigDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RadioConfigDialog(QWidget* parent = nullptr);

signals:
    void configChanged();

private slots:
    void onOk();
    void onApply();
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
};
