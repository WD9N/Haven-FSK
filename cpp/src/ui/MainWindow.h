#pragma once
#include <QMainWindow>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QProgressBar>
#include <memory>

class AudioEngine;

namespace HavenFSK {
    class DspPipeline;
    struct RxMessage;
    enum class RxState;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onTransmit();
    void onMessageReceived(const HavenFSK::RxMessage& msg);
    void onDcdChanged(bool active);
    void onRxStateChanged(HavenFSK::RxState state);
    void onTxComplete();
    void onAudioError(const QString& message);
    void onRxLevelChanged(float level);

private:
    void setupUi();
    void setupConnections();
    void startAudio();

    // ── UI widgets ────────────────────────────────────────────────────────
    QTextEdit*    m_rxText         {nullptr};  // received messages
    QLineEdit*    m_txInput        {nullptr};  // operator text input
    QPushButton*  m_txButton       {nullptr};  // transmit button
    QLabel*       m_dcdLabel       {nullptr};  // carrier detect indicator
    QLabel*       m_statusLabel    {nullptr};  // status bar text
    QLabel*       m_rxStateLabel   {nullptr};  // Idle / Searching / Receiving
    QProgressBar* m_rxLevel        {nullptr};  // audio input level meter
    QComboBox*    m_inputDevCombo  {nullptr};
    QComboBox*    m_outputDevCombo {nullptr};

    // ── Backend objects ───────────────────────────────────────────────────
    AudioEngine*            m_audio    {nullptr};
    HavenFSK::DspPipeline* m_pipeline {nullptr};
};
