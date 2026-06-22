#pragma once
#include <QTextBrowser>
#include <QDateTime>
#include "../dsp/DspPipeline.h"

// RxDisplay — decoded message display with clickable structured elements.
//
// Recognized structured field tags (FIELD:value format):
//   NAME:  QTH:  GRID:  RS:  POTA:  SOTA:  FD:
//
// Callsigns and structured values are wrapped in haven:// anchor links.
// Clicking emits elementClicked(scheme, value) for MainWindow to route
// to the log entry panel.

class RxDisplay : public QTextBrowser
{
    Q_OBJECT
public:
    explicit RxDisplay(QWidget* parent = nullptr);

    void appendMessage(const QString& text,
                       const QString& senderCallsign,
                       const QDateTime& timestamp,
                       bool crcOk,
                       bool converged);

    // Append a transmitted message in amber [TX] styling
    void appendTxMessage(const QString& text, const QString& myCallsign);

    void clearMessages();

signals:
    // scheme: "callsign", "pota", "sota", "grid", "rs", "name", "qth", "fd"
    void elementClicked(const QString& scheme, const QString& value);

private slots:
    void onAnchorClicked(const QUrl& url);

private:
    QString renderMessage(const QString& text,
                          const QString& senderCallsign) const;

    static QString makeLink(const QString& scheme,
                            const QString& value,
                            const QString& display);

    bool isCallsign(const QString& word) const;

    int m_messageCount = 0;
    static constexpr int MAX_MESSAGES = 500;
};
