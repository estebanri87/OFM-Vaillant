#pragma once
#include "VaillantConfig.h"
#ifdef OPENKNX_VAILLANT

#include "OpenKNX.h"
#include <math.h>
#include <string>

namespace Vaillant
{
    // Send-on-change for status objects. The cloud is polled every minute; without this
    // every poll would put every value on the bus again.

    struct LastFloat
    {
        float value = NAN;
    };
    struct LastInt
    {
        int32_t value = INT32_MIN;
    };
    struct LastText
    {
        std::string value;
        bool sent = false;
    };

    // NAN means "no value from the cloud": nothing is sent.
    inline void sendFloat(GroupObject &ko, float v, const Dpt &dpt, LastFloat &last, float epsilon = 0.01f)
    {
        if (isnan(v)) return;
        if (!isnan(last.value) && fabsf(v - last.value) < epsilon) return;
        last.value = v;
        ko.value(v, dpt);
    }

    inline void sendInt(GroupObject &ko, int32_t v, const Dpt &dpt, LastInt &last)
    {
        if (last.value == v) return;
        last.value = v;
        ko.value(v, dpt);
    }

    inline void sendBool(GroupObject &ko, bool v, const Dpt &dpt, LastInt &last)
    {
        if (last.value == (int32_t)v) return;
        last.value = v;
        ko.value(v, dpt);
    }

    // DPT 16.001 carries 14 characters.
    inline void sendText(GroupObject &ko, const char *v, LastText &last)
    {
        const std::string text = std::string(v ? v : "").substr(0, 14);
        if (last.sent && last.value == text) return;
        last.value = text;
        last.sent = true;
        ko.value(text.c_str(), DPT_String_8859_1);
    }
} // namespace Vaillant

#endif // OPENKNX_VAILLANT
