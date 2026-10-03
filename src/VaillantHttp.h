#pragma once
#include "VaillantConfig.h"
#ifdef OPENKNX_VAILLANT

#include <string>
#include <vector>

namespace Vaillant
{
    struct HttpResponse
    {
        int status = 0; // < 0: transport error, see error
        std::string body;
        std::string location;
        std::vector<std::string> cookies; // "name=value", attributes stripped
        std::string error;

        bool ok() const { return status >= 200 && status < 300; }
    };

    // Minimal blocking HTTPS/1.1 client on WiFiClientSecure. Only for the worker task.
    //
    // Not Arduino's HTTPClient: the Keycloak login needs every Set-Cookie header and a
    // Location header of several hundred bytes, and must not follow redirects.
    // Not OFM-Network's webclient either: it truncates request headers at 768 bytes,
    // and the bearer token alone is larger.
    class Https
    {
      public:
        // headers: complete "Name: value" lines without CRLF. A body is sent with
        // Content-Length; POST/PUT/PATCH/DELETE without body send "Content-Length: 0".
        static HttpResponse request(const char *method, const std::string &url,
                                    const std::vector<std::string> &headers,
                                    const std::string &body = std::string(),
                                    size_t maxBody = 96 * 1024, uint32_t timeoutMs = 20000);

        static std::string urlEncode(const std::string &s);
        static std::string urlDecode(const std::string &s);
        // Value of a query parameter in a URL, decoded; empty if absent.
        static std::string queryParam(const std::string &url, const char *name);
    };
} // namespace Vaillant

#endif // OPENKNX_VAILLANT
