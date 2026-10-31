#include "PenumbraUiBackend/ChaosApplication.h"
#include "PenumbraUiBackend/NyxApplicationBridge.h"

#include "chaos/ChaosConfig.h"
#include "chaos/ChaosNyxDriver.h"

#include "Penumbra/Application.h"
#include "Penumbra/Widgets/Box.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
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

Chaos::ChaosConfig TestConfig() {
    return Chaos::ChaosConfig{Chaos::ChaosBuildTarget::UmbraEngine, "1", {}};
}

void TestBridgeConstructsChaosDriverAgainstItsOwnedRuntime() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");

    Bridge.Runtime().RegisterFunction(
        "BridgeIdentityMarker", []() -> bool { return true; });

    ExpectBridge(&Bridge.ChaosDriver().Runtime() == &Bridge.Runtime(),
                 "NyxApplicationBridge constructs ChaosNyxDriver against the bridge-owned runtime");
    ExpectBridge(Bridge.ChaosDriver().Runtime().Globals().count("BridgeIdentityMarker") == 1,
                 "registrations made through NyxApplicationBridge::Runtime are visible to its Chaos driver");
}

void TestBridgeRuntimeIsIsolatedFromASelfOwnedChaosDriver() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");
    Bridge.Runtime().RegisterFunction(
        "BridgeOnlyMarker", []() -> bool { return true; });

    Chaos::ChaosNyxDriver SelfOwnedDriver(TestConfig(), ".");

    ExpectBridge(&SelfOwnedDriver.Runtime() != &Bridge.Runtime(),
                 "a self-owned ChaosNyxDriver uses a different runtime from NyxApplicationBridge");
    ExpectBridge(SelfOwnedDriver.Runtime().Globals().count("BridgeOnlyMarker") == 0,
                 "bridge-only registrations do not leak into a self-owned ChaosNyxDriver runtime");
}

