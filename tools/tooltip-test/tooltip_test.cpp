// The host half of page tooltips (src/TooltipCore.h, 1.2.0), on the desktop: build.bat builds and runs this;
// exit 0 when every check passes.

#include "TooltipCore.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace Magelight::Tooltip;

static int s_failed = 0;

static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++s_failed;
}

int main()
{
    std::vector<std::pair<int, std::string>> list;
    Coalesce(list, 1, std::string("Save"));
    Coalesce(list, 2, std::string("Close"));
    Coalesce(list, 1, std::string(""));
    Check(list.size() == 2 && list[0].first == 1 && list[0].second.empty() && list[1].second == "Close",
          "coalesce: the last change per view wins, in first-change order, and a clear is kept");

    Check(std::fabs(CursorCss(24.f, 2160.f, 2.f) - 24.f) < 0.01f, "cursor: 24 px at 1080 lines is 48 backbuffer px at 4K, 24 CSS px at scale 2");
    Check(std::fabs(CursorCss(24.f, 1080.f, 0.f) - 24.f) < 0.01f, "cursor: a zero device scale reads as 1");
    Check(std::fabs(CursorCss(24.f, 0.f, 1.f) - 24.f) < 0.01f, "cursor: an unknown backbuffer reads as 1080 lines");

    Check(Payload("Save the game", 24.f) == "{\"t\":\"Save the game\",\"c\":24.0}", "payload: plain text");
    Check(Payload("", 12.5f) == "{\"t\":\"\",\"c\":12.5}", "payload: a clear");
    Check(Payload("a\"b\\c\nd\te", 0.f) == "{\"t\":\"a\\\"b\\\\c\\nd\\te\",\"c\":0.0}", "payload: quotes, backslashes, newlines and tabs are escaped");
    Check(Payload(std::string("x\x01y"), 0.f) == "{\"t\":\"x\\u0001y\",\"c\":0.0}", "payload: other control characters are \\u escapes");
    Check(Payload("caf\xC3\xA9", 0.f) == "{\"t\":\"caf\xC3\xA9\",\"c\":0.0}", "payload: UTF-8 passes through");
    Check(Payload("x", NAN) == "{\"t\":\"x\",\"c\":0.0}" && Payload("x", 9999.f) == "{\"t\":\"x\",\"c\":256.0}",
          "payload: the cursor height is finite and capped");

    const std::string longText(kMaxTextBytes + 50, 'a');
    Check(Clip(longText).size() == kMaxTextBytes, "clip: a long title is cut to the cap");
    Check(Clip(std::string("\xC3\xA9\xC3\xA9"), 3) == "\xC3\xA9", "clip: never splits a UTF-8 character");

    std::printf("%s\n", s_failed ? "FAILED" : "all passed");
    return s_failed ? 1 : 0;
}
