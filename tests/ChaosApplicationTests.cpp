#include "PenumbraUiBackend/ChaosApplication.h"
#include "PenumbraUiBackend/NyxApplicationBridge.h"

#include "Chaos/ChaosConfig.h"
#include "Chaos/ChaosNyxDriver.h"

#include "Penumbra/Application.h"
#include "Penumbra/Backends/IIconBackend.h"
#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/IconWidget.h"
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
// (ChaosNyxDriver::MountRoot needs a real `.chaos` -> ChaosIrDocument -> Component pipeline,
// not a hand-built tree) -- kept local rather than shared since TempProject lives in
// WalkerTests.cpp's own anonymous namespace.
class TempProject {
public:
    TempProject() {
        Root_ = std::filesystem::temp_directory_path() / "penumbra_ui_backend_chaos_application_test";
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

Chaos::ChaosConfig TestConfig() {
    Chaos::ChaosConfig Config;
    Config.Target      = Chaos::ChaosBuildTarget::UmbraEngine;
    Config.SearchPaths = {"demo"};
    return Config;
}

void TestMountComponentAutoDiscoversItsOwnColocatedStylesheet() {
    TempProject Project;
    Project.Write("Solo.cosmos", ".foo { background-color: #FF0000; }");
    Project.Write("Solo.chaos", "Solo :: () -> Penumbra:::Component {\n"
                                 "    return (\n"
                                 "        <text ref=\"label\">hello</text>\n"
                                 "    );\n"
                                 "}\n");

    Chaos::ChaosNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());

    Expect(App.ComposedStylesheet().Component == nullptr || App.ComposedStylesheet().Component->Rules.empty(),
           "nothing composed before the first mount");

    std::vector<std::shared_ptr<Chaos::Component>> KeepAlive;
    App.MountComponent("Solo.chaos", "Solo", {}, KeepAlive);

    Expect(Driver.Errors().empty(), "the Solo fixture compiles and mounts with no errors");
    Expect(App.ComposedStylesheet().Component != nullptr && !App.ComposedStylesheet().Component->Rules.empty(),
           "MountComponent auto-discovers Solo.chaos's own colocated Solo.cosmos with no explicit call");
}

void TestMountComponentDiscoversAnImportedComponentsStylesheetInASeparateDirectoryTransitively() {
    TempProject Project;
    const std::filesystem::path NestedDir = std::filesystem::path(Project.RootPath()) / "nested";
    std::filesystem::create_directories(NestedDir);
    std::ofstream(NestedDir / "Child.cosmos") << ".child { background-color: #00FF00; }";
    std::ofstream(NestedDir / "Child.chaos") << "Child :: () -> Penumbra:::Component {\n"
                                                 "    return (\n"
                                                 "        <text>child</text>\n"
                                                 "    );\n"
                                                 "}\n";
    Project.Write("Parent.chaos", "Child :: import Child;\n"
                                   "Parent :: () -> Penumbra:::Component {\n"
                                   "    return (\n"
                                   "        <Child />\n"
                                   "    );\n"
                                   "}\n");

    Chaos::ChaosConfig Config = TestConfig();
    Config.SearchPaths      = {"demo", "nested"};
    Chaos::ChaosNyxDriver Driver(Config, Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());

    std::vector<std::shared_ptr<Chaos::Component>> KeepAlive;
    App.MountComponent("Parent.chaos", "Parent", {}, KeepAlive);

    Expect(Driver.Errors().empty(), "Parent.chaos (importing Child from a separate directory) mounts with no errors");
    Expect(App.ComposedStylesheet().Component != nullptr && !App.ComposedStylesheet().Component->Rules.empty(),
           "mounting Parent.chaos also discovers Child.chaos's own directory (nested/, not demo/) via the import "
           "graph, before Child is ever actually invoked as a <Slot>-mediated child");
}

void TestStylesFromTwoIndependentMountsInSeparateDirectoriesAccumulateRatherThanReplace() {
    TempProject Project;
    Project.Write("First.cosmos", ".first { background-color: #FF0000; }");
    Project.Write("First.chaos", "First :: () -> Penumbra:::Component {\n"
                                  "    return (\n"
                                  "        <text>first</text>\n"
                                  "    );\n"
                                  "}\n");
    const std::filesystem::path NestedDir = std::filesystem::path(Project.RootPath()) / "nested";
    std::filesystem::create_directories(NestedDir);
    std::ofstream(NestedDir / "Second.cosmos") << ".second { background-color: #0000FF; }";
    std::ofstream(NestedDir / "Second.chaos") << "Second :: () -> Penumbra:::Component {\n"
                                                  "    return (\n"
                                                  "        <text>second</text>\n"
                                                  "    );\n"
                                                  "}\n";

    Chaos::ChaosConfig Config = TestConfig();
    Config.SearchPaths      = {"demo", "nested"};
    Chaos::ChaosNyxDriver Driver(Config, Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());

    std::vector<std::shared_ptr<Chaos::Component>> KeepAlive;
    App.MountComponent("First.chaos", "First", {}, KeepAlive);
    const std::size_t RuleCountAfterFirst = App.ComposedStylesheet().Component->Rules.size();

    PenumbraUiBackend::ChaosApplication::MountResult SecondResult =
        App.MountComponent("../nested/Second.chaos", "Second", {}, KeepAlive);
    const std::size_t RuleCountAfterSecond = App.ComposedStylesheet().Component->Rules.size();

    Expect(SecondResult.Widget != nullptr, "the second, separately-directoried fixture mounts too");
    Expect(RuleCountAfterSecond > RuleCountAfterFirst,
           "a later, independent MountComponent call for a component in a different directory adds its own rules "
           "on top of an earlier mount's, rather than replacing them");
}

void TestGlobalCosmosLoadsIntoTheGlobalSlotAndItsRootVariablesReachEveryComponent() {
    TempProject Project;
    Project.Write("global.cosmos", ":root {\n"
                                   "    --accent: #CEFF00FF;\n"
                                   "}\n"
                                   ".shared { border-radius: 4px; }\n");
    Project.Write("Solo.cosmos", ".foo { background-color: var(--accent); }");
    Project.Write("Solo.chaos", "Solo :: () -> Penumbra:::Component {\n"
                                 "    return (\n"
                                 "        <frame class=\"foo\" ref=\"box\">\n"
                                 "            <text>hello</text>\n"
                                 "        </frame>\n"
                                 "    );\n"
                                 "}\n");

    Chaos::ChaosNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());

