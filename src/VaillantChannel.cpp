#include "VaillantConfig.h"
#ifdef OPENKNX_VAILLANT

#include "VaillantChannel.h"
#include "VaillantModule.h"

using namespace Vaillant;

VaillantChannel::VaillantChannel(uint8_t index, VaillantModule &module)
    : _module(module)
{
    _channelIndex = index;
}

const std::string VaillantChannel::name()
{
    return "VaillantChannel";
}

void VaillantChannel::setup()
{
    _type = ParamVAI_CHType;
    _zone = ParamVAI_CHZone;
    _dhw = ParamVAI_CHDhw;
    _energyDevice = ParamVAI_CHEnergyDevice;
    _energyMode = ParamVAI_CHEnergyMode;
    logDebugP("type=%u zone=%u dhw=%u energy=%u/%u", (unsigned)_type, (unsigned)_zone, (unsigned)_dhw,
              (unsigned)_energyDevice, (unsigned)_energyMode);
}

uint8_t VaillantChannel::energyMask() const
{
    if (_type != TYPE_ENERGY || _energyDevice >= DEV_COUNT) return 0;
    if (_energyMode == EMODE_SUM)
        return energyBit(_energyDevice, EMODE_HEATING) | energyBit(_energyDevice, EMODE_DHW) | energyBit(_energyDevice, EMODE_COOLING);
    return _energyMode < EMODE_COUNT ? energyBit(_energyDevice, _energyMode) : 0;
}

void VaillantChannel::apply(const State &state)
{
    switch (_type)
    {
        case TYPE_ZONE:
            applyZone(state);
            break;
        case TYPE_DHW:
            applyDhw(state);
            break;
        case TYPE_ENERGY:
            applyEnergy(state);
            break;
    }
}

void VaillantChannel::applyZone(const State &state)
{
    if (_zone >= kMaxZones || !state.zones[_zone].valid) return;
    const ZoneState &z = state.zones[_zone];

    sendFloat(KoVAI_CHRoomTemp, z.roomTemp, DPT_Value_Temp, _roomTemp);
    sendFloat(KoVAI_CHHumidity, z.humidity, DPT_Value_Humidity, _humidity);
    sendFloat(KoVAI_CHSetpoint, z.setpoint, DPT_Value_Temp, _setpoint);
    sendText(KoVAI_CHHeatingState, z.heatingState, _heatingState);
    if (z.opMode != MODE_UNKNOWN) sendInt(KoVAI_CHZoneModeStatus, z.opMode, DPT_Value_1_Ucount, _zoneMode);
    sendBool(KoVAI_CHQuickVetoActive, z.quickVeto, DPT_State, _quickVeto);
    sendFloat(KoVAI_CHManualSetpointStatus, z.manualSetpoint, DPT_Value_Temp, _manualSetpoint);
    sendFloat(KoVAI_CHSetBackStatus, z.setBack, DPT_Value_Temp, _setBack);

    _circuit = z.circuit;
    if (_circuit < 0 || _circuit >= kMaxCircuits || !state.circuits[_circuit].valid) return;
    const CircuitState &c = state.circuits[_circuit];
    sendFloat(KoVAI_CHFlowTemp, c.flowTemp, DPT_Value_Temp, _flowTemp);
    sendFloat(KoVAI_CHFlowSetpoint, c.flowSetpoint, DPT_Value_Temp, _flowSetpoint);
    sendText(KoVAI_CHCircuitState, c.state, _circuitState);
    sendFloat(KoVAI_CHHeatingCurveStatus, c.heatingCurve, DPT_Value_Acceleration, _heatingCurve, 0.005f);
}

void VaillantChannel::applyDhw(const State &state)
{
    if (_dhw >= kMaxDhw || !state.dhw[_dhw].valid) return;
    const DhwState &d = state.dhw[_dhw];

    sendFloat(KoVAI_CHDhwTemp, d.temp, DPT_Value_Temp, _dhwTemp);
    sendFloat(KoVAI_CHDhwSetpointStatus, d.setpoint, DPT_Value_Temp, _dhwSetpoint);
    if (d.opMode != MODE_UNKNOWN) sendInt(KoVAI_CHDhwModeStatus, d.opMode, DPT_Value_1_Ucount, _dhwMode);
    sendBool(KoVAI_CHDhwBoostStatus, d.boost, DPT_State, _dhwBoost);
}

