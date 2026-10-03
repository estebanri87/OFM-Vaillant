#pragma once
#include "VaillantConfig.h"
#ifdef OPENKNX_VAILLANT

#include "OpenKNX.h"
#include "VaillantKo.h"
#include "VaillantState.h"

class VaillantModule;

// One channel = one zone (with its heating circuit), one hot water cylinder or one
// energy figure set. The type is chosen in the channel selection.
class VaillantChannel : public OpenKNX::Channel
{
  public:
    // Values of ParamVAI_CHType
    enum ChannelType : uint8_t
    {
        TYPE_NONE = 0,
        TYPE_ZONE = 1,
        TYPE_DHW = 2,
        TYPE_ENERGY = 3,
    };

    VaillantChannel(uint8_t index, VaillantModule &module);

    const std::string name() override;
    void setup() override;
    void processInputKo(GroupObject &ko) override;

    // New cloud snapshot, in KNX loop context.
    void apply(const Vaillant::State &state);
    // Energy figures this channel needs from the cloud (Vaillant::energyBit()).
    uint8_t energyMask() const;

  private:
    VaillantModule &_module;
    uint8_t _type = TYPE_NONE;
    uint8_t _zone = 0;
    uint8_t _dhw = 0;
    uint8_t _energyDevice = 0;
    uint8_t _energyMode = 0;
    int8_t _circuit = -1; // heating circuit of the zone, from the last snapshot

    Vaillant::LastFloat _roomTemp, _humidity, _setpoint, _manualSetpoint, _setBack, _flowTemp, _flowSetpoint, _heatingCurve;
    Vaillant::LastFloat _dhwTemp, _dhwSetpoint, _cop;
    Vaillant::LastInt _zoneMode, _quickVeto, _dhwMode, _dhwBoost, _consumed, _environment, _generated;
    Vaillant::LastText _heatingState, _circuitState;

    void applyZone(const Vaillant::State &state);
    void applyDhw(const Vaillant::State &state);
    void applyEnergy(const Vaillant::State &state);
    float energySum(const Vaillant::State &state, uint8_t type) const;
};

#endif // OPENKNX_VAILLANT
