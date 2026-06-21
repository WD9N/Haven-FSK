#include "TCIClient.h"

TCIClient::TCIClient(const QString& url, QObject *parent)
    : RadioInterface(parent), m_url(url)
{}

TCIClient::~TCIClient() = default;

bool TCIClient::connect() { return false; }
void TCIClient::disconnect() {}
bool TCIClient::setPTT(bool) { return false; }
bool TCIClient::isConnected() const { return m_connected; }