    std::vector<std::shared_ptr<Chaos::Component>> KeepAlive;
    PenumbraUiBackend::ChaosApplication::MountResult Result = App.MountComponent("Solo.chaos", "Solo", {}, KeepAlive);

    const ::Cosmos::StylesheetSet& Sheets = App.ComposedStylesheet();
    Expect(Driver.Errors().empty(), "the global.cosmos fixture compiles and mounts with no errors");
    Expect(Sheets.Global != nullptr && Sheets.Global->Root.has_value() && Sheets.Global->Root->Variables.size() == 1,
           "global.cosmos's :root variables land in the Global slot");
    Expect(Sheets.Global != nullptr && Sheets.Global->Rules.size() == 1 && Sheets.Component->Rules.size() == 1,
           "global.cosmos's rules land in the Global slot, not the component sheet");
    const auto* AsBox = dynamic_cast<Penumbra::Widgets::Box*>(Result.Refs.count("box") ? Result.Refs.at("box") : nullptr);
    Expect(AsBox != nullptr && AsBox->Style.ColorBackground.R == 0xCE && AsBox->Style.ColorBackground.G == 0xFF &&
               AsBox->Style.ColorBackground.B == 0x00,
           "a component rule's var(--accent) resolves to global.cosmos's value");
}

void TestRootVariablesInAComponentStylesheetAreKept() {
    TempProject Project;
    Project.Write("Solo.cosmos", ":root {\n"
                                 "    --tint: #123456FF;\n"
                                 "}\n"
                                 ".foo { background-color: var(--tint); }\n");
    Project.Write("Solo.chaos", "Solo :: () -> Penumbra:::Component {\n"
                                 "    return (\n"
                                 "        <frame class=\"foo\" ref=\"box\">\n"
                                 "            <text>hello</text>\n"
                                 "        </frame>\n"
                                 "    );\n"
                                 "}\n");

    Chaos::ChaosNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());

    std::vector<std::shared_ptr<Chaos::Component>> KeepAlive;
    PenumbraUiBackend::ChaosApplication::MountResult Result = App.MountComponent("Solo.chaos", "Solo", {}, KeepAlive);

    const ::Cosmos::StylesheetSet& Sheets = App.ComposedStylesheet();
    Expect(Sheets.Component != nullptr && Sheets.Component->Root.has_value() &&
               Sheets.Component->Root->Variables.size() == 1,
           "a :root block in a component stylesheet is kept in the composed sheet, not dropped");
    const auto* AsBox = dynamic_cast<Penumbra::Widgets::Box*>(Result.Refs.count("box") ? Result.Refs.at("box") : nullptr);
    Expect(AsBox != nullptr && AsBox->Style.ColorBackground.R == 0x12 && AsBox->Style.ColorBackground.G == 0x34 &&
               AsBox->Style.ColorBackground.B == 0x56,
           "var(--tint) from a component stylesheet's own :root resolves");
}

