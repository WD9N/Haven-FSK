#pragma once
#include <QWidget>
#include <QPushButton>
#include <QString>

// MacroPanel — flat 6×3 grid of 18 user-configurable macro buttons.
//
// Supported tags in macro text:
//   <myCall>  <myParks>  <mySOTA>  <myGrid>  <myName>
//   <myQTH>   <myState>  <myCounty>  <theirCall>  <rstSent>
//
// Left-click: expand tags and emit macroTriggered(text).
// Right-click: open edit dialog for label and macro text.

class MacroPanel : public QWidget {
    Q_OBJECT
public:
    static constexpr int NUM_MACROS = 18;

    explicit MacroPanel(QWidget* parent = nullptr);

    void setTheirCall(const QString& call) { m_theirCall = call; }
    void setRsSent(const QString& rs)      { m_rsSent = rs; }

signals:
    void macroTriggered(const QString& text);

private slots:
    void onMacroClicked(int index);
    void onMacroRightClicked(int index);

private:
    void    setupUi();
    void    loadMacros();
    void    saveMacros();
    void    updateButton(int index);
    QString expandMacro(const QString& text) const;

    QPushButton* m_buttons[NUM_MACROS];
    QString      m_macroLabel[NUM_MACROS];
    QString      m_macroText[NUM_MACROS];
    QString      m_theirCall;
    QString      m_rsSent;
};
