#include "VaillantConfig.h"
#ifdef OPENKNX_VAILLANT

#include "VaillantCloud.h"
#include "OpenKNX.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

namespace Vaillant
{
    namespace
    {
        const char *const kApiBase = "https://api.vaillant-group.com/service-connected-control/end-user-app-api/v1";
        const char *const kModeNames[] = {"OFF", "MANUAL", "TIME_CONTROLLED"};
        const char *const kEnergyModeNames[EMODE_COUNT] = {"HEATING", "DOMESTIC_HOT_WATER", "COOLING"};
        const char *const kEnergyTypeNames[ETYPE_COUNT] = {"CONSUMED_ELECTRICAL_ENERGY", "EARNED_ENVIRONMENT_ENERGY", "HEAT_GENERATED"};
        const char *const kDeviceKeys[DEV_COUNT][2] = {{"primary_heat_generator", "primaryHeatGenerator"},
                                                       {"electric_backup_heater", "electricBackupHeater"}};

        // Growing pauses after failures. A wrong password must not hammer the login,
        // Keycloak locks accounts after repeated failures.
        const uint32_t kBackoffMs[] = {60000, 300000, 900000, 3600000};
        const uint32_t kPersistIntervalMs = 3600000;    // refresh token to NVS at most hourly
        const uint32_t kDeviceRefreshMs = 6 * 3600000; // device list for the energy figures
        const uint32_t kCommandCollectMs = 1500;       // gather follow-up commands (dimming a setpoint)
        const uint32_t kPollAfterCommandMs = 5000;
        const uint32_t kTaskStack = 12288;
        const char *const kNvsNamespace = "vaillant";

        float num(JsonVariantConst v)
        {
            return v.is<float>() ? v.as<float>() : NAN;
        }

        void copyText(char *dst, size_t size, const char *src)
        {
            strncpy(dst, src ? src : "", size - 1);
            dst[size - 1] = 0;
        }

        uint8_t parseMode(const char *s)
        {
            if (!s) return MODE_UNKNOWN;
            for (uint8_t i = 0; i < 3; i++)
                if (strcmp(s, kModeNames[i]) == 0) return i;
            return MODE_UNKNOWN;
        }

        // currentSystem mixes snake_case and camelCase between API versions.
        JsonVariantConst field(JsonObjectConst o, const char *snake, const char *camel)
        {
            JsonVariantConst v = o[snake];
            return v.isNull() ? o[camel] : v;
        }

        bool timeValid()
        {
            return time(nullptr) > 1700000000;
        }

        std::string isoUtc(time_t t)
        {
            struct tm tmv;
            gmtime_r(&t, &tmv);
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000Z", &tmv);
            return buf;
        }

        bool due(uint32_t at)
        {
            return (int32_t)(millis() - at) >= 0;
        }
    } // namespace

    void Cloud::begin(const Settings &settings)
    {
        if (_task) return;
        _settings = settings;
        if (_settings.pollSeconds < 30) _settings.pollSeconds = 30;
        if (_settings.extraMinutes < 5) _settings.extraMinutes = 5;
        _auth.configure(_settings.user, _settings.password, _settings.country);

        _queue = xQueueCreate(16, sizeof(Command));
        _mutex = xSemaphoreCreateMutex();
        setDiag("Start");
        publish();
        // Core 0 keeps the KNX loop on core 1 free; the proof of work yields every try.
        xTaskCreatePinnedToCore(taskEntry, "Vaillant", kTaskStack, this, 1, &_task, 0);
    }

    bool Cloud::enqueue(CommandType type, uint8_t index, float value)
    {
        if (!_queue) return false;
        const Command cmd = {type, index, value};
        return xQueueSend(_queue, &cmd, 0) == pdTRUE;
    }

    bool Cloud::snapshot(State &out, uint32_t lastSeq)
    {
        if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return false;
        const bool changed = _shared.seq != lastSeq;
        if (changed) out = _shared;
        xSemaphoreGive(_mutex);
        return changed;
    }

    void Cloud::publish()
    {
        _work.seq++;
        _work.tokenExpiresAtMs = _token.expiresAtMs;
        _work.altchaMs = _auth.lastAltchaMs();
        copyText(_work.systemId, sizeof(_work.systemId), _systemId.c_str());
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _shared = _work;
        xSemaphoreGive(_mutex);
    }

