#include "PenumbraUiBackend/IrisApplication.h"
#include "PenumbraUiBackend/NyxApplicationBridge.h"

#include "Iris/IrisConfig.h"
#include "Iris/IrisNyxDriver.h"

#include "Penumbra/Application.h"
#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/TextArea.h"
#include "Penumbra/Widgets/TextInput.h"

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

void TestMountComponentAutoDiscoversItsOwnColocatedStylesheet() {
    TempProject Project;
    Project.Write("Solo.lustre", ".foo { background-color: #FF0000; }");
    Project.Write("Solo.irisx", "void Solo() {\n"
                                 "    render {\n"
                                 "        <Text ref=\"label\">hello</Text>\n"
                                 "    }\n"
                                 "}\n");

    Iris::IrisNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());

    Expect(App.ComposedStylesheet().Component == nullptr || App.ComposedStylesheet().Component->Rules.empty(),
           "nothing composed before the first mount");

    std::vector<std::shared_ptr<Iris::Component>> KeepAlive;
    App.MountComponent("Solo.irisx", "Solo", {}, KeepAlive);

    Expect(Driver.Errors().empty(), "the Solo fixture compiles and mounts with no errors");
    Expect(App.ComposedStylesheet().Component != nullptr && !App.ComposedStylesheet().Component->Rules.empty(),
           "MountComponent auto-discovers Solo.irisx's own colocated Solo.lustre with no explicit call");
}

void TestMountComponentDiscoversAnImportedComponentsStylesheetInASeparateDirectoryTransitively() {
    TempProject Project;
    const std::filesystem::path NestedDir = std::filesystem::path(Project.RootPath()) / "nested";
    std::filesystem::create_directories(NestedDir);
    std::ofstream(NestedDir / "Child.lustre") << ".child { background-color: #00FF00; }";
    std::ofstream(NestedDir / "Child.irisx") << "void Child() {\n"
                                                 "    render {\n"
                                                 "        <Text>child</Text>\n"
                                                 "    }\n"
                                                 "}\n";
    Project.Write("Parent.irisx", "import Child\n"
                                   "void Parent() {\n"
                                   "    render {\n"
                                   "        <Child />\n"
                                   "    }\n"
                                   "}\n");

    Iris::IrisConfig Config = TestConfig();
    Config.SearchPaths      = {"demo", "nested"};
    Iris::IrisNyxDriver Driver(Config, Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());

    std::vector<std::shared_ptr<Iris::Component>> KeepAlive;
    App.MountComponent("Parent.irisx", "Parent", {}, KeepAlive);

    Expect(Driver.Errors().empty(), "Parent.irisx (importing Child from a separate directory) mounts with no errors");
    Expect(App.ComposedStylesheet().Component != nullptr && !App.ComposedStylesheet().Component->Rules.empty(),
           "mounting Parent.irisx also discovers Child.irisx's own directory (nested/, not demo/) via the import "
           "graph, before Child is ever actually invoked as a <Slot>-mediated child");
}

void TestStylesFromTwoIndependentMountsInSeparateDirectoriesAccumulateRatherThanReplace() {
    TempProject Project;
    Project.Write("First.lustre", ".first { background-color: #FF0000; }");
    Project.Write("First.irisx", "void First() {\n"
                                  "    render {\n"
                                  "        <Text>first</Text>\n"
                                  "    }\n"
                                  "}\n");
    const std::filesystem::path NestedDir = std::filesystem::path(Project.RootPath()) / "nested";
    std::filesystem::create_directories(NestedDir);
    std::ofstream(NestedDir / "Second.lustre") << ".second { background-color: #0000FF; }";
    std::ofstream(NestedDir / "Second.irisx") << "void Second() {\n"
                                                  "    render {\n"
                                                  "        <Text>second</Text>\n"
                                                  "    }\n"
                                                  "}\n";

    Iris::IrisConfig Config = TestConfig();
    Config.SearchPaths      = {"demo", "nested"};
    Iris::IrisNyxDriver Driver(Config, Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());

    std::vector<std::shared_ptr<Iris::Component>> KeepAlive;
    App.MountComponent("First.irisx", "First", {}, KeepAlive);
    const std::size_t RuleCountAfterFirst = App.ComposedStylesheet().Component->Rules.size();

    PenumbraUiBackend::IrisApplication::MountResult SecondResult =
        App.MountComponent("../nested/Second.irisx", "Second", {}, KeepAlive);
    const std::size_t RuleCountAfterSecond = App.ComposedStylesheet().Component->Rules.size();

    Expect(SecondResult.Widget != nullptr, "the second, separately-directoried fixture mounts too");
    Expect(RuleCountAfterSecond > RuleCountAfterFirst,
           "a later, independent MountComponent call for a component in a different directory adds its own rules "
           "on top of an earlier mount's, rather than replacing them");
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
    PenumbraUiBackend::IrisApplication::MountResult Result = App.MountComponent("Card.irisx", "Card", {}, KeepAlive);

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

    const bool Mounted = App.MountAppRoot("Root.irisx", "Root");
    Expect(Mounted, "MountAppRoot succeeds against a real fixture");
    Expect(App.GetRootWidget() != nullptr, "MountAppRoot calls SetRootWidget with a real widget");
    Expect(App.GetRef("content") != nullptr, "MountAppRoot's ref map is queryable via GetRef");
    Expect(App.GetRef("no-such-ref") == nullptr, "GetRef returns nullptr for an unknown ref name");

    App.TeardownRootWidget();
    Expect(App.GetRootWidget() == nullptr, "TeardownRootWidget clears the root widget");
    Expect(App.GetRef("content") == nullptr, "TeardownRootWidget clears the ref map too");
}

