#include "LogPanel.h"
#include "../dsp/Constants.h"
#include "../dsp/FieldMarkers.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFrame>
#include <QFont>
#include <QSizePolicy>
#include <QRegularExpression>
#include <QPair>
#include <QList>
#include <cmath>

// "20260709" -> "2026-07-09" for the contacts-table date column
static QString displayDate(const QString& yyyymmdd) {
    if (yyyymmdd.length() != 8) return yyyymmdd;
    return yyyymmdd.mid(0, 4) + "-" + yyyymmdd.mid(4, 2) + "-"
         + yyyymmdd.mid(6, 2);
}

// Auto-correct POTA ref to canonical XX-NNNN format
static QString fixPotaRef(const QString& raw) {
    QString s = raw.trimmed().toUpper();
    static QRegularExpression correct("^[A-Z]{2}-[0-9]+$");
    if (correct.match(s).hasMatch()) return s;
    static QRegularExpression noHyphen("^([A-Z]{2})([0-9]+)$");
    auto m = noHyphen.match(s);
    if (m.hasMatch()) return m.captured(1) + "-" + m.captured(2);
    return s;
}

// Structured field tags recognized in decoded message text -- kept in sync
// with RxDisplay::renderMessage's tag set (NAME/QTH/GRID/RS/POTA/SOTA/FD).
// Duplicated rather than shared across panels: RxDisplay's copy exists to
// build clickable HTML with position info this caller doesn't need, and
// each panel owning its own small parsing helper avoids a UI-panel-to-
// UI-panel dependency for one regex.
// QString-space scan of ADR-133 inline field markers. Same duplication
// rationale as parseStructuredTags below: each panel owns its own small
// parsing helper (RxDisplay's copy also tracks display positions this
// caller doesn't need).
struct ScannedMarkers {
    QString display;                       // text with markers removed
    QList<QPair<char, QString>> fields;    // (field id, value)
};

static ScannedMarkers scanFieldMarkers(const QString& text) {
    ScannedMarkers out;
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
            out.fields.append({id, value});
        out.display += value;
    }
    return out;
}

static QList<QPair<QString, QString>> parseStructuredTags(const QString& text) {
    // RS gets its own fixed-2-character branch, not the generic
    // lazy-to-next-tag rule -- the report has no closing tag to bound it
    // (it's typically followed by a literal "K" over-prosign, e.g.
    // "RS:52 K"), so the generic rule would swallow the "K" as part of
    // the value. Kept in sync with RxDisplay::renderMessage's copy.
    static QRegularExpression tagRe(
        "RS:(?<rsval>\\S{1,2})"
        "|(?<tag>NAME|QTH|GRID|POTA|SOTA|FD):"
        "(?<val>[^\\s].*?)(?=\\s+(?:NAME:|QTH:|GRID:|"
        "RS:|POTA:|SOTA:|FD:)|$)",
        QRegularExpression::CaseInsensitiveOption);

    QList<QPair<QString, QString>> result;
    auto it = tagRe.globalMatch(text.toUpper());
    while (it.hasNext()) {
        auto m = it.next();
        QString rsVal = m.captured("rsval");
        QString tag   = rsVal.isEmpty() ? m.captured("tag").toLower() : "rs";
        QString val   = rsVal.isEmpty() ? m.captured("val").trimmed() : rsVal;
        if (!tag.isEmpty() && !val.isEmpty())
            result.append({tag, val});
    }
    return result;
}

LogPanel::LogPanel(QWidget* parent)
    : QGroupBox("Log", parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(4, 4, 4, 4);

    setupEntryStrip();
    setupContactTable();

    // updateFieldVisibility called after all widgets are created
    updateFieldVisibility();
}