    void Cloud::setDiag(const char *text)
    {
        copyText(_work.diag, sizeof(_work.diag), text);
    }

    void Cloud::fail(const char *diag, const std::string &error)
    {
        _work.cloudOk = false;
        setDiag(diag);
        copyText(_work.lastError, sizeof(_work.lastError), error.c_str());
        const uint8_t step = _failures < 3 ? _failures : 3;
        _backoffUntil = millis() + kBackoffMs[step];
        if (_failures < 255) _failures++;
        logError("Vaillant", "%s: %s - retry in %u s", diag, error.c_str(), (unsigned)(kBackoffMs[step] / 1000));
        publish();
    }

    void Cloud::succeed()
    {
        _failures = 0;
        _work.cloudOk = true;
        setDiag("verbunden");
    }

    void Cloud::taskEntry(void *arg)
    {
        static_cast<Cloud *>(arg)->run();
        vTaskDelete(nullptr);
    }

    void Cloud::run()
    {
        loadRefreshToken();
        _nextPoll = millis();
        _nextExtra = millis() + 10000;

        while (true)
        {
            Command cmd;
            const bool got = xQueueReceive(_queue, &cmd, pdMS_TO_TICKS(1000)) == pdTRUE;

            if (got && cmd.type == CMD_RELOGIN)
            {
                _token = Token();
                _systemId.clear();
                Preferences prefs;
                if (prefs.begin(kNvsNamespace, false))
                {
                    prefs.remove("rt");
                    prefs.end();
                }
                _backoffUntil = millis();
                _failures = 0;
                _nextPoll = millis();
                continue;
            }
            if (got && cmd.type == CMD_POLL)
            {
                _backoffUntil = millis();
                _nextPoll = millis();
                _nextExtra = millis();
            }

            if (!due(_backoffUntil))
            {
                if (got && cmd.type != CMD_POLL) logWarning("Vaillant", "command %u dropped, cloud not reachable", (unsigned)cmd.type);
                continue;
            }
            if (!ensureToken() || !ensureSystem()) continue;

            if (got && cmd.type != CMD_POLL)
            {
                collectAndRunCommands(cmd);
                _nextPoll = millis() + kPollAfterCommandMs;
            }
            if (due(_nextPoll))
            {
                pollSystem();
                _nextPoll = millis() + _settings.pollSeconds * 1000UL;
            }
            if (due(_nextExtra) && due(_backoffUntil))
            {
                pollExtras();
                _nextExtra = millis() + _settings.extraMinutes * 60000UL;
            }
        }
    }

    void Cloud::loadRefreshToken()
    {
        Preferences prefs;
        if (!prefs.begin(kNvsNamespace, true)) return;
        // Only reuse it for the same account; a changed e-mail in the ETS means a new login.
        if (std::string(prefs.getString("user", "").c_str()) == _settings.user)
            _token.refresh = prefs.getString("rt", "").c_str();
        prefs.end();
        if (!_token.refresh.empty()) logInfo("Vaillant", "stored refresh token found, skipping the full login");
    }

    void Cloud::persistRefreshToken(bool force)
    {
        if (_token.refresh.empty()) return;
        // Keycloak keeps older refresh tokens valid within the session, so writing every
        // rotation would only wear the flash.
        if (!force && _lastPersist && millis() - _lastPersist < kPersistIntervalMs) return;
        Preferences prefs;
        if (!prefs.begin(kNvsNamespace, false)) return;
        prefs.putString("user", _settings.user.c_str());
        prefs.putString("rt", _token.refresh.c_str());
        prefs.end();
        _lastPersist = millis();
    }

    bool Cloud::ensureToken()
    {
        if (_token.valid() && !due(_token.expiresAtMs)) return true;

        std::string error;
        if (!_token.refresh.empty())
        {
            bool rejected = false;
            if (_auth.refresh(_token, error, rejected))
            {
                persistRefreshToken(false);
                return true;
            }
            if (!rejected)
            {
                fail("Netzwerkfehler", error);
                return false;
            }
            logWarning("Vaillant", "refresh token rejected (%s), logging in again", error.c_str());
            _token = Token();
        }

        setDiag("Anmeldung...");
        publish();
        logInfo("Vaillant", "logging in to myVAILLANT, the proof of work takes up to a minute");
        if (_auth.login(_token, error))
        {
            persistRefreshToken(true);
            logInfo("Vaillant", "login successful");
            return true;
        }
        fail("Login Fehler", error);
        return false;
    }

