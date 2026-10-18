#pragma once
// Page tooltips (1.2.0): the host half. WebCore reports the title under the pointer through
// ViewListener::OnChangeTooltip; the host collects those changes for a frame (Coalesce) and hands each to the page
// core's reserved '__tooltip' channel (Payload), which draws the tooltip inside the page. Engine-free, so
// tools/tooltip-test runs it on the desktop.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Magelight::Tooltip {

    // The longest title a page is sent: a tooltip is a line or two, and the outbound queue holds scripts.
    inline constexpr std::size_t kMaxTextBytes = 2000;

    // One change per key, the last one winning, in the order the keys first changed this frame. A clear ("") is
    // never dropped: it replaces whatever text the same key had.
    template <class K>
    void Coalesce(std::vector<std::pair<K, std::string>>& list, K key, std::string text)
    {
        for (auto& e : list) {
            if (e.first == key) {
                e.second = std::move(text);
                return;
            }
        }
        list.emplace_back(key, std::move(text));
    }

    // The drawn cursor's height in a view's CSS px, so the tooltip clears it: cursorHeight is Magelight.json's
    // (backbuffer px at 1080 lines), scaled to the backbuffer and divided by the view's device scale.
    inline float CursorCss(float cursorHeight, float backbufferH, float deviceScale)
    {
        const float ds = deviceScale > 0.f ? deviceScale : 1.f;
        const float bh = backbufferH > 0.f ? backbufferH : 1080.f;
        return cursorHeight * (bh / 1080.f) / ds;
    }

    // `s` cut to at most n bytes on a UTF-8 character boundary.
    inline std::string Clip(std::string s, std::size_t n = kMaxTextBytes)
    {
        if (s.size() <= n) return s;
        std::size_t cut = n;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s.resize(cut);
        return s;
    }

    // The JSON the page core parses: {"t":"<text>","c":<cursor height in CSS px>}. "" text hides the tooltip.
    inline std::string Payload(std::string_view text, float cursorCss)
    {
        std::string out = "{\"t\":\"";
        for (unsigned char c : Clip(std::string(text))) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
            }
        }
        char num[32];
        const float c = std::isfinite(cursorCss) ? std::clamp(cursorCss, 0.f, 256.f) : 0.f;
        std::snprintf(num, sizeof num, "%.1f", c);
        out += "\",\"c\":";
        out += num;
        out += "}";
        return out;
    }
}
