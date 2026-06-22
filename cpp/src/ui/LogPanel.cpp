#include "LogPanel.h"
#include "../dsp/Constants.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFrame>
#include <QFont>
#include <QSizePolicy>
#include <QRegularExpression>

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
    m_rsSent->setReadOnly(true);
    m_rsSent->setToolTip("Auto-computed from received signal (read-only)");
    m_rsSent->setStyleSheet(
        "QLineEdit { background: #1a1a1a; color: #888; "
        "border: 1px solid #2a2a2a; }");
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
    m_contactTable = new QTableWidget(0, 8, this);
    m_contactTable->setHorizontalHeaderLabels({
        "Time", "Callsign", "Freq MHz",
        "RS-R", "RS-S", "Parks/SOTA", "Grid", "Notes"
    });
    m_contactTable->horizontalHeader()->setStretchLastSection(true);
    m_contactTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_contactTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_contactTable->setAlternatingRowColors(true);
    m_contactTable->setFont(QFont("Courier New", 9));
    m_contactTable->verticalHeader()->setVisible(false);
    m_contactTable->setMinimumHeight(80);

    m_contactTable->setColumnWidth(0, 65);
    m_contactTable->setColumnWidth(1, 90);
    m_contactTable->setColumnWidth(2, 90);
    m_contactTable->setColumnWidth(3, 45);
    m_contactTable->setColumnWidth(4, 45);
    m_contactTable->setColumnWidth(5, 140);
    m_contactTable->setColumnWidth(6, 60);

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
    HavenFSK::StationInfo info = HavenFSK::loadStationInfo();

    bool showPota    = !m_fdMode && !info.potaRefs.isEmpty();
    bool showSota    = !m_fdMode && !info.sotaRef.isEmpty();
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
    else if (scheme == "grid")  m_theirGrid->setText(value.toUpper());
    else if (scheme == "rs")    m_rsReceived->setText(value);
    else if (scheme == "name")  m_theirName->setText(value);
    else if (scheme == "qth")   m_theirQth->setText(value);
    else if (scheme == "fd")    m_fdExchange->setText(value.toUpper());
}

void LogPanel::setRsSent(const QString& rs)    { m_rsSent->setText(rs); }
void LogPanel::setFrequency(uint64_t hz)        { m_frequency = hz; }

void LogPanel::exitEditMode() {
    m_editingRow = -1;
    m_logButton->setText("Log It");
    m_logButton->setStyleSheet("");
    m_clearButton->setText("Clear");
    m_deleteButton->setVisible(false);
}

void LogPanel::onLogIt() {
    QString call = m_callEntry->text().trimmed().toUpper();
    if (call.isEmpty()) {
        QMessageBox::warning(this, "Log Entry",
            "Please enter the contacted station's callsign.");
        return;
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
    fields["their_fd"]        = m_fdExchange->text().trimmed().toUpper();
    fields["notes"]           = m_notes->text().trimmed();
    fields["frequency_hz"]    = QVariant::fromValue(m_frequency);
    fields["mode"]            = "DIGITAL";
    fields["submode"]         = "HAVEN-FSK";
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
        // Edit mode: update existing entry, preserve original date/time and db_id
        QVariant origData = m_contactTable->item(m_editingRow, 0)
                                ->data(Qt::UserRole);
        if (origData.isValid()) {
            QVariantMap orig = origData.toMap();
            fields["date_utc"] = orig["date_utc"];
            fields["time_utc"] = orig["time_utc"];
            fields["db_id"]    = orig["db_id"];
        }
        updateContactRow(m_editingRow, fields);
        emit contactUpdated(fields);
        exitEditMode();
    } else {
        QDateTime utcNow = QDateTime::currentDateTimeUtc();
        fields["date_utc"] = utcNow.toString("yyyyMMdd");
        fields["time_utc"] = utcNow.toString("hhmmss");
        addContactRow(fields);
        emit contactLogged(fields);
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
    m_fdExchange->clear();
    m_notes->clear();
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

    auto* timeItem = new QTableWidgetItem(fields["time_utc"].toString());
    auto* callItem = new QTableWidgetItem(fields["their_callsign"].toString());
    auto* freqItem = new QTableWidgetItem(freqStr);
    freqItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* rsrItem  = new QTableWidgetItem(fields["rs_received"].toString());
    auto* rssItem  = new QTableWidgetItem(fields["rs_sent"].toString());
    auto* actItem  = new QTableWidgetItem(activity);
    auto* gridItem = new QTableWidgetItem(fields["their_grid"].toString());
    auto* noteItem = new QTableWidgetItem(fields["notes"].toString());

    m_contactTable->setItem(0, 0, timeItem);
    m_contactTable->setItem(0, 1, callItem);
    m_contactTable->setItem(0, 2, freqItem);
    m_contactTable->setItem(0, 3, rsrItem);
    m_contactTable->setItem(0, 4, rssItem);
    m_contactTable->setItem(0, 5, actItem);
    m_contactTable->setItem(0, 6, gridItem);
    m_contactTable->setItem(0, 7, noteItem);

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

    m_contactTable->item(row, 0)->setText(fields["time_utc"].toString());
    m_contactTable->item(row, 1)->setText(fields["their_callsign"].toString());
    m_contactTable->item(row, 2)->setText(freqStr);
    m_contactTable->item(row, 3)->setText(fields["rs_received"].toString());
    m_contactTable->item(row, 4)->setText(fields["rs_sent"].toString());
    m_contactTable->item(row, 5)->setText(activity);
    m_contactTable->item(row, 6)->setText(fields["their_grid"].toString());
    m_contactTable->item(row, 7)->setText(fields["notes"].toString());
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
    m_fdExchange->setText(fields["their_fd"].toString());
    m_notes->setText(fields["notes"].toString());
}

void LogPanel::onContactRowDoubleClicked(int row, int col) {
    onContactRowClicked(row, col);

    m_editingRow = row;
    m_logButton->setText("Update");
    m_logButton->setStyleSheet(
        "QPushButton { background: #1a4a2a; color: #88cc88; "
        "border: 1px solid #336633; }");
    m_clearButton->setText("Cancel Edit");
    m_deleteButton->setVisible(true);
}
