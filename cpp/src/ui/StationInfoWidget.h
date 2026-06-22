#pragma once
#include <QFrame>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFont>
#include "../radio/RadioSettings.h"

class StationInfoWidget : public QFrame
{
    Q_OBJECT
public:
    explicit StationInfoWidget(QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
        setLineWidth(1);
        setMaximumHeight(80);

        auto* outer = new QHBoxLayout(this);
        outer->setContentsMargins(8, 4, 8, 4);
        outer->setSpacing(16);

        // Callsign + grid
        auto* leftCol = new QVBoxLayout;
        m_callLabel = new QLabel("--");
        m_callLabel->setFont(QFont("Courier New", 14, QFont::Bold));
        m_gridLabel = new QLabel("----");
        m_gridLabel->setStyleSheet("color: gray; font-size: 9pt;");
        leftCol->addWidget(m_callLabel);
        leftCol->addWidget(m_gridLabel);
        outer->addLayout(leftCol);

        // Activity references
        auto* midCol = new QVBoxLayout;
        m_activityLabel = new QLabel;
        m_activityLabel->setFont(QFont("Courier New", 9));
        m_activityLabel->setWordWrap(true);
        midCol->addWidget(m_activityLabel);
        midCol->addStretch();
        outer->addLayout(midCol, 1);

        // Name + activator badge
        auto* rightCol = new QVBoxLayout;
        m_nameLabel = new QLabel;
        m_nameLabel->setStyleSheet("color: gray; font-size: 9pt;");
        m_modeBadge = new QLabel;
        m_modeBadge->setStyleSheet(
            "background: #2a6496; color: white; "
            "padding: 2px 8px; border-radius: 3px; font-size: 9pt;");
        m_modeBadge->setVisible(false);
        rightCol->addWidget(m_nameLabel);
        rightCol->addWidget(m_modeBadge);
        rightCol->addStretch();
        outer->addLayout(rightCol);
    }

public slots:
    void refresh() {
        HavenFSK::StationInfo info = HavenFSK::loadStationInfo();

        // Callsign
        if (info.callsign.isEmpty()) {
            m_callLabel->setText("NO CALLSIGN");
            m_callLabel->setStyleSheet(
                "color: red; font-weight: bold; "
                "font-family: 'Courier New'; font-size: 14pt;");
        } else {
            m_callLabel->setText(info.callsign);
            m_callLabel->setStyleSheet(
                "font-family: 'Courier New'; font-size: 14pt; "
                "font-weight: bold;");
        }

        // Grid + state/county context
        QString gridLine = info.grid.isEmpty() ? "No grid set" : info.grid;
        if (!info.state.isEmpty()) {
            gridLine += "  " + info.state;
            if (!info.county.isEmpty())
                gridLine += " / " + info.county;
        }
        m_gridLabel->setText(gridLine);

        // Activity references
        QStringList activity;
        if (!info.potaRefs.isEmpty())
            activity << "POTA: " + info.potaRefs.join("  ");
        if (!info.sotaRef.isEmpty())
            activity << "SOTA: " + info.sotaRef;
        if (!info.fdClass.isEmpty()) {
            QString fd = "FD: " + info.fdClass;
            if (!info.fdSection.isEmpty())
                fd += " " + info.fdSection;
            activity << fd;
        }
        m_activityLabel->setText(activity.join("\n"));

        // Operator name
        m_nameLabel->setText(info.opName);

        // Activator badge
        if (info.isActivator()) {
            m_modeBadge->setText("ACTIVATOR");
            m_modeBadge->setVisible(true);
        } else {
            m_modeBadge->setVisible(false);
        }
    }

private:
    QLabel* m_callLabel     {nullptr};
    QLabel* m_gridLabel     {nullptr};
    QLabel* m_activityLabel {nullptr};
    QLabel* m_nameLabel     {nullptr};
    QLabel* m_modeBadge     {nullptr};
};
