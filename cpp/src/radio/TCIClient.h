#pragma once
#include "RadioInterface.h"
#include <QWebSocket>

class TCIClient : public RadioInterface
{
    Q_OBJECT
public:
    explicit TCIClient(const QString& url, QObject *parent = nullptr);
    ~TCIClient() override;

    bool connect() override;
    void disconnect() override;
    bool setPTT(bool active) override;
    bool isConnected() const override;

private:
    QString      m_url;
    QWebSocket  *m_socket{nullptr};
    bool         m_connected{false};
};
