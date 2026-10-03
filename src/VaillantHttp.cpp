#include "VaillantConfig.h"
#ifdef OPENKNX_VAILLANT

#include "VaillantHttp.h"
#include "OpenKNX.h"
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

namespace Vaillant
{
    namespace
    {
        // A TLS session needs about 40 KB. Without PSRAM routing that comes from internal
        // RAM, which the web server, MQTT and the KNX stack need as well. Same approach
        // as OFM-HueGatewayModule; installing it twice is harmless.
        void *tlsCalloc(size_t count, size_t size)
        {
            void *p = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (p == nullptr) p = calloc(count, size); // PSRAM exhausted: internal RAM still works
            return p;
        }

        void tlsFree(void *p)
        {
            free(p);
        }

        void installPsramTlsAllocatorOnce()
        {
            static bool attempted = false;
            if (attempted) return;
            attempted = true;
            if (!psramFound()) return;
            if (mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree) != 0)
                logError("Vaillant", "TLS allocator: switch to PSRAM refused");
        }

        // Buffered reader over the TLS client with an overall deadline.
        class Reader
        {
          public:
            Reader(WiFiClientSecure &client, uint32_t timeoutMs)
                : _client(client), _start(millis()), _timeoutMs(timeoutMs) {}

            int get()
            {
                while (_pos == _len)
                {
                    if (millis() - _start > _timeoutMs) return -1;
                    const int avail = _client.available();
                    if (avail > 0)
                    {
                        const int n = _client.read(_buf, avail < (int)sizeof(_buf) ? avail : sizeof(_buf));
                        if (n > 0)
                        {
                            _pos = 0;
                            _len = (size_t)n;
                            break;
                        }
                    }
                    else if (!_client.connected())
                        return -1;
                    else
                        vTaskDelay(1);
                }
                return _buf[_pos++];
            }

            // Reads one line without CRLF. false on EOF/timeout or when longer than maxLen.
            bool line(std::string &out, size_t maxLen = 8192)
            {
                out.clear();
                while (true)
                {
                    const int c = get();
                    if (c < 0) return false;
                    if (c == '\n')
                    {
                        if (!out.empty() && out.back() == '\r') out.pop_back();
                        return true;
                    }
                    if (out.size() >= maxLen) return false;
                    out += (char)c;
                }
            }

            bool timedOut() const { return millis() - _start > _timeoutMs; }

          private:
            WiFiClientSecure &_client;
            uint32_t _start;
            uint32_t _timeoutMs;
            uint8_t _buf[1024];
            size_t _pos = 0;
            size_t _len = 0;
        };

        bool startsWithNoCase(const std::string &s, const char *prefix)
        {
            return strncasecmp(s.c_str(), prefix, strlen(prefix)) == 0;
        }

