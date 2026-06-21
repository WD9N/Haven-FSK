#include "LogPanel.h"
#include "../dsp/Constants.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFrame>
#include <QFont>

LogPanel::LogPanel(QWidget* parent)
    : QGroupBox("Log", parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(4, 4, 4, 4);

    setupEntryStrip();
    setupContactTable();

    refresh();
}

void LogPanel::setupEntryStrip() {
    auto* strip = new QWidget(this);
    auto* row1  = new QHBoxLayout;
    auto* row2  = new QHBoxLayout;
    auto* vl    = new QVBoxLayout(strip);
    vl->setSpacing(2);
    vl->setContentsMargins(0, 0, 0, 2);

    QFont mono("Courier New", 10);

    // Row 1: Callsign, RS-R, RS-S, Parks, SOTA, FD exchange, Log/Clear
    m_callEntry = new QLineEdit;
    m_callEntry->setPlaceholderText("Their Call");
    m_callEntry->setFont(mono);
    m_callEntry->setMaximumWidth(100);
    row1->addWidget(m_callEntry);

    row1->addWidget(new QLabel("RS-R:"));
    m_rsReceived = new QLineEdit;
    m_rsReceived->setMaximumWidth(40);
    m_rsReceived->setFont(mono);
    m_rsReceived->setPlaceholderText("59");
    row1->addWidget(m_rsReceived);

    row1->addWidget(new QLabel("RS-S:"));
    m_rsSent = new QLineEdit;
    m_rsSent->setMaximumWidth(40);
    m_rsSent->setFont(mono);
    m_rsSent->setPlaceholderText("--");
    m_rsSent->setReadOnly(true);
    m_rsSent->setToolTip("Auto-computed from received signal measurement");
    m_rsSent->setStyleSheet("background: #f0f0f0;");
    row1->addWidget(m_rsSent);

    m_parksLabel = new QLabel("Parks:");
    row1->addWidget(m_parksLabel);
    m_theirParks = new QLineEdit;
    m_theirParks->setPlaceholderText("US-XXXX ...");
    m_theirParks->setFont(mono);
    m_theirParks->setMinimumWidth(120);
    row1->addWidget(m_theirParks, 1);

    m_sotaLabel = new QLabel("SOTA:");
    row1->addWidget(m_sotaLabel);
    m_theirSota = new QLineEdit;
    m_theirSota->setMaximumWidth(100);
    m_theirSota->setFont(mono);
    row1->addWidget(m_theirSota);

    m_fdLabel = new QLabel("FD Exch:");
    row1->addWidget(m_fdLabel);
    m_fdExchange = new QLineEdit;
    m_fdExchange->setMaximumWidth(80);
    m_fdExchange->setFont(mono);
    m_fdExchange->setPlaceholderText("2A MWA");
    row1->addWidget(m_fdExchange);

    m_logButton   = new QPushButton("Log It");
    m_clearButton = new QPushButton("Clear");
    m_logButton->setMinimumWidth(70);
    m_clearButton->setMinimumWidth(60);
    row1->addWidget(m_logButton);
    row1->addWidget(m_clearButton);

    // Row 2: Grid, Name, QTH, Notes
    m_gridLabel = new QLabel("Grid:");
    row2->addWidget(m_gridLabel);
    m_theirGrid = new QLineEdit;
    m_theirGrid->setMaximumWidth(70);
    m_theirGrid->setFont(mono);
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

    connect(m_logButton,   &QPushButton::clicked, this, &LogPanel::onLogIt);
    connect(m_clearButton, &QPushButton::clicked, this, &LogPanel::onClear);
    connect(m_callEntry, &QLineEdit::returnPressed, this, &LogPanel::onLogIt);
}

void LogPanel::setupContactTable() {
    m_contactTable = new QTableWidget(0, 7, this);
    m_contactTable->setHorizontalHeaderLabels({
        "Time", "Callsign", "RS-R", "RS-S", "Parks/SOTA", "Grid", "Notes"
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
    m_contactTable->setColumnWidth(2, 45);
    m_contactTable->setColumnWidth(3, 45);
    m_contactTable->setColumnWidth(4, 150);
    m_contactTable->setColumnWidth(5, 60);

    static_cast<QVBoxLayout*>(layout())->addWidget(m_contactTable);

    connect(m_contactTable, &QTableWidget::cellClicked,
            this, &LogPanel::onContactRowClicked);
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
            if (!existing.contains(park))
                existing += (existing.isEmpty() ? "" : " ") + park;
        }
        m_theirParks->setText(existing.toUpper());
    }
    else if (scheme == "sota")     m_theirSota->setText(value.toUpper());
    else if (scheme == "grid")     m_theirGrid->setText(value.toUpper());
    else if (scheme == "rs")       m_rsReceived->setText(value);
    else if (scheme == "name")     m_theirName->setText(value);
    else if (scheme == "qth")      m_theirQth->setText(value);
    else if (scheme == "fd")       m_fdExchange->setText(value.toUpper());
}

