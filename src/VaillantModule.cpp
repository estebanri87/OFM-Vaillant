#include "VaillantConfig.h"
#ifdef OPENKNX_VAILLANT

#include "VaillantModule.h"
#include "VaillantChannel.h"

using namespace Vaillant;

namespace
{
    // ETS text parameters are not terminated when filled to the last byte.
    std::string paramText(const uint8_t *data, size_t size)
    {
        const char *text = (const char *)data;
        return std::string(text, strnlen(text, size));
    }
} // namespace

VaillantModule::VaillantModule()
    : VAIChannelOwnerModule(VAI_ChannelCount)
{
}

const std::string VaillantModule::name()
{
    return "Vaillant";
}

const std::string VaillantModule::version()
{
#ifdef MODULE_Vaillant_Version
    return MODULE_Vaillant_Version;
#else
    return "0.1.0";
#endif
}

OpenKNX::Channel *VaillantModule::createChannel(uint8_t _channelIndex)
{
    // Nur aktivierte, nicht suspendierte Kanäle anlegen (Kanalauswahl).
    if (ParamVAI_CHType == VaillantChannel::TYPE_NONE || ParamVAI_CHSuspended) return nullptr;
    return new VaillantChannel(_channelIndex, *this);
}

uint8_t VaillantModule::energyMask()
{
    uint8_t mask = 0;
    for (uint8_t i = 0; i < getNumberOfChannels(); i++)
        if (auto *channel = static_cast<VaillantChannel *>(getChannel(i))) mask |= channel->energyMask();
    return mask;
}

void VaillantModule::setup(bool configured)
{
    VAIChannelOwnerModule::setup(configured); // creates the channels
    if (!configured || !ParamVAI_VAIActive) return;

    Cloud::Settings s;
    s.user = paramText(ParamVAI_VAIUser, 64);
    s.password = paramText(ParamVAI_VAIPassword, 64);
    s.country = ParamVAI_VAICountry;
    s.homeIndex = ParamVAI_VAIHomeIndex;
    s.pollSeconds = ParamVAI_VAIPollInterval;
    s.extraMinutes = ParamVAI_VAIExtraInterval;
    s.quickVetoHours = ParamVAI_VAIQuickVetoHours;
    s.energyMask = energyMask();
    _cloud.begin(s);
}

void VaillantModule::loop(bool configured)
{
    VAIChannelOwnerModule::loop(configured);
    if (!configured || !_cloud.started()) return;

    if (_cloud.snapshot(_state, _seq))
    {
        _seq = _state.seq;
        applyState();
    }
}

void VaillantModule::applyState()
{
    sendBool(KoVAI_VAIConnected, _state.cloudOk, DPT_State, _lastConnected);
    sendText(KoVAI_VAIDiag, _state.diag, _lastDiag);
    if (!_state.systemValid) return;

    sendBool(KoVAI_VAIOnline, _state.online, DPT_State, _lastOnline);
    sendFloat(KoVAI_VAIOutdoorTemp, _state.outdoor, DPT_Value_Temp, _lastOutdoor, 0.05f);
    sendFloat(KoVAI_VAIOutdoorTempAvg, _state.outdoorAvg, DPT_Value_Temp, _lastOutdoorAvg, 0.05f);
    // The cloud reports bar, KNX 9.006 is Pa.
    if (!isnan(_state.pressure)) sendFloat(KoVAI_VAIPressure, _state.pressure * 100000.0f, DPT_Value_Pres, _lastPressure, 500.0f);
    sendText(KoVAI_VAIEnergyManager, _state.energyManagerState, _lastEnergyManager);
    sendBool(KoVAI_VAIAwayStatus, _state.away, DPT_State, _lastAway);
    sendFloat(KoVAI_VAIPower, _state.power, DPT_Value_Power, _lastPower, 1.0f);
    if (_state.dtcValid)
    {
        sendBool(KoVAI_VAIFault, _state.fault, DPT_Alarm, _lastFault);
        sendBool(KoVAI_VAIMaintenance, _state.maintenance, DPT_Alarm, _lastMaintenance);
        sendText(KoVAI_VAIFaultText, _state.faultText, _lastFaultText);
    }

    for (uint8_t i = 0; i < getNumberOfChannels(); i++)
        if (auto *channel = static_cast<VaillantChannel *>(getChannel(i))) channel->apply(_state);
}

