#include "VaillantConfig.h"
#ifdef OPENKNX_VAILLANT

#include "VaillantAuth.h"
#include "OpenKNX.h"
#include "VaillantHttp.h"
#include <ArduinoJson.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>

namespace Vaillant
{
    const char *const kCountries[] = {
        "albania", "austria", "belgium", "bosnia", "bulgaria", "croatia", "cyprus",
        "czechrepublic", "denmark", "estonia", "finland", "france", "georgia", "germany",
        "greece", "hungary", "ireland", "italy", "kosovo", "latvia", "lithuania",
        "luxembourg", "moldavia", "netherlands", "new-zealand", "north-macedonia", "norway",
        "poland", "portugal", "romania", "serbia", "slovakia", "slovenia", "spain", "sweden",
        "switzerland", "turkiye", "ukraine", "unitedkingdom", "uzbekistan"};
    const uint8_t kCountryCount = sizeof(kCountries) / sizeof(kCountries[0]);

    namespace
    {
        const char *const kIdentityBase = "https://identity.vaillant-group.com";
        const char *const kAltchaUrl = "https://identity.vaillant-group.com/api/altcha/challenge";
        const char *const kClientId = "myvaillant";
        const char *const kRedirectUri = "enduservaillant.page.link://login";
        const char *const kFormType = "Content-Type: application/x-www-form-urlencoded";
        const char *const kBrowserAgent = "User-Agent: Mozilla/5.0 (Linux; Android 13) myVAILLANT";

        // Upper bound for the proof of work. A one-byte key prefix needs 256 tries on
        // average; anything far beyond that means the challenge changed shape.
        const uint32_t kAltchaMaxCounter = 200000;
        const uint32_t kAltchaMaxMs = 180000;

        bool fromHex(const char *hex, std::vector<uint8_t> &out)
        {
            out.clear();
            if (!hex) return false;
            const size_t len = strlen(hex);
            if (len % 2) return false;
            for (size_t i = 0; i < len; i += 2)
            {
                char byte[3] = {hex[i], hex[i + 1], 0};
                char *end = nullptr;
                const long v = strtol(byte, &end, 16);
                if (end != byte + 2) return false;
                out.push_back((uint8_t)v);
            }
            return true;
        }

        std::string toHex(const uint8_t *data, size_t len)
        {
            static const char digits[] = "0123456789abcdef";
            std::string out;
            out.reserve(len * 2);
            for (size_t i = 0; i < len; i++)
            {
                out += digits[data[i] >> 4];
                out += digits[data[i] & 0x0F];
            }
            return out;
        }

        std::string base64(const uint8_t *data, size_t len)
        {
            size_t olen = 0;
            mbedtls_base64_encode(nullptr, 0, &olen, data, len);
            std::string out(olen, '\0');
            if (mbedtls_base64_encode((unsigned char *)&out[0], out.size(), &olen, data, len) != 0) return std::string();
            out.resize(olen);
            return out;
        }

        // RFC 8018 PBKDF2 on mbedTLS' HMAC. Written out instead of
        // mbedtls_pkcs5_pbkdf2_hmac_ext() so it does not depend on the mbedTLS major
        // version of the Arduino core.
        bool pbkdf2(mbedtls_md_type_t type, const uint8_t *pw, size_t pwLen, const uint8_t *salt, size_t saltLen,
                    uint32_t iterations, uint8_t *out, size_t outLen)
        {
            const mbedtls_md_info_t *info = mbedtls_md_info_from_type(type);
            if (!info || iterations == 0) return false;
            const size_t hLen = mbedtls_md_get_size(info);

            mbedtls_md_context_t ctx;
            mbedtls_md_init(&ctx);
            bool ok = mbedtls_md_setup(&ctx, info, 1) == 0 && mbedtls_md_hmac_starts(&ctx, pw, pwLen) == 0;

            uint8_t u[MBEDTLS_MD_MAX_SIZE];
            uint8_t t[MBEDTLS_MD_MAX_SIZE];
            uint32_t block = 1;
            size_t done = 0;
            while (ok && done < outLen)
            {
                const uint8_t be[4] = {(uint8_t)(block >> 24), (uint8_t)(block >> 16), (uint8_t)(block >> 8), (uint8_t)block};
                ok = mbedtls_md_hmac_reset(&ctx) == 0 && mbedtls_md_hmac_update(&ctx, salt, saltLen) == 0 &&
                     mbedtls_md_hmac_update(&ctx, be, 4) == 0 && mbedtls_md_hmac_finish(&ctx, u) == 0;
                memcpy(t, u, hLen);
                for (uint32_t i = 1; ok && i < iterations; i++)
                {
                    ok = mbedtls_md_hmac_reset(&ctx) == 0 && mbedtls_md_hmac_update(&ctx, u, hLen) == 0 &&
                         mbedtls_md_hmac_finish(&ctx, u) == 0;
                    for (size_t j = 0; j < hLen; j++) t[j] ^= u[j];
                }
                const size_t n = (outLen - done) < hLen ? (outLen - done) : hLen;
                memcpy(out + done, t, n);
                done += n;
                block++;
            }
            mbedtls_md_free(&ctx);
            return ok;
        }