void TestMountComponentBuildsAWidgetAndPopulatesRefMap() {
    TempProject Project;
    Project.Write("Card.chaos", "Card :: () -> Penumbra:::Component {\n"
                                 "    return (\n"
                                 "        <text ref=\"label\">hello</text>\n"
                                 "    );\n"
                                 "}\n");

    Chaos::ChaosNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());

    std::vector<std::shared_ptr<Chaos::Component>> KeepAlive;
    PenumbraUiBackend::ChaosApplication::MountResult Result = App.MountComponent("Card.chaos", "Card", {}, KeepAlive);

    Expect(Driver.Errors().empty(), "the Card fixture compiles and mounts with no errors");
    Expect(Result.Widget != nullptr, "MountComponent returns a real built widget");
    Expect(Result.Refs.count("label") == 1, "MountComponent's RefMap captures the ref=\"label\" node");
    Expect(KeepAlive.size() == 1, "MountComponent pushes exactly one Component onto the caller's KeepAlive vector");
}

void TestMountedIconsGetTheBackendGivenToSetIconBackend() {
    struct FakeIconBackend : Penumbra::Backends::IIconBackend {
        void DrawIcon(Penumbra::Render::Renderer&, std::string_view, Penumbra::Rect, Penumbra::Render::Color) override {}
    } Backend;

    TempProject Project;
    Project.Write("Glyph.chaos", "Glyph :: () -> Penumbra:::Component {\n"
                                  "    return (\n"
                                  "        <frame>\n"
                                  "            <icon ref=\"glyph\" icon=\"close\" />\n"
                                  "        </frame>\n"
                                  "    );\n"
                                  "}\n");

    Chaos::ChaosNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());
    App.SetIconBackend(&Backend);
    Expect(App.MountAppRoot("Glyph.chaos", "Glyph"), "MountAppRoot mounts a root holding an <Icon>");

    const auto* Glyph = dynamic_cast<Penumbra::Widgets::IconWidget*>(App.GetRef("glyph"));
    Expect(Glyph != nullptr && Glyph->IconBackend == &Backend,
           "a mounted <Icon> draws through the backend given to SetIconBackend");
}