        std::string trimmed(const std::string &s, size_t from)
        {
            while (from < s.size() && (s[from] == ' ' || s[from] == '\t')) from++;
            return s.substr(from);
        }
    } // namespace

    HttpResponse Https::request(const char *method, const std::string &url,
                                const std::vector<std::string> &headers,
                                const std::string &body, size_t maxBody, uint32_t timeoutMs)
    {
        HttpResponse res;

        if (url.rfind("https://", 0) != 0)
        {
            res.status = -1;
            res.error = "only https URLs";
            return res;
        }
        const size_t hostStart = 8;
        size_t pathStart = url.find('/', hostStart);
        const std::string host = url.substr(hostStart, pathStart == std::string::npos ? std::string::npos : pathStart - hostStart);
        const std::string path = pathStart == std::string::npos ? "/" : url.substr(pathStart);

        installPsramTlsAllocatorOnce();

        WiFiClientSecure client;
        client.setInsecure(); // like the HA integration's default; the token is the secret, not the server identity
        client.setHandshakeTimeout(timeoutMs / 1000);
        if (!client.connect(host.c_str(), 443, (int32_t)timeoutMs))
        {
            res.status = -1;
            res.error = "connect failed: " + host;
            return res;
        }

        std::string req;
        req.reserve(512 + body.size());
        req += method;
        req += ' ';
        req += path;
        req += " HTTP/1.1\r\nHost: ";
        req += host;
        req += "\r\nConnection: close\r\nAccept-Encoding: identity\r\n";
        for (const auto &h : headers)
        {
            req += h;
            req += "\r\n";
        }
        const bool needsLength = !body.empty() || strcmp(method, "GET") != 0;
        if (needsLength)
        {
            req += "Content-Length: ";
            req += std::to_string(body.size());
            req += "\r\n";
        }
        req += "\r\n";
        req += body;

        size_t sent = 0;
        while (sent < req.size())
        {
            const size_t n = client.write((const uint8_t *)req.data() + sent, req.size() - sent);
            if (n == 0)
            {
                client.stop();
                res.status = -1;
                res.error = "write failed";
                return res;
            }
            sent += n;
        }

        Reader in(client, timeoutMs);
        std::string line;

        // Status line
        if (!in.line(line) || line.rfind("HTTP/", 0) != 0)
        {
            client.stop();
            res.status = -1;
            res.error = in.timedOut() ? "timeout" : "no status line";
            return res;
        }
        const size_t sp = line.find(' ');
        res.status = sp == std::string::npos ? -1 : atoi(line.c_str() + sp + 1);

        // Headers
        long contentLength = -1;
        bool chunked = false;
        while (true)
        {
            if (!in.line(line))
            {
                client.stop();
                res.status = -1;
                res.error = "header read failed";
                return res;
            }
            if (line.empty()) break;
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            if (startsWithNoCase(line, "content-length:"))
                contentLength = atol(line.c_str() + colon + 1);
            else if (startsWithNoCase(line, "transfer-encoding:"))
                chunked = strcasestr(line.c_str() + colon + 1, "chunked") != nullptr;
            else if (startsWithNoCase(line, "location:"))
                res.location = trimmed(line, colon + 1);
            else if (startsWithNoCase(line, "set-cookie:"))
            {
                std::string cookie = trimmed(line, colon + 1);
                const size_t semi = cookie.find(';');
                if (semi != std::string::npos) cookie.resize(semi);
                res.cookies.push_back(cookie);
            }
        }

        // Body
        auto appendByte = [&](int c) -> bool {
            if (res.body.size() >= maxBody) return false;
            res.body += (char)c;
            return true;
        };

        bool bodyOk = true;
        if (strcmp(method, "HEAD") == 0 || res.status == 204 || res.status == 304)
        {
            // no body
        }
        else if (chunked)
        {
            while (bodyOk)
            {
                if (!in.line(line))
                {
                    bodyOk = false;
                    break;
                }
                const long size = strtol(line.c_str(), nullptr, 16);
                if (size <= 0) break;
                if (res.body.size() + (size_t)size > maxBody)
                {
                    bodyOk = false;
                    break;
                }
                res.body.reserve(res.body.size() + size);
                for (long i = 0; i < size && bodyOk; i++)
                {
                    const int c = in.get();
                    bodyOk = c >= 0 && appendByte(c);
                }
                in.line(line); // CRLF after the chunk
            }
        }
        else if (contentLength >= 0)
        {
            if ((size_t)contentLength > maxBody)
                bodyOk = false;
            else
            {
                res.body.reserve(contentLength);
                for (long i = 0; i < contentLength && bodyOk; i++)
                {
                    const int c = in.get();
                    bodyOk = c >= 0 && appendByte(c);
                }
            }
        }
        else
        {
            int c;
            while ((c = in.get()) >= 0)
                if (!appendByte(c))
                {
                    bodyOk = false;
                    break;
                }
        }

        client.stop();
        if (!bodyOk)
        {
            res.error = res.body.size() >= maxBody ? "body too large" : "body read failed";
            res.status = -2;
        }
        return res;
    }

    std::string Https::urlEncode(const std::string &s)
    {
        static const char hex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() * 3);
        for (unsigned char c : s)
        {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                out += (char)c;
            else
            {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 0x0F];
            }
        }
        return out;
    }

    std::string Https::urlDecode(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++)
        {
            if (s[i] == '%' && i + 2 < s.size())
            {
                out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
                i += 2;
            }
            else if (s[i] == '+')
                out += ' ';
            else
                out += s[i];
        }
        return out;
    }

    std::string Https::queryParam(const std::string &url, const char *name)
    {
        const std::string key = std::string(name) + "=";
        size_t pos = url.find('?');
        while (pos != std::string::npos)
        {
            pos++;
            if (url.compare(pos, key.size(), key) == 0)
            {
                const size_t end = url.find_first_of("&#", pos);
                return urlDecode(url.substr(pos + key.size(), end == std::string::npos ? std::string::npos : end - pos - key.size()));
            }
            pos = url.find('&', pos);
        }
        return std::string();
    }
} // namespace Vaillant

#endif // OPENKNX_VAILLANT
