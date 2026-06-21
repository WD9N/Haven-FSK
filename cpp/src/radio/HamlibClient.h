#pragma once
#include "RadioInterface.h"
#include <cstdint>

// HamlibClient — direct Hamlib linking stub.
// Deferred to a future phase per ADR-017.
// RigctldClient (TCP to rigctld) is the active Hamlib-based implementation.

class HamlibClient : public RadioInterface
{
    Q_OBJECT
public:
    explicit HamlibClient(const QString& host, int port,
                          QObject* parent = nullptr);
    ~HamlibClient() override;

    bool     connect()           override;
    void     disconnect()        override;
    bool     isConnected() const override { return false; }
    QString  rigName()     const override { return "Hamlib (stub)"; }

    bool     setPTT(bool active)          override;
    uint64_t getFrequency()               override { return 0; }
    bool     setFrequency(uint64_t hz)    override;
    bool     setMode(const QString& mode) override;
    QString  getMode()                    override { return {}; }
    bool     setSplit(bool enable, uint64_t txHz = 0) override;

private:
    QString m_host;
    int     m_port;
};
