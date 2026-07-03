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

    // Continuous character-stream RX (PSK31) — appends text in place on
    // the current line (no timestamp, no CRC/FEC badges; those don't
    // apply to a mode with no framing or FEC). Starts a fresh line with
    // a one-time timestamp marker if not already mid-stream. Call
    // endStreamingLine() at a natural break point (mode change, carrier
    // drop) so the next transmission starts its own line rather than
    // running on from the previous one.
    void appendStreamingText(const QString& text);
    void endStreamingLine();

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

    bool m_streamingActive = false;  // mid-line of streaming (PSK31) text
};