void TestMountAppRootWrapsInOverlayHostAndPopulatesGetRef() {
    TempProject Project;
    Project.Write("Root.chaos", "Root :: () -> Penumbra:::Component {\n"
                                 "    return (\n"
                                 "        <frame ref=\"content\"></frame>\n"
                                 "    );\n"
                                 "}\n");

    Chaos::ChaosNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());

    const bool Mounted = App.MountAppRoot("Root.chaos", "Root");
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
    Project.Write("Fields.cosmos", ".fields { display: stack; flex-direction: column; }\n"
                                    ".editor { width: 200px; height: 100px; }\n");
    Project.Write("Fields.chaos", "Fields :: () -> Penumbra:::Component {\n"
                                   "    return (\n"
                                   "        <frame class=\"fields\">\n"
                                   "            <input ref=\"field\" />\n"
                                   "            <textArea ref=\"editor\" class=\"editor\" />\n"
                                   "        </frame>\n"
                                   "    );\n"
                                   "}\n");

    Chaos::ChaosNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());
    Expect(App.MountAppRoot("Fields.chaos", "Fields"), "MountAppRoot mounts a root holding an <Input> and a <TextArea>");

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

void TestGetRefFindsRefsInsideSlotMountedContent() {
    TempProject Project;
    Project.Write("Board.chaos", "Board :: () -> Penumbra:::Component {\n"
                                  "    return (\n"
                                  "        <frame ref=\"board\">\n"
                                  "            <slot>\n"
                                  "                !{() -> <frame ref=\"lane-todo\" />}\n"
                                  "            </slot>\n"
                                  "            <frame ref=\"content\"></frame>\n"
                                  "        </frame>\n"
                                  "    );\n"
                                  "}\n");
    Project.Write("Panel.chaos", "Panel :: () -> Penumbra:::Component {\n"
                                  "    return (\n"
                                  "        <frame ref=\"panel-static\">\n"
                                  "            <slot>\n"
                                  "                !{() -> <frame ref=\"panel-slot\" />}\n"
                                  "            </slot>\n"
                                  "        </frame>\n"
                                  "    );\n"
                                  "}\n");

    Chaos::ChaosNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());
    Expect(App.MountAppRoot("Board.chaos", "Board"), "MountAppRoot mounts a root with a ref inside a <Slot>");

    auto* Board = dynamic_cast<Penumbra::Widgets::Box*>(App.GetRef("board"));
    Penumbra::Widgets::WidgetBase* Lane = App.GetRef("lane-todo");
    Expect(Lane != nullptr && Board != nullptr && Board->GetChildCount() > 0 && Board->GetChildAt(0) == Lane,
           "GetRef finds a ref inside <Slot>-mounted content, and it's the widget the Slot mounted");

    Expect(App.MountReconciledComponent("Panel.chaos", "Panel", {}, "content"),
           "MountReconciledComponent mounts a component with refs in its static part and in a <Slot>");
    Expect(App.GetRef("panel-static") != nullptr, "GetRef finds a ref in a reconciled mount's static part");
    Expect(App.GetRef("panel-slot") != nullptr, "GetRef finds a ref inside a reconciled mount's <Slot>");

    App.TeardownReconciledComponent("content");
    Expect(App.GetRef("panel-static") == nullptr && App.GetRef("panel-slot") == nullptr,
           "once the reconciled mount is torn down, its refs are gone rather than dangling");
    App.TeardownRootWidget();
}

