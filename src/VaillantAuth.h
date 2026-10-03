#pragma once
#include "VaillantConfig.h"
#ifdef OPENKNX_VAILLANT

#include <stdint.h>
#include <string>
#include <vector>

namespace Vaillant
{
    // Country keys of the Keycloak realm "vaillant-<country>-b2c", in the order of the ETS
    // enumeration PT-VAICountry (myPyllant const.COUNTRIES["vaillant"]).
    extern const char *const kCountries[];
    extern const uint8_t kCountryCount;

    struct Token
    {
        std::string access;
        std::string refresh;
        uint32_t expiresAtMs = 0; // millis() at which the access token expires

        bool valid() const { return !access.empty(); }
    };

    // Keycloak login of the myVAILLANT app, as done by myPyllant: PKCE, the HTML login
    // form, an ALTCHA proof of work and the token endpoint. All calls block for seconds
    // (the proof of work for tens of seconds) and belong in the worker task only.
    class Auth
    {
      public:
        void configure(const std::string &user, const std::string &password, uint8_t country);

        // Full login. On failure, error holds a short reason for the diagnostic object.
        bool login(Token &token, std::string &error);
        // Exchange the refresh token for a new pair. On failure, rejected tells a refused
        // token (full login needed) apart from a network problem (retry later).
        bool refresh(Token &token, std::string &error, bool &rejected);

        uint32_t lastAltchaMs() const { return _lastAltchaMs; }

      private:
        std::string _user;
        std::string _password;
        std::string _realm;
        uint32_t _lastAltchaMs = 0;

        std::string realmUrl() const;
        bool readToken(const std::string &body, int status, Token &token, std::string &error);
        bool solveAltcha(const std::string &challengeJson, std::string &payload, std::string &error);

        static void makePkce(std::string &verifier, std::string &challenge);
        static std::string cookieHeader(const std::vector<std::string> &cookies);
        static void mergeCookies(std::vector<std::string> &jar, const std::vector<std::string> &received);
    };
} // namespace Vaillant

#endif // OPENKNX_VAILLANT
