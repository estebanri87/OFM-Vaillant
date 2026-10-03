#pragma once
#include "VaillantConfig.h"
#ifdef OPENKNX_VAILLANT

#include <math.h>
#include <stdint.h>

namespace Vaillant
{
    constexpr uint8_t kMaxZones = 9;    // zone index 0..8 as used by the API
    constexpr uint8_t kMaxCircuits = 9; // circuit index 0..8
    constexpr uint8_t kMaxDhw = 2;      // list position, the API index is often 255

    // Values of the KNX objects "Betriebsart" (DPT 5.010) for zones and hot water.
    enum OpMode : uint8_t
    {
        MODE_OFF = 0,
        MODE_MANUAL = 1,
        MODE_TIME_CONTROLLED = 2,
        MODE_UNKNOWN = 255,
    };

    // Energy channel parameters (ParamVAI_CHEnergyDevice / ParamVAI_CHEnergyMode).
    enum EnergyDevice : uint8_t
    {
        DEV_HEAT_GENERATOR = 0, // primary_heat_generator, the heat pump
        DEV_BACKUP_HEATER = 1,  // electric_backup_heater
        DEV_COUNT = 2,
    };
    enum EnergyMode : uint8_t
    {
        EMODE_HEATING = 0,
        EMODE_DHW = 1,
        EMODE_COOLING = 2,
        EMODE_COUNT = 3,
        EMODE_SUM = 3, // channel parameter only: all of the above
    };
    enum EnergyType : uint8_t
    {
        ETYPE_CONSUMED = 0,    // CONSUMED_ELECTRICAL_ENERGY
        ETYPE_ENVIRONMENT = 1, // EARNED_ENVIRONMENT_ENERGY
        ETYPE_GENERATED = 2,   // HEAT_GENERATED
        ETYPE_COUNT = 3,
    };

    // Bit in Cloud::Settings::energyMask for one device/mode pair.
    inline uint8_t energyBit(uint8_t device, uint8_t mode)
    {
        return (uint8_t)(1u << (device * EMODE_COUNT + mode));
    }

    struct ZoneState
    {
        bool valid = false;
        float roomTemp = NAN;
        float humidity = NAN;
        float setpoint = NAN; // currently effective
        char heatingState[24] = {};
        bool quickVeto = false;
        uint8_t opMode = MODE_UNKNOWN;
        float manualSetpoint = NAN;
        float setBack = NAN;
        int8_t circuit = -1;
    };

    struct CircuitState
    {
        bool valid = false;
        float flowTemp = NAN;
        float flowSetpoint = NAN;
        float heatingCurve = NAN;
        char state[24] = {};
    };

    struct DhwState
    {
        bool valid = false;
        uint8_t index = 255; // API index, needed in the URL
        float temp = NAN;
        float setpoint = NAN;
        float minSetpoint = 35;
        float maxSetpoint = 70;
        uint8_t opMode = MODE_UNKNOWN;
        bool boost = false;
    };

    struct State
    {
        uint32_t seq = 0; // increments on every change the module should look at

        bool cloudOk = false; // logged in and the last request went through
        char diag[15] = {};   // short ASCII status for DPT 16.001

        // Console diagnostics only
        char lastError[64] = {};
        char systemId[48] = {};
        uint32_t requests = 0;
        uint32_t tokenExpiresAtMs = 0;
        uint32_t lastPollMs = 0;
        uint32_t altchaMs = 0;

        bool systemValid = false;
        bool online = false; // gateway connected to the cloud
        float outdoor = NAN;
        float outdoorAvg = NAN;
        float pressure = NAN; // bar
        char energyManagerState[24] = {};
        bool away = false;

        bool dtcValid = false;
        bool fault = false;
        bool maintenance = false;
        char faultText[15] = {};

        float power = NAN; // W, sum over all devices (MPC)

        ZoneState zones[kMaxZones];
        CircuitState circuits[kMaxCircuits];
        DhwState dhw[kMaxDhw];

        // Today's energy in Wh. NAN = not available for this device/mode/type.
        float energy[DEV_COUNT][EMODE_COUNT][ETYPE_COUNT];

        State()
        {
            for (auto &d : energy)
                for (auto &m : d)
                    for (auto &t : m) t = NAN;
        }
    };
} // namespace Vaillant

#endif // OPENKNX_VAILLANT