void TestMountReconciledComponentMountsResolvesSlotsAndReplacesOnRemount() {
    TempProject Project;
    Project.Write("Root.chaos", "Root :: () -> Penumbra:::Component {\n"
                                 "    return (\n"
                                 "        <frame ref=\"content\"></frame>\n"
                                 "    );\n"
                                 "}\n");
    Project.Write("CardListThree.chaos",
                   "CardListThree :: () -> Penumbra:::Component {\n"
                   "    return (\n"
                   "        <frame>\n"
                   "            <slot>\n"
                   "                !{() -> {\n"
                   "                    names: Array<string> = [\"Ann\", \"Bo\", \"Cy\"];\n"
                   "                    return names.map((item: string) -> <frame class={item} />);\n"
                   "                }}\n"
                   "            </slot>\n"
                   "        </frame>\n"
                   "    );\n"
                   "}\n");
    Project.Write("CardListTwo.chaos",
                   "CardListTwo :: () -> Penumbra:::Component {\n"
                   "    return (\n"
                   "        <frame>\n"
                   "            <slot>\n"
                   "                !{() -> {\n"
                   "                    names: Array<string> = [\"Dee\", \"Eff\"];\n"
                   "                    return names.map((item: string) -> <frame class={item} />);\n"
                   "                }}\n"
                   "            </slot>\n"
                   "        </frame>\n"
                   "    );\n"
                   "}\n");

    Chaos::ChaosNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());
    App.MountAppRoot("Root.chaos", "Root");

    const bool MountedFirst = App.MountReconciledComponent("CardListThree.chaos", "CardListThree", {}, "content");
    Expect(MountedFirst, "MountReconciledComponent mounts against a real fixture");

    auto* Content = dynamic_cast<Penumbra::Widgets::Box*>(App.GetRef("content"));
    Expect(Content != nullptr, "the app root's \"content\" ref resolves to a Box");
    Expect(Content != nullptr && Content->GetChildCount() == 1,
           "the mounted component's own root becomes content's sole child");
    auto* Inner = Content ? dynamic_cast<Penumbra::Widgets::Box*>(Content->GetChildAt(0)) : nullptr;
    Expect(Inner != nullptr && Inner->GetChildCount() == 3,
           "the <Slot> .Map() over 3 names resolves into 3 real children");

    const bool MountedSecond = App.MountReconciledComponent("CardListTwo.chaos", "CardListTwo", {}, "content");
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
    Project.Write("Root.chaos", "Root :: () -> Penumbra:::Component {\n"
                                 "    return (\n"
                                 "        <frame ref=\"content\"></frame>\n"
                                 "    );\n"
                                 "}\n");
    Project.Write("Card.chaos", "Card :: () -> Penumbra:::Component {\n"
                                 "    return (\n"
                                 "        <text>hello</text>\n"
                                 "    );\n"
                                 "}\n");

    Chaos::ChaosNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());
    App.MountAppRoot("Root.chaos", "Root");

    const bool Mounted = App.MountReconciledComponent("Card.chaos", "Card", {}, "no-such-ref");
    Expect(!Mounted, "MountReconciledComponent returns false for a TargetRefName the app root never declared");
}

void TestMountAppRootFailsGracefullyOnAMalformedFixture() {
    TempProject Project;
    Project.Write("Broken.chaos", "this is not valid chaos source {{{\n");

    Chaos::ChaosNyxDriver Driver(TestConfig(), Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());

    const bool Mounted = App.MountAppRoot("Broken.chaos", "Broken");
    Expect(!Mounted, "MountAppRoot returns false for a fixture that fails to compile");
    Expect(App.GetRootWidget() == nullptr, "a failed MountAppRoot never calls SetRootWidget");
}

void TestStyleApplierFollowsTheDpiScaleGivenToReloadFont() {
    PenumbraUiBackend::ChaosApplication BuiltAfter;
    BuiltAfter.ReloadFont(2.0F);
    Expect(BuiltAfter.StyleApplier().DpiScaleFactor() == 2.0F,
           "a style applier built after ReloadFont uses the DPI scale it was given");

    PenumbraUiBackend::ChaosApplication BuiltBefore;
    BuiltBefore.StyleApplier();
    BuiltBefore.ReloadFont(2.0F);
    Expect(BuiltBefore.StyleApplier().DpiScaleFactor() == 2.0F,
           "ReloadFont moves an already built style applier to the new DPI scale");
}