void TestMountAppRootGivesTextFieldsAFocusStateAndAClipboard() {
    TempProject Project;
    Project.Write("Fields.lustre", ".fields { display: stack; flex-direction: column; }\n"
                                    ".editor { width: 200px; height: 100px; }\n");
    Project.Write("Fields.irisx", "void Fields() {\n"
                                   "    render {\n"
                                   "        <Frame class=\"fields\">\n"
                                   "            <Input ref=\"field\" />\n"
                                   "            <TextArea ref=\"editor\" class=\"editor\" />\n"
                                   "        </Frame>\n"
                                   "    }\n"
                                   "}\n");

    Iris::IrisNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());
    Expect(App.MountAppRoot("Fields.irisx", "Fields"), "MountAppRoot mounts a root holding an <Input> and a <TextArea>");

    const auto* Field = dynamic_cast<Penumbra::Widgets::TextInput*>(App.GetRef("field"));
    auto* Editor = dynamic_cast<Penumbra::Widgets::TextArea*>(App.GetRef("editor"));
    Expect(Field != nullptr && Field->Focus != nullptr && Field->Clipboard != nullptr,
           "a mounted <Input> gets a FocusState and a clipboard, so it can be clicked into and typed in");
    Expect(Editor != nullptr && Editor->Focus != nullptr && Editor->Clipboard != nullptr,
           "a mounted <TextArea> gets a FocusState and a clipboard, so it can be clicked into and typed in");
    Expect(Field != nullptr && Editor != nullptr && Field->Focus == Editor->Focus,
           "every text field in one application shares a single FocusState");

    if (Editor != nullptr) {
        Penumbra::Widgets::WidgetBase* Root = App.GetRootWidget();
        Root->Measure({800.0f, 600.0f});
        Root->Arrange({0.0f, 0.0f, 800.0f, 600.0f});
        const Penumbra::Rect Rect = Editor->GetArrangedRect();
        const Penumbra::Point Inside{Rect.X + Rect.W / 2.0f, Rect.Y + Rect.H / 2.0f};

        Penumbra::Platform::InputState Click;
        Click.MousePosition = Inside;
        Click.MouseButtonPressedThisFrame[0] = true;
        Click.MouseButtonDown[0] = true;
        Root->UpdateInteractionState(Click);

        Penumbra::Platform::InputState Typing;
        Typing.MousePosition = Inside;
        Typing.TextInputThisFrame = "typed";
        Root->UpdateInteractionState(Typing);
        Expect(Editor->Text == "typed", "clicking a mounted <TextArea> focuses it, and typed text lands in it");
    }
    App.TeardownRootWidget();
}