    bool Cloud::ensureSystem()
    {
        if (!_systemId.empty()) return true;

        HttpResponse res = api("GET", "/homes");
        if (!res.ok())
        {
            fail("Anlage Fehler", "homes: HTTP " + std::to_string(res.status) + " " + res.error);
            return false;
        }
        JsonDocument doc;
        if (deserializeJson(doc, res.body) || doc.as<JsonArrayConst>().size() == 0)
        {
            fail("keine Anlage", "no system registered in this account");
            return false;
        }
        JsonArrayConst homes = doc.as<JsonArrayConst>();
        const size_t index = _settings.homeIndex < homes.size() ? _settings.homeIndex : 0;
        JsonObjectConst home = homes[index];
        _systemId = home["systemId"] | "";
        _work.online = strcmp(home["onlineState"] | "", "ONLINE") == 0;
        if (_systemId.empty())
        {
            fail("keine Anlage", "home without systemId");
            return false;
        }

        // Only the sensoCOMFORT "tli" API is implemented; VRC700 uses other paths.
        HttpResponse ci = api("GET", "/systems/" + _systemId + "/meta-info/control-identifier");
        if (ci.ok())
        {
            JsonDocument d;
            if (!deserializeJson(d, ci.body))
            {
                const char *id = d["controlIdentifier"] | "tli";
                if (strcasecmp(id, "tli") != 0)
                {
                    fail("Regler n.unterst", std::string("control identifier ") + id + " not supported");
                    _systemId.clear();
                    return false;
                }
            }
        }

        logInfo("Vaillant", "system %s (home %u of %u)", _systemId.c_str(), (unsigned)index + 1, (unsigned)homes.size());
        _nextDevices = millis();
        return true;
    }

    std::string Cloud::systemBase() const
    {
        return "/systems/" + _systemId + "/tli";
    }

    HttpResponse Cloud::api(const char *method, const std::string &path, const std::string &body, size_t maxBody)
    {
        const std::string url = std::string(kApiBase) + path;
        HttpResponse res;
        for (int attempt = 0; attempt < 2; attempt++)
        {
            std::vector<std::string> headers = {
                "Authorization: Bearer " + _token.access,
                "x-app-identifier: VAILLANT",
                "Accept-Language: en-GB",
                "Accept: application/json, text/plain, */*",
                "x-client-locale: en-GB",
                "x-idm-identifier: KEYCLOAK",
                "ocp-apim-subscription-key: 1e0a2f3511fb4c5bbb1c7f9fedd20b1c",
                "User-Agent: okhttp/4.9.2",
            };
            if (!body.empty()) headers.push_back("Content-Type: application/json");

            _work.requests++;
            res = Https::request(method, url, headers, body, maxBody);
            if (_rawLog)
                logInfo("Vaillant", "%s %s -> %d, %u B %s", method, path.c_str(), res.status, (unsigned)res.body.size(), res.error.c_str());

            // Expired early or revoked: renew once and repeat.
            if (res.status == 401 && attempt == 0)
            {
                _token.access.clear();
                if (!ensureToken()) return res;
                continue;
            }
            break;
        }
        return res;
    }

    void Cloud::collectAndRunCommands(Command first)
    {
        // Coalesce: per (type, index) only the last value counts, so dimming a setpoint
        // on a push-button does not become a burst of cloud calls.
        Command pending[16];
        uint8_t count = 0;
        auto add = [&](const Command &c) {
            if (c.type == CMD_POLL || c.type == CMD_RELOGIN) return;
            for (uint8_t i = 0; i < count; i++)
                if (pending[i].type == c.type && pending[i].index == c.index)
                {
                    pending[i] = c;
                    return;
                }
            if (count < 16) pending[count++] = c;
        };
        add(first);

        const uint32_t until = millis() + kCommandCollectMs;
        Command next;
        while (!due(until) && xQueueReceive(_queue, &next, pdMS_TO_TICKS(100)) == pdTRUE)
            add(next);

        bool allOk = true;
        for (uint8_t i = 0; i < count; i++)
            allOk = runCommand(pending[i]) && allOk;
        if (!allOk)
        {
            setDiag("Befehl Fehler");
            publish();
        }
    }

