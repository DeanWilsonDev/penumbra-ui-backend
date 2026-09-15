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

} // namespace

void RunNyxApplicationBridgeTests() {
    TestBridgeConstructsIrisDriverAgainstItsOwnedRuntime();
    TestBridgeRuntimeIsIsolatedFromASelfOwnedIrisDriver();
    TestBridgeLoadsNyxApplicationAgainstSharedRuntime();
}
