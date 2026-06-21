#pragma once
#include "RadioInterface.h"

class HamlibClient : public RadioInterface
{
    Q_OBJECT
public:
    explicit HamlibClient(const QString& host, int port, QObject *parent = nullptr);
    ~HamlibClient() override;

    bool connect() override;
    void disconnect() override;
    bool setPTT(bool active) override;
    bool isConnected() const override;

private:
    QString m_host;
    int     m_port;
    bool    m_connected{false};
};
