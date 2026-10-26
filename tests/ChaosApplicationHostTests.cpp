#include "PenumbraUiBackend/ChaosApplicationHost.h"

#include <runtime/value.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

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

class HostProject {
public:
    HostProject() {
        Root_ = std::filesystem::temp_directory_path() / "penumbra_ui_backend_chaos_application_host_test";
        std::filesystem::remove_all(Root_);
        std::filesystem::create_directories(Root_ / "ui");
        std::filesystem::create_directories(Root_ / "logic");
    }
    ~HostProject() { std::filesystem::remove_all(Root_); }

    void Write(const std::filesystem::path& RelativePath, const std::string& Source) const {
        std::ofstream(Root_ / RelativePath) << Source;
    }

    PenumbraUiBackend::ChaosApplicationHostOptions Options(std::string ApplicationFile) const {
        PenumbraUiBackend::ChaosApplicationHostOptions HostOptions;
        HostOptions.UiDirectory          = Root_ / "ui";
        HostOptions.ApplicationFile      = std::move(ApplicationFile);
        HostOptions.ApplicationClass     = "HostedApplication";
        HostOptions.NyxSourceDirectories = {Root_ / "logic"};
        HostOptions.FontPath             = Root_ / "missing-font.ttf";
        return HostOptions;
    }

    std::filesystem::path Root() const { return Root_; }

private:
    std::filesystem::path Root_;
};

std::string StringOf(const std::optional<nyx::runtime::Value>& Result) {
    return Result && Result->IsString() ? std::string(Result->AsString()) : std::string{};
}

void TestHostLoadsAnApplicationThatUsesItsBindingsAndAnAppFunction() {
    HostProject Project;
    Project.Write("logic/greeting.nyx", "Greeting :: namespace {\n"
                                        "    Text :: (name: string) -> string { return `hello ${name}`; }\n"
                                        "}\n");
    Project.Write("ui/note.txt", "from a file");
    Project.Write("ui/HostedApplication.nyx",
                  "Greeting :: import Greeting;\n"
                  "HostedApplication :: class : Application {\n"
                  "public:\n"
                  "    Greet :: () -> string { return Greeting:::Text(appName()); }\n"
                  "    Note :: (path: string) -> string { return readTextFile(path); }\n"
                  "    Self :: () -> bool {\n"
                  "        if app := appInstance() as HostedApplication {\n"
                  "            return true;\n"
                  "        }\n"
                  "        return false;\n"
                  "    }\n"
                  "}\n");

    PenumbraUiBackend::ChaosApplicationHost Host(Project.Options("HostedApplication.nyx"));
    Host.Runtime().RegisterFunction("appName", []() -> std::string { return "pharos"; });
    PenumbraUiBackend::ChaosApplication* App = Host.Load();

    Expect(App != nullptr && App == Host.App(), "the host loads the application class from its UI directory");
    if (App == nullptr) {
        std::printf("%s", Host.LastDiagnostics().c_str());
        return;
    }
    Expect(StringOf(Host.Bridge().CallApplicationMethod(*App, "Greet")) == "hello pharos",
           "the application imports from a Nyx source directory and calls an app-registered host function");
    const std::string NotePath = (Project.Root() / "ui" / "note.txt").string();
    Expect(StringOf(Host.Bridge().CallApplicationMethod(*App, "Note", {nyx::runtime::Value(NotePath)})) ==
               "from a file",
           "the file binding is registered");
    const std::optional<nyx::runtime::Value> Self = Host.Bridge().CallApplicationMethod(*App, "Self");
    Expect(Self && std::holds_alternative<bool>(Self->data) && std::get<bool>(Self->data),
           "appInstance() returns the loaded application");
}

void TestHostReportsDiagnosticsWhenTheApplicationDoesNotLoad() {
    HostProject Project;
    Project.Write("ui/HostedApplication.nyx", "HostedApplication :: class : Application {\n"
                                              "    Broken :: () -> i32 { return undeclaredName; }\n"
                                              "}\n");

    PenumbraUiBackend::ChaosApplicationHost Host(Project.Options("HostedApplication.nyx"));
    Expect(Host.Load() == nullptr, "a broken application doesn't load");
    Expect(Host.LastDiagnostics().find("undeclaredName") != std::string::npos,
           "LastDiagnostics renders the error that stopped it");
}

} // namespace

void RunChaosApplicationHostTests() {
    TestHostLoadsAnApplicationThatUsesItsBindingsAndAnAppFunction();
    TestHostReportsDiagnosticsWhenTheApplicationDoesNotLoad();
}