void TestMountReconciledComponentMountsResolvesSlotsAndReplacesOnRemount() {
    TempProject Project;
    Project.Write("Root.irisx", "void Root() {\n"
                                 "    render {\n"
                                 "        <Frame ref=\"content\"></Frame>\n"
                                 "    }\n"
                                 "}\n");
    Project.Write("CardListThree.irisx",
                   "void CardListThree() {\n"
                   "    render {\n"
                   "        <Frame>\n"
                   "            <Slot>\n"
                   "                !{() -> {\n"
                   "                    Array<string> names = [\"Ann\", \"Bo\", \"Cy\"];\n"
                   "                    return names.Map((string item) -> <Frame class={item} />);\n"
                   "                }}\n"
                   "            </Slot>\n"
                   "        </Frame>\n"
                   "    }\n"
                   "}\n");
    Project.Write("CardListTwo.irisx",
                   "void CardListTwo() {\n"
                   "    render {\n"
                   "        <Frame>\n"
                   "            <Slot>\n"
                   "                !{() -> {\n"
                   "                    Array<string> names = [\"Dee\", \"Eff\"];\n"
                   "                    return names.Map((string item) -> <Frame class={item} />);\n"
                   "                }}\n"
                   "            </Slot>\n"
                   "        </Frame>\n"
                   "    }\n"
                   "}\n");

    Iris::IrisNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());
    App.MountAppRoot("Root.irisx", "Root");

    const bool MountedFirst = App.MountReconciledComponent("CardListThree.irisx", "CardListThree", {}, "content");
    Expect(MountedFirst, "MountReconciledComponent mounts against a real fixture");

    auto* Content = dynamic_cast<Penumbra::Widgets::Box*>(App.GetRef("content"));
    Expect(Content != nullptr, "the app root's \"content\" ref resolves to a Box");
    Expect(Content != nullptr && Content->GetChildCount() == 1,
           "the mounted component's own root becomes content's sole child");
    auto* Inner = Content ? dynamic_cast<Penumbra::Widgets::Box*>(Content->GetChildAt(0)) : nullptr;
    Expect(Inner != nullptr && Inner->GetChildCount() == 3,
           "the <Slot> .Map() over 3 names resolves into 3 real children");

    const bool MountedSecond = App.MountReconciledComponent("CardListTwo.irisx", "CardListTwo", {}, "content");
    Expect(MountedSecond, "a second MountReconciledComponent call against the same target succeeds");
    Expect(Content != nullptr && Content->GetChildCount() == 1,
           "the second mount still leaves exactly one child -- the first mount's tree was cleared, not "
           "accumulated");
    Inner = Content ? dynamic_cast<Penumbra::Widgets::Box*>(Content->GetChildAt(0)) : nullptr;
    Expect(Inner != nullptr && Inner->GetChildCount() == 2, "the second mount's own <Slot> .Map() resolves into 2 "
                                                             "real children, not 3");

    App.TeardownReconciledComponent("content");
    Expect(Content != nullptr && Content->GetChildCount() == 0, "TeardownReconciledComponent clears the target with "
                                                                 "nothing mounted in its place");
}

void TestMountReconciledComponentFailsForAnUnknownTargetRef() {
    TempProject Project;
    Project.Write("Root.irisx", "void Root() {\n"
                                 "    render {\n"
                                 "        <Frame ref=\"content\"></Frame>\n"
                                 "    }\n"
                                 "}\n");
    Project.Write("Card.irisx", "void Card() {\n"
                                 "    render {\n"
                                 "        <Text>hello</Text>\n"
                                 "    }\n"
                                 "}\n");

    Iris::IrisNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());
    App.MountAppRoot("Root.irisx", "Root");

    const bool Mounted = App.MountReconciledComponent("Card.irisx", "Card", {}, "no-such-ref");
    Expect(!Mounted, "MountReconciledComponent returns false for a TargetRefName the app root never declared");
}

void TestMountAppRootFailsGracefullyOnAMalformedFixture() {
    TempProject Project;
    Project.Write("Broken.irisx", "this is not valid irisx source {{{\n");

    Iris::IrisNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());

    const bool Mounted = App.MountAppRoot("Broken.irisx", "Broken");
    Expect(!Mounted, "MountAppRoot returns false for a fixture that fails to compile");
    Expect(App.GetRootWidget() == nullptr, "a failed MountAppRoot never calls SetRootWidget");
}

void TestBridgedIrisApplicationExposesInheritedMethodsToNyx() {
    PenumbraUiBackend::IrisApplicationBridge Bridge(TestConfig(), ".");

    PenumbraUiBackend::IrisApplication* Application = Bridge.LoadApplication(
        "class TestApplication : Application {\n"
        "    bool OnStart() override {\n"
        "        return this.MountAppRoot(\"BridgedRoot.irisx\", \"BridgedRoot\");\n"
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
    TestMountComponentAutoDiscoversItsOwnColocatedStylesheet();
    TestMountComponentDiscoversAnImportedComponentsStylesheetInASeparateDirectoryTransitively();
    TestStylesFromTwoIndependentMountsInSeparateDirectoriesAccumulateRatherThanReplace();
    TestMountComponentBuildsAWidgetAndPopulatesRefMap();
    TestMountAppRootWrapsInOverlayHostAndPopulatesGetRef();
    TestMountAppRootGivesTextFieldsAFocusStateAndAClipboard();
    TestMountReconciledComponentMountsResolvesSlotsAndReplacesOnRemount();
    TestMountReconciledComponentFailsForAnUnknownTargetRef();
    TestMountAppRootFailsGracefullyOnAMalformedFixture();
    TestBridgedIrisApplicationExposesInheritedMethodsToNyx();
    TestRegisterInstanceForwardersDispatchesToACustomMethodByBareName();
}
