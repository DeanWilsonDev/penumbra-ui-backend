#include "PenumbraUiBackend/NyxApplicationBridge.h"

#include "Iris/IrisConfig.h"
#include "Iris/IrisNyxDriver.h"

#include "Penumbra/Application.h"

#include <cstdio>
#include <string>
#include <vector>

extern int Failures;

namespace {

void ExpectBridge(bool Condition, const std::string& Description) {
    if (Condition) {
        std::printf("[PASS] %s\n", Description.c_str());
    } else {
        std::printf("[FAIL] %s\n", Description.c_str());
        ++Failures;
    }
}

Iris::IrisConfig TestConfig() {
    return Iris::IrisConfig{Iris::IrisBuildTarget::UmbraEngine, "1", {}};
}

void TestBridgeConstructsIrisDriverAgainstItsOwnedRuntime() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");

    Bridge.Runtime().RegisterFunction(
        "BridgeIdentityMarker",
        [](std::vector<nyx::runtime::Value>) -> nyx::runtime::Value { return nyx::runtime::Value(true); });

    ExpectBridge(&Bridge.IrisDriver().Runtime() == &Bridge.Runtime(),
                 "NyxApplicationBridge constructs IrisNyxDriver against the bridge-owned runtime");
    ExpectBridge(Bridge.IrisDriver().Runtime().Globals().count("BridgeIdentityMarker") == 1,
                 "registrations made through NyxApplicationBridge::Runtime are visible to its Iris driver");
}

void TestBridgeRuntimeIsIsolatedFromASelfOwnedIrisDriver() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");
    Bridge.Runtime().RegisterFunction(
        "BridgeOnlyMarker",
        [](std::vector<nyx::runtime::Value>) -> nyx::runtime::Value { return nyx::runtime::Value(true); });

    Iris::IrisNyxDriver SelfOwnedDriver(TestConfig(), ".");

    ExpectBridge(&SelfOwnedDriver.Runtime() != &Bridge.Runtime(),
                 "a self-owned IrisNyxDriver uses a different runtime from NyxApplicationBridge");
    ExpectBridge(SelfOwnedDriver.Runtime().Globals().count("BridgeOnlyMarker") == 0,
                 "bridge-only registrations do not leak into a self-owned IrisNyxDriver runtime");
}

void TestBridgeLoadsNyxApplicationAgainstSharedRuntime() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");
    Bridge.Runtime().RegisterFunction(
        "HostStartResult", [](std::vector<nyx::runtime::Value>) -> nyx::runtime::Value {
            return nyx::host::ToValue(false);
        });

    Penumbra::Application* Application = Bridge.LoadApplication(
        "class TestApplication : Application { bool OnStart() override { return HostStartResult(); } }",
        "bridge-test.nyx", "TestApplication");

    ExpectBridge(Application != nullptr, "NyxApplicationBridge loads a Nyx Application subclass");
    if (Application) {
        ExpectBridge(!Application->OnStart(),
                     "loaded Application virtual methods use registrations from the shared runtime");
        delete Application;
    }
}

// Configure() is protected on Penumbra::Application (only Run() calls it internally,
// before window construction) -- this is the standard "using-declaration accessor" idiom
// for reaching a protected virtual from a test: ConfigureProbe doesn't override Configure,
// so &ConfigureProbe::Configure is a pointer-to-member of Application itself, callable on
// any Application& (real dynamic type included) via ordinary virtual dispatch. No object
// pointer/reference is ever reinterpreted as a different type.
struct ConfigureProbe : Penumbra::Application {
    using Penumbra::Application::Configure;
};

void TestBridgeAppliesNyxConfigureOverrideToApplicationConfig() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");

    Penumbra::Application* Application = Bridge.LoadApplication(
        "class TestApplication : Application {\n"
        "    void Configure() override {\n"
        "        this.SetWindowTitle(\"Configured Title\");\n"
        "        this.SetWindowSize(640, 480);\n"
        "    }\n"
        "}\n",
        "configure-test.nyx", "TestApplication");

    ExpectBridge(Application != nullptr, "NyxApplicationBridge loads a Configure-overriding Nyx Application");
    if (Application) {
        Penumbra::ApplicationConfig Config;
        (Application->*&ConfigureProbe::Configure)(Config);
        ExpectBridge(Config.Title == "Configured Title",
                     "a Nyx Configure() override can set the window title via SetWindowTitle");
        ExpectBridge(Config.WindowLogicalWidth == 640 && Config.WindowLogicalHeight == 480,
                     "a Nyx Configure() override can set the window size via SetWindowSize");
        delete Application;
    }
}

void TestBridgeCallApplicationMethodInvokesACustomNyxMethod() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");

    Penumbra::Application* Application = Bridge.LoadApplication(
        "class TestApplication : Application {\n"
        "    int Double(int value) { return value * 2; }\n"
        "}\n",
        "custom-method-test.nyx", "TestApplication");

    ExpectBridge(Application != nullptr, "NyxApplicationBridge loads a Nyx Application with a custom method");
    if (Application) {
        std::optional<nyx::runtime::Value> Result =
            Bridge.CallApplicationMethod(*Application, "Double", {nyx::host::ToValue(std::int32_t{21})});
        ExpectBridge(Result.has_value(), "CallApplicationMethod reaches a custom method not part of the lifecycle hooks");
        if (Result) {
            ExpectBridge(nyx::host::FromValue<std::int32_t>(*Result) == 42,
                         "CallApplicationMethod marshals arguments and the return value correctly");
        }

        std::optional<nyx::runtime::Value> Missing = Bridge.CallApplicationMethod(*Application, "NoSuchMethod", {});
        ExpectBridge(!Missing.has_value(), "CallApplicationMethod returns nullopt for a method the Nyx class doesn't define");
        delete Application;
    }
}

} // namespace

void RunNyxApplicationBridgeTests() {
    TestBridgeConstructsIrisDriverAgainstItsOwnedRuntime();
    TestBridgeRuntimeIsIsolatedFromASelfOwnedIrisDriver();
    TestBridgeLoadsNyxApplicationAgainstSharedRuntime();
    TestBridgeAppliesNyxConfigureOverrideToApplicationConfig();
    TestBridgeCallApplicationMethodInvokesACustomNyxMethod();
}