void LogPanel::setupEntryStrip() {
    auto* strip = new QWidget(this);
    auto* row1  = new QHBoxLayout;
    auto* row2  = new QHBoxLayout;
    auto* vl    = new QVBoxLayout(strip);
    vl->setSpacing(2);
    vl->setContentsMargins(0, 0, 0, 2);

    QFont mono("Courier New", 10);

    // Fix 4: auto-uppercase
    auto forceUpper = [](QLineEdit* edit) {
        QObject::connect(edit, &QLineEdit::textEdited,
                         edit, [edit](const QString& text) {
                             int pos = edit->cursorPosition();
                             edit->setText(text.toUpper());
                             edit->setCursorPosition(pos);
                         });
    };

    // ── Row 1 ─────────────────────────────────────────────────────────────
    // LOGPANEL_FIX: use direct addWidget with addSpacing(8) and
    // setFixedWidth so Qt layout engine cannot separate label from field.

    m_callEntry = new QLineEdit;
    m_callEntry->setPlaceholderText("Their Call");
    m_callEntry->setFont(mono);
    m_callEntry->setMaximumWidth(100);
    forceUpper(m_callEntry);
    // textChanged (not textEdited): textEdited's argument is the
    // pre-uppercase keystroke value (forceUpper's setText() call above
    // doesn't retroactively fix up what's delivered to other slots on the
    // same signal emission), and textChanged also naturally covers
    // populateField()'s setText() and clear() below, keeping this a
    // faithful mirror of "whatever this field currently shows."
    connect(m_callEntry, &QLineEdit::textChanged,
            this, &LogPanel::theirCallChanged);
    row1->addWidget(m_callEntry, 0);

    // RS-R: label immediately followed by field, no gap
    row1->addSpacing(8);
    auto* rsrLabel = new QLabel("RS-R:");
    rsrLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    row1->addWidget(rsrLabel);
    m_rsReceived = new QLineEdit;
    m_rsReceived->setFixedWidth(38);
    m_rsReceived->setFont(mono);
    m_rsReceived->setPlaceholderText("--");
    row1->addWidget(m_rsReceived);

    // RS-S: label immediately followed by field, visually read-only
    row1->addSpacing(8);
    auto* rssLabel = new QLabel("RS-S:");
    rssLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    row1->addWidget(rssLabel);
    m_rsSent = new QLineEdit;
    m_rsSent->setFixedWidth(38);
    m_rsSent->setFont(mono);
    m_rsSent->setPlaceholderText("--");
    m_rsSent->setToolTip("Auto-computed from received signal -- edit to override");
    forceUpper(m_rsSent);
    // textChanged (not textEdited) for the same reason as m_callEntry
    // below: covers setRsSent()/maybeSetAutoRsSent()/clear() too, keeping
    // consumers (MacroPanel's <rstSent>) a faithful mirror of whatever
    // this field currently shows, however the value got there.
    connect(m_rsSent, &QLineEdit::textChanged,
            this, &LogPanel::rsSentChanged);
    row1->addWidget(m_rsSent);

    row1->addSpacing(8);
    m_parksLabel = new QLabel("Parks:");
    row1->addWidget(m_parksLabel);
    m_theirParks = new QLineEdit;
    m_theirParks->setPlaceholderText("US-XXXX ...");
    m_theirParks->setFont(mono);
    m_theirParks->setMinimumWidth(120);
    forceUpper(m_theirParks);
    row1->addWidget(m_theirParks, 1);  // stretch factor 1 — fills middle

    m_sotaLabel = new QLabel("SOTA:");
    row1->addWidget(m_sotaLabel);
    m_theirSota = new QLineEdit;
    m_theirSota->setMaximumWidth(100);
    m_theirSota->setFont(mono);
    forceUpper(m_theirSota);
    row1->addWidget(m_theirSota);

    m_fdLabel = new QLabel("FD Exch:");
    row1->addWidget(m_fdLabel);
    m_fdExchange = new QLineEdit;
    m_fdExchange->setMaximumWidth(80);
    m_fdExchange->setFont(mono);
    m_fdExchange->setPlaceholderText("2A MWA");
    row1->addWidget(m_fdExchange);

    // LOGPANEL_FIX: double-click hint label before buttons
    auto* editHint = new QLabel("Double-click row to edit");
    editHint->setStyleSheet(
        "color: #555; font-size: 8pt; font-style: italic;");
    editHint->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    row1->addWidget(editHint);

    // DELETE_LOG: Delete button, hidden until edit mode
    m_deleteButton = new QPushButton("Delete");
    m_deleteButton->setStyleSheet(
        "QPushButton {"
        "  background: #3a0a0a; color: #cc4444;"
        "  border: 1px solid #662222; padding: 4px 10px;"
        "}"
        "QPushButton:hover { background: #5a1a1a; }");
    m_deleteButton->setVisible(false);
    m_deleteButton->setToolTip("Delete this log entry (with confirmation)");
    row1->addWidget(m_deleteButton);

    m_logButton   = new QPushButton("Log It");
    m_clearButton = new QPushButton("Clear");
    m_logButton->setMinimumWidth(70);
    m_clearButton->setMinimumWidth(60);
    row1->addWidget(m_logButton);
    row1->addWidget(m_clearButton);

    // ── Row 2 ─────────────────────────────────────────────────────────────
    // Frequency/date/time — blank means auto (live dial + now-UTC). Manual
    // entry covers no-radio operation and paper-log transcription; edit
    // mode fills these with the row's original values so an edit can't
    // silently pick up the current dial frequency (audit 2026-07-09).
    row2->addWidget(new QLabel("MHz:"));
    m_freqEntry = new QLineEdit;
    m_freqEntry->setMaximumWidth(80);
    m_freqEntry->setFont(mono);
    m_freqEntry->setPlaceholderText("auto");
    m_freqEntry->setToolTip(
        "Frequency in MHz (e.g. 14.074).\n"
        "Blank = current dial frequency.");
    connect(m_freqEntry, &QLineEdit::textEdited,
            this, [this]() { m_freqManual = true; });
    row2->addWidget(m_freqEntry);

    row2->addWidget(new QLabel("Date:"));
    m_dateEntry = new QLineEdit;
    m_dateEntry->setMaximumWidth(80);
    m_dateEntry->setFont(mono);
    m_dateEntry->setPlaceholderText("auto");
    m_dateEntry->setToolTip("QSO date, UTC (YYYYMMDD). Blank = today.");
    row2->addWidget(m_dateEntry);

    row2->addWidget(new QLabel("Time:"));
    m_timeEntry = new QLineEdit;
    m_timeEntry->setMaximumWidth(64);
    m_timeEntry->setFont(mono);
    m_timeEntry->setPlaceholderText("auto");
    m_timeEntry->setToolTip("QSO time, UTC (HHMM or HHMMSS). Blank = now.");
    row2->addWidget(m_timeEntry);

    m_gridLabel = new QLabel("Grid:");
    row2->addWidget(m_gridLabel);
    m_theirGrid = new QLineEdit;
    m_theirGrid->setMaximumWidth(70);
    m_theirGrid->setFont(mono);
    forceUpper(m_theirGrid);
    row2->addWidget(m_theirGrid);

    m_nameLabel = new QLabel("Name:");
    row2->addWidget(m_nameLabel);
    m_theirName = new QLineEdit;
    m_theirName->setMaximumWidth(100);
    m_theirName->setFont(mono);
    row2->addWidget(m_theirName);

    m_qthLabel = new QLabel("QTH:");
    row2->addWidget(m_qthLabel);
    m_theirQth = new QLineEdit;
    m_theirQth->setMaximumWidth(120);
    m_theirQth->setFont(mono);
    row2->addWidget(m_theirQth);

    m_stateLabel = new QLabel("State:");
    row2->addWidget(m_stateLabel);
    m_theirState = new QLineEdit;
    m_theirState->setMaximumWidth(50);
    m_theirState->setFont(mono);
    m_theirState->setToolTip("Their state/province (e.g. IL, ON)");
    forceUpper(m_theirState);
    row2->addWidget(m_theirState);

    m_countyLabel = new QLabel("County:");
    row2->addWidget(m_countyLabel);
    m_theirCounty = new QLineEdit;
    m_theirCounty->setMaximumWidth(100);
    m_theirCounty->setFont(mono);
    m_theirCounty->setToolTip("Their county");
    forceUpper(m_theirCounty);
    row2->addWidget(m_theirCounty);

    row2->addWidget(new QLabel("Notes:"));
    m_notes = new QLineEdit;
    m_notes->setFont(mono);
    row2->addWidget(m_notes, 1);

    vl->addLayout(row1);
    vl->addLayout(row2);

    static_cast<QVBoxLayout*>(layout())->addWidget(strip);

    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    static_cast<QVBoxLayout*>(layout())->addWidget(line);

    connect(m_logButton,    &QPushButton::clicked, this, &LogPanel::onLogIt);
    connect(m_clearButton,  &QPushButton::clicked, this, &LogPanel::onClear);
    connect(m_deleteButton, &QPushButton::clicked, this, &LogPanel::onDeleteEntry);
    connect(m_callEntry, &QLineEdit::returnPressed, this, &LogPanel::onLogIt);
}

