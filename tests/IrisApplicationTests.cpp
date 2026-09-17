#include "PenumbraUiBackend/IrisApplication.h"
#include "PenumbraUiBackend/NyxApplicationBridge.h"

#include "Iris/IrisConfig.h"
#include "Iris/IrisNyxDriver.h"

#include "Penumbra/Application.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

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

// Same real-on-disk-fixture convention tests/WalkerTests.cpp's own TempProject uses
// (IrisNyxDriver::MountRoot needs a real `.irisx` -> IrisIrDocument -> Component pipeline,
// not a hand-built tree) -- kept local rather than shared since TempProject lives in
// WalkerTests.cpp's own anonymous namespace.
class TempProject {
public:
    TempProject() {
        Root_ = std::filesystem::temp_directory_path() / "penumbra_ui_backend_iris_application_test";
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

void TestLoadStylesheetPopulatesGetStylesheetByName() {
    TempProject Project;
    Project.Write("Test.lustre", ".foo { background-color: #FF0000; }");

    Iris::IrisNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());

    Expect(App.GetStylesheet("Test") == nullptr, "GetStylesheet returns nullptr before LoadStylesheet is called");

    const bool Loaded = App.LoadStylesheet("Test");
    Expect(Loaded, "LoadStylesheet returns true for a real .lustre file");
    const ::Lustre::StylesheetSet* Set = App.GetStylesheet("Test");
    Expect(Set != nullptr, "GetStylesheet returns a non-null set after LoadStylesheet");
    Expect(Set != nullptr && Set->Global != nullptr && !Set->Global->Rules.empty(),
           "the loaded stylesheet carries the real parsed rule");
}

void TestLoadStylesheetOfAMissingFileStillRegistersAnEmptySheet() {
    TempProject Project;
    Iris::IrisNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());

    const bool Loaded = App.LoadStylesheet("NoSuchSheet");
    Expect(Loaded, "LoadStylesheet still returns true for a missing file (LoadStylesheetFromFile's own tolerant "
                   "contract -- logs to stderr, returns an empty Stylesheet rather than failing)");
    const ::Lustre::StylesheetSet* Set = App.GetStylesheet("NoSuchSheet");
    Expect(Set != nullptr && Set->Global != nullptr && Set->Global->Rules.empty(),
           "a missing sheet is registered as empty, not left absent");
}

void TestMountComponentBuildsAWidgetAndPopulatesRefMap() {
    TempProject Project;
    Project.Write("Card.irisx", "void Card() {\n"
                                 "    render {\n"
                                 "        <Text ref=\"label\">hello</Text>\n"
                                 "    }\n"
                                 "}\n");

    Iris::IrisNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());

    std::vector<std::shared_ptr<Iris::Component>> KeepAlive;
    PenumbraUiBackend::IrisApplication::MountResult Result =
        App.MountComponent("Card.irisx", "Card", {}, "NoSuchStylesheet", KeepAlive);

    Expect(Driver.Errors().empty(), "the Card fixture compiles and mounts with no errors");
    Expect(Result.Widget != nullptr, "MountComponent returns a real built widget");
    Expect(Result.Refs.count("label") == 1, "MountComponent's RefMap captures the ref=\"label\" node");
    Expect(KeepAlive.size() == 1, "MountComponent pushes exactly one Component onto the caller's KeepAlive vector");
}

void TestMountAppRootWrapsInOverlayHostAndPopulatesGetRef() {
    TempProject Project;
    Project.Write("Root.irisx", "void Root() {\n"
                                 "    render {\n"
                                 "        <Frame ref=\"content\"></Frame>\n"
                                 "    }\n"
                                 "}\n");

    Iris::IrisNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());

    const bool Mounted = App.MountAppRoot("Root.irisx", "Root", "NoSuchStylesheet");
    Expect(Mounted, "MountAppRoot succeeds against a real fixture");
    Expect(App.GetRootWidget() != nullptr, "MountAppRoot calls SetRootWidget with a real widget");
    Expect(App.GetRef("content") != nullptr, "MountAppRoot's ref map is queryable via GetRef");
    Expect(App.GetRef("no-such-ref") == nullptr, "GetRef returns nullptr for an unknown ref name");

    App.TeardownRootWidget();
    Expect(App.GetRootWidget() == nullptr, "TeardownRootWidget clears the root widget");
    Expect(App.GetRef("content") == nullptr, "TeardownRootWidget clears the ref map too");
}