    bool Cloud::runCommand(const Command &cmd)
    {
        char json[160] = "";
        std::string path;
        const char *method = "PATCH";
        const std::string idx = std::to_string(cmd.index);

        switch (cmd.type)
        {
            case CMD_ZONE_MODE:
            {
                const uint8_t mode = (uint8_t)cmd.value;
                if (mode > MODE_TIME_CONTROLLED) return false;
                path = systemBase() + "/zones/" + idx + "/heating-operation-mode";
                snprintf(json, sizeof(json), "{\"operationMode\":\"%s\"}", kModeNames[mode]);
                break;
            }
            case CMD_ZONE_QUICK_VETO:
                if (cmd.index >= kMaxZones) return false;
                path = systemBase() + "/zones/" + idx + "/quick-veto";
                if (cmd.value <= 0)
                    method = "DELETE";
                else if (_work.zones[cmd.index].quickVeto)
                    snprintf(json, sizeof(json), "{\"desiredRoomTemperatureSetpoint\":%.1f}", cmd.value);
                else
                {
                    method = "POST";
                    snprintf(json, sizeof(json), "{\"desiredRoomTemperatureSetpoint\":%.1f,\"duration\":%.1f}", cmd.value, _settings.quickVetoHours);
                }
                break;
            case CMD_ZONE_MANUAL_SETPOINT:
                path = systemBase() + "/zones/" + idx + "/manual-mode-setpoint";
                snprintf(json, sizeof(json), "{\"setpoint\":%.1f,\"type\":\"HEATING\"}", cmd.value);
                break;
            case CMD_ZONE_SET_BACK:
                path = systemBase() + "/zones/" + idx + "/set-back-temperature";
                snprintf(json, sizeof(json), "{\"setBackTemperature\":%.1f}", cmd.value);
                break;
            case CMD_CIRCUIT_HEATING_CURVE:
                path = systemBase() + "/circuit/" + idx + "/heating-curve";
                snprintf(json, sizeof(json), "{\"heatingCurve\":%.2f}", cmd.value);
                break;
            case CMD_DHW_SETPOINT:
            case CMD_DHW_MODE:
            case CMD_DHW_BOOST:
            {
                if (cmd.index >= kMaxDhw || !_work.dhw[cmd.index].valid) return false;
                const DhwState &dhw = _work.dhw[cmd.index];
                const std::string base = systemBase() + "/domestic-hot-water/" + std::to_string(dhw.index);
                if (cmd.type == CMD_DHW_SETPOINT)
                {
                    // The tli controllers only take whole degrees.
                    float sp = roundf(cmd.value);
                    if (sp < dhw.minSetpoint) sp = dhw.minSetpoint;
                    if (sp > dhw.maxSetpoint) sp = dhw.maxSetpoint;
                    path = base + "/temperature";
                    snprintf(json, sizeof(json), "{\"setpoint\":%d}", (int)sp);
                }
                else if (cmd.type == CMD_DHW_MODE)
                {
                    const uint8_t mode = (uint8_t)cmd.value;
                    if (mode > MODE_TIME_CONTROLLED) return false;
                    path = base + "/operation-mode";
                    snprintf(json, sizeof(json), "{\"operationMode\":\"%s\"}", kModeNames[mode]);
                }
                else
                {
                    path = base + "/boost";
                    method = cmd.value > 0 ? "POST" : "DELETE";
                    if (cmd.value > 0) strcpy(json, "{}");
                }
                break;
            }
            case CMD_AWAY:
                path = systemBase() + "/away-mode";
                if (cmd.value > 0)
                {
                    if (!timeValid())
                    {
                        logError("Vaillant", "away mode needs the current time, NTP not synchronised yet");
                        return false;
                    }
                    // Open-ended like the app's "until further notice": one year.
                    const time_t now = time(nullptr);
                    method = "POST";
                    snprintf(json, sizeof(json), "{\"startDateTime\":\"%s\",\"endDateTime\":\"%s\"}",
                             isoUtc(now).c_str(), isoUtc(now + 365L * 86400L).c_str());
                }
                else
                    method = "DELETE";
                break;
            default:
                return false;
        }

        HttpResponse res = api(method, path, json);
        if (res.ok())
        {
            logInfo("Vaillant", "%s %s %s: OK", method, path.c_str(), json);
            return true;
        }
        logError("Vaillant", "%s %s %s: HTTP %d %s", method, path.c_str(), json, res.status, res.body.substr(0, 120).c_str());
        copyText(_work.lastError, sizeof(_work.lastError), (path + ": HTTP " + std::to_string(res.status)).c_str());
        return false;
    }