void VaillantModule::processInputKo(GroupObject &ko)
{
    VAIChannelOwnerModule::processInputKo(ko);
    if (!_cloud.started()) return;

    if (ko.asap() == VAI_KoVAIAway)
        _cloud.enqueue(Cloud::CMD_AWAY, 0, ko.value(DPT_Switch) ? 1 : 0);
}

void VaillantModule::printStatus()
{
    logInfoP("active: %s, cloud task: %s", ParamVAI_VAIActive ? "yes" : "no", _cloud.started() ? "running" : "stopped");
    if (!_cloud.started()) return;
    State s;
    if (!_cloud.snapshot(s, s.seq - 1)) return; // force a copy
    logInfoP("status: %s (%s)", s.diag, s.cloudOk ? "ok" : "error");
    if (s.lastError[0]) logInfoP("last error: %s", s.lastError);
    logInfoP("system: %s, gateway %s", s.systemId[0] ? s.systemId : "(unknown)", s.online ? "online" : "offline");
    if (s.tokenExpiresAtMs)
        logInfoP("token valid for %ld s, ALTCHA %u ms, %u requests", (long)((int32_t)(s.tokenExpiresAtMs - millis()) / 1000),
                 (unsigned)s.altchaMs, (unsigned)s.requests);
    if (s.systemValid)
    {
        logInfoP("outdoor %.1f C, pressure %.2f bar, power %.0f W, away %s, last poll %lu s ago", s.outdoor, s.pressure, s.power,
                 s.away ? "on" : "off", (unsigned long)((millis() - s.lastPollMs) / 1000));
        for (uint8_t i = 0; i < kMaxZones; i++)
            if (s.zones[i].valid)
                logInfoP("zone %u: %.1f C (set %.1f) %s mode %u veto %u circuit %d", (unsigned)i, s.zones[i].roomTemp, s.zones[i].setpoint,
                         s.zones[i].heatingState, (unsigned)s.zones[i].opMode, (unsigned)s.zones[i].quickVeto, (int)s.zones[i].circuit);
        for (uint8_t i = 0; i < kMaxDhw; i++)
            if (s.dhw[i].valid)
                logInfoP("dhw %u (index %u): %.1f C (set %.1f) mode %u boost %u", (unsigned)i, (unsigned)s.dhw[i].index, s.dhw[i].temp,
                         s.dhw[i].setpoint, (unsigned)s.dhw[i].opMode, (unsigned)s.dhw[i].boost);
    }
    if (s.dtcValid) logInfoP("fault %u, maintenance %u: %s", (unsigned)s.fault, (unsigned)s.maintenance, s.faultText);
}

void VaillantModule::showHelp()
{
    openknx.console.printHelpLine("vai", "Vaillant cloud status");
    openknx.console.printHelpLine("vai poll", "Poll the cloud now");
    openknx.console.printHelpLine("vai login", "Discard the stored token and log in again");
    openknx.console.printHelpLine("vai raw on|off", "Log every cloud request");
}

bool VaillantModule::processCommand(const std::string command, bool debugKo)
{
    if (command.rfind("vai", 0) != 0) return false;

    if (command == "vai")
    {
        printStatus();
        return true;
    }
    if (command == "vai poll")
    {
        logInfoP("poll %s", _cloud.enqueue(Cloud::CMD_POLL) ? "requested" : "not possible");
        return true;
    }
    if (command == "vai login")
    {
        logInfoP("re-login %s", _cloud.enqueue(Cloud::CMD_RELOGIN) ? "requested" : "not possible");
        return true;
    }
    if (command.rfind("vai raw", 0) == 0)
    {
        const bool on = command.find("on") != std::string::npos;
        _cloud.setRawLog(on);
        logInfoP("raw logging %s", on ? "on" : "off");
        return true;
    }
    return false;
}

#endif // OPENKNX_VAILLANT
