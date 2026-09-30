#include "PenumbraUiBackend/IrisApplication.h"
#include "PenumbraUiBackend/NyxApplicationBridge.h"

#include "Iris/IrisConfig.h"
#include "Iris/IrisNyxDriver.h"

#include "Penumbra/Application.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unistd.h>

extern int Failures;

namespace {

void Expect(bool Condition, const std::string& Description) {
    if (Condition) {
        std::printf("[PASS] %s\n", Description.c_str());
    } else {
        std::printf("[FAIL] %s\n", Description.c_str());
        ++Failures;
    }
}

std::string CaptureStderr(const std::function<void()>& Fn) {
    std::fflush(stderr);
    const int SavedFd = dup(fileno(stderr));
    char TmpPath[] = "/tmp/penumbra_ui_backend_stderr_capture_XXXXXX";
    const int TmpFd = mkstemp(TmpPath);
    dup2(TmpFd, fileno(stderr));
    close(TmpFd);

    Fn();

    std::fflush(stderr);
    dup2(SavedFd, fileno(stderr));
    close(SavedFd);

    std::ostringstream Captured;
    if (FILE* TmpFile = std::fopen(TmpPath, "r")) {
        char Buffer[512];
        std::size_t Read = 0;
        while ((Read = std::fread(Buffer, 1, sizeof(Buffer), TmpFile)) > 0) {
            Captured.write(Buffer, static_cast<std::streamsize>(Read));
        }
        std::fclose(TmpFile);
    }
    std::remove(TmpPath);
    return Captured.str();
}

bool Contains(const std::string& Haystack, const std::string& Needle) {
    return Haystack.find(Needle) != std::string::npos;
}

class TempProject {
public:
    TempProject() {
        Root_ = std::filesystem::temp_directory_path() / "penumbra_ui_backend_error_reporting_test";
        std::filesystem::remove_all(Root_);
        std::filesystem::create_directories(Root_ / "demo");
    }
    ~TempProject() { std::filesystem::remove_all(Root_); }

    std::string Write(const std::string& Name, std::string_view Source) {
        const std::filesystem::path Path = Root_ / "demo" / Name;
        std::ofstream(Path) << Source;
        return Path.string();
    }

    std::string RootPath() const { return Root_.string(); }
    std::string UiDir() const { return (Root_ / "demo").string(); }

private:
    std::filesystem::path Root_;
};

Iris::IrisConfig TestConfig() {
    Iris::IrisConfig Config;
    Config.Target      = Iris::IrisBuildTarget::UmbraEngine;
    Config.SearchPaths = {"demo"};
    return Config;
}

void TestTickIrisReportsASlotErrorOnceAndRemountStillSucceeds() {
    TempProject Project;
    Project.Write("Root.irisx", "string Explode() {\n"
                                 "    string s = \"text\";\n"
                                 "    bool b = !s;\n"
                                 "    return \"never\";\n"
                                 "}\n"
                                 "\n"
                                 "void Root() {\n"
                                 "    @signal int count = 0;\n"
                                 "\n"
                                 "    render {\n"
                                 "        <Frame ref=\"button\" onPress={() -> { count = count + 1; }}>\n"
                                 "            <Slot>\n"
                                 "                !{() -> count == 0 ? <Frame class=\"ok\" /> : <Frame class={Explode()} />}\n"
                                 "            </Slot>\n"
                                 "        </Frame>\n"
                                 "    }\n"
                                 "}\n");

    Iris::IrisNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());

    bool Mounted = false;
    const std::string MountOutput = CaptureStderr([&]() { Mounted = App.MountAppRoot("Root.irisx", "Root"); });
    Expect(Mounted && !Contains(MountOutput, "[IrisApplication]"), "a clean mount succeeds and reports no errors");

    Penumbra::Widgets::WidgetBase* Button = App.GetRef("button");
    Expect(Button != nullptr && Button->OnPressed != nullptr, "the mounted button carries its onPress handler");
    if (!Button || !Button->OnPressed) return;

    Button->OnPressed();
    const std::string TickOutput = CaptureStderr([&]() { App.TickIris(); });
    Expect(Contains(TickOutput, "[IrisApplication]") && Contains(TickOutput, "Root.irisx") &&
               Contains(TickOutput, "bool"),
           "TickIris reports a Nyx error raised while a <Slot> re-renders, with its file");

    const std::string SecondTickOutput = CaptureStderr([&]() { App.TickIris(); });
    Expect(!Contains(SecondTickOutput, "[IrisApplication]"), "an error already reported is not reported again on the next tick");

    bool Remounted = false;
    CaptureStderr([&]() { Remounted = App.MountAppRoot("Root.irisx", "Root"); });
    Expect(Remounted, "MountAppRoot still succeeds after an earlier, unrelated error was recorded");
}

void TestIrisApplicationBridgeReportsLifecycleHookErrors() {
    PenumbraUiBackend::IrisApplicationBridge Bridge(TestConfig(), ".");
    PenumbraUiBackend::IrisApplication* Application = Bridge.LoadApplication(
        "class TestApplication : Application {\n"
        "    bool OnStart() override {\n"
        "        string s = \"start\";\n"
        "        bool b = !s;\n"
        "        return true;\n"
        "    }\n"
        "    void OnUpdate(float deltaSeconds) override {\n"
        "        string s = \"update\";\n"
        "        bool b = !s;\n"
        "    }\n"
        "}\n",
        "hook-error-test.nyx", "TestApplication");
    Expect(Application != nullptr, "IrisApplicationBridge loads an Application whose hooks raise");
    if (!Application) return;

    bool Started = true;
    const std::string StartOutput = CaptureStderr([&]() { Started = Application->OnStart(); });
    Expect(!Started, "OnStart returns false when the Nyx override raises");
    Expect(Contains(StartOutput, "OnStart raised Error::"), "OnStart reports the error it raised");

    const std::string FirstUpdate = CaptureStderr([&]() { Application->OnUpdate(0.016f); });
    const std::string SecondUpdate = CaptureStderr([&]() { Application->OnUpdate(0.016f); });
    Expect(Contains(FirstUpdate, "OnUpdate raised Error::"), "OnUpdate reports the error it raised");
    Expect(SecondUpdate.empty(), "the same OnUpdate error raised again on the next frame is not repeated");

    delete Application;
}

void TestNyxApplicationBridgeReportsLifecycleHookErrors() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");
    Penumbra::Application* Application = Bridge.LoadApplication(
        "class TestApplication : Application {\n"
        "    void OnShutdown() override {\n"
        "        string s = \"shutdown\";\n"
        "        bool b = !s;\n"
        "    }\n"
        "}\n",
        "plain-hook-error-test.nyx", "TestApplication");
    Expect(Application != nullptr, "NyxApplicationBridge loads an Application whose hook raises");
    if (!Application) return;

    const std::string Output = CaptureStderr([&]() { Application->OnShutdown(); });
    Expect(Contains(Output, "OnShutdown raised Error::"), "a plain NyxApplicationBridge reports a hook's error too");

    delete Application;
}

} // namespace

void RunErrorReportingTests() {
    TestTickIrisReportsASlotErrorOnceAndRemountStillSucceeds();
    TestIrisApplicationBridgeReportsLifecycleHookErrors();
    TestNyxApplicationBridgeReportsLifecycleHookErrors();
}