    bool Cloud::pollSystem()
    {
        HttpResponse res = api("GET", systemBase(), std::string(), 256 * 1024);
        if (!res.ok())
        {
            fail("Abfrage Fehler", "system: HTTP " + std::to_string(res.status) + " " + res.error);
            return false;
        }

        // Only what is mapped to KNX; the time programs make up most of the reply.
        JsonDocument filter;
        filter["state"]["system"] = true;
        filter["state"]["zones"][0] = true;
        filter["state"]["circuits"][0] = true;
        filter["state"]["dhw"][0] = true;
        filter["properties"]["zones"][0]["index"] = true;
        filter["properties"]["zones"][0]["associatedCircuitIndex"] = true;
        filter["properties"]["dhw"][0]["index"] = true;
        filter["properties"]["dhw"][0]["minSetpoint"] = true;
        filter["properties"]["dhw"][0]["maxSetpoint"] = true;
        filter["configuration"]["zones"][0]["index"] = true;
        filter["configuration"]["zones"][0]["heating"]["operationModeHeating"] = true;
        filter["configuration"]["zones"][0]["heating"]["setBackTemperature"] = true;
        filter["configuration"]["zones"][0]["heating"]["manualModeSetpointHeating"] = true;
        filter["configuration"]["circuits"][0]["index"] = true;
        filter["configuration"]["circuits"][0]["heatingCurve"] = true;
        filter["configuration"]["dhw"][0]["index"] = true;
        filter["configuration"]["dhw"][0]["operationModeDhw"] = true;
        filter["configuration"]["dhw"][0]["tappingSetpoint"] = true;

        JsonDocument doc;
        const DeserializationError err = deserializeJson(doc, res.body, DeserializationOption::Filter(filter));
        res.body.clear();
        res.body.shrink_to_fit();
        if (err)
        {
            fail("Daten Fehler", std::string("system JSON: ") + err.c_str());
            return false;
        }

        JsonObjectConst sys = doc["state"]["system"];
        _work.outdoor = num(sys["outdoorTemperature"]);
        _work.outdoorAvg = num(sys["outdoorTemperatureAverage24h"]);
        _work.pressure = num(sys["systemWaterPressure"]);
        copyText(_work.energyManagerState, sizeof(_work.energyManagerState), sys["energyManagerState"] | "");

        bool away = false;
        for (auto &z : _work.zones) z = ZoneState();
        for (JsonObjectConst z : doc["state"]["zones"].as<JsonArrayConst>())
        {
            const int i = z["index"] | -1;
            if (i < 0 || i >= kMaxZones) continue;
            ZoneState &zs = _work.zones[i];
            zs.valid = true;
            zs.roomTemp = num(z["currentRoomTemperature"]);
            zs.humidity = num(z["currentRoomHumidity"]);
            zs.setpoint = num(z["desiredRoomTemperatureSetpoint"]);
            copyText(zs.heatingState, sizeof(zs.heatingState), z["heatingState"] | "");
            const char *special = z["currentSpecialFunction"] | "";
            zs.quickVeto = strcmp(special, "QUICK_VETO") == 0;
            if (strcmp(special, "HOLIDAY") == 0) away = true;
        }
        for (JsonObjectConst z : doc["properties"]["zones"].as<JsonArrayConst>())
        {
            const int i = z["index"] | -1;
            if (i >= 0 && i < kMaxZones) _work.zones[i].circuit = z["associatedCircuitIndex"] | -1;
        }
        for (JsonObjectConst z : doc["configuration"]["zones"].as<JsonArrayConst>())
        {
            const int i = z["index"] | -1;
            if (i < 0 || i >= kMaxZones) continue;
            ZoneState &zs = _work.zones[i];
            zs.opMode = parseMode(z["heating"]["operationModeHeating"]);
            zs.setBack = num(z["heating"]["setBackTemperature"]);
            zs.manualSetpoint = num(z["heating"]["manualModeSetpointHeating"]);
        }

        for (auto &c : _work.circuits) c = CircuitState();
        for (JsonObjectConst c : doc["state"]["circuits"].as<JsonArrayConst>())
        {
            const int i = c["index"] | -1;
            if (i < 0 || i >= kMaxCircuits) continue;
            CircuitState &cs = _work.circuits[i];
            cs.valid = true;
            cs.flowTemp = num(c["currentCircuitFlowTemperature"]);
            cs.flowSetpoint = num(c["heatingCircuitFlowSetpoint"]);
            copyText(cs.state, sizeof(cs.state), c["circuitState"] | "");
        }
        for (JsonObjectConst c : doc["configuration"]["circuits"].as<JsonArrayConst>())
        {
            const int i = c["index"] | -1;
            if (i >= 0 && i < kMaxCircuits) _work.circuits[i].heatingCurve = num(c["heatingCurve"]);
        }

        // Hot water is addressed by list position: its API index is usually 255.
        for (auto &d : _work.dhw) d = DhwState();
        uint8_t pos = 0;
        for (JsonObjectConst d : doc["state"]["dhw"].as<JsonArrayConst>())
        {
            if (pos >= kMaxDhw) break;
            DhwState &ds = _work.dhw[pos++];
            ds.valid = true;
            ds.index = d["index"] | 255;
            ds.temp = num(d["currentDhwTemperature"]);
            ds.boost = strcmp(d["currentSpecialFunction"] | "", "CYLINDER_BOOST") == 0;
        }
        auto findDhw = [&](int apiIndex) -> DhwState * {
            for (auto &d : _work.dhw)
                if (d.valid && d.index == apiIndex) return &d;
            return nullptr;
        };
        for (JsonObjectConst d : doc["configuration"]["dhw"].as<JsonArrayConst>())
            if (DhwState *ds = findDhw(d["index"] | -1))
            {
                ds->opMode = parseMode(d["operationModeDhw"]);
                ds->setpoint = num(d["tappingSetpoint"]);
            }
        for (JsonObjectConst d : doc["properties"]["dhw"].as<JsonArrayConst>())
            if (DhwState *ds = findDhw(d["index"] | -1))
            {
                if (!isnan(num(d["minSetpoint"]))) ds->minSetpoint = num(d["minSetpoint"]);
                if (!isnan(num(d["maxSetpoint"]))) ds->maxSetpoint = num(d["maxSetpoint"]);
            }

        _work.away = away;
        _work.systemValid = true;
        _work.lastPollMs = millis();
        succeed();
        publish();
        return true;
    }

