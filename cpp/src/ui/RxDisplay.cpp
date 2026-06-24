#include "RxDisplay.h"
#include <QRegularExpression>
#include <QScrollBar>
#include <QFont>
#include <QUrl>
#include <QList>
#include <tuple>

RxDisplay::RxDisplay(QWidget* parent)
    : QTextBrowser(parent)
{
    setOpenLinks(false);
    setFont(QFont("Courier New", 10));
    setReadOnly(true);

    connect(this, &QTextBrowser::anchorClicked,
            this, &RxDisplay::onAnchorClicked);
}

void RxDisplay::appendMessage(const QString& text,
                               const QString& senderCallsign,
                               const QDateTime& timestamp,
                               bool crcOk,
                               bool converged)
{
    if (m_messageCount >= MAX_MESSAGES) {
        QTextCursor c = textCursor();
        c.movePosition(QTextCursor::Start);
        c.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    } else {
        m_messageCount++;
    }

    QString ts = timestamp.toUTC().toString("hh:mm:ss");

    QStringList lines = text.split('\n');
    QStringList renderedLines;
    for (const QString& line : lines) {
        if (line.trimmed().isEmpty())
            renderedLines.append("&nbsp;");
        else
            renderedLines.append(renderMessage(line, senderCallsign));
    }
    QString rendered = renderedLines.join("<br>");

    QString flags;
    if (!crcOk)     flags += " <span style='color:red'>[CRC]</span>";
    if (!converged) flags += " <span style='color:orange'>[NC]</span>";

    QString html = QString(
        "<span style='color:gray'>[%1]</span> %2%3<br>")
        .arg(ts, rendered, flags);

    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::End);
    c.insertHtml(html);

    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void RxDisplay::clearMessages() {
    clear();
    m_messageCount = 0;
}

void RxDisplay::appendTxMessage(const QString& text,
                                 const QString& myCallsign)
{
    if (text.trimmed().isEmpty()) return;

    QString ts     = QDateTime::currentDateTimeUtc().toString("hh:mm:ss");
    QString caller = myCallsign.isEmpty() ? "TX" : myCallsign.toUpper();

    QString displayText = text.toHtmlEscaped().replace('\n', "<br>");

    QString html = QString(
        "<span style='color:gray'>[%1]</span> "
        "<span style='color:#C8860A'>"
        "<b>[TX] %2:</b> %3"
        "</span><br>")
        .arg(ts)
        .arg(caller.toHtmlEscaped())
        .arg(displayText);

    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::End);
    c.insertHtml(html);

    verticalScrollBar()->setValue(verticalScrollBar()->maximum());

    if (m_messageCount < MAX_MESSAGES)
        m_messageCount++;
}

void RxDisplay::onAnchorClicked(const QUrl& url) {
    if (url.scheme() != "haven") return;
    QString scheme = url.host();
    QString value  = url.path();
    if (value.startsWith('/')) value = value.mid(1);
    value = QUrl::fromPercentEncoding(value.toUtf8());
    emit elementClicked(scheme, value);
}

QString RxDisplay::makeLink(const QString& scheme,
                             const QString& value,
                             const QString& display)
{
    QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(value));
    QString href    = QString("haven://%1/%2").arg(scheme, encoded);
    return QString("<a href='%1' style='color:#4a9fd4;"
                   "text-decoration:none'>%2</a>")
        .arg(href, display.toHtmlEscaped());
}

QString RxDisplay::renderMessage(const QString& text,
                                  const QString& senderCallsign) const
{
    // Regex for structured field tags: TAG:value
    static QRegularExpression tagRe(
        "(NAME:|QTH:|GRID:|RS:|POTA:|SOTA:|FD:)"
        "([^\\s][^N^Q^G^R^P^S^F]*?)(?=\\s+(?:NAME:|QTH:|GRID:|"
        "RS:|POTA:|SOTA:|FD:)|$)",
        QRegularExpression::CaseInsensitiveOption);

    // Build list of tag replacements from the original text
    QList<std::tuple<int,int,QString>> replacements;

    auto tagIt = tagRe.globalMatch(text.toUpper());
    while (tagIt.hasNext()) {
        auto match  = tagIt.next();
        QString tag = match.captured(1).chopped(1).toLower();  // e.g. "name"
        QString val = match.captured(2).trimmed();
        if (val.isEmpty()) continue;

        QString linkHtml = QString(
            "<span style='color:gray'>%1:</span>%2")
            .arg(match.captured(1).chopped(1),
                 makeLink(tag, val, val));
        replacements.append({match.capturedStart(),
                             match.capturedLength(),
                             linkHtml});
    }

    // Apply tag replacements to original text in reverse order (preserves positions)
    QString processed = text;
    for (int i = replacements.size() - 1; i >= 0; i--) {
        auto [pos, len, html] = replacements[i];
        processed = processed.left(pos) + html + processed.mid(pos + len);
    }

    // Highlight sender callsign (bold + clickable)
    if (!senderCallsign.isEmpty()) {
        QString callLink = "<b>" + makeLink("callsign",
                                             senderCallsign,
                                             senderCallsign) + "</b>";
        processed.replace(senderCallsign.toHtmlEscaped(), callLink);
        processed.replace(senderCallsign, callLink);
    }

    // Link any remaining callsign-like words not already linked
    static QRegularExpression wordRe(
        "\\b([A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z])\\b");
    auto wordIt = wordRe.globalMatch(text.toUpper());
    QStringList foundCalls;
    while (wordIt.hasNext()) {
        QString word = wordIt.next().captured(1);
        if (word != senderCallsign.toUpper() && !foundCalls.contains(word))
            foundCalls.append(word);
    }
    for (const QString& call : foundCalls) {
        if (!processed.contains("haven://callsign/"))
            processed.replace(call, makeLink("callsign", call, call));
    }

    return processed;
}

bool RxDisplay::isCallsign(const QString& word) const {
    static QRegularExpression re(
        "^[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z]$");
    return re.match(word.toUpper()).hasMatch();
}
