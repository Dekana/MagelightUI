// stack_probe - the stack stress tests (MgStackTest/views/test/stack-tests.js) in Ultralight, offline, on a thread
// with the game's main-thread stack (1 MB reserve, from SkyrimSE.exe's header) of which <usedKB> is already in use,
// the way Magelight ran pages under the game's frame before 1.3.0. <reserveKB> > 0 passes a JavaScript stack reserve
// (JSC_ environment variables through ucrtbase's _putenv_s, before Renderer::Create; the hard zone two thirds of it,
// "auto" two fifths of the stack left, 128..384 KB: an approach set aside, see README.md); 0 keeps WebKit's
// defaults. One line per test; a test that runs the real stack out ends the process with 0xC00000FD.
// [guaranteeKB] sets a stack guarantee on the thread first (SetThreadStackGuarantee), as a plugin may on the game's
// main thread.
//   build.bat, then: stack_probe.exe <usedKB> <reserveKB|auto|0> <sdkDir> <stack-tests.js> [guaranteeKB]

#include <windows.h>
#include <malloc.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <AppCore/Platform.h>
#include <Ultralight/Ultralight.h>

using namespace ultralight;

namespace
{
    int         g_usedKb = 0;
    int         g_reserveKb = 0;
    int         g_guaranteeKb = 0;
    std::string g_sdk;
    std::string g_script;
    RefPtr<Renderer> g_renderer;
    RefPtr<View>     g_view;
    bool             g_ready = false;

    class Loader : public LoadListener
    {
    public:
        void OnDOMReady(View*, uint64_t, bool isMainFrame, const String&) override
        {
            if (isMainFrame) g_ready = true;
        }
    } g_loader;

    // kb 0 removes the variables.
    void SetReserve(int kb)
    {
        using PutEnvFn = int(__cdecl*)(const char*, const char*);
        HMODULE ucrt = GetModuleHandleW(L"ucrtbase.dll");
        if (!ucrt) ucrt = LoadLibraryW(L"ucrtbase.dll");
        const auto putEnv = ucrt ? reinterpret_cast<PutEnvFn>(GetProcAddress(ucrt, "_putenv_s")) : nullptr;
        if (!putEnv) {
            std::printf("ucrtbase's _putenv_s not found\n");
            return;
        }
        const unsigned soft = static_cast<unsigned>(kb) * 1024u;
        const std::string softText = kb ? std::to_string(soft) : std::string();
        const std::string hardText = kb ? std::to_string(soft / 3 * 2) : std::string();
        putEnv("JSC_reservedZoneSize", hardText.c_str());
        putEnv("JSC_softReservedZoneSize", softText.c_str());
    }

    // Test `index` as one result line. The C++ objects live here, below the SEH frame in GuardedRun.
    void RunTestAt(int index, char* out, std::size_t outSize)
    {
        const std::string js = "(function(){var t=MgStackTests.tests[" + std::to_string(index) + "];if(!t)return '';"
                               "var r=MgStackTests.runOne(t);return [MgStackTests.passed(r)?'ok  ':'FAIL',r.id,"
                               "r.outcome,'depth '+r.depth,r.ms+' ms',r.extra].join('  ');})()";
        String exception;
        const String result = g_view->EvaluateScript(String(js.c_str()), &exception);
        std::string line = result.utf8().data();
        if (!exception.empty()) line += std::string("  (exception: ") + exception.utf8().data() + ")";
        strncpy_s(out, outSize, line.c_str(), _TRUNCATE);
    }

    // Uses about `kb` of stack in 16 KB frames, then runs the test at that depth.
    __declspec(noinline) void Deep(int kb, int index, char* out, std::size_t outSize)
    {
        if (kb > 0) {
            volatile char* p = static_cast<volatile char*>(_alloca(16 * 1024));
            p[0] = 1;
            p[16 * 1024 - 1] = 1;
            Deep(kb - 16, index, out, outSize);
            p[1] = p[0];   // keeps the frame live past the call (no tail call)
            return;
        }
        RunTestAt(index, out, outSize);
    }

