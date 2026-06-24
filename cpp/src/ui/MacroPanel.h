#pragma once
#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QString>
#include <QVector>

// MacroPanel — two banks (A/B) of 8 user-configurable macro buttons.
//
// Supported tags in macro text:
//   <myCall>    <myParks>   <mySOTA>   <myGrid>
//   <myName>    <myQTH>     <myFD>
//   <theirCall> <rstSent>
//   <TX>        — auto-transmit when clicked (removed from sent text)
//
// Without <TX>: text placed in TX input for operator review.
// With <TX>: text placed in TX input and transmitted immediately.
// Right-click any button to edit label and macro text.

class MacroPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MacroPanel(QWidget* parent = nullptr);

    void setTheirCall(const QString& call) { m_theirCall = call; }
    void setRsSent(const QString& rs)      { m_rsSent    = rs;   }

signals:
    void macroTriggered(const QString& text, bool autoTx);

private slots:
    void onMacroClicked(int bank, int index);
    void onMacroRightClicked(int bank, int index);

private:
    void setupUi();
    void loadMacros();
    void saveMacro(int bank, int index);
    QString expandMacro(const QString& text) const;
    QString defaultMacro(int bank, int index) const;
    QString defaultLabel(int bank, int index) const;
    void updateButton(int bank, int index);

    static constexpr int NUM_BANKS  = 2;
    static constexpr int NUM_MACROS = 8;

    QString      m_macroText[NUM_BANKS][NUM_MACROS];
    QString      m_macroLabel[NUM_BANKS][NUM_MACROS];
    QPushButton* m_buttons[NUM_BANKS][NUM_MACROS];

    QString m_theirCall;
    QString m_rsSent;
};
