#include "ModemFactory.h"
#include "MfskModem.h"
#include "psk/Psk31Modem.h"

namespace HavenFSK {

std::unique_ptr<IModem> createModem(ModemMode mode, const ModemConfig& cfg) {
    switch (mode) {
        case ModemMode::Mfsk16:
            return std::make_unique<MfskModem>();
        case ModemMode::Psk31:
            return std::make_unique<Psk31Modem>(cfg.pskBaudRate, cfg.pskVariant);
    }
    return std::make_unique<MfskModem>();
}

} // namespace HavenFSK
