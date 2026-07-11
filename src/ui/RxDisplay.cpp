#include "RxDisplay.h"
#include <QRegularExpression>
#include <QScrollBar>
#include <QFont>
#include <QUrl>
#include <QList>
#include <tuple>
#include <algorithm>

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
    // Most values are "anything, lazily, up to the next tag or end of
    // string" — the lookahead assertion alone correctly bounds the match.
    // An earlier version also excluded the individual letters N/Q/G/R/P/S/F
    // from the value (a broken attempt at "don't match into the next tag
    // name" — inside a character class, only a leading '^' negates; every
    // '^' after that is a literal character to exclude, not a fresh
    // negation). That silently truncated any value containing those very
    // common letters, e.g. "Springfield" or "US-1234".
    // RS is different: the signal report is always exactly 2 characters,
    // with no closing tag to bound it (it's typically the last field
    // before a literal "K" over-prosign, e.g. "RS:52 K") — the generic
    // lazy-to-next-tag rule would swallow that trailing "K" as part of
    // the value. So RS gets its own fixed-width branch instead.
    static QRegularExpression tagRe(
        "RS:(?<rsval>\\S{1,2})"
        "|(?<tag>NAME|QTH|GRID|POTA|SOTA|FD):"
        "(?<val>[^\\s].*?)(?=\\s+(?:NAME:|QTH:|GRID:|"
        "RS:|POTA:|SOTA:|FD:)|$)",
        QRegularExpression::CaseInsensitiveOption);

    // Build list of tag replacements from the original text
    QList<std::tuple<int,int,QString>> replacements;

    auto tagIt = tagRe.globalMatch(text.toUpper());
    while (tagIt.hasNext()) {
        auto match = tagIt.next();
        QString rsVal = match.captured("rsval");
        QString tag   = rsVal.isEmpty() ? match.captured("tag").toLower() : "rs";
        QString val   = rsVal.isEmpty() ? match.captured("val").trimmed() : rsVal;
        if (tag.isEmpty() || val.isEmpty()) continue;

        QString linkHtml = QString(
            "<span style='color:gray'>%1:</span>%2")
            .arg(tag.toUpper(), makeLink(tag, val, val));
        replacements.append({match.capturedStart(),
                             match.capturedLength(),
                             linkHtml});
    }

    // Callsign links are collected the same way — (position, length, html)
    // ranges over the original text — so every callsign can be linked in
    // the same single splice pass as the tag links. The old approach
    // patched callsigns into the already-built HTML with QString::replace,
    // which also matches inside href attributes (the ADR-110 corruption
    // class); its whole-string "already linked?" guard prevented that but
    // made every callsign after the first a dead letter. Matching against
    // the uppercased text also fixes the old replace()'s case sensitivity.
    // Callsign-shaped words inside an already-linked tag value (e.g. the
    // "W7W" in SOTA:W7W/SE-001) are skipped via the overlap check.
    static QRegularExpression wordRe(
        "\\b([A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z])\\b");
    // Bare 6-char grid squares are callsign-shaped (EN52XA) — don't link
    // them as callsigns (mirrors DspPipeline::parseSenderCallsign).
    static QRegularExpression gridRe("^[A-R]{2}[0-9]{2}[A-X]{2}$");
    const QString senderUpper = senderCallsign.toUpper();
    auto wordIt = wordRe.globalMatch(text.toUpper());
    while (wordIt.hasNext()) {
        auto m = wordIt.next();
        if (gridRe.match(m.captured(1)).hasMatch()) continue;
        int pos = m.capturedStart(1);
        int len = m.capturedLength(1);
        bool overlaps = false;
        for (const auto& rep : replacements) {
            int rPos = std::get<0>(rep);
            int rLen = std::get<1>(rep);
            if (pos < rPos + rLen && rPos < pos + len) { overlaps = true; break; }
        }
        if (overlaps) continue;

        QString call = m.captured(1);
        QString link = makeLink("callsign", call, text.mid(pos, len));
        if (call == senderUpper)
            link = "<b>" + link + "</b>";   // sender highlighted bold
        replacements.append({pos, len, link});
    }
    std::sort(replacements.begin(), replacements.end(),
              [](const auto& a, const auto& b) {
                  return std::get<0>(a) < std::get<0>(b);
              });

    // Splice escaped plain-text segments around the generated link HTML.
    // The message text arrived over the air and is untrusted: unescaped,
    // insertHtml() would render any markup a station transmits (spoofed
    // "[hh:mm:ss] CALL:" lines, hidden/styled text) and silently swallow
    // legitimate angle-bracket text like the "<sk>" prosign as an unknown
    // tag. Only the link HTML built above goes in unescaped.
    QString processed;
    int cursor = 0;
    for (const auto& rep : replacements) {
        const auto& [pos, len, html] = rep;
        processed += text.mid(cursor, pos - cursor).toHtmlEscaped();
        processed += html;
        cursor = pos + len;
    }
    processed += text.mid(cursor).toHtmlEscaped();

    return processed;
}

bool RxDisplay::isCallsign(const QString& word) const {
    static QRegularExpression re(
        "^[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z]$");
    return re.match(word.toUpper()).hasMatch();
}
