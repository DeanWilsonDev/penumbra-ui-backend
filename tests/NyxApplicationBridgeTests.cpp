#include "PenumbraUiBackend/NyxApplicationBridge.h"

#include "Iris/IrisConfig.h"
#include "Iris/IrisNyxDriver.h"

#include "Penumbra/Application.h"
#include "Penumbra/Widgets/Box.h"

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

// GetApplicationInstanceValue's own real production shape: a native primitive (here,
// "GetApp") hands the live instance back to a *different* Nyx scope than the one that
// declared its class, which then calls a custom instance method on it directly
// (`GetApp().Bump()`) -- no CallApplicationMethod/RegisterInstanceForwarders round trip
// through C++ at all. Exercises nyx-proto's own cross-interpreter instance-method
// dispatch (main-cpp-reduction's "let a Nyx script call another script's custom instance
// methods without a native forwarder per method" gap) through this bridge's real API,
// the same path CairnAppLib.cpp's own "App()" primitive uses.
void TestGetApplicationInstanceValueLetsAnotherScopeCallCustomMethodsDirectly() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");

    Penumbra::Application* Application = Bridge.LoadApplication(
        "class TestApplication : Application {\n"
        "    int counter = 0;\n"
        "    void Bump() { this.counter = this.counter + 1; }\n"
        "    int GetCounter() { return this.counter; }\n"
        "}\n",
        "instance-value-test.nyx", "TestApplication");

    ExpectBridge(Application != nullptr, "NyxApplicationBridge loads a Nyx Application with custom instance methods");
    if (!Application) return;

    Bridge.Runtime().RegisterFunction("GetApp", [&Bridge, Application](std::vector<nyx::runtime::Value>) {
        return Bridge.GetApplicationInstanceValue(*Application);
    });

    // A second scope, parsed and interpreted independently of the one TestApplication was
    // declared in (mirrors Iris's own per-.irisx-file GetFileScope) -- it never mentions
    // TestApplication in its own source at all.
    nyx::host::NyxRuntime::NyxScope CallerScope = Bridge.Runtime().CreateScope("", "caller.nyx");
    Bridge.Runtime().EvaluateInScope(CallerScope, "GetApp().Bump()");
    Bridge.Runtime().EvaluateInScope(CallerScope, "GetApp().Bump()");
    nyx::runtime::Value CounterResult = Bridge.Runtime().EvaluateInScope(CallerScope, "GetApp().GetCounter()");

    ExpectBridge(nyx::host::FromValue<std::int32_t>(CounterResult) == 2,
                 "a Value from GetApplicationInstanceValue is callable from a different Nyx scope "
                 "by ordinary object.Method() syntax, with no forwarder registered for Bump/GetCounter");
    delete Application;
}

void TestNyxApplicationCanReadPointerStateAndHitTestAWidget() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");

    Penumbra::Application* Application = Bridge.LoadApplication(
        "class TestApplication : Application {\n"
        "    float MouseX() { return this.GetMouseX(); }\n"
        "    float MouseY() { return this.GetMouseY(); }\n"
        "    bool MouseDown() { return this.IsMouseButtonDown(); }\n"
        "    bool RootContains(float x, float y) { return this.GetRootWidget().ContainsPoint(x, y); }\n"
        "}\n",
        "pointer-test.nyx", "TestApplication");

    ExpectBridge(Application != nullptr, "NyxApplicationBridge loads a Nyx Application that reads pointer state");
    if (!Application) return;

    auto Call = [&](const std::string& Name, std::vector<nyx::runtime::Value> Args = {}) {
        return Bridge.CallApplicationMethod(*Application, Name, std::move(Args));
    };

    std::optional<nyx::runtime::Value> X = Call("MouseX");
    std::optional<nyx::runtime::Value> Y = Call("MouseY");
    std::optional<nyx::runtime::Value> Down = Call("MouseDown");
    ExpectBridge(X && Y && nyx::host::FromValue<float>(*X) == 0.0f && nyx::host::FromValue<float>(*Y) == 0.0f,
                 "GetMouseX/GetMouseY are callable from Nyx and read the frame's InputState");
    ExpectBridge(Down && !nyx::host::FromValue<bool>(*Down),
                 "IsMouseButtonDown is callable from Nyx and reads the frame's InputState");

    Application->SetRootWidget(std::make_unique<Penumbra::Widgets::Box>());
    Application->GetRootWidget()->Arrange({10.0f, 20.0f, 100.0f, 50.0f});
    auto Contains = [&](float PointX, float PointY) {
        std::optional<nyx::runtime::Value> Result =
            Call("RootContains", {nyx::host::ToValue(PointX), nyx::host::ToValue(PointY)});
        return Result && nyx::host::FromValue<bool>(*Result);
    };
    ExpectBridge(Contains(10.0f, 20.0f) && Contains(60.0f, 45.0f) && Contains(109.0f, 69.0f),
                 "PenumbraWidget.ContainsPoint is true inside the widget's arranged rect");
    ExpectBridge(!Contains(9.0f, 45.0f) && !Contains(110.0f, 45.0f) && !Contains(60.0f, 70.0f),
                 "PenumbraWidget.ContainsPoint is false outside it, with the right/bottom edges exclusive");
    delete Application;
}

} // namespace

void RunNyxApplicationBridgeTests() {
    TestBridgeConstructsIrisDriverAgainstItsOwnedRuntime();
    TestBridgeRuntimeIsIsolatedFromASelfOwnedIrisDriver();
    TestBridgeLoadsNyxApplicationAgainstSharedRuntime();
    TestBridgeAppliesNyxConfigureOverrideToApplicationConfig();
    TestBridgeCallApplicationMethodInvokesACustomNyxMethod();
    TestGetApplicationInstanceValueLetsAnotherScopeCallCustomMethodsDirectly();
    TestNyxApplicationCanReadPointerStateAndHitTestAWidget();
}