    void Cloud::pollExtras()
    {
        const std::string sys = "/systems/" + _systemId;

        if (_settings.wantOnline)
        {
            HttpResponse conn = api("GET", sys + "/meta-info/connection-status");
            if (conn.ok())
            {
                JsonDocument d;
                if (!deserializeJson(d, conn.body)) _work.online = d["connected"] | false;
            }
        }

        HttpResponse dtc;
        if (_settings.wantFaults) dtc = api("GET", sys + "/diagnostic-trouble-codes", std::string(), 32 * 1024);
        if (dtc.ok())
        {
            JsonDocument d;
            if (!deserializeJson(d, dtc.body))
            {
                bool fault = false, maintenance = false;
                char faultText[15] = "", maintenanceText[15] = "";
                for (JsonObjectConst device : d.as<JsonArrayConst>())
                    for (JsonObjectConst code : device["codes"].as<JsonArrayConst>())
                    {
                        const bool isMaintenance = strcmp(code["type"] | "", "MAINTENANCE") == 0;
                        char *text = isMaintenance ? maintenanceText : faultText;
                        if (!text[0]) snprintf(text, 15, "%s %s", code["code"] | "?", code["title"] | "");
                        (isMaintenance ? maintenance : fault) = true;
                    }
                _work.fault = fault;
                _work.maintenance = maintenance;
                copyText(_work.faultText, sizeof(_work.faultText), fault ? faultText : maintenanceText);
                _work.dtcValid = true;
            }
        }

        // Live power. Not every system has it, then the object stays silent.
        HttpResponse mpc;
        if (_settings.wantPower) mpc = api("GET", "/hem/" + _systemId + "/mpc");
        _work.power = NAN;
        if (mpc.ok())
        {
            JsonDocument d;
            if (!deserializeJson(d, mpc.body))
            {
                float sum = 0;
                bool any = false;
                for (JsonObjectConst device : d["devices"].as<JsonArrayConst>())
                {
                    const float p = num(device["currentPower"]);
                    if (!isnan(p))
                    {
                        sum += p;
                        any = true;
                    }
                }
                if (any) _work.power = sum;
            }
        }

        if (_settings.energyMask)
        {
            if (due(_nextDevices)) pollDevices();
            pollEnergy();
        }
        publish();
    }