void TestMountAppRootFailsGracefullyOnAMalformedFixture() {
    TempProject Project;
    Project.Write("Broken.irisx", "this is not valid irisx source {{{\n");

    Iris::IrisNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());

    const bool Mounted = App.MountAppRoot("Broken.irisx", "Broken", "NoSuchStylesheet");
    Expect(!Mounted, "MountAppRoot returns false for a fixture that fails to compile");
    Expect(App.GetRootWidget() == nullptr, "a failed MountAppRoot never calls SetRootWidget");
}

void TestBridgedIrisApplicationExposesInheritedMethodsToNyx() {
    PenumbraUiBackend::IrisApplicationBridge Bridge(TestConfig(), ".");

    PenumbraUiBackend::IrisApplication* Application = Bridge.LoadApplication(
        "class TestApplication : Application {\n"
        "    bool OnStart() override {\n"
        "        this.LoadStylesheet(\"NoSuchSheet\");\n"
        "        return this.MountAppRoot(\"BridgedRoot.irisx\", \"BridgedRoot\", \"NoSuchSheet\");\n"
        "    }\n"
        "}\n",
        "bridged-iris-application-test.nyx", "TestApplication");

    Expect(Application != nullptr, "IrisApplicationBridge loads a Nyx Application backed by IrisApplication");
    if (Application) {
        delete Application;
    }
}

void TestRegisterInstanceForwardersDispatchesToACustomMethodByBareName() {
    PenumbraUiBackend::IrisApplicationBridge Bridge(TestConfig(), ".");

    PenumbraUiBackend::IrisApplication* Application = Bridge.LoadApplication(
        "class TestApplication : Application {\n"
        "    int LastValue = 0;\n"
        "    void Record(int value) { this.LastValue = value; }\n"
        "    int GetLastValue() { return this.LastValue; }\n"
        "}\n",
        "forwarder-test.nyx", "TestApplication");

    Expect(Application != nullptr, "IrisApplicationBridge loads a Nyx Application with a custom method to forward to");
    if (!Application) return;

    int PreDispatchCalls = 0;
    PenumbraUiBackend::RegisterInstanceForwarders(Bridge.Runtime(), Bridge, *Application, {"Record"},
                                                   [&PreDispatchCalls] { ++PreDispatchCalls; });

    // RegisterFunction stores the registered lambda directly as a NyxCallable's own
    // std::function alternative (callable.hpp's own doc comment: "a host-registered free
    // function... purely additive, so a host function can be bound into an Environment and
    // called through the same CallCallable path as a Nyx-declared one") -- invoking it
    // directly here avoids needing a live Interpreter just to call one global by name.
    Expect(Bridge.Runtime().Globals().count("Record") == 1, "RegisterInstanceForwarders registers a global named "
                                                             "after the forwarded method");
    auto Callable = std::get<std::shared_ptr<nyx::runtime::NyxCallable>>(Bridge.Runtime().Globals().at("Record").data);
    std::get<std::function<nyx::runtime::Value(std::vector<nyx::runtime::Value>)>>(Callable->declaration)(
        {nyx::host::ToValue(std::int32_t{7})});
    Expect(PreDispatchCalls == 1, "RegisterInstanceForwarders runs PreDispatch exactly once per call");

    std::optional<nyx::runtime::Value> LastValue = Bridge.CallApplicationMethod(*Application, "GetLastValue", {});
    Expect(LastValue.has_value() && nyx::host::FromValue<std::int32_t>(*LastValue) == 7,
           "the forwarded call actually reached the live Nyx instance's own method, not just PreDispatch");

    delete Application;
}

} // namespace

void RunIrisApplicationTests() {
    TestLoadStylesheetPopulatesGetStylesheetByName();
    TestLoadStylesheetOfAMissingFileStillRegistersAnEmptySheet();
    TestMountComponentBuildsAWidgetAndPopulatesRefMap();
    TestMountAppRootWrapsInOverlayHostAndPopulatesGetRef();
    TestMountAppRootFailsGracefullyOnAMalformedFixture();
    TestBridgedIrisApplicationExposesInheritedMethodsToNyx();
    TestRegisterInstanceForwardersDispatchesToACustomMethodByBareName();
}