void TestBridgeLoadsNyxApplicationAgainstSharedRuntime() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");
    Bridge.Runtime().RegisterFunction(
        "HostStartResult", []() -> bool { return false; });

    Penumbra::Application* Application = Bridge.LoadApplication(
        "TestApplication :: class : Application {\n"
        "public:\n"
        "    OnStart~: () -> bool override { return HostStartResult(); }\n"
        "}\n",
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
        "TestApplication :: class : Application {\n"
        "public:\n"
        "    Configure~: () -> void override {\n"
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

// PlatformWindow has no title getter, so this reads the one open window's title from SDL.
std::string LiveWindowTitle() {
    int          Count   = 0;
    SDL_Window** Windows = SDL_GetWindows(&Count);
    std::string  Title   = (Windows && Count == 1) ? SDL_GetWindowTitle(Windows[0]) : "";
    SDL_free(Windows);
    return Title;
}

// SetWindowTitle after Configure has to reach the window Initialize() opened, as when an
// app retitles itself after loading a different file. SDL's dummy video driver gives
// a real window without a display.
template <typename BridgeT>
void TestSetWindowTitleRetitlesTheLiveWindow(const std::string& BridgeName) {
    BridgeT Bridge(TestConfig(), ".");

    auto* Application = Bridge.LoadApplication(
        "TestApplication :: class : Application {\n"
        "public:\n"
        "    Configure~: () -> void override { this.SetWindowTitle(\"Configured Title\"); }\n"
        "    OnStart~: () -> bool override {\n"
        "        this.SetWindowTitle(\"Started Title\");\n"
        "        return true;\n"
        "    }\n"
        "    Retitle~: (title: string) -> void { this.SetWindowTitle(title); }\n"
        "}\n",
        "retitle-test.nyx", "TestApplication");

    ExpectBridge(Application != nullptr, BridgeName + " loads a Nyx Application that retitles its window");
    if (!Application) return;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    const bool Initialized = Application->Initialize();
    ExpectBridge(Initialized, BridgeName + "'s application opens a window under SDL's dummy driver" +
                                  (Initialized ? std::string() : std::string(" (SDL: ") + SDL_GetError() + ")"));
    if (Initialized) {
        ExpectBridge(LiveWindowTitle() == "Started Title",
                     BridgeName + ": SetWindowTitle from OnStart retitles the window Configure named");

        const std::string Reloaded = "Pharos — vulkan-analysis";
        Bridge.CallApplicationMethod(*Application, "Retitle", {nyx::host::ToValue(Reloaded)});
        ExpectBridge(LiveWindowTitle() == Reloaded,
                     BridgeName + ": SetWindowTitle called later retitles the live window again");
        Application->Shutdown();
    }
    SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
    delete Application;
}

void TestBridgeCallApplicationMethodInvokesACustomNyxMethod() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");

    Penumbra::Application* Application = Bridge.LoadApplication(
        "TestApplication :: class : Application {\n"
        "public:\n"
        "    Double :: (value: i32) -> i32 { return value * 2; }\n"
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

    const std::filesystem::path Directory = std::filesystem::temp_directory_path() / "pub-instance-value-test";
    std::filesystem::create_directories(Directory);
    {
        std::ofstream File(Directory / "test-application.nyx");
        File << "TestApplication :: class : Application {\n"
                "public:\n"
                "    counter~ i32 = 0;\n"
                "    Bump~: () -> void { this.counter = this.counter + 1; }\n"
                "    GetCounter :: () -> i32 { return this.counter; }\n"
                "}\n";
    }
    Bridge.Runtime().SetImportSearchPaths({Directory});
    Penumbra::Application* Application =
        Bridge.LoadApplicationFromFile(Directory / "test-application.nyx", "TestApplication");

    ExpectBridge(Application != nullptr, "NyxApplicationBridge loads a Nyx Application with custom instance methods");
    if (!Application) return;

    Bridge.Runtime().RegisterFunction("GetApp", "() -> Application", [&Bridge, Application](std::vector<nyx::runtime::Value>) {
        return Bridge.GetApplicationInstanceValue(*Application);
    });

    nyx::host::NyxRuntime::NyxScope CallerScope =
        Bridge.Runtime().CreateScope("Test :: import TestApplication;\n", "caller.nyx");
    Bridge.Runtime().EvaluateInScope(CallerScope, "(GetApp() is Test).Bump()");
    Bridge.Runtime().EvaluateInScope(CallerScope, "(GetApp() is Test).Bump()");
    nyx::runtime::Value CounterResult =
        Bridge.Runtime().EvaluateInScope(CallerScope, "(GetApp() is Test).GetCounter()");

    ExpectBridge(nyx::host::FromValue<std::int32_t>(CounterResult) == 2,
                 "a Value from GetApplicationInstanceValue is callable from a different Nyx scope "
                 "by ordinary object.Method() syntax, with no forwarder registered for Bump/GetCounter");
    delete Application;
}

void TestNyxApplicationCanReadPointerStateAndHitTestAWidget() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");

    Penumbra::Application* Application = Bridge.LoadApplication(
        "TestApplication :: class : Application {\n"
        "public:\n"
        "    MouseX :: () -> f32 { return this.GetMouseX(); }\n"
        "    MouseY :: () -> f32 { return this.GetMouseY(); }\n"
        "    MouseDown :: () -> bool { return this.IsMouseButtonDown(); }\n"
        "    RootContains :: (x: f32, y: f32) -> bool { return this.GetRootWidget()!.ContainsPoint(x, y); }\n"
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

void TestNyxApplicationCanCallApplicationAccessorsAndShowOrHideAWidget() {
    PenumbraUiBackend::NyxApplicationBridge Bridge(TestConfig(), ".");

    Penumbra::Application* Application = Bridge.LoadApplication(
        "TestApplication :: class : Application {\n"
        "public:\n"
        "    WindowWidth :: () -> f32 { return this.GetWindowLogicalSize()!.X(); }\n"
        "    WindowHeight :: () -> f32 { return this.GetWindowLogicalSize()!.Y(); }\n"
        "    Dpi :: () -> f32 { return this.GetDpiScaleFactor(); }\n"
        "    HasFontBackend :: () -> bool { return this.GetFontBackend() != null; }\n"
        "    HasLifecycleRegistry :: () -> bool { return this.GetLifecycleRegistry() != null; }\n"
        "    HideRoot~: () -> void { this.GetRootWidget()!.SetVisible(false); }\n"
        "    RootVisible :: () -> bool { return this.GetRootWidget()!.IsVisible(); }\n"
        "    RootRight :: () -> f32 { guard root := this.GetRootWidget() return 0.0f; return root.GetX() + root.GetWidth(); }\n"
        "    RootBottom :: () -> f32 { guard root := this.GetRootWidget() return 0.0f; return root.GetY() + root.GetHeight(); }\n"
        "}\n",
        "accessor-test.nyx", "TestApplication");

    ExpectBridge(Application != nullptr, "NyxApplicationBridge loads a Nyx Application that calls Application's accessors");
    if (!Application) return;

    auto Call = [&](const std::string& Name) { return Bridge.CallApplicationMethod(*Application, Name); };
    auto Float = [&](const std::string& Name) {
        std::optional<nyx::runtime::Value> Result = Call(Name);
        return Result ? nyx::host::FromValue<float>(*Result) : -1.0f;
    };
    auto Bool = [&](const std::string& Name) {
        std::optional<nyx::runtime::Value> Result = Call(Name);
        return Result && nyx::host::FromValue<bool>(*Result);
    };

    ExpectBridge(Float("WindowWidth") == 0.0f && Float("WindowHeight") == 0.0f,
                 "GetWindowLogicalSize is callable from Nyx and hands back X and Y (0 with no window open)");
    ExpectBridge(Float("Dpi") == 1.0f, "GetDpiScaleFactor is callable from Nyx");
    ExpectBridge(Bool("HasFontBackend"), "GetFontBackend is callable from Nyx and hands back the font backend");
    ExpectBridge(Bool("HasLifecycleRegistry"), "GetLifecycleRegistry is callable from Nyx");

    Application->SetRootWidget(std::make_unique<Penumbra::Widgets::Box>());
    Application->GetRootWidget()->Arrange({10.0f, 20.0f, 100.0f, 50.0f});
    ExpectBridge(Float("RootRight") == 110.0f && Float("RootBottom") == 70.0f,
                 "PenumbraWidget.GetX/GetY/GetWidth/GetHeight read the widget's arranged rect");
    ExpectBridge(Bool("RootVisible"), "PenumbraWidget.IsVisible is true for a shown widget");
    Call("HideRoot");
    ExpectBridge(!Application->GetRootWidget()->GetIsVisible() && !Bool("RootVisible"),
                 "PenumbraWidget.SetVisible(false) hides the real widget");
    delete Application;
}

} // namespace

void RunNyxApplicationBridgeTests() {
    TestBridgeConstructsChaosDriverAgainstItsOwnedRuntime();
    TestBridgeRuntimeIsIsolatedFromASelfOwnedChaosDriver();
    TestBridgeLoadsNyxApplicationAgainstSharedRuntime();
    TestBridgeAppliesNyxConfigureOverrideToApplicationConfig();
    TestSetWindowTitleRetitlesTheLiveWindow<PenumbraUiBackend::NyxApplicationBridge>("NyxApplicationBridge");
    TestSetWindowTitleRetitlesTheLiveWindow<PenumbraUiBackend::ChaosApplicationBridge>("ChaosApplicationBridge");
    TestBridgeCallApplicationMethodInvokesACustomNyxMethod();
    TestGetApplicationInstanceValueLetsAnotherScopeCallCustomMethodsDirectly();
    TestNyxApplicationCanReadPointerStateAndHitTestAWidget();
    TestNyxApplicationCanCallApplicationAccessorsAndShowOrHideAWidget();
}
