#pragma once

// OFM-Vaillant is ESP32-only.
//
// myVAILLANT is a cloud API: TLS to api.vaillant-group.com and a Keycloak login that
// demands an ALTCHA proof of work (about a million HMAC-SHA256). That needs mbedTLS with
// hardware SHA and PSRAM for the TLS buffers. On RP2040 BearSSL's 4 KB input buffer
// cannot take the 16 KB records of a cloud server, and the proof of work would block
// the single core for minutes.
//
// Every header and source file of this module is wrapped in OPENKNX_VAILLANT, and the
// parent project guards its addModule() call with it. This header itself stays
// unguarded so that guard can be evaluated.

#if defined(ARDUINO_ARCH_ESP32) && (defined(KNX_IP_WIFI) || defined(KNX_IP_LAN))
#define OPENKNX_VAILLANT 1
#endif
