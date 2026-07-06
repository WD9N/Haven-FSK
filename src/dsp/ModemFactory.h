#pragma once
#include "IModem.h"

// Declaration only — createModem() itself is declared in IModem.h and
// defined in ModemFactory.cpp. This header exists so callers that only
// need to trigger modem construction don't have to know about every
// concrete modem class's header.
