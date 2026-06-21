#include "HamlibClient.h"

HamlibClient::HamlibClient(const QString& host, int port, QObject *parent)
    : RadioInterface(parent), m_host(host), m_port(port)
{}

HamlibClient::~HamlibClient() = default;

bool HamlibClient::connect() { return false; }
void HamlibClient::disconnect() {}
bool HamlibClient::setPTT(bool) { return false; }
bool HamlibClient::isConnected() const { return m_connected; }
