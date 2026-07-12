#pragma once
#include <QWidget>
#include <QPushButton>
#include <QString>
#include <QList>

// MacroPanel — flat 6×3 grid of 18 user-configurable macro buttons.
//
// Expansion tags in macro text:
//   <myCall>  <myParks>  <mySOTA>  <myGrid>  <myName>
//   <myQTH>   <myState>  <myCounty>  <theirCall>  <rstSent>
//
// Behavioral tags (stripped before transmission):
//   <clr> — clear TX input before inserting macro text
//   <TX>  — auto-transmit after inserting macro text
//
// Left-click: expand tags and emit macroTriggered(segments, clearFirst,
// autoTx). Expansion produces segments rather than a flat string so the
// TX editor can tag each data value's span with its field ID (ADR-133);
// spans are serialized to inline field markers at send time.
// Right-click: open edit dialog for label and macro text.

// One run of expanded macro text. fieldId is 0 for plain prose, or a
// FieldId char (FieldMarkers.h) for a value inserted by a data tag.
struct MacroSegment {
    QString text;
    char    fieldId = 0;
};

class MacroPanel : public QWidget {
    Q_OBJECT
public:
    static constexpr int NUM_MACROS = 18;

    explicit MacroPanel(QWidget* parent = nullptr);

    void setTheirCall(const QString& call) { m_theirCall = call; }
    void setRsSent(const QString& rs)      { m_rsSent = rs; }

signals:
    void macroTriggered(const QList<MacroSegment>& segments,
                        bool clearFirst,
                        bool autoTx);

private slots:
    void onMacroClicked(int index);
    void onMacroRightClicked(int index);

private:
    void    setupUi();
    void    loadMacros();
    void    saveMacros();
    void    updateButton(int index);
    QList<MacroSegment> expandMacro(const QString& text) const;

    QPushButton* m_buttons[NUM_MACROS];
    QString      m_macroLabel[NUM_MACROS];
    QString      m_macroText[NUM_MACROS];
    QString      m_theirCall;
    QString      m_rsSent;
};