void LogPanel::setupContactTable() {
    m_contactTable = new QTableWidget(0, 10, this);
    m_contactTable->setHorizontalHeaderLabels({
        "UTC Date", "UTC", "Callsign", "Freq MHz",
        "RS-R", "RS-S", "Parks/SOTA", "Grid", "St", "Notes"
    });
    m_contactTable->horizontalHeader()->setStretchLastSection(true);
    m_contactTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_contactTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_contactTable->setAlternatingRowColors(true);
    m_contactTable->setFont(QFont("Courier New", 9));
    m_contactTable->verticalHeader()->setVisible(false);
    m_contactTable->setMinimumHeight(80);

    m_contactTable->setColumnWidth(0, 80);
    m_contactTable->setColumnWidth(1, 65);
    m_contactTable->setColumnWidth(2, 90);
    m_contactTable->setColumnWidth(3, 90);
    m_contactTable->setColumnWidth(4, 45);
    m_contactTable->setColumnWidth(5, 45);
    m_contactTable->setColumnWidth(6, 140);
    m_contactTable->setColumnWidth(7, 60);
    m_contactTable->setColumnWidth(8, 35);

    static_cast<QVBoxLayout*>(layout())->addWidget(m_contactTable);

    connect(m_contactTable, &QTableWidget::cellClicked,
            this, &LogPanel::onContactRowClicked);
    connect(m_contactTable, &QTableWidget::cellDoubleClicked,
            this, &LogPanel::onContactRowDoubleClicked);
}

void LogPanel::refresh() {
    updateFieldVisibility();
}

