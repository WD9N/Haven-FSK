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

void RxDisplay::appendStreamingText(const QString& text) {
    if (text.isEmpty()) return;

    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::End);

    if (!m_streamingActive) {
        QString ts = QDateTime::currentDateTimeUtc().toUTC().toString("hh:mm:ss");
        c.insertHtml(QString(
            "<span style='color:gray'>[%1]</span> ").arg(ts));
        m_streamingActive = true;
    }

    // insertText(), not insertHtml(): HTML parsing collapses whitespace
    // runs and treats a bare '\n' as insignificant rather than a line
    // break, so single spaces and CR/LF from the decoded PSK31 stream
    // were silently disappearing. insertText() preserves both exactly
    // and creates a new block on '\n' — normalize a lone '\r' (some
    // PSK31 stations send CR, not LF, as their line ending) to '\n' so
    // it gets the same treatment.
    QString plain = text;
    plain.replace('\r', '\n');
    c.insertText(plain);
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void RxDisplay::endStreamingLine() {
    if (!m_streamingActive) return;
    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::End);
    c.insertHtml("<br>");
    m_streamingActive = false;
}

void RxDisplay::appendMessage(const QString& text,
                               const QString& senderCallsign,
                               const QDateTime& timestamp,
                               bool crcOk,
                               bool converged)
{
    endStreamingLine();  // don't let a framed message run onto a streaming line

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
    m_streamingActive = false;
}

void RxDisplay::appendTxMessage(const QString& text,
                                 const QString& myCallsign)
{
    if (text.trimmed().isEmpty()) return;
    endStreamingLine();  // don't let a TX message run onto a streaming line

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
    // The value is "anything, lazily, up to the next tag or end of string" —
    // the lookahead assertion alone correctly bounds the match. An earlier
    // version also excluded the individual letters N/Q/G/R/P/S/F from the
    // value (a broken attempt at "don't match into the next tag name" —
    // inside a character class, only a leading '^' negates; every '^'
    // after that is a literal character to exclude, not a fresh negation).
    // That silently truncated any value containing those very common
    // letters, e.g. "Springfield" or "US-1234".
    static QRegularExpression tagRe(
        "(NAME:|QTH:|GRID:|RS:|POTA:|SOTA:|FD:)"
        "([^\\s].*?)(?=\\s+(?:NAME:|QTH:|GRID:|"
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

    // Highlight sender callsign (bold + clickable). A single replace() only
    // -- the link HTML itself contains the callsign text (in both the
    // haven://callsign/<call> href and the visible link text), so a second
    // replace() pass over the same string would match those and re-wrap
    // them, corrupting the markup (this actually happened -- see
    // DECISIONS.md).
    if (!senderCallsign.isEmpty()) {
        QString callLink = "<b>" + makeLink("callsign",
                                             senderCallsign,
                                             senderCallsign) + "</b>";
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
