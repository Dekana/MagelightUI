// fiber_probe - stack_probe with every Ultralight call on a fiber of its own (option 2), entered from <usedKB> deep in
// a 1 MB thread the way Magelight would enter it from the game's frame. Between jobs the thread runs on its own stack
// for a while, as the game does between frames, so a collector that scanned the thread then would read the wrong
// stack. WebKit's own JavaScript stack reserve throughout; "noconcgc" sets JSC_useConcurrentGC=false first.
//   fiber_probe.exe <usedKB> <sdkDir> <tests.js> [fiberMB=8] [noconcgc|concgc] [rounds=1] [loadhere]

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
    int         g_fiberMb = 8;
    int         g_rounds = 1;
    bool        g_noConcGc = false;
    bool        g_loadHere = false;   // "loadhere": load the Ultralight DLLs on the probe thread first, as Magelight does
    std::string g_sdk;
    std::string g_script;

    RefPtr<Renderer> g_renderer;
    RefPtr<View>     g_view;
    bool             g_ready = false;
    int              g_count = 0;

    LPVOID g_mainFiber = nullptr;
    LPVOID g_ulFiber = nullptr;
    enum class Job { Init, Test, Frames, Shutdown };
    Job   g_job = Job::Init;
    int   g_testIndex = 0;
    int   g_jobResult = 0;   // 0 done, 1 stack overflow, 2 failed
    char  g_line[2048];

    class Loader : public LoadListener
    {
    public:
        void OnDOMReady(View*, uint64_t, bool isMainFrame, const String&) override
        {
            if (isMainFrame) g_ready = true;
        }
    } g_loader;

    void Frames(int n)
    {
        for (int i = 0; i < n; ++i) {
            g_renderer->Update();
            g_renderer->Render();
        }
    }

    bool InitUl()
    {
        if (g_noConcGc) {
            using PutEnvFn = int(__cdecl*)(const char*, const char*);
            HMODULE ucrt = GetModuleHandleW(L"ucrtbase.dll");
            if (!ucrt) ucrt = LoadLibraryW(L"ucrtbase.dll");
            if (auto putEnv = reinterpret_cast<PutEnvFn>(GetProcAddress(ucrt, "_putenv_s")))
                putEnv("JSC_useConcurrentGC", "false");
        }
        Config cfg;
        Platform::instance().set_config(cfg);
        Platform::instance().set_font_loader(GetPlatformFontLoader());
        Platform::instance().set_file_system(GetPlatformFileSystem(String(g_sdk.c_str())));
        Platform::instance().set_logger(GetDefaultLogger("fiber_probe_ul.log"));
        g_renderer = Renderer::Create();
        if (!g_renderer) return false;
        ViewConfig vc;
        vc.is_accelerated = false;
        g_view = g_renderer->CreateView(320, 240, vc, nullptr);
        g_view->set_load_listener(&g_loader);
        g_view->LoadHTML("<html><body>fiber probe</body></html>");
        for (int i = 0; i < 1000 && !g_ready; ++i) {
            Frames(1);
            Sleep(5);
        }
        if (!g_ready) return false;
        String exception;
        g_view->EvaluateScript(String(g_script.c_str()), &exception);
        if (!exception.empty()) {
            std::printf("tests threw: %s\n", exception.utf8().data());
            return false;
        }
        g_count = std::atoi(g_view->EvaluateScript("String(MgStackTests.tests.length)").utf8().data());
        return true;
    }

    void RunTest(int index)
    {
        const std::string js = "(function(){var t=MgStackTests.tests[" + std::to_string(index) + "];if(!t)return '';"
                               "var r=MgStackTests.runOne(t);return [MgStackTests.passed(r)?'ok  ':'FAIL',r.id,"
                               "r.outcome,'depth '+r.depth,r.ms+' ms',r.extra].join('  ');})()";
        String exception;
        const String result = g_view->EvaluateScript(String(js.c_str()), &exception);
        std::string line = result.utf8().data();
        if (!exception.empty()) line += std::string("  (exception: ") + exception.utf8().data() + ")";
        strncpy_s(g_line, sizeof g_line, line.c_str(), _TRUNCATE);
    }

    // Garbage for the collector between tests: the heap grows while the thread is away on its own stack.
    void Churn()
    {
        g_view->EvaluateScript(
            "(function(){window.__keep=window.__keep||[];for(var i=0;i<20000;i++){window.__keep.push({a:i,b:'s'+i,"
            "c:[i,i+1,i+2]});}if(window.__keep.length>200000)window.__keep=[];return 0;})()");
    }

    int DoJob()
    {
        switch (g_job) {
        case Job::Init:
            return InitUl() ? 0 : 2;
        case Job::Test:
            RunTest(g_testIndex);
            return 0;
        case Job::Frames:
            Churn();
            Frames(3);
            return 0;
        case Job::Shutdown:
            // Ultralight objects die on the thread (and stack) they live on.
            g_view = nullptr;
            g_renderer = nullptr;
            return 0;
        }
        return 2;
    }

    int GuardedJob()
    {
        __try {
            return DoJob();
        } __except (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW ? EXCEPTION_EXECUTE_HANDLER
                                                                     : EXCEPTION_CONTINUE_SEARCH) {
            return 1;
        }
    }

    VOID CALLBACK UlFiberProc(LPVOID)
    {
        for (;;) {
            g_jobResult = GuardedJob();
            SwitchToFiber(g_mainFiber);
        }
    }

    // The "game": about `kb` of the thread's own stack in use, then one Ultralight job on the fiber.
    __declspec(noinline) void DeepSwitch(int kb)
    {
        if (kb > 0) {
            volatile char* p = static_cast<volatile char*>(_alloca(16 * 1024));
            p[0] = 1;
            p[16 * 1024 - 1] = 1;
            DeepSwitch(kb - 16);
            p[1] = p[0];
            return;
        }
        SwitchToFiber(g_ulFiber);
    }

    // The game between frames: its own work, on its own stack, for a moment.
    void GameBetweenFrames()
    {
        volatile unsigned x = 0;
        const DWORD until = GetTickCount() + 15;
        while (GetTickCount() < until) {
            for (int i = 0; i < 100000; ++i) x += i;
        }
    }

    DWORD WINAPI ProbeThread(void*)
    {
        if (g_loadHere) {
            // Delay-loaded (build_delay.bat): their process attach runs here, on this thread's own stack.
            for (const wchar_t* dll : { L"UltralightCore.dll", L"WebCore.dll", L"Ultralight.dll", L"AppCore.dll" }) {
                if (!LoadLibraryW(dll)) {
                    std::printf("could not load a runtime DLL (%lu)\n", GetLastError());
                    return 2;
                }
            }
            std::printf("runtime loaded on the probe thread before the fiber\n");
        }
        g_mainFiber = ConvertThreadToFiber(nullptr);
        g_ulFiber = CreateFiberEx(64 * 1024, static_cast<SIZE_T>(g_fiberMb) << 20, FIBER_FLAG_FLOAT_SWITCH,
                                  UlFiberProc, nullptr);
        if (!g_mainFiber || !g_ulFiber) {
            std::printf("no fiber\n");
            return 2;
        }
        g_job = Job::Init;
        DeepSwitch(g_usedKb);
        if (g_jobResult) {
            std::printf("init failed (%d)\n", g_jobResult);
            return 2;
        }
        std::printf("used %d KB of a 1 MB stack, Ultralight on a %d MB fiber%s, %d round(s)\n", g_usedKb, g_fiberMb,
                    g_noConcGc ? ", concurrent GC off" : "", g_rounds);
        std::fflush(stdout);
        int failures = 0;
        for (int round = 0; round < g_rounds; ++round) {
            for (int i = 0; i < g_count; ++i) {
                g_job = Job::Test;
                g_testIndex = i;
                g_line[0] = 0;
                DeepSwitch(g_usedKb);
                if (g_jobResult == 1) {
                    std::printf("STACK OVERFLOW in test %d (round %d)\n", i, round);
                    std::fflush(stdout);
                    ExitProcess(3);
                }
                if (round == 0 || std::string(g_line).rfind("FAIL", 0) == 0) std::printf("%s\n", g_line);
                if (std::string(g_line).rfind("FAIL", 0) == 0) ++failures;
                std::fflush(stdout);
                // Frames with garbage, the thread away on its own stack in between.
                for (int f = 0; f < 4; ++f) {
                    GameBetweenFrames();
                    g_job = Job::Frames;
                    DeepSwitch(g_usedKb);
                }
            }
        }
        std::printf("done: %d round(s), %d failure(s)\n", g_rounds, failures);
        std::fflush(stdout);
        g_job = Job::Shutdown;
        DeepSwitch(g_usedKb);
        ConvertFiberToThread();
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::printf("usage: fiber_probe <usedKB> <sdkDir> <tests.js> [fiberMB] [noconcgc] [rounds]\n");
        return 1;
    }
    g_usedKb = std::atoi(argv[1]);
    g_sdk = argv[2];
    if (argc > 4) g_fiberMb = std::atoi(argv[4]);
    if (argc > 5) g_noConcGc = std::string(argv[5]) == "noconcgc";
    if (argc > 6) g_rounds = std::atoi(argv[6]);
    if (argc > 7) g_loadHere = std::string(argv[7]) == "loadhere";
    // Delay-loaded DLLs must not be loaded yet when "loadhere" asks for the probe thread to load them.
    if (g_loadHere && GetModuleHandleW(L"WebCore.dll")) std::printf("WebCore.dll is already loaded (not delay-loaded?)\n");
    std::ifstream in(argv[3], std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    g_script = ss.str();
    if (g_script.empty()) {
        std::printf("could not read %s\n", argv[3]);
        return 1;
    }
    HANDLE thread = CreateThread(nullptr, 1u << 20, ProbeThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!thread) return 1;
    WaitForSingleObject(thread, INFINITE);
    DWORD code = 0;
    GetExitCodeThread(thread, &code);
    return static_cast<int>(code);
}