void LogPanel::updateFieldVisibility() {
    // Parks/SOTA visibility must not depend on whether *my* station has a
    // POTA/SOTA reference configured in Settings — a POTA/SOTA hunter with
    // no activation of their own still needs to log the refs *they* work.
    bool showPota    = !m_fdMode;
    bool showSota    = !m_fdMode;
    bool showGeneral = !m_fdMode;

    m_parksLabel->setVisible(showPota);
    m_theirParks->setVisible(showPota);
    m_sotaLabel->setVisible(showSota);
    m_theirSota->setVisible(showSota);
    m_gridLabel->setVisible(showGeneral);
    m_theirGrid->setVisible(showGeneral);
    m_nameLabel->setVisible(showGeneral);
    m_theirName->setVisible(showGeneral);
    m_qthLabel->setVisible(showGeneral);
    m_theirQth->setVisible(showGeneral);
    m_stateLabel->setVisible(showGeneral);
    m_theirState->setVisible(showGeneral);
    m_countyLabel->setVisible(showGeneral);
    m_theirCounty->setVisible(showGeneral);
    m_fdLabel->setVisible(m_fdMode);
    m_fdExchange->setVisible(m_fdMode);
}

void LogPanel::setFieldDayMode(bool enabled) {
    m_fdMode = enabled;
    updateFieldVisibility();
}

