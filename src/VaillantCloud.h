#pragma once
#include "VaillantConfig.h"
#ifdef OPENKNX_VAILLANT

#include "VaillantAuth.h"
#include "VaillantHttp.h"
#include "VaillantState.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <string>

namespace Vaillant
{
    // Worker task between the KNX loop and the myVAILLANT cloud. Every HTTPS call, the
    // login and its proof of work run here; the module only enqueues commands and copies
    // finished snapshots, so nothing in the KNX loop ever blocks on the network.
    class Cloud
    {
      public:
        struct Settings
        {
            std::string user;
            std::string password;
            uint8_t country = 13; // germany
            uint8_t homeIndex = 0;
            uint16_t pollSeconds = 60;
            uint8_t extraMinutes = 15; // energy, faults, power, connection state
            float quickVetoHours = 3;
            uint8_t energyMask = 0; // bit (device * EMODE_COUNT + mode) per wanted figure
            // Only fetch what a used group object needs.
            bool wantOnline = true;
            bool wantFaults = true;
            bool wantPower = true;
        };

        enum CommandType : uint8_t
        {
            CMD_ZONE_MODE,
            CMD_ZONE_QUICK_VETO, // value <= 0 ends it
            CMD_ZONE_MANUAL_SETPOINT,
            CMD_ZONE_SET_BACK,
            CMD_CIRCUIT_HEATING_CURVE,
            CMD_DHW_SETPOINT,
            CMD_DHW_MODE,
            CMD_DHW_BOOST,
            CMD_AWAY,
            CMD_POLL,
            CMD_RELOGIN,
        };

        struct Command
        {
            CommandType type;
            uint8_t index; // zone/circuit index, or DHW list position
            float value;
        };

        void begin(const Settings &settings);
        bool started() const { return _task != nullptr; }

        bool enqueue(CommandType type, uint8_t index = 0, float value = 0);
        // Copies the shared state if it changed since lastSeq. Called from the KNX loop.
        bool snapshot(State &out, uint32_t lastSeq);

        void setRawLog(bool on) { _rawLog = on; }

      private:
        Settings _settings;
        Auth _auth;
        Token _token;
        std::string _systemId;

        TaskHandle_t _task = nullptr;
        QueueHandle_t _queue = nullptr;
        SemaphoreHandle_t _mutex = nullptr;
        State _shared; // guarded by _mutex
        State _work;   // worker task only

        uint32_t _backoffUntil = 0;
        uint8_t _failures = 0;
        uint32_t _nextPoll = 0;
        uint32_t _nextExtra = 0;
        uint32_t _nextDevices = 0;
        uint32_t _lastPersist = 0;
        bool _rawLog = false;

        // Device UUIDs and the (mode, type) pairs each device reports, from currentSystem.
        std::string _deviceUuid[DEV_COUNT];
        uint16_t _deviceData[DEV_COUNT] = {}; // bit (mode * ETYPE_COUNT + type)

        static void taskEntry(void *arg);
        void run();

        bool ensureToken();
        bool ensureSystem();
        void loadRefreshToken();
        void persistRefreshToken(bool force);

        HttpResponse api(const char *method, const std::string &path, const std::string &body = std::string(), size_t maxBody = 16 * 1024);
        std::string systemBase() const;

        void collectAndRunCommands(Command first);
        bool runCommand(const Command &cmd);
        bool pollSystem();
        void pollExtras();
        void pollDevices();
        void pollEnergy();

        void fail(const char *diag, const std::string &error);
        void succeed();
        void publish();
        void setDiag(const char *text);
    };
} // namespace Vaillant

#endif // OPENKNX_VAILLANT