void TestStyleApplierDefaultsToTheFontGivenToSetFontConfig() {
    PenumbraUiBackend::ChaosApplication BuiltAfter;
    BuiltAfter.SetFontConfig("/fonts/Body.ttf", 14.0F);
    const auto& AfterDefault = BuiltAfter.StyleApplier().DefaultFont();
    Expect(AfterDefault && AfterDefault->Path == "/fonts/Body.ttf" && AfterDefault->SizeLogical == 14.0F,
           "a style applier built after SetFontConfig defaults to that font");

    PenumbraUiBackend::ChaosApplication BuiltBefore;
    Expect(!BuiltBefore.StyleApplier().DefaultFont(), "a style applier has no default font before SetFontConfig");
    BuiltBefore.SetFontConfig("/fonts/Body.ttf", 16.0F);
    const auto& BeforeDefault = BuiltBefore.StyleApplier().DefaultFont();
    Expect(BeforeDefault && BeforeDefault->SizeLogical == 16.0F,
           "SetFontConfig gives an already built style applier its default font");
}

void TestBridgedChaosApplicationExposesInheritedMethodsToNyx() {
    PenumbraUiBackend::ChaosApplicationBridge Bridge(TestConfig(), ".");

    PenumbraUiBackend::ChaosApplication* Application = Bridge.LoadApplication(
        "TestApplication :: class : Application {\n"
        "public:\n"
        "    OnStart~: () -> bool override {\n"
        "        return this.MountAppRoot(\"BridgedRoot.chaos\", \"BridgedRoot\");\n"
        "    }\n"
        "}\n",
        "bridged-chaos-application-test.nyx", "TestApplication");

    Expect(Application != nullptr, "ChaosApplicationBridge loads a Nyx Application backed by ChaosApplication");
    if (Application) {
        delete Application;
    }
}

void TestRegisterInstanceForwardersDispatchesToACustomMethodByBareName() {
    PenumbraUiBackend::ChaosApplicationBridge Bridge(TestConfig(), ".");

    PenumbraUiBackend::ChaosApplication* Application = Bridge.LoadApplication(
        "TestApplication :: class : Application {\n"
        "public:\n"
        "    LastValue~ i32 = 0;\n"
        "    Record~: (value: i32) -> void { this.LastValue = value; }\n"
        "    GetLastValue :: () -> i32 { return this.LastValue; }\n"
        "}\n",
        "forwarder-test.nyx", "TestApplication");

    Expect(Application != nullptr, "ChaosApplicationBridge loads a Nyx Application with a custom method to forward to");
    if (!Application) return;

    int PreDispatchCalls = 0;
    PenumbraUiBackend::RegisterInstanceForwarders(Bridge.Runtime(), Bridge, *Application,
                                                   {{"Record", "(value: i32) -> void"}},
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

void RunChaosApplicationTests() {
    TestMountComponentAutoDiscoversItsOwnColocatedStylesheet();
    TestMountComponentDiscoversAnImportedComponentsStylesheetInASeparateDirectoryTransitively();
    TestStylesFromTwoIndependentMountsInSeparateDirectoriesAccumulateRatherThanReplace();
    TestGlobalCosmosLoadsIntoTheGlobalSlotAndItsRootVariablesReachEveryComponent();
    TestRootVariablesInAComponentStylesheetAreKept();
    TestMountComponentBuildsAWidgetAndPopulatesRefMap();
    TestMountAppRootWrapsInOverlayHostAndPopulatesGetRef();
    TestGetRefFindsRefsInsideSlotMountedContent();
    TestMountedIconsGetTheBackendGivenToSetIconBackend();
    TestMountAppRootGivesTextFieldsAFocusStateAndAClipboard();
    TestMountReconciledComponentMountsResolvesSlotsAndReplacesOnRemount();
    TestMountReconciledComponentFailsForAnUnknownTargetRef();
    TestMountAppRootFailsGracefullyOnAMalformedFixture();
    TestStyleApplierFollowsTheDpiScaleGivenToReloadFont();
    TestStyleApplierDefaultsToTheFontGivenToSetFontConfig();
    TestBridgedChaosApplicationExposesInheritedMethodsToNyx();
    TestRegisterInstanceForwardersDispatchesToACustomMethodByBareName();
}