void LogPanel::setRsSent(const QString& rs) {
    m_rsSent->setText(rs);
}

void LogPanel::setFrequency(uint64_t hz) {
    m_frequency = hz;
}

void LogPanel::onLogIt() {
    QString call = m_callEntry->text().trimmed().toUpper();
    if (call.isEmpty()) {
        QMessageBox::warning(this, "Log Entry",
            "Please enter the contacted station's callsign.");
        return;
    }

    HavenFSK::StationInfo myInfo = HavenFSK::loadStationInfo();
    QDateTime utcNow = QDateTime::currentDateTimeUtc();

    QVariantMap fields;
    fields["their_callsign"]  = call;
    fields["rs_received"]     = m_rsReceived->text().trimmed();
    fields["rs_sent"]         = m_rsSent->text().trimmed();
    fields["their_pota_refs"] = m_theirParks->text().trimmed()
                                    .split(' ', Qt::SkipEmptyParts);
    fields["their_sota_ref"]  = m_theirSota->text().trimmed().toUpper();
    fields["their_grid"]      = m_theirGrid->text().trimmed().toUpper();
    fields["their_name"]      = m_theirName->text().trimmed();
    fields["their_qth"]       = m_theirQth->text().trimmed();
    fields["their_fd"]        = m_fdExchange->text().trimmed().toUpper();
    fields["notes"]           = m_notes->text().trimmed();
    fields["frequency_hz"]    = QVariant::fromValue(m_frequency);
    fields["date_utc"]        = utcNow.toString("yyyyMMdd");
    fields["time_utc"]        = utcNow.toString("hhmmss");
    fields["mode"]            = "DIGITAL";
    fields["submode"]         = "HAVEN-FSK";
    fields["my_callsign"]     = myInfo.callsign;
    fields["my_grid"]         = myInfo.grid;
    fields["my_pota_refs"]    = myInfo.potaRefs;
    fields["my_sota_ref"]     = myInfo.sotaRef;
    fields["my_fd_class"]     = myInfo.fdClass;
    fields["my_fd_section"]   = myInfo.fdSection;
    fields["my_op_name"]      = myInfo.opName;

    addContactRow(fields);
    emit contactLogged(fields);
    onClear();
}

void LogPanel::onClear() {
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

void LogPanel::addContactRow(const QVariantMap& fields) {
    // Most recent at top
    m_contactTable->insertRow(0);

    auto* timeItem = new QTableWidgetItem(fields["time_utc"].toString());
    auto* callItem = new QTableWidgetItem(fields["their_callsign"].toString());
    auto* rsrItem  = new QTableWidgetItem(fields["rs_received"].toString());
    auto* rssItem  = new QTableWidgetItem(fields["rs_sent"].toString());

    QString activity;
    QStringList parks = fields["their_pota_refs"].toStringList();
    if (!parks.isEmpty()) activity = parks.join(" ");
    QString sota = fields["their_sota_ref"].toString();
    if (!sota.isEmpty())
        activity += (activity.isEmpty() ? "" : " ") + sota;
    QString fd = fields["their_fd"].toString();
    if (!fd.isEmpty()) activity = fd;

    auto* actItem  = new QTableWidgetItem(activity);
    auto* gridItem = new QTableWidgetItem(fields["their_grid"].toString());
    auto* noteItem = new QTableWidgetItem(fields["notes"].toString());

    m_contactTable->setItem(0, 0, timeItem);
    m_contactTable->setItem(0, 1, callItem);
    m_contactTable->setItem(0, 2, rsrItem);
    m_contactTable->setItem(0, 3, rssItem);
    m_contactTable->setItem(0, 4, actItem);
    m_contactTable->setItem(0, 5, gridItem);
    m_contactTable->setItem(0, 6, noteItem);

    // Store full fields on the time cell for re-population on click
    m_contactTable->item(0, 0)->setData(Qt::UserRole, fields);

    // Keep scroll history bounded
    while (m_contactTable->rowCount() > MAX_VISIBLE_ROWS * 3)
        m_contactTable->removeRow(m_contactTable->rowCount() - 1);
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

QString LogPanel::formatFrequency(uint64_t hz) const {
    return QString("%1 MHz")
        .arg(static_cast<double>(hz) / 1e6, 0, 'f', 6);
}
