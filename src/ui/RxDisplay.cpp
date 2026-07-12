#include "RxDisplay.h"
#include "../dsp/FieldMarkers.h"
#include "../util/CallsignPattern.h"
#include <QRegularExpression>
#include <QMenu>
#include <QContextMenuEvent>
#include <QScrollBar>
#include <QFont>
#include <QUrl>
#include <QList>
#include <tuple>
#include <algorithm>

// QString-space scan of ADR-133 inline field markers: returns the
// display text (markers stripped, values kept) and each known field's
// id/position/length within that display text. Mirrors the byte-level
// FieldMarkers.h parser, but positions here must be QString (UTF-16)
// offsets for the HTML splice below — the std::string version's byte
// offsets would drift on any non-ASCII payload.
struct MarkedSpan { char id; int pos; int len; QString value; };
struct MarkedLine { QString display; QList<MarkedSpan> spans; };

static MarkedLine scanMarkers(const QString& text) {
    MarkedLine out;
    out.display.reserve(text.size());
    const QChar START(HavenFSK::FIELD_START);
    const QChar END(HavenFSK::FIELD_END);

    int i = 0;
    while (i < text.size()) {
        QChar ch = text.at(i);
        if (ch == END) { i++; continue; }
        if (ch != START) { out.display += ch; i++; continue; }

        if (i + 1 >= text.size()) break;          // dangling start marker
        char id = text.at(i + 1).toLatin1();
        i += 2;

        int valStart = i;
        while (i < text.size() && text.at(i) != END && text.at(i) != START)
            i++;
        QString value = text.mid(valStart, i - valStart);
        if (i < text.size() && text.at(i) == END) i++;

        if (!value.isEmpty() && HavenFSK::isKnownFieldId(id))
            out.spans.append({id, int(out.display.size()),
                              int(value.size()), value});
        out.display += value;
    }
    return out;
}

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
    // Field-marker bytes never belong in a character stream (PSK31);
    // scrub them in case a noise burst decodes to one.
    plain.remove(QChar(HavenFSK::FIELD_START));
    plain.remove(QChar(HavenFSK::FIELD_END));
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

    // Echoed TX text carries the wire bytes — show it as the other
    // station will see it, markers stripped.
    QString displayText = scanMarkers(text).display
                              .toHtmlEscaped().replace('\n', "<br>");

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