void LogPanel::populateField(const QString& scheme, const QString& value) {
    if (scheme == "callsign") {
        if (!m_callEntry->text().trimmed().isEmpty() &&
            m_callEntry->text().trimmed().toUpper() != value.toUpper())
        {
            auto reply = QMessageBox::question(
                this, "Replace Callsign",
                QString("Replace %1 with %2?")
                    .arg(m_callEntry->text(), value),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (reply != QMessageBox::Yes) return;
        }
        m_callEntry->setText(value.toUpper());
    }
    else if (scheme == "pota") {
        QString existing = m_theirParks->text().trimmed();
        for (const QString& park : value.split(' ', Qt::SkipEmptyParts)) {
            QString fixed = fixPotaRef(park);
            if (!existing.contains(fixed))
                existing += (existing.isEmpty() ? "" : " ") + fixed;
        }
        m_theirParks->setText(existing);
    }
    else if (scheme == "sota")  m_theirSota->setText(value.toUpper());
    else if (scheme == "state")  m_theirState->setText(value.toUpper());
    else if (scheme == "county") m_theirCounty->setText(value.toUpper());
    else if (scheme == "grid")  m_theirGrid->setText(value.toUpper());
    else if (scheme == "rs")    m_rsReceived->setText(value);
    else if (scheme == "name")  m_theirName->setText(value);
    else if (scheme == "qth")   m_theirQth->setText(value);
    else if (scheme == "fd")    m_fdExchange->setText(value.toUpper());
}

void LogPanel::autoPopulateFromMessage(const QString& senderCallsign,
                                        const QString& text)
{
    // ADR-133 multi-party rule set. Inline markers are authoritative
    // where present; the text-tag fallback below obeys the same gates.
    ScannedMarkers mk = scanFieldMarkers(text);

    QString sender    = senderCallsign.trimmed().toUpper();
    QString recipient;                    // 'c' marker, if declared
    for (const auto& f : mk.fields) {
        if (f.first == HavenFSK::FieldId::Sender)
            sender = f.second.trimmed().toUpper();
        else if (f.first == HavenFSK::FieldId::Recipient)
            recipient = f.second.trimmed().toUpper();
    }

    QString current = m_callEntry->text().trimmed().toUpper();
    QString myCall  = HavenFSK::loadStationInfo().callsign.trimmed().toUpper();

    // Addressed to me: the sender's 'c' declaration when present,
    // otherwise my callsign appearing in the (marker-stripped) text.
    bool addressedToMe = false;
    if (!recipient.isEmpty()) {
        addressedToMe = !myCall.isEmpty() && recipient == myCall;
    } else if (!myCall.isEmpty()) {
        QRegularExpression re("\\b" + QRegularExpression::escape(myCall) + "\\b");
        addressedToMe = re.match(mk.display.toUpper()).hasMatch();
    }

    // Rule 4: a non-empty entry holding a different callsign is never
    // replaced automatically — even by a message addressed to me (an
    // interloper calling me mid-QSO must not steal the entry). Only
    // Log It, Clear, or a click switches contacts.
    if (!current.isEmpty() && sender != current) return;

    if (current.isEmpty()) {
        if (sender.isEmpty()) return;
        // Rule 5: the station I logged minutes ago doesn't re-seed the
        // entry (their late "TU 73" must not block the next caller).
        auto it = m_recentlyLogged.constFind(sender);
        if (it != m_recentlyLogged.constEnd() &&
            it.value().secsTo(QDateTime::currentDateTimeUtc())
                < RELOG_SUPPRESS_SECS)
            return;
        // Rule 3 seeding: a message for me, or an unaddressed broadcast
        // (CQ/info — markers make its fields trustworthy). Unaddressed
        // markerless traffic is someone else's QSO — never seeds.
        if (!addressedToMe && !(recipient.isEmpty() && !mk.fields.isEmpty()))
            return;
        m_callEntry->setText(sender);
    } else if (!addressedToMe && !recipient.isEmpty()) {
        // My current contact talking to someone else (activator working
        // another hunter): their exchange is not mine — rule 2's wedge.
        return;
    }

    // ── Field routing ────────────────────────────────────────────────
    // Fill only empty fields — a manual edit is never clobbered. Rule 2:
    // pair-scoped RS lands only when the message is addressed to me.
    auto fillIfEmpty = [](QLineEdit* target, const QString& value) {
        if (target && target->text().trimmed().isEmpty())
            target->setText(value.toUpper());
    };

    for (const auto& f : mk.fields) {
        const QString& value = f.second;
        switch (f.first) {
            case HavenFSK::FieldId::Pota:
                populateField("pota", value);  // merge-safe, never overwrites
                break;
            case HavenFSK::FieldId::Rs:
                if (addressedToMe) fillIfEmpty(m_rsReceived, value);
                break;
            case HavenFSK::FieldId::Grid: fillIfEmpty(m_theirGrid, value); break;
            case HavenFSK::FieldId::Sota: fillIfEmpty(m_theirSota, value); break;
            case HavenFSK::FieldId::Name: fillIfEmpty(m_theirName, value); break;
            case HavenFSK::FieldId::Qth:  fillIfEmpty(m_theirQth,  value); break;
            case HavenFSK::FieldId::Fd:   fillIfEmpty(m_fdExchange, value); break;
            case HavenFSK::FieldId::State:
                fillIfEmpty(m_theirState, value); break;
            case HavenFSK::FieldId::County:
                fillIfEmpty(m_theirCounty, value); break;
            default: break;   // d/c handled above
        }
    }

    // Text-tag fallback (hand-typed exchanges, stations without
    // markers) — same gates, scanning the marker-stripped text.
    for (const auto& tag : parseStructuredTags(mk.display)) {
        const QString& scheme = tag.first;
        const QString& value  = tag.second;

        if (scheme == "pota") {
            populateField("pota", value);
            continue;
        }
        if (scheme == "rs") {
            if (addressedToMe) fillIfEmpty(m_rsReceived, value);
            continue;
        }
        QLineEdit* target = nullptr;
        if      (scheme == "name") target = m_theirName;
        else if (scheme == "qth")  target = m_theirQth;
        else if (scheme == "grid") target = m_theirGrid;
        else if (scheme == "sota") target = m_theirSota;
        else if (scheme == "fd")   target = m_fdExchange;
        fillIfEmpty(target, value);
    }

    // Bare-shape fallback: real traffic says "CQ POTA DE N8SDR US-1234
    // EM79RJ K" with no POTA:/GRID: prefix and no markers. Park refs
    // and Maidenhead grids are distinctive shapes, and this point is
    // only reached once the addressing gates above have passed —
    // they're facts about the station already in (or seeding) the
    // entry. Mirrors RxDisplay::renderMessage's bare-shape link passes.
    QString dispUpper = mk.display.toUpper();
    static QRegularExpression bareRefRe(
        "\\b([A-Z0-9]{1,2}-[0-9]{4,5})\\b");
    auto refIt = bareRefRe.globalMatch(dispUpper);
    while (refIt.hasNext())
        populateField("pota", refIt.next().captured(1));  // merge-safe

    static QRegularExpression bareGridRe(
        "\\b([A-R]{2}[0-9]{2}(?:[A-X]{2})?)\\b");
    auto gridIt = bareGridRe.globalMatch(dispUpper);
    if (gridIt.hasNext())
        fillIfEmpty(m_theirGrid, gridIt.next().captured(1));
}

void LogPanel::loadContacts(const QList<QVariantMap>& contacts) {
    m_contactTable->setRowCount(0);
    // Ascending time order in, insert-at-row-0 per contact — newest ends
    // up on top, matching live logging.
    for (const auto& c : contacts) {
        QVariantMap fields = c;
        fields["db_id"] = c["id"];
        // The DB stores parks space-joined; the table code expects a list.
        fields["their_pota_refs"] =
            c["their_pota_refs"].toString().split(' ', Qt::SkipEmptyParts);
        addContactRow(fields);
    }
}

void LogPanel::onContactPersisted(const QVariantMap& fields) {
    // Row 0 is the row addContactRow() created for this same contact
    // moments ago in the same synchronous signal chain (onLogIt →
    // contactLogged → LogManager::logContact → contactSaved). Verify
    // anyway — never stamp a foreign row.
    if (m_contactTable->rowCount() == 0) return;
    auto* item = m_contactTable->item(0, 0);
    QVariantMap row = item->data(Qt::UserRole).toMap();
    if (row["their_callsign"].toString() != fields["their_callsign"].toString() ||
        row["time_utc"].toString()       != fields["time_utc"].toString())
        return;
    row["db_id"] = fields["db_id"];
    item->setData(Qt::UserRole, row);
}

void LogPanel::setRsSent(const QString& rs)    { m_rsSent->setText(rs); }

bool LogPanel::maybeSetAutoRsSent(const QString& senderCallsign,
                                   const QString& rs)
{
    if (senderCallsign.trimmed().toUpper() !=
        m_callEntry->text().trimmed().toUpper())
        return false;
    if (m_callEntry->text().trimmed().isEmpty()) return false;
    if (!m_rsSent->text().trimmed().isEmpty())   return false;
    m_rsSent->setText(rs);
    return true;
}

QString LogPanel::mhzText(uint64_t hz) {
    QString s = QString::number(static_cast<double>(hz) / 1e6, 'f', 6);
    while (s.endsWith('0')) s.chop(1);
    if (s.endsWith('.')) s.chop(1);
    return s;
}

void LogPanel::setFrequency(uint64_t hz) {
    m_frequency = hz;
    // Mirror the live dial into the entry field unless the operator (or
    // edit mode) has put their own value there.
    if (!m_freqManual)
        m_freqEntry->setText(hz > 0 ? mhzText(hz) : QString());
}

void LogPanel::exitEditMode() {
    m_editingRow = -1;
    m_logButton->setText("Log It");
    m_logButton->setStyleSheet("");
    m_clearButton->setText("Clear");
    m_deleteButton->setVisible(false);
    // Edit mode showed the row's original date/time as placeholders
    m_dateEntry->setPlaceholderText("auto");
    m_timeEntry->setPlaceholderText("auto");
}

void LogPanel::onLogIt() {
    QString call = m_callEntry->text().trimmed().toUpper();
    if (call.isEmpty()) {
        QMessageBox::warning(this, "Log Entry",
            "Please enter the contacted station's callsign.");
        return;
    }

    // Frequency: manual entry wins; blank = live dial frequency.
    uint64_t hz = m_frequency;
    QString freqText = m_freqEntry->text().trimmed();
    if (!freqText.isEmpty()) {
        bool ok = false;
        double mhz = freqText.toDouble(&ok);
        if (!ok || mhz <= 0.0) {
            QMessageBox::warning(this, "Log Entry",
                "Frequency must be in MHz, e.g. 14.074");
            return;
        }
        hz = static_cast<uint64_t>(std::llround(mhz * 1e6));
    }
    if (hz == 0) {
        // Contacts without a frequency get no valid BAND, and POTA (among
        // others) rejects records without one — flag it now, while the
        // operator still remembers the frequency, not at export time.
        auto reply = QMessageBox::question(this, "Log Entry",
            "No frequency set — the contact will export without\n"
            "band information (POTA uploads reject such records).\n\n"
            "Log it anyway?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    }

    // Date/time: manual entry wins; blank = now (UTC).
    QDateTime utcNow = QDateTime::currentDateTimeUtc();
    QString dateText = m_dateEntry->text().trimmed().remove('-');
    QString timeText = m_timeEntry->text().trimmed().remove(':');
    static QRegularExpression dateRe("^\\d{8}$");
    static QRegularExpression timeRe("^\\d{4}(\\d{2})?$");
    if (!dateText.isEmpty() && !dateRe.match(dateText).hasMatch()) {
        QMessageBox::warning(this, "Log Entry",
            "Date must be YYYYMMDD (UTC), e.g. 20260709");
        return;
    }
    if (!timeText.isEmpty()) {
        if (!timeRe.match(timeText).hasMatch()) {
            QMessageBox::warning(this, "Log Entry",
                "Time must be HHMM or HHMMSS (UTC), e.g. 1832");
            return;
        }
        if (timeText.length() == 4) timeText += "00";
    }

    HavenFSK::StationInfo myInfo = HavenFSK::loadStationInfo();

    QVariantMap fields;
    fields["their_callsign"]  = call;
    fields["rs_received"]     = m_rsReceived->text().trimmed();
    fields["rs_sent"]         = m_rsSent->text().trimmed();

    QStringList rawParks = m_theirParks->text().trimmed()
                               .split(' ', Qt::SkipEmptyParts);
    QStringList fixedParks;
    for (const QString& p : rawParks)
        fixedParks.append(fixPotaRef(p));
    fields["their_pota_refs"] = fixedParks;

    fields["their_sota_ref"]  = m_theirSota->text().trimmed().toUpper();
    fields["their_grid"]      = m_theirGrid->text().trimmed().toUpper();
    fields["their_name"]      = m_theirName->text().trimmed();
    fields["their_qth"]       = m_theirQth->text().trimmed();
    fields["their_state"]     = m_theirState->text().trimmed().toUpper();
    fields["their_county"]    = m_theirCounty->text().trimmed().toUpper();
    fields["their_fd"]        = m_fdExchange->text().trimmed().toUpper();
    fields["notes"]           = m_notes->text().trimmed();
    fields["frequency_hz"]    = QVariant::fromValue(hz);
    // mode/submode intentionally not set here — LogManager::logContact()
    // derives them from the active modem (fields["modem_name"], stamped
    // by MainWindow) so PSK31 QSOs aren't logged as HAVEN-FSK.
    fields["my_callsign"]     = myInfo.callsign;
    fields["my_grid"]         = myInfo.grid;
    fields["my_pota_refs"]    = myInfo.potaRefs;
    fields["my_sota_ref"]     = myInfo.sotaRef;
    fields["my_fd_class"]     = myInfo.fdClass;
    fields["my_fd_section"]   = myInfo.fdSection;
    fields["my_op_name"]      = myInfo.opName;
    fields["my_state"]        = myInfo.state;    // STATION_FIELDS
    fields["my_county"]       = myInfo.county;   // STATION_FIELDS

    if (m_editingRow >= 0) {
        // Edit mode: update existing entry, keep its db_id. Date/time come
        // from the entry fields (filled with the row's originals when edit
        // mode was entered), falling back to the originals if cleared.
        QVariant origData = m_contactTable->item(m_editingRow, 0)
                                ->data(Qt::UserRole);
        if (origData.isValid()) {
            QVariantMap orig = origData.toMap();
            fields["date_utc"] = dateText.isEmpty() ? orig["date_utc"]
                                                    : QVariant(dateText);
            fields["time_utc"] = timeText.isEmpty() ? orig["time_utc"]
                                                    : QVariant(timeText);
            fields["db_id"]    = orig["db_id"];
        }
        updateContactRow(m_editingRow, fields);
        emit contactUpdated(fields);
        exitEditMode();
    } else {
        fields["date_utc"] = dateText.isEmpty() ? utcNow.toString("yyyyMMdd")
                                                : dateText;
        fields["time_utc"] = timeText.isEmpty() ? utcNow.toString("hhmmss")
                                                : timeText;
        addContactRow(fields);
        emit contactLogged(fields);

        // ADR-133 rule 5: remember who was just logged so their late
        // "TU 73" doesn't re-seed the entry cleared for the next caller.
        QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        m_recentlyLogged[call] = nowUtc;
        for (auto it = m_recentlyLogged.begin();
             it != m_recentlyLogged.end(); ) {
            if (it.value().secsTo(nowUtc) > RELOG_SUPPRESS_SECS)
                it = m_recentlyLogged.erase(it);
            else
                ++it;
        }
    }
    onClear();
}

void LogPanel::onClear() {
    exitEditMode();
    m_callEntry->clear();
    m_rsReceived->clear();
    m_rsSent->clear();
    m_theirParks->clear();
    m_theirSota->clear();
    m_theirGrid->clear();
    m_theirName->clear();
    m_theirQth->clear();
    m_theirState->clear();
    m_theirCounty->clear();
    m_fdExchange->clear();
    m_notes->clear();
    m_dateEntry->clear();
    m_timeEntry->clear();
    m_freqManual = false;
    m_freqEntry->setText(m_frequency > 0 ? mhzText(m_frequency) : QString());
    emit entryCleared();
}

void LogPanel::onDeleteEntry() {
    if (m_editingRow < 0) return;

    QString call = m_callEntry->text().trimmed();
    if (call.isEmpty()) {
        QVariant data = m_contactTable->item(m_editingRow, 0)
                            ->data(Qt::UserRole);
        if (data.isValid())
            call = data.toMap()["their_callsign"].toString();
    }

    auto reply = QMessageBox::warning(
        this, "Delete Log Entry",
        QString("Delete contact with %1?\n\nThis cannot be undone.")
        .arg(call.isEmpty() ? "this station" : call),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);  // No is default — safer

    if (reply != QMessageBox::Yes) return;

    QVariant data = m_contactTable->item(m_editingRow, 0)->data(Qt::UserRole);
    int dbId = 0;
    if (data.isValid())
        dbId = data.toMap()["db_id"].toInt();

    m_contactTable->removeRow(m_editingRow);
    exitEditMode();
    onClear();

    if (dbId > 0)
        emit contactDeleted(dbId);
}

void LogPanel::addContactRow(const QVariantMap& fields) {
    m_contactTable->insertRow(0);

    QString freqStr;
    uint64_t hz = fields["frequency_hz"].toULongLong();
    if (hz > 0)
        freqStr = QString::number(static_cast<double>(hz) / 1.0e6, 'f', 3);

    QString activity;
    QStringList parks = fields["their_pota_refs"].toStringList();
    if (!parks.isEmpty()) activity = parks.join(" ");
    QString sota = fields["their_sota_ref"].toString();
    if (!sota.isEmpty())
        activity += (activity.isEmpty() ? "" : " ") + sota;
    QString fd = fields["their_fd"].toString();
    if (!fd.isEmpty()) activity = fd;

    auto* dateItem = new QTableWidgetItem(
        displayDate(fields["date_utc"].toString()));
    auto* timeItem = new QTableWidgetItem(fields["time_utc"].toString());
    auto* callItem = new QTableWidgetItem(fields["their_callsign"].toString());
    auto* freqItem = new QTableWidgetItem(freqStr);
    freqItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* rsrItem  = new QTableWidgetItem(fields["rs_received"].toString());
    auto* rssItem  = new QTableWidgetItem(fields["rs_sent"].toString());
    auto* actItem  = new QTableWidgetItem(activity);
    auto* gridItem = new QTableWidgetItem(fields["their_grid"].toString());
    auto* stItem   = new QTableWidgetItem(fields["their_state"].toString());
    auto* noteItem = new QTableWidgetItem(fields["notes"].toString());

    m_contactTable->setItem(0, 0, dateItem);
    m_contactTable->setItem(0, 1, timeItem);
    m_contactTable->setItem(0, 2, callItem);
    m_contactTable->setItem(0, 3, freqItem);
    m_contactTable->setItem(0, 4, rsrItem);
    m_contactTable->setItem(0, 5, rssItem);
    m_contactTable->setItem(0, 6, actItem);
    m_contactTable->setItem(0, 7, gridItem);
    m_contactTable->setItem(0, 8, stItem);
    m_contactTable->setItem(0, 9, noteItem);

    m_contactTable->item(0, 0)->setData(Qt::UserRole, fields);

    while (m_contactTable->rowCount() > MAX_VISIBLE_ROWS * 3)
        m_contactTable->removeRow(m_contactTable->rowCount() - 1);
}

void LogPanel::updateContactRow(int row, const QVariantMap& fields) {
    if (row < 0 || row >= m_contactTable->rowCount()) return;

    QString freqStr;
    uint64_t hz = fields["frequency_hz"].toULongLong();
    if (hz > 0)
        freqStr = QString::number(static_cast<double>(hz) / 1.0e6, 'f', 3);

    QString activity;
    QStringList parks = fields["their_pota_refs"].toStringList();
    if (!parks.isEmpty()) activity = parks.join(" ");
    QString sota = fields["their_sota_ref"].toString();
    if (!sota.isEmpty())
        activity += (activity.isEmpty() ? "" : " ") + sota;
    QString fd = fields["their_fd"].toString();
    if (!fd.isEmpty()) activity = fd;

    m_contactTable->item(row, 0)->setText(
        displayDate(fields["date_utc"].toString()));
    m_contactTable->item(row, 1)->setText(fields["time_utc"].toString());
    m_contactTable->item(row, 2)->setText(fields["their_callsign"].toString());
    m_contactTable->item(row, 3)->setText(freqStr);
    m_contactTable->item(row, 4)->setText(fields["rs_received"].toString());
    m_contactTable->item(row, 5)->setText(fields["rs_sent"].toString());
    m_contactTable->item(row, 6)->setText(activity);
    m_contactTable->item(row, 7)->setText(fields["their_grid"].toString());
    m_contactTable->item(row, 8)->setText(fields["their_state"].toString());
    m_contactTable->item(row, 9)->setText(fields["notes"].toString());
    m_contactTable->item(row, 0)->setData(Qt::UserRole, fields);
}

void LogPanel::onContactRowClicked(int row, int col) {
    Q_UNUSED(col)
    QVariant data = m_contactTable->item(row, 0)->data(Qt::UserRole);
    if (!data.isValid()) return;
    QVariantMap fields = data.toMap();

    m_callEntry->setText(fields["their_callsign"].toString());
    m_rsReceived->setText(fields["rs_received"].toString());
    m_rsSent->setText(fields["rs_sent"].toString());
    m_theirParks->setText(fields["their_pota_refs"].toStringList().join(" "));
    m_theirSota->setText(fields["their_sota_ref"].toString());
    m_theirGrid->setText(fields["their_grid"].toString());
    m_theirName->setText(fields["their_name"].toString());
    m_theirQth->setText(fields["their_qth"].toString());
    m_theirState->setText(fields["their_state"].toString());
    m_theirCounty->setText(fields["their_county"].toString());
    m_fdExchange->setText(fields["their_fd"].toString());
    m_notes->setText(fields["notes"].toString());
}

void LogPanel::onContactRowDoubleClicked(int row, int col) {
    onContactRowClicked(row, col);

    // Edit mode edits the row's own frequency, not the live dial value —
    // without this, updating an old contact silently rewrote its
    // frequency to wherever the radio is tuned right now. Date/time stay
    // BLANK: blank keeps the row's originals (shown as placeholders), so
    // they only change when the operator specifically types new values.
    QVariant data = m_contactTable->item(row, 0)->data(Qt::UserRole);
    if (data.isValid()) {
        QVariantMap orig = data.toMap();
        uint64_t hz = orig["frequency_hz"].toULongLong();
        m_freqEntry->setText(hz > 0 ? mhzText(hz) : QString());
        m_freqManual = true;
        m_dateEntry->clear();
        m_timeEntry->clear();
        m_dateEntry->setPlaceholderText(orig["date_utc"].toString());
        m_timeEntry->setPlaceholderText(orig["time_utc"].toString());
    }

    m_editingRow = row;
    m_logButton->setText("Update");
    m_logButton->setStyleSheet(
        "QPushButton { background: #1a4a2a; color: #88cc88; "
        "border: 1px solid #336633; }");
    m_clearButton->setText("Cancel Edit");
    m_deleteButton->setVisible(true);
}