        void replaceAll(std::string &s, const char *from, const char *to)
        {
            const size_t fromLen = strlen(from);
            const size_t toLen = strlen(to);
            for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + toLen))
                s.replace(pos, fromLen, to);
        }
    } // namespace

    void Auth::configure(const std::string &user, const std::string &password, uint8_t country)
    {
        _user = user;
        _password = password;
        _realm = std::string("vaillant-") + kCountries[country < kCountryCount ? country : 13] + "-b2c";
    }

    std::string Auth::realmUrl() const
    {
        return std::string(kIdentityBase) + "/auth/realms/" + _realm;
    }

    void Auth::makePkce(std::string &verifier, std::string &challenge)
    {
        static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        verifier.clear();
        for (int i = 0; i < 64; i++)
            verifier += alphabet[esp_random() % (sizeof(alphabet) - 1)];

        uint8_t hash[32];
        mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const uint8_t *)verifier.data(), verifier.size(), hash);
        challenge = base64(hash, sizeof(hash));
        replaceAll(challenge, "+", "-");
        replaceAll(challenge, "/", "_");
        while (!challenge.empty() && challenge.back() == '=') challenge.pop_back();
    }

    void Auth::mergeCookies(std::vector<std::string> &jar, const std::vector<std::string> &received)
    {
        for (const auto &cookie : received)
        {
            const std::string name = cookie.substr(0, cookie.find('='));
            bool replaced = false;
            for (auto &existing : jar)
                if (existing.compare(0, name.size() + 1, name + "=") == 0)
                {
                    existing = cookie;
                    replaced = true;
                }
            if (!replaced) jar.push_back(cookie);
        }
    }

    std::string Auth::cookieHeader(const std::vector<std::string> &cookies)
    {
        std::string header = "Cookie: ";
        for (size_t i = 0; i < cookies.size(); i++)
        {
            if (i) header += "; ";
            header += cookies[i];
        }
        return header;
    }

    bool Auth::solveAltcha(const std::string &challengeJson, std::string &payload, std::string &error)
    {
        JsonDocument doc;
        if (deserializeJson(doc, challengeJson))
        {
            error = "ALTCHA: kein JSON";
            return false;
        }
        JsonObject params = doc["parameters"];
        if (params.isNull())
        {
            error = "ALTCHA: unbekanntes Format";
            return false;
        }

        std::vector<uint8_t> nonce, salt, prefix;
        if (!fromHex(params["nonce"], nonce) || !fromHex(params["salt"], salt) || !fromHex(params["keyPrefix"], prefix))
        {
            error = "ALTCHA: Parameter fehlen";
            return false;
        }
        const uint32_t cost = params["cost"] | 0;
        const size_t keyLength = params["keyLength"] | 32;
        const char *algorithm = params["algorithm"] | "PBKDF2/SHA-256";
        mbedtls_md_type_t md = MBEDTLS_MD_SHA256;
        if (strstr(algorithm, "SHA-512")) md = MBEDTLS_MD_SHA512;
        else if (strstr(algorithm, "SHA-384")) md = MBEDTLS_MD_SHA384;
        if (cost == 0 || keyLength == 0 || keyLength > 64 || prefix.size() > keyLength)
        {
            error = "ALTCHA: Parameter ungültig";
            return false;
        }

        std::vector<uint8_t> password(nonce);
        password.resize(nonce.size() + 4);
        uint8_t derived[64];

        const uint32_t start = millis();
        for (uint32_t counter = 0; counter < kAltchaMaxCounter; counter++)
        {
            password[nonce.size() + 0] = (uint8_t)(counter >> 24);
            password[nonce.size() + 1] = (uint8_t)(counter >> 16);
            password[nonce.size() + 2] = (uint8_t)(counter >> 8);
            password[nonce.size() + 3] = (uint8_t)counter;
            if (!pbkdf2(md, password.data(), password.size(), salt.data(), salt.size(), cost, derived, keyLength))
            {
                error = "ALTCHA: PBKDF2 fehlgeschlagen";
                return false;
            }
            if (memcmp(derived, prefix.data(), prefix.size()) == 0)
            {
                _lastAltchaMs = millis() - start;

                JsonDocument out;
                out["challenge"]["parameters"] = params;
                out["challenge"]["signature"] = doc["signature"];
                out["solution"]["counter"] = counter;
                out["solution"]["derivedKey"] = toHex(derived, keyLength);
                out["solution"]["time"] = 0;
                std::string json;
                serializeJson(out, json);
                payload = base64((const uint8_t *)json.data(), json.size());
                logInfo("Vaillant", "ALTCHA solved: counter %u, cost %u, %u ms", (unsigned)counter, (unsigned)cost, (unsigned)_lastAltchaMs);
                return true;
            }
            if (millis() - start > kAltchaMaxMs) break;
            vTaskDelay(1); // keep the idle task of this core fed
        }
        error = "ALTCHA: keine Lösung";
        return false;
    }

    bool Auth::readToken(const std::string &body, int status, Token &token, std::string &error)
    {
        JsonDocument doc;
        if (deserializeJson(doc, body))
        {
            error = "Token: HTTP " + std::to_string(status);
            return false;
        }
        if (status >= 400 || doc["access_token"].isNull())
        {
            const char *desc = doc["error_description"] | (doc["error"] | "abgelehnt");
            error = std::string("Token: ") + desc;
            return false;
        }
        token.access = doc["access_token"].as<const char *>();
        if (!doc["refresh_token"].isNull()) token.refresh = doc["refresh_token"].as<const char *>();
        const uint32_t expiresIn = doc["expires_in"] | 300;
        // Renew a minute early so a request never goes out with a token about to expire.
        token.expiresAtMs = millis() + (expiresIn > 120 ? expiresIn - 60 : expiresIn / 2) * 1000UL;
        return true;
    }

    bool Auth::login(Token &token, std::string &error)
    {
        if (_user.empty() || _password.empty())
        {
            error = "Zugangsdaten fehlen";
            return false;
        }

        std::string verifier, challenge;
        makePkce(verifier, challenge);
        std::vector<std::string> jar;

        const std::string authUrl = realmUrl() + "/protocol/openid-connect/auth?response_type=code&client_id=" + kClientId +
                                    "&code=code_challenge&redirect_uri=" + Https::urlEncode(kRedirectUri) +
                                    "&code_challenge_method=S256&code_challenge=" + challenge;
        HttpResponse page = Https::request("GET", authUrl, {kBrowserAgent, "Accept: text/html"}, std::string(), 256 * 1024);
        if (page.status < 0)
        {
            error = "Login-Seite: " + page.error;
            return false;
        }
        mergeCookies(jar, page.cookies);

        // A still valid SSO session redirects straight away; normally the login form comes.
        std::string code = Https::queryParam(page.location, "code");
        if (code.empty())
        {
            const std::string formPrefix = realmUrl() + "/login-actions/authenticate?";
            const size_t start = page.body.find(formPrefix);
            if (start == std::string::npos)
            {
                error = "Login-Formular nicht gefunden (HTTP " + std::to_string(page.status) + ")";
                return false;
            }
            std::string loginUrl = page.body.substr(start, page.body.find('"', start) - start);
            replaceAll(loginUrl, "&amp;", "&");
            page.body.clear();
            page.body.shrink_to_fit();

            std::string form = "username=" + Https::urlEncode(_user) + "&password=" + Https::urlEncode(_password) + "&credentialId=";

            // myPyllant also carries on without ALTCHA when the challenge cannot be had.
            HttpResponse altcha = Https::request("GET", kAltchaUrl, {kBrowserAgent, cookieHeader(jar)}, std::string(), 16 * 1024);
            if (altcha.ok())
            {
                mergeCookies(jar, altcha.cookies);
                std::string solution, altchaError;
                if (solveAltcha(altcha.body, solution, altchaError))
                    form += "&altcha=" + Https::urlEncode(solution);
                else
                    logWarning("Vaillant", "%s, trying without", altchaError.c_str());
            }
            else
                logWarning("Vaillant", "no ALTCHA challenge (HTTP %d), trying without", altcha.status);

            HttpResponse post = Https::request("POST", loginUrl, {kBrowserAgent, kFormType, cookieHeader(jar)}, form, 256 * 1024);
            if (post.status < 0)
            {
                error = "Login: " + post.error;
                return false;
            }
            code = Https::queryParam(post.location, "code");
            if (code.empty())
            {
                // Keycloak answers a wrong password with the form again (200), not a redirect.
                error = post.status == 200 ? "Anmeldung abgelehnt (E-Mail/Passwort?)" : "Login: HTTP " + std::to_string(post.status);
                return false;
            }
        }

        const std::string form = std::string("grant_type=authorization_code&client_id=") + kClientId + "&code=" + Https::urlEncode(code) +
                                 "&code_verifier=" + verifier + "&redirect_uri=" + Https::urlEncode(kRedirectUri);
        HttpResponse res = Https::request("POST", realmUrl() + "/protocol/openid-connect/token", {kFormType}, form, 32 * 1024);
        if (res.status < 0)
        {
            error = "Token: " + res.error;
            return false;
        }
        return readToken(res.body, res.status, token, error);
    }

    bool Auth::refresh(Token &token, std::string &error, bool &rejected)
    {
        rejected = false;
        if (token.refresh.empty())
        {
            rejected = true;
            error = "kein Refresh-Token";
            return false;
        }
        const std::string form = "refresh_token=" + Https::urlEncode(token.refresh) + "&client_id=" + kClientId + "&grant_type=refresh_token";
        HttpResponse res = Https::request("POST", realmUrl() + "/protocol/openid-connect/token", {kFormType}, form, 32 * 1024);
        if (res.status < 0 || res.status >= 500)
        {
            error = res.status < 0 ? "Refresh: " + res.error : "Refresh: HTTP " + std::to_string(res.status);
            return false; // transient, keep the refresh token
        }
        if (readToken(res.body, res.status, token, error)) return true;
        rejected = true; // 4xx: expired or revoked, a full login is needed
        return false;
    }
} // namespace Vaillant

#endif // OPENKNX_VAILLANT