    int GuardedRun(int index, char* out, std::size_t outSize)
    {
        __try {
            Deep(g_usedKb, index, out, outSize);
            return 0;
        } __except (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW ? EXCEPTION_EXECUTE_HANDLER
                                                                     : EXCEPTION_CONTINUE_SEARCH) {
            return 1;
        }
    }

    DWORD WINAPI ProbeThread(void*)
    {
        if (g_guaranteeKb > 0) {
            ULONG guarantee = static_cast<ULONG>(g_guaranteeKb) * 1024;
            SetThreadStackGuarantee(&guarantee);
        }
        ULONG held = 0;   // 0 reads it back, as Magelight's log does
        SetThreadStackGuarantee(&held);
        std::printf("stack guarantee on the thread: %lu KB\n", held / 1024);
        if (g_reserveKb > 0) SetReserve(g_reserveKb);
        Config cfg;
        Platform::instance().set_config(cfg);
        Platform::instance().set_font_loader(GetPlatformFontLoader());
        Platform::instance().set_file_system(GetPlatformFileSystem(String(g_sdk.c_str())));
        Platform::instance().set_logger(GetDefaultLogger("stack_probe_ul.log"));
        g_renderer = Renderer::Create();
        if (g_reserveKb > 0) SetReserve(0);
        if (!g_renderer) {
            std::printf("the renderer could not be created\n");
            return 2;
        }
        ViewConfig vc;
        vc.is_accelerated = false;
        g_view = g_renderer->CreateView(320, 240, vc, nullptr);
        g_view->set_load_listener(&g_loader);
        g_view->LoadHTML("<html><body>stack probe</body></html>");
        for (int i = 0; i < 1000 && !g_ready; ++i) {
            g_renderer->Update();
            g_renderer->Render();
            Sleep(5);
        }
        if (!g_ready) {
            std::printf("the page never loaded\n");
            return 2;
        }
        String exception;
        g_view->EvaluateScript(String(g_script.c_str()), &exception);
        if (!exception.empty()) {
            std::printf("stack-tests.js threw: %s\n", exception.utf8().data());
            return 2;
        }
        const int count = std::atoi(g_view->EvaluateScript("String(MgStackTests.tests.length)").utf8().data());
        std::printf("used %d KB of a 1 MB stack, JavaScript reserve: %s\n", g_usedKb,
                    g_reserveKb ? (std::to_string(g_reserveKb) + " KB").c_str() : "WebKit's defaults");
        std::fflush(stdout);
        static char line[2048];
        for (int i = 0; i < count; ++i) {
            line[0] = 0;
            std::printf("  running test %d...\r", i);
            std::fflush(stdout);
            if (GuardedRun(i, line, sizeof line)) {
                std::printf("STACK OVERFLOW in test %d - the probe stops\n", i);
                std::fflush(stdout);
                ExitProcess(3);
            }
            std::printf("%s\n", line);
            std::fflush(stdout);
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::printf("usage: stack_probe <usedKB> <reserveKB|auto|0> <sdkDir> <stack-tests.js> [guaranteeKB]\n");
        return 1;
    }
    g_usedKb = std::atoi(argv[1]);
    // "auto": two fifths of the stack left where pages run, 128..384 KB.
    if (std::string(argv[2]) == "auto") {
        const int left = 1024 - g_usedKb;
        g_reserveKb = left * 2 / 5 < 128 ? 128 : left * 2 / 5 > 384 ? 384 : left * 2 / 5;
    } else {
        g_reserveKb = std::atoi(argv[2]);
    }
    g_sdk = argv[3];
    if (argc > 5) g_guaranteeKb = std::atoi(argv[5]);
    std::ifstream in(argv[4], std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    g_script = ss.str();
    if (g_script.empty()) {
        std::printf("could not read %s\n", argv[4]);
        return 1;
    }
    // The game's main thread: a 1 MB reserve.
    HANDLE thread = CreateThread(nullptr, 1u << 20, ProbeThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!thread) return 1;
    WaitForSingleObject(thread, INFINITE);
    DWORD code = 0;
    GetExitCodeThread(thread, &code);
    return static_cast<int>(code);
}
