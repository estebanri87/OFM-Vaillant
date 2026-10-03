#pragma once
#include "VaillantConfig.h"
#ifdef OPENKNX_VAILLANT

#include "VAIChannelOwnerModule.h"
#include "VaillantCloud.h"
#include "VaillantKo.h"
#include "VaillantState.h"

// Vaillant heat pump / boiler on the KNX bus via the myVAILLANT cloud (sensoCOMFORT
// VRC720 + VR921, "tli" API). System values live on module objects, zones, hot water
// and energy figures on channels.
class VaillantModule : public VAIChannelOwnerModule
{
  public:
    VaillantModule();

    const std::string name() override;
    const std::string version() override;

    void setup(bool configured) override;
    void loop(bool configured) override;
    void processInputKo(GroupObject &ko) override;

    OpenKNX::Channel *createChannel(uint8_t _channelIndex) override;

    void showHelp() override;
    bool processCommand(const std::string command, bool debugKo) override;

    Vaillant::Cloud &cloud() { return _cloud; }

  private:
    Vaillant::Cloud _cloud;
    Vaillant::State _state;
    uint32_t _seq = 0;

    Vaillant::LastInt _lastConnected, _lastOnline, _lastFault, _lastMaintenance, _lastAway;
    Vaillant::LastFloat _lastOutdoor, _lastOutdoorAvg, _lastPressure, _lastPower;
    Vaillant::LastText _lastDiag, _lastEnergyManager, _lastFaultText;

    uint8_t energyMask();
    void applyState();
    void printStatus();
};

extern VaillantModule openknxVaillantModule;

#endif // OPENKNX_VAILLANT