// Sum over the modes this channel covers; NAN if none of them has data.
float VaillantChannel::energySum(const State &state, uint8_t type) const
{
    float sum = 0;
    bool any = false;
    for (uint8_t mode = 0; mode < EMODE_COUNT; mode++)
    {
        if (_energyMode != EMODE_SUM && _energyMode != mode) continue;
        const float v = state.energy[_energyDevice][mode][type];
        if (isnan(v)) continue;
        sum += v;
        any = true;
    }
    return any ? sum : NAN;
}

void VaillantChannel::applyEnergy(const State &state)
{
    if (_energyDevice >= DEV_COUNT) return;
    const float consumed = energySum(state, ETYPE_CONSUMED);
    const float environment = energySum(state, ETYPE_ENVIRONMENT);
    const float generated = energySum(state, ETYPE_GENERATED);

    if (!isnan(consumed)) sendInt(KoVAI_CHEnergyConsumed, lroundf(consumed), DPT_ActiveEnergy, _consumed);
    if (!isnan(environment)) sendInt(KoVAI_CHEnergyEnvironment, lroundf(environment), DPT_ActiveEnergy, _environment);
    if (!isnan(generated)) sendInt(KoVAI_CHEnergyGenerated, lroundf(generated), DPT_ActiveEnergy, _generated);
    // Daily coefficient of performance; meaningless before the first consumed Wh.
    if (!isnan(consumed) && !isnan(generated) && consumed > 0)
        sendFloat(KoVAI_CHCop, generated / consumed, DPT_Value_Acceleration, _cop);
}

void VaillantChannel::processInputKo(GroupObject &ko)
{
    if (_type == TYPE_NONE) return;
    const int index = VAI_KoCalcIndex(ko.asap());
    if (index < 0) return;

    Cloud &cloud = _module.cloud();
    switch (index)
    {
        case VAI_KoCHZoneMode:
        {
            const uint8_t mode = (uint8_t)ko.value(DPT_Value_1_Ucount);
            if (_type == TYPE_ZONE && mode <= MODE_TIME_CONTROLLED) cloud.enqueue(Cloud::CMD_ZONE_MODE, _zone, mode);
            break;
        }
        case VAI_KoCHQuickVeto:
            if (_type == TYPE_ZONE) cloud.enqueue(Cloud::CMD_ZONE_QUICK_VETO, _zone, (float)ko.value(DPT_Value_Temp));
            break;
        case VAI_KoCHManualSetpoint:
            if (_type == TYPE_ZONE) cloud.enqueue(Cloud::CMD_ZONE_MANUAL_SETPOINT, _zone, (float)ko.value(DPT_Value_Temp));
            break;
        case VAI_KoCHSetBack:
            if (_type == TYPE_ZONE) cloud.enqueue(Cloud::CMD_ZONE_SET_BACK, _zone, (float)ko.value(DPT_Value_Temp));
            break;
        case VAI_KoCHHeatingCurve:
            // The circuit comes from the zone's properties; until the first poll the zone
            // index is the best guess, it matches on single-circuit systems.
            if (_type == TYPE_ZONE)
                cloud.enqueue(Cloud::CMD_CIRCUIT_HEATING_CURVE, _circuit >= 0 ? (uint8_t)_circuit : _zone, (float)ko.value(DPT_Value_Acceleration));
            break;
        case VAI_KoCHDhwSetpoint:
            if (_type == TYPE_DHW) cloud.enqueue(Cloud::CMD_DHW_SETPOINT, _dhw, (float)ko.value(DPT_Value_Temp));
            break;
        case VAI_KoCHDhwMode:
        {
            const uint8_t mode = (uint8_t)ko.value(DPT_Value_1_Ucount);
            if (_type == TYPE_DHW && mode <= MODE_TIME_CONTROLLED) cloud.enqueue(Cloud::CMD_DHW_MODE, _dhw, mode);
            break;
        }
        case VAI_KoCHDhwBoost:
            if (_type == TYPE_DHW) cloud.enqueue(Cloud::CMD_DHW_BOOST, _dhw, ko.value(DPT_Switch) ? 1 : 0);
            break;
    }
}

#endif // OPENKNX_VAILLANT