void RxDisplay::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = createStandardContextMenu(event->pos());

    // QTextCursor::selectedText() uses U+2029 as the paragraph separator.
    QString sel = textCursor().selectedText()
                      .replace(QChar(0x2029), ' ').trimmed();
    if (!sel.isEmpty()) {
        // Field list in default order; the shape-guessed field is
        // promoted to a direct top-level action.
        struct Field { const char* scheme; const char* label; };
        static const Field fields[] = {
            {"callsign", "Call"},  {"name", "Name"},   {"qth", "QTH"},
            {"grid", "Grid"},      {"rs", "RS-R"},     {"rss", "RS-S"},
            {"pota", "POTA"},      {"sota", "SOTA"},   {"state", "State"},
            {"county", "County"},  {"fd", "FD Exch"},
        };

        // Shape guess (mirrors the bare-shape detectors elsewhere).
        const QString up = sel.toUpper();
        static QRegularExpression callRe(
            "^" + QString::fromStdString(HavenFSK::callsignPattern()) + "$");
        static QRegularExpression gridRe("^[A-R]{2}[0-9]{2}(?:[A-X]{2})?$");
        static QRegularExpression potaRe("^[A-Z0-9]{1,2}-[0-9]{4,5}$");
        static QRegularExpression sotaRe(
            "^[A-Z0-9]{1,3}/[A-Z]{2}-[0-9]{3}$");
        static QRegularExpression rsRe("^[1-5][1-9][1-9]?$");
        static QRegularExpression stateRe("^[A-Z]{2}$");
        const char* guess = nullptr;
        if      (gridRe.match(up).hasMatch())  guess = "grid";
        else if (potaRe.match(up).hasMatch())  guess = "pota";
        else if (sotaRe.match(up).hasMatch())  guess = "sota";
        else if (rsRe.match(up).hasMatch())    guess = "rs";
        else if (callRe.match(up).hasMatch())  guess = "callsign";
        else if (stateRe.match(up).hasMatch()) guess = "state";

        QString shown = sel.length() > 24 ? sel.left(21) + "..." : sel;
        auto makeLogAction = [this, sel](QObject* parent, const QString& text,
                                         const QString& scheme) {
            QAction* a = new QAction(text, parent);
            connect(a, &QAction::triggered, this, [this, scheme, sel] {
                emit elementClicked(scheme, sel);
            });
            return a;
        };

        QAction* first = menu->actions().isEmpty()
                             ? nullptr : menu->actions().first();
        // Direct action for the shape-guessed field, then a submenu with
        // the full field list for everything else.
        QMenu* sub = new QMenu(QString("Log \"%1\" as").arg(shown), menu);
        for (const Field& f : fields) {
            if (guess && QString(f.scheme) == guess)
                menu->insertAction(first, makeLogAction(menu,
                    QString("Log \"%1\" as %2").arg(shown, f.label),
                    f.scheme));
            else
                sub->addAction(makeLogAction(sub, f.label, f.scheme));
        }
        menu->insertMenu(first, sub);
        menu->insertSeparator(first);
    }

    menu->exec(event->globalPos());
    delete menu;
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
    // Inline field markers (ADR-133) are authoritative where present:
    // the sender declared what each span is, so those spans become
    // links directly. The legacy TAG: regex and the callsign-shaped-
    // word pass still run afterward on the stripped text — hand-typed
    // portions and messages from stations without markers keep
    // working — skipping anything a marker already claimed.
    const MarkedLine marked = scanMarkers(text);
    const QString&   disp   = marked.display;

    QList<std::tuple<int,int,QString>> replacements;

    const QString senderUpperEarly = senderCallsign.toUpper();
    for (const MarkedSpan& span : marked.spans) {
        QString scheme;
        switch (span.id) {
            case HavenFSK::FieldId::Sender:
            case HavenFSK::FieldId::Recipient: scheme = "callsign"; break;
            case HavenFSK::FieldId::Rs:        scheme = "rs";       break;
            case HavenFSK::FieldId::Grid:      scheme = "grid";     break;
            case HavenFSK::FieldId::Pota:      scheme = "pota";     break;
            case HavenFSK::FieldId::Sota:      scheme = "sota";     break;
            case HavenFSK::FieldId::Name:      scheme = "name";     break;
            case HavenFSK::FieldId::Qth:       scheme = "qth";      break;
            case HavenFSK::FieldId::Fd:        scheme = "fd";       break;
            case HavenFSK::FieldId::State:     scheme = "state";    break;
            case HavenFSK::FieldId::County:    scheme = "county";   break;
            default: continue;
        }
        QString link = makeLink(scheme, span.value, span.value);
        if (span.id == HavenFSK::FieldId::Sender ||
            span.value.toUpper() == senderUpperEarly)
            link = "<b>" + link + "</b>";   // sender highlighted bold
        replacements.append({span.pos, span.len, link});
    }

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

    auto overlapsExisting = [&replacements](int pos, int len) {
        for (const auto& rep : replacements) {
            int rPos = std::get<0>(rep);
            int rLen = std::get<1>(rep);
            if (pos < rPos + rLen && rPos < pos + len) return true;
        }
        return false;
    };

    auto tagIt = tagRe.globalMatch(disp.toUpper());
    while (tagIt.hasNext()) {
        auto match = tagIt.next();
        QString rsVal = match.captured("rsval");
        QString tag   = rsVal.isEmpty() ? match.captured("tag").toLower() : "rs";
        QString val   = rsVal.isEmpty() ? match.captured("val").trimmed() : rsVal;
        if (tag.isEmpty() || val.isEmpty()) continue;
        if (overlapsExisting(match.capturedStart(), match.capturedLength()))
            continue;

        QString linkHtml = QString(
            "<span style='color:gray'>%1:</span>%2")
            .arg(tag.toUpper(), makeLink(tag, val, val));
        replacements.append({match.capturedStart(),
                             match.capturedLength(),
                             linkHtml});
    }

    // Bare-shape passes: POTA park refs (US-1234, legacy K-1234) and
    // Maidenhead grids (EM79, EM79RJ) are distinctive enough to link on
    // pattern alone — real CQs say "CQ POTA DE N8SDR US-1234 EM79RJ K"
    // with no POTA:/GRID: prefix and no markers, and unclickable park
    // refs there defeat click-to-populate exactly where hunters need it.
    static QRegularExpression bareRefRe(
        "\\b([A-Z0-9]{1,2}-[0-9]{4,5})\\b");
    auto refIt = bareRefRe.globalMatch(disp.toUpper());
    while (refIt.hasNext()) {
        auto m = refIt.next();
        int pos = m.capturedStart(1);
        int len = m.capturedLength(1);
        if (overlapsExisting(pos, len)) continue;
        replacements.append({pos, len,
            makeLink("pota", m.captured(1), disp.mid(pos, len))});
    }

    static QRegularExpression bareGridRe(
        "\\b([A-R]{2}[0-9]{2}(?:[A-X]{2})?)\\b");
    auto gridIt = bareGridRe.globalMatch(disp.toUpper());
    while (gridIt.hasNext()) {
        auto m = gridIt.next();
        int pos = m.capturedStart(1);
        int len = m.capturedLength(1);
        if (overlapsExisting(pos, len)) continue;
        replacements.append({pos, len,
            makeLink("grid", m.captured(1), disp.mid(pos, len))});
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
        "\\b(" + QString::fromStdString(HavenFSK::callsignPattern())
        + ")\\b");
    // Bare 6-char grid squares are callsign-shaped (EN52XA) — don't link
    // them as callsigns (mirrors DspPipeline::parseSenderCallsign).
    static QRegularExpression gridRe("^[A-R]{2}[0-9]{2}[A-X]{2}$");
    const QString senderUpper = senderCallsign.toUpper();
    auto wordIt = wordRe.globalMatch(disp.toUpper());
    while (wordIt.hasNext()) {
        auto m = wordIt.next();
        if (gridRe.match(m.captured(1)).hasMatch()) continue;
        int pos = m.capturedStart(1);
        int len = m.capturedLength(1);
        if (overlapsExisting(pos, len)) continue;

        QString call = m.captured(1);
        QString link = makeLink("callsign", call, disp.mid(pos, len));
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
        processed += disp.mid(cursor, pos - cursor).toHtmlEscaped();
        processed += html;
        cursor = pos + len;
    }
    processed += disp.mid(cursor).toHtmlEscaped();

    return processed;
}

bool RxDisplay::isCallsign(const QString& word) const {
    static QRegularExpression re(
        "^" + QString::fromStdString(HavenFSK::callsignPattern()) + "$");
    return re.match(word.toUpper()).hasMatch();
}