    void Cloud::pollDevices()
    {
        HttpResponse res = api("GET", "/emf/v2/" + _systemId + "/currentSystem", std::string(), 32 * 1024);
        if (!res.ok())
        {
            logWarning("Vaillant", "currentSystem: HTTP %d, no energy figures", res.status);
            _nextDevices = millis() + 600000;
            return;
        }
        JsonDocument doc;
        if (deserializeJson(doc, res.body)) return;
        JsonObjectConst root = doc.as<JsonObjectConst>();

        for (uint8_t dev = 0; dev < DEV_COUNT; dev++)
        {
            _deviceUuid[dev].clear();
            _deviceData[dev] = 0;
            JsonObjectConst device = field(root, kDeviceKeys[dev][0], kDeviceKeys[dev][1]).as<JsonObjectConst>();
            if (device.isNull()) continue;
            _deviceUuid[dev] = field(device, "device_uuid", "deviceUuid") | "";
            for (JsonObjectConst data : device["data"].as<JsonArrayConst>())
            {
                const char *mode = field(data, "operation_mode", "operationMode") | "";
                const char *type = field(data, "value_type", "valueType") | "";
                for (uint8_t m = 0; m < EMODE_COUNT; m++)
                    for (uint8_t t = 0; t < ETYPE_COUNT; t++)
                        if (strcmp(mode, kEnergyModeNames[m]) == 0 && strcmp(type, kEnergyTypeNames[t]) == 0)
                            _deviceData[dev] |= 1u << (m * ETYPE_COUNT + t);
            }
            logInfo("Vaillant", "energy device %u: %s, data mask 0x%03X", (unsigned)dev,
                    _deviceUuid[dev].empty() ? "(none)" : _deviceUuid[dev].c_str(), (unsigned)_deviceData[dev]);
        }
        _nextDevices = millis() + kDeviceRefreshMs;
    }

    void Cloud::pollEnergy()
    {
        if (!timeValid())
        {
            logDebug("Vaillant", "no valid time yet, energy figures skipped");
            return;
        }
        // Today from local midnight. The local time zone comes from the device settings.
        const time_t now = time(nullptr);
        struct tm local;
        localtime_r(&now, &local);
        local.tm_hour = local.tm_min = local.tm_sec = 0;
        const time_t start = mktime(&local);
        const std::string from = Https::urlEncode(isoUtc(start));
        const std::string to = Https::urlEncode(isoUtc(start + 86400));

        for (uint8_t dev = 0; dev < DEV_COUNT; dev++)
            for (uint8_t mode = 0; mode < EMODE_COUNT; mode++)
            {
                if (!(_settings.energyMask & energyBit(dev, mode))) continue;
                for (uint8_t type = 0; type < ETYPE_COUNT; type++)
                {
                    float &target = _work.energy[dev][mode][type];
                    if (_deviceUuid[dev].empty() || !(_deviceData[dev] & (1u << (mode * ETYPE_COUNT + type))))
                    {
                        target = NAN;
                        continue;
                    }
                    const std::string path = "/emf/v2/" + _systemId + "/devices/" + _deviceUuid[dev] +
                                             "/buckets?resolution=DAY&operationMode=" + kEnergyModeNames[mode] +
                                             "&energyType=" + kEnergyTypeNames[type] + "&startDate=" + from + "&endDate=" + to;
                    HttpResponse res = api("GET", path, std::string(), 32 * 1024);
                    if (!res.ok()) continue; // keep the last value
                    JsonDocument d;
                    if (deserializeJson(d, res.body)) continue;
                    float total = num(d["totalConsumption"]);
                    if (isnan(total))
                    {
                        total = 0;
                        for (JsonObjectConst bucket : d["data"].as<JsonArrayConst>())
                        {
                            const float v = num(bucket["value"]);
                            if (!isnan(v)) total += v;
                        }
                    }
                    target = total;
                }
            }
    }
} // namespace Vaillant

#endif // OPENKNX_VAILLANT
