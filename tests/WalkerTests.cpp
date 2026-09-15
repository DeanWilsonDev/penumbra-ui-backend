#include "PenumbraUiBackend/Walker.h"

#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"

#include "Iris/ComponentInstance.h"
#include "Iris/IrisNyxDriver.h"

#include "Penumbra/Application.h"
#include "Penumbra/Backends/IIconBackend.h"
#include "Penumbra/LifecycleRegistry.h"
#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/IconWidget.h"
#include "Penumbra/Widgets/ImageWidget.h"
#include "Penumbra/Widgets/InlineContainer.h"
#include "Penumbra/Widgets/Label.h"
#include "Penumbra/Widgets/ScrollablePanel.h"
#include "Penumbra/Widgets/SplitPanel.h"
#include "Penumbra/Widgets/TextInput.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

int Failures = 0; // shared across all test files in this executable

namespace {

void Expect(bool Condition, const std::string& Description) {
    if (Condition) {
        std::printf("[PASS] %s\n", Description.c_str());
    } else {
        std::printf("[FAIL] %s\n", Description.c_str());
        ++Failures;
    }
}

using Iris::Component;
using Iris::IrisElementTag;
using Iris::IrisProps;
using Iris::IrisPropValue;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::BuildWidgetTree;
using PenumbraUiBackend::PenumbraWidget;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::IconWidget;
using Penumbra::Widgets::ImageWidget;
using Penumbra::Widgets::InlineContainer;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::LayoutMode;
using Penumbra::Widgets::ScrollablePanel;
using Penumbra::Widgets::SplitAxis;
using Penumbra::Widgets::SplitPanel;
using Penumbra::Widgets::TextInput;
using Penumbra::Widgets::WidgetBase;

Component MakeNode(IrisElementTag Tag, IrisProps Props = {}, std::vector<Component> Children = {}) {
    return Component(Tag, std::move(Props), std::move(Children), nullptr);
}

void TestNoneProducesNoWidget() {
    const auto Built = BuildWidgetTree(Component(nullptr), BuildContext{});
    Expect(Built == nullptr, "IrisElementTag::None builds to nullptr — no widget, not an error");
}

void TestFrameBuildsABoxWithClassName() {
    IrisProps Props;
    Props["class"] = IrisPropValue{std::string("health-bar")};
    const auto Node = MakeNode(IrisElementTag::Frame, Props);

    const auto Built = BuildWidgetTree(Node, BuildContext{});
    Expect(Built != nullptr, "<Frame> builds a widget");
    const auto* AsBox = dynamic_cast<Box*>(Built.get());
    Expect(AsBox != nullptr, "<Frame> builds specifically a Box");
    Expect(AsBox != nullptr && AsBox->ClassName == "health-bar", "the class prop reaches Box::ClassName");
}

void TestFrameChildrenAreAttachedAndNoneChildrenAreSkipped() {
    std::vector<Component> Children;
    Children.push_back(MakeNode(IrisElementTag::Frame));
    Children.push_back(Component(nullptr)); // None — should be silently dropped
    Children.push_back(MakeNode(IrisElementTag::Frame));

    const auto Node = MakeNode(IrisElementTag::Frame, {}, std::move(Children));
    const auto Built = BuildWidgetTree(Node, BuildContext{});
    Expect(Built != nullptr && Built->GetChildCount() == 2,
           "two real children are attached; the None child contributes nothing, not even a hole");
}

void TestFramePressEventReachesWidgetBase() {
    bool      Pressed = false;
    IrisProps Props;
    Props["onPress"] = IrisPropValue{std::function<void()>([&Pressed]() { Pressed = true; })};
    const auto Node = MakeNode(IrisElementTag::Frame, Props);

    const auto Built = BuildWidgetTree(Node, BuildContext{});
    Expect(Built != nullptr && static_cast<bool>(Built->OnPressed), "onPress reaches WidgetBase::OnPressed");
    if (Built != nullptr && Built->OnPressed) {
        Built->OnPressed();
        Expect(Pressed, "invoking the built widget's OnPressed calls back into the original Iris escape hatch");
    }
}

void TestGridBuildsABoxWithHorizontalStackLayout() {
    const auto Node = MakeNode(IrisElementTag::Grid);
    const auto Built = BuildWidgetTree(Node, BuildContext{});
    const auto* AsBox = dynamic_cast<Box*>(Built.get());
    Expect(AsBox != nullptr, "<Grid> builds a Box (the stub mapping, docs/iris_stage2_decision_doc.md §3)");
    Expect(AsBox != nullptr && AsBox->Layout == LayoutMode::HorizontalStack,
           "the stub sets LayoutMode::HorizontalStack, since Penumbra has no real grid layout yet");
}

void TestInlineBuildsAnInlineContainer() {
    const auto Node = MakeNode(IrisElementTag::Inline);
    const auto Built = BuildWidgetTree(Node, BuildContext{});
    Expect(dynamic_cast<InlineContainer*>(Built.get()) != nullptr,
           "<Inline> builds an InlineContainer, distinct from a plain Box");
}

void TestTextBuildsALabelWithContent() {
    IrisProps Props;
    Props["text"] = IrisPropValue{std::string("Hello")};
    const auto Node = MakeNode(IrisElementTag::Text, Props);

    const auto Built = BuildWidgetTree(Node, BuildContext{});
    const auto* AsLabel = dynamic_cast<Label*>(Built.get());
    Expect(AsLabel != nullptr, "<Text> builds a Label");
    Expect(AsLabel != nullptr && AsLabel->Text == "Hello", "the text prop reaches Label::Text");
}

void TestTextPicksUpFontFromBuildContext() {
    const auto Node = MakeNode(IrisElementTag::Text);

    BuildContext Context;
    Context.Font = 42;
    const auto Built = BuildWidgetTree(Node, Context);
    const auto* AsLabel = dynamic_cast<Label*>(Built.get());
    Expect(AsLabel != nullptr && AsLabel->Font == 42,
           "Label::Font is populated from BuildContext, since Label::Builder has no method for it");
}

void TestImageBuildsWithoutLoadingWhenNoBackendProvided() {
    IrisProps Props;
    Props["src"] = IrisPropValue{std::string("assets/icons/health.png")};
    const auto Node = MakeNode(IrisElementTag::Image, Props);

    const auto Built = BuildWidgetTree(Node, BuildContext{}); // no ImageBackend/SdlRenderer
    const auto* AsImage = dynamic_cast<ImageWidget*>(Built.get());
    Expect(AsImage != nullptr, "<Image> builds an ImageWidget");
    Expect(AsImage != nullptr && AsImage->FilePath == "assets/icons/health.png",
           "the src prop reaches ImageWidget::FilePath even though nothing was actually loaded");
    Expect(AsImage != nullptr && AsImage->Texture == nullptr,
           "no texture is loaded when BuildContext has no ImageBackend/SdlRenderer");
}

void TestIconBuildsWithIconNameEvenWithoutBackendProvided() {
    IrisProps Props;
    Props["icon"] = IrisPropValue{std::string("chevron-down")};
    const auto Node = MakeNode(IrisElementTag::Icon, Props);

    const auto Built = BuildWidgetTree(Node, BuildContext{}); // no IconBackend
    const auto* AsIcon = dynamic_cast<IconWidget*>(Built.get());
    Expect(AsIcon != nullptr, "<Icon> builds an IconWidget");
    Expect(AsIcon != nullptr && AsIcon->IconName == "chevron-down",
           "the icon prop reaches IconWidget::IconName even though no backend was provided");
    Expect(AsIcon != nullptr && AsIcon->IconBackend == nullptr,
           "IconWidget::IconBackend stays null when BuildContext has no IconBackend");
}

void TestIconPicksUpIconBackendFromBuildContext() {
    struct FakeIconBackend : Penumbra::Backends::IIconBackend {
        void DrawIcon(Penumbra::Render::Renderer&, std::string_view, Penumbra::Rect, Penumbra::Render::Color) override {}
    } Backend;

    IrisProps Props;
    Props["icon"] = IrisPropValue{std::string("chevron-down")};
    const auto Node = MakeNode(IrisElementTag::Icon, Props);

    BuildContext Context;
    Context.IconBackend = &Backend;
    const auto Built = BuildWidgetTree(Node, Context);
    const auto* AsIcon = dynamic_cast<IconWidget*>(Built.get());
    Expect(AsIcon != nullptr && AsIcon->IconBackend == &Backend,
           "IconWidget::IconBackend is populated from BuildContext.IconBackend");
}

void TestIconSizeOverridesTheDefault() {
    IrisProps Props;
    Props["icon"] = IrisPropValue{std::string("chevron-down")};
    Props["size"] = IrisPropValue{14.0f};
    const auto Node = MakeNode(IrisElementTag::Icon, Props);

    const auto Built = BuildWidgetTree(Node, BuildContext{});
    const auto* AsIcon = dynamic_cast<IconWidget*>(Built.get());
    Expect(AsIcon != nullptr && AsIcon->SizeLogical == 14.0f,
           "the size prop reaches IconWidget::SizeLogical, overriding its 16px default");
}

void TestIconWithNoSizePropKeepsTheDefault() {
    IrisProps Props;
    Props["icon"] = IrisPropValue{std::string("chevron-down")};
    const auto Node = MakeNode(IrisElementTag::Icon, Props);

    const auto Built = BuildWidgetTree(Node, BuildContext{});
    const auto* AsIcon = dynamic_cast<IconWidget*>(Built.get());
    Expect(AsIcon != nullptr && AsIcon->SizeLogical == 16.0f,
           "an absent size prop leaves IconWidget::SizeLogical at its own default (Builder::size() never called)");
}

void TestScrollBuildsAScrollablePanelWithChildrenAndWheelStep() {
    std::vector<Component> RowChildren;
    RowChildren.push_back(MakeNode(IrisElementTag::Frame));
    RowChildren.push_back(MakeNode(IrisElementTag::Frame));

    IrisProps Props;
    Props["wheelStep"] = IrisPropValue{24.0f};
    const auto Node = MakeNode(IrisElementTag::Scroll, Props, RowChildren);

    const auto Built = BuildWidgetTree(Node, BuildContext{});
    const auto* AsScroll = dynamic_cast<ScrollablePanel*>(Built.get());
    Expect(AsScroll != nullptr, "<Scroll> builds a ScrollablePanel");
    Expect(AsScroll != nullptr && AsScroll->WheelStepLogical == 24.0f,
           "the wheelStep prop reaches ScrollablePanel::WheelStepLogical");
    Expect(AsScroll != nullptr && AsScroll->GetChildCount() == 2,
           "both element children were attached via AddChild");
}

void TestScrollWithNoWheelStepKeepsTheDefault() {
    const auto Node = MakeNode(IrisElementTag::Scroll);
    const auto Built = BuildWidgetTree(Node, BuildContext{});
    const auto* AsScroll = dynamic_cast<ScrollablePanel*>(Built.get());
    Expect(AsScroll != nullptr && AsScroll->WheelStepLogical == 0.0f,
           "an absent wheelStep prop leaves ScrollablePanel::WheelStepLogical at its own default");
}

void TestInputBuildsATextInputWithTextAndPreferredWidth() {
    IrisProps Props;
    Props["text"] = IrisPropValue{std::string("hello")};
    Props["preferredWidth"] = IrisPropValue{200.0f};
    const auto Node = MakeNode(IrisElementTag::Input, Props);

    const auto Built = BuildWidgetTree(Node, BuildContext{});
    const auto* AsInput = dynamic_cast<TextInput*>(Built.get());
    Expect(AsInput != nullptr, "<Input> builds a TextInput");
    Expect(AsInput != nullptr && AsInput->Text == "hello", "the text prop reaches TextInput::Text");
    Expect(AsInput != nullptr && AsInput->PreferredWidthLogical == 200.0f,
           "the preferredWidth prop reaches TextInput::PreferredWidthLogical");
    Expect(AsInput != nullptr && AsInput->Focus == nullptr && AsInput->Clipboard == nullptr,
           "TextInput::Focus/Clipboard stay null when BuildContext supplies neither -- still built, just inert");
}

void TestInputPicksUpFocusAndClipboardFromBuildContext() {
    Penumbra::Widgets::FocusState  Focus;
    struct FakeClipboard : Penumbra::Platform::IClipboard {
        void        SetClipboardText(const std::string&) override {}
        std::string GetClipboardText() const override { return {}; }
    } Clipboard;

    const auto Node = MakeNode(IrisElementTag::Input);
    BuildContext Context;
    Context.Focus = &Focus;
    Context.Clipboard = &Clipboard;
    const auto Built = BuildWidgetTree(Node, Context);
    const auto* AsInput = dynamic_cast<TextInput*>(Built.get());
    Expect(AsInput != nullptr && AsInput->Focus == &Focus && AsInput->Clipboard == &Clipboard,
           "TextInput::Focus/Clipboard are populated from BuildContext");
}

void TestInputOnTextChangeReachesTextInputOnTextChanged() {
    std::string LastValue;
    IrisProps   Props;
    Props["onTextChange"] = IrisPropValue{std::function<void(std::string)>([&LastValue](std::string NewText) {
        LastValue = std::move(NewText);
    })};
    const auto Node = MakeNode(IrisElementTag::Input, Props);

    const auto Built = BuildWidgetTree(Node, BuildContext{});
    const auto* AsInput = dynamic_cast<TextInput*>(Built.get());
    Expect(AsInput != nullptr && static_cast<bool>(AsInput->OnTextChanged),
           "the onTextChange prop reaches TextInput::OnTextChanged");
    if (AsInput != nullptr && AsInput->OnTextChanged) {
        AsInput->OnTextChanged("typed");
        Expect(LastValue == "typed", "invoking TextInput::OnTextChanged calls through to the onTextChange callback");
    }
}

void TestSplitBuildsASplitPanelWithBothPanesAndProps() {
    IrisProps Props;
    Props["axis"] = IrisPropValue{std::string("vertical")};
    Props["ratio"] = IrisPropValue{0.3f};
    Props["minPaneSize"] = IrisPropValue{50.0f};
    Props["handleThickness"] = IrisPropValue{6.0f};

    std::vector<Component> Panes;
    Panes.push_back(MakeNode(IrisElementTag::Frame));
    IrisProps SecondPaneProps;
    SecondPaneProps["text"] = IrisPropValue{std::string("second-pane")};
    Panes.push_back(MakeNode(IrisElementTag::Text, SecondPaneProps));
    const auto Node = MakeNode(IrisElementTag::Split, Props, Panes);

    const auto Built = BuildWidgetTree(Node, BuildContext{});
    const auto* AsSplit = dynamic_cast<SplitPanel*>(Built.get());
    Expect(AsSplit != nullptr, "<Split> builds a SplitPanel");
    Expect(AsSplit != nullptr && AsSplit->Axis == SplitAxis::Vertical, "the axis prop reaches SplitPanel::Axis");
    Expect(AsSplit != nullptr && AsSplit->SplitRatio == 0.3f, "the ratio prop reaches SplitPanel::SplitRatio");
    Expect(AsSplit != nullptr && AsSplit->MinPaneSizeLogical == 50.0f,
           "the minPaneSize prop reaches SplitPanel::MinPaneSizeLogical");
    Expect(AsSplit != nullptr && AsSplit->HandleThicknessLogical == 6.0f,
           "the handleThickness prop reaches SplitPanel::HandleThicknessLogical");
    Expect(AsSplit != nullptr && AsSplit->GetChildCount() == 2, "both panes were attached via SetFirst/SetSecond");
    const auto* FirstPane = AsSplit != nullptr ? dynamic_cast<const Box*>(AsSplit->GetChildAt(0)) : nullptr;
    const auto* SecondPane = AsSplit != nullptr ? dynamic_cast<const Label*>(AsSplit->GetChildAt(1)) : nullptr;
    Expect(FirstPane != nullptr, "the leading child (Children[0]) was reached via SetFirst, at GetChildAt(0)");
    Expect(SecondPane != nullptr && SecondPane->Text == "second-pane",
           "the trailing child (Children[1]) was reached via SetSecond, at GetChildAt(1) -- pane order isn't swapped");
}

void TestSplitWithNoPropsKeepsDefaults() {
    std::vector<Component> Panes;
    Panes.push_back(MakeNode(IrisElementTag::Frame));
    Panes.push_back(MakeNode(IrisElementTag::Frame));
    const auto Node = MakeNode(IrisElementTag::Split, {}, Panes);

    const auto Built = BuildWidgetTree(Node, BuildContext{});
    const auto* AsSplit = dynamic_cast<SplitPanel*>(Built.get());
    Expect(AsSplit != nullptr && AsSplit->Axis == SplitAxis::Horizontal,
           "an absent axis prop leaves SplitPanel::Axis at its own Horizontal default");
    Expect(AsSplit != nullptr && AsSplit->SplitRatio == 0.5f,
           "an absent ratio prop leaves SplitPanel::SplitRatio at its own 0.5 default");
}

void TestNativeUnwrapsAPenumbraWidgetToItsRealWidgetBase() {
    auto  InnerBox = std::make_unique<Box>();
    Box* InnerBoxRaw = InnerBox.get();
    auto  Wrapped = std::make_unique<PenumbraWidget>(std::move(InnerBox));

    // std::function (what MakeNativeBuilder wraps this in) requires a copyable target --
    // a bare move-captured unique_ptr in the lambda isn't, so it's boxed in a shared_ptr
    // here purely to satisfy that, even though Build() is only ever actually invoked once.
    auto SharedWrapped = std::make_shared<std::unique_ptr<PenumbraWidget>>(std::move(Wrapped));

    Component Node = MakeNode(IrisElementTag::Native);
    Node.NativeBuilder = Iris::MakeNativeBuilder(
        [SharedWrapped]() -> std::unique_ptr<Umbra::IWidget> { return std::move(*SharedWrapped); });

    const auto Built = BuildWidgetTree(Node, BuildContext{});
    Expect(Built.get() == InnerBoxRaw,
           "<Native> unwraps a PenumbraWidget handle back to the real WidgetBase it owns, via DetachOwnership");
}

void TestNativeWithNoBuilderProducesNoWidget() {
    const auto Node = MakeNode(IrisElementTag::Native);
    const auto Built = BuildWidgetTree(Node, BuildContext{});
    Expect(Built == nullptr,
           "<Native> with no build prop (malformed -- Codegen itself already rejects this) builds to nullptr, "
           "not a crash");
}

void TestNativeBuildReturningNonPenumbraWidgetProducesNoWidget() {
    struct FakeNonPenumbraWidget : Umbra::IWidget {
        void                             ApplyPropDiff(const Umbra::IrisPropDiff&) override {}
        std::size_t                     GetChildCount() const override { return 0; }
        Umbra::IWidget*                 GetChildAt(std::size_t) const override { return nullptr; }
        void                             InsertChildAt(std::size_t, std::unique_ptr<Umbra::IWidget>) override {}
        std::unique_ptr<Umbra::IWidget> RemoveChildAt(std::size_t) override { return nullptr; }
    };

    Component Node = MakeNode(IrisElementTag::Native);
    Node.NativeBuilder = Iris::MakeNativeBuilder(
        []() -> std::unique_ptr<Umbra::IWidget> { return std::make_unique<FakeNonPenumbraWidget>(); });

    const auto Built = BuildWidgetTree(Node, BuildContext{});
    Expect(Built == nullptr,
           "a build prop returning a non-PenumbraWidget Umbra::IWidget has nothing to unwrap -- builds to nullptr "
           "rather than crashing the dynamic_cast");
}

void TestNestedTreeBuildsRecursively() {
    // <Frame class="party-row"><HealthBar-shaped Frame/></Frame> — a small stand-in for
    // the spec §9 PartyScreen shape, since <HealthBar> itself is a component invocation
    // Codegen resolves away before this walker ever sees an Component tree.
    std::vector<Component> Inner;
    IrisProps                   TextProps;
    TextProps["text"] = IrisPropValue{std::string("42/100")};
    Inner.push_back(MakeNode(IrisElementTag::Text, TextProps));

    std::vector<Component> Outer;
    Outer.push_back(MakeNode(IrisElementTag::Frame, {}, std::move(Inner)));

    const auto Node = MakeNode(IrisElementTag::Frame, {}, std::move(Outer));
    const auto Built = BuildWidgetTree(Node, BuildContext{});

    Expect(Built != nullptr && Built->GetChildCount() == 1, "outer Frame has one child");
    const WidgetBase* MiddleFrame = Built != nullptr && Built->GetChildCount() == 1 ? Built->GetChildAt(0) : nullptr;
    Expect(MiddleFrame != nullptr && MiddleFrame->GetChildCount() == 1, "middle Frame has one child");
    const WidgetBase* InnerLabel =
        MiddleFrame != nullptr && MiddleFrame->GetChildCount() == 1 ? MiddleFrame->GetChildAt(0) : nullptr;
    const auto* AsLabel = dynamic_cast<const Label*>(InnerLabel);
    Expect(AsLabel != nullptr && AsLabel->Text == "42/100", "the nested Label is reached at the correct depth");
}

void TestRefTaggedNodeIsRecordedInOutRefs() {
    std::vector<Component> Children;
    Component               InnerText = MakeNode(IrisElementTag::Text);
    InnerText.Ref = IrisPropValue{std::string("trigger-icon")};
    Children.push_back(std::move(InnerText));

    const auto Node = MakeNode(IrisElementTag::Frame, {}, std::move(Children));

    PenumbraUiBackend::RefMap Refs;
    const auto                Built = BuildWidgetTree(Node, BuildContext{}, /*OutTags=*/nullptr, &Refs);

    Expect(Refs.size() == 1, "exactly one ref-tagged node was recorded");
    Expect(Built != nullptr && Built->GetChildCount() == 1 && Refs.count("trigger-icon") == 1 &&
               Refs.at("trigger-icon") == Built->GetChildAt(0),
           "the recorded pointer is the real built widget for the ref-tagged node, not some other widget");
}

void TestNoRefLeavesOutRefsEmpty() {
    const auto Node = MakeNode(IrisElementTag::Frame);

    PenumbraUiBackend::RefMap Refs;
    BuildWidgetTree(Node, BuildContext{}, /*OutTags=*/nullptr, &Refs);

    Expect(Refs.empty(), "a tree with no `ref` props records nothing, even when OutRefs is provided");
}

// docs/next_steps.md's "reconciler-side wiring for a framework-owned component
// lifecycle system" ask. FakeLifecycle counts each hook call rather than asserting
// order/timing beyond what each test below checks explicitly.
struct FakeLifecycle : Umbra::IWidgetLifecycle {
    int MountCount = 0;
    int UnmountCount = 0;
    int TickCount = 0;

    void OnMount() override { ++MountCount; }
    void OnUnmount() override { ++UnmountCount; }
    void OnTick(const Umbra::TickInfo&) override { ++TickCount; }
};

// A Component with a real ComponentInstance whose Lifecycle field points at Lifecycle --
// the same shape `iris::RegisterLifecycle` produces inside a real component body, built
// by hand here since these tests build a Component tree directly rather than going
// through Codegen.
Component MakeNodeWithLifecycle(IrisElementTag Tag, Umbra::IWidgetLifecycle* Lifecycle) {
    Component Node = MakeNode(Tag);
    Node.Instance = std::make_shared<iris::ComponentInstance>();
    Node.Instance->Lifecycle = Lifecycle;
    return Node;
}

void TestLifecycleRegistersOnBuildWhenLifecycleHostIsSet() {
    FakeLifecycle Lifecycle;
    Penumbra::LifecycleRegistry Host;
    BuildContext Context;
    Context.LifecycleHost = &Host;

    const auto Node = MakeNodeWithLifecycle(IrisElementTag::Frame, &Lifecycle);
    const auto Built = BuildWidgetTree(Node, Context);

    Expect(Lifecycle.MountCount == 1,
           "building a Component with a live Instance->Lifecycle registers it against "
           "Context.LifecycleHost, which calls OnMount immediately (LifecycleRegistry::RegisterLifecycle)");

    Host.Tick(0.5f);
    Expect(Lifecycle.TickCount == 1, "the registered lifecycle receives OnTick from the host LifecycleRegistry");
}

void TestLifecycleUnregistersWhenTheBuiltWidgetIsDestroyed() {
    FakeLifecycle Lifecycle;
    Penumbra::LifecycleRegistry Host;
    BuildContext Context;
    Context.LifecycleHost = &Host;

    const auto Node = MakeNodeWithLifecycle(IrisElementTag::Frame, &Lifecycle);
    auto Built = BuildWidgetTree(Node, Context);
    Expect(Lifecycle.UnmountCount == 0, "not yet unmounted while the built widget is still alive");

    Built.reset(); // real widget teardown -- WidgetBase::OnDestroyed should fire
    Expect(Lifecycle.UnmountCount == 1,
           "destroying the built widget fires WidgetBase::OnDestroyed, which unregisters the "
           "lifecycle (calling OnUnmount via LifecycleRegistry::UnregisterLifecycle) without needing "
           "ComponentInstance to have a destructor of its own");

    Host.Tick(0.1f);
    Expect(Lifecycle.TickCount == 0, "once unregistered, the host LifecycleRegistry no longer dispatches OnTick to it");
}

// umbra-interfaces' Umbra::LivenessGuard (commit b59bf17) -- UmbraLifecycleBridge (this
// file, private to the anonymous namespace) now wires its own Inner_ through a
// LivenessGuard::Watch and calls AssertAlive() before every OnMount/OnUnmount/OnTick
// dereference. This reproduces the exact ordering mistake pharos-proto hit in
// production -- Inner_'s own pointee destroyed while the bridge built from it is still
// registered against a live LifecycleRegistry, previously an EXC_BAD_ACCESS only
// diagnosable after the fact via lldb -- and confirms it now aborts immediately with a
// clear message instead. No existing death-test convention anywhere in this repo's test
// suite (grepped tests/*.cpp for fork/abort/SIGABRT/death before writing this -- none),
// so this uses a plain fork()/waitpid() subprocess check rather than inventing an
// unproven pattern; UmbraLifecycleBridge itself has no test-reachable entry point of its
// own (anonymous-namespace, private to Walker.cpp), so this exercises it the only way
// test code can -- indirectly, through BuildWidgetTree/Host.Tick(), same as the two
// lifecycle tests directly above.
void TestUmbraLifecycleBridgeAbortsWhenInnerLifecycleIsDestroyedFirst() {
    const pid_t Pid = fork();
    Expect(Pid >= 0, "fork() succeeded");
    if (Pid == 0) {
        // Child process: build up the real dangling-pointer shape, then trigger the
        // dereference that used to crash.
        Penumbra::LifecycleRegistry Host;
        BuildContext                Context;
        Context.LifecycleHost = &Host;

        std::unique_ptr<Penumbra::Widgets::WidgetBase> Built;
        {
            FakeLifecycle Lifecycle;
            Built = BuildWidgetTree(MakeNodeWithLifecycle(IrisElementTag::Frame, &Lifecycle), Context);
        } // Lifecycle destroyed here -- Built (and Host's registration of its bridge) is still alive.

        Host.Tick(0.1f); // Should AssertAlive() -> abort(), not dereference a dangling Lifecycle.
        _exit(0);        // Unreachable if the guard is working.
    }
    int Status = 0;
    waitpid(Pid, &Status, 0);
    Expect(WIFSIGNALED(Status) && WTERMSIG(Status) == SIGABRT,
           "UmbraLifecycleBridge::OnTick aborts via LivenessGuard::AssertAlive when its Inner_ pointee "
           "was destroyed first, instead of dereferencing a dangling pointer");
}

void TestNoLifecycleHostMeansNoRegistrationEvenWithALiveInstance() {
    FakeLifecycle Lifecycle;
    BuildContext  Context; // LifecycleHost left null, the default

    const auto Node = MakeNodeWithLifecycle(IrisElementTag::Frame, &Lifecycle);
    const auto Built = BuildWidgetTree(Node, Context);

    Expect(Lifecycle.MountCount == 0,
           "Context.LifecycleHost == nullptr (the default) skips registration entirely, exactly "
           "pre-wiring behavior, even when Instance->Lifecycle is live");
}

void TestNoComponentInstanceMeansNoRegistrationEvenWithALifecycleHost() {
    Penumbra::LifecycleRegistry Host;
    BuildContext                Context;
    Context.LifecycleHost = &Host;

    const auto Node = MakeNode(IrisElementTag::Frame); // Node.Instance left unset
    const auto Built = BuildWidgetTree(Node, Context);
    Expect(Built != nullptr, "builds normally with no Instance at all -- registration is purely opt-in");
}

void TestNestedNonSlotComponentInvocationAlsoRegistersItsOwnLifecycle() {
    // A plain nested <ChildComponent .../> (no <Slot>) still gets its own
    // Component::Instance from MountComponentInstance, inline in the same tree --
    // confirmed by reading Iris/Component.h directly. The registration check has to
    // happen at every node BuildWidgetTreeInternal visits, not just the outer Node
    // BuildWidgetTree itself was called with.
    FakeLifecycle OuterLifecycle;
    FakeLifecycle InnerLifecycle;
    Penumbra::LifecycleRegistry Host;
    BuildContext Context;
    Context.LifecycleHost = &Host;

    Component Inner = MakeNodeWithLifecycle(IrisElementTag::Frame, &InnerLifecycle);
    std::vector<Component> Children;
    Children.push_back(std::move(Inner));

    Component Outer = MakeNodeWithLifecycle(IrisElementTag::Frame, &OuterLifecycle);
    Outer.Children = std::move(Children);

    const auto Built = BuildWidgetTree(Outer, Context);

    Expect(OuterLifecycle.MountCount == 1, "the outer (root) Component's own lifecycle registers");
    Expect(InnerLifecycle.MountCount == 1,
           "a nested Component's lifecycle also registers, even though it isn't Node's own root -- "
           "the per-node walk, not a root-only check, is what BuildWidgetTree.h's own comment documents");
}

// docs/next_steps.md's "widen BuildContext::LifecycleHost from Penumbra::Application* to
// Penumbra::LifecycleRegistry*" ask. The tests above now construct a bare
// Penumbra::LifecycleRegistry directly (BuildWidgetTree/RegisterLifecycleIfPresent only ever
// call RegisterLifecycle/UnregisterLifecycle on Context.LifecycleHost, so they no longer need a
// full Application), which means Application::RegisterLifecycle/UnregisterLifecycle's own thin
// forward onto its owned LifecycleRegistry (Application.cpp, since f4db86f's refactor) is no
// longer exercised anywhere in this walk. penumbra-proto itself has no test suite of its own
// (confirmed: no tests/ directory, no test target in its CMakeLists.txt) to cover that forward
// independently, so this one test keeps it covered here -- unrelated to BuildContext/Walker
// otherwise, it exercises Application's own public API directly.
struct FakePenumbraLifecycle : Penumbra::IWidgetLifecycle {
    int MountCount = 0;
    int UnmountCount = 0;
    int TickCount = 0;

    void OnMount() override { ++MountCount; }
    void OnUnmount() override { ++UnmountCount; }
    void OnTick(const Penumbra::TickInfo&) override { ++TickCount; }
};

void TestApplicationRegisterLifecycleStillForwardsToItsOwnLifecycleRegistry() {
    Penumbra::Application     Host;
    FakePenumbraLifecycle Lifecycle;

    Host.RegisterLifecycle(&Lifecycle);
    Expect(Lifecycle.MountCount == 1,
           "Application::RegisterLifecycle still forwards to its owned LifecycleRegistry's "
           "RegisterLifecycle (which calls OnMount immediately), unchanged since the f4db86f refactor");

    Host.GetLifecycleRegistry().Tick(0.25f);
    Expect(Lifecycle.TickCount == 1,
           "the lifecycle registered via Application::RegisterLifecycle is reachable through "
           "Application::GetLifecycleRegistry(), the same registry object -- not a separate copy");

    Host.UnregisterLifecycle(&Lifecycle);
    Expect(Lifecycle.UnmountCount == 1, "Application::UnregisterLifecycle still forwards too, calling OnUnmount");

    Host.GetLifecycleRegistry().Tick(0.25f);
    Expect(Lifecycle.TickCount == 1, "once unregistered via Application's own API, it no longer receives OnTick");
}

// docs/next_steps.md's "a Nyx-authored OnMount/OnTick has no way to reach its own
// component's ref'd widgets" ask. Unlike the hand-built-Component lifecycle tests above,
// GetRef needs a real `Iris::NyxDriverState` (a real nyx::host::NyxRuntime::NyxScope with a
// real Environment) behind Node.Instance->DriverState -- only IrisNyxDriver::MountRoot
// produces that, so these tests go through the real `.irisx` -> IrisIrDocument -> Component
// pipeline end to end, the same way iris-proto's own IrisNyxDriverTests.cpp verifies
// `auto OnTick = ...` itself. TempProject mirrors that file's own test helper exactly (a
// real on-disk fixture, not a hand-built IrisIrDocument).
class TempProject {
public:
    TempProject() {
        Root_ = std::filesystem::temp_directory_path() / "penumbra_ui_backend_getref_test";
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

private:
    std::filesystem::path Root_;
};

Iris::IrisConfig UmbraConfig() {
    Iris::IrisConfig Config;
    Config.Target      = Iris::IrisBuildTarget::UmbraEngine;
    Config.SearchPaths = {"demo"};
    return Config;
}

void TestNyxOnTickCanLookUpAndMutateARefdWidgetViaGetRef() {
    TempProject       Project;
    const std::string AppPath = Project.Write("RefTicker.irisx",
                                               "void RefTicker() {\n"
                                               "    auto OnTick = (float dt) -> {\n"
                                               "        auto handle = GetRef(\"label\");\n"
                                               "        handle.SetText(\"ticked\");\n"
                                               "        handle.SetColor(1, 2, 3, 255);\n"
                                               "        handle.SetVisible(false);\n"
                                               "    };\n"
                                               "\n"
                                               "    render {\n"
                                               "        <Text ref=\"label\">idle</Text>\n"
                                               "    }\n"
                                               "}\n");

    Iris::IrisNyxDriver Driver(UmbraConfig(), Project.RootPath());
    const Component      Root = Driver.MountRoot(AppPath, "RefTicker");
    Expect(Driver.Errors().empty(), "the RefTicker fixture compiles and mounts with no errors");
    Expect(Root.Instance != nullptr && Root.Instance->Lifecycle != nullptr,
           "the `auto OnTick = ...` local wires a real Instance->Lifecycle, same as iris-proto's own test");

    BuildContext Context;
    Context.NyxHost = &Driver.Runtime();
    PenumbraUiBackend::RefMap Refs;
    const auto                Built = BuildWidgetTree(Root, Context, /*OutTags=*/nullptr, &Refs);

    auto* LabelWidget = Refs.count("label") != 0 ? dynamic_cast<Label*>(Refs.at("label")) : nullptr;
    Expect(LabelWidget != nullptr, "the ref=\"label\" node built a real Label, recorded in RefMap");
    Expect(LabelWidget != nullptr && LabelWidget->Text == "idle",
           "before OnTick ever runs, the Label still shows its original render-time text");

    Root.Instance->Lifecycle->OnTick(Umbra::TickInfo{0.5f});

    Expect(LabelWidget != nullptr && LabelWidget->Text == "ticked",
           "OnTick's GetRef(\"label\").SetText(...) call reached the real built Label, not a copy");
    Expect(LabelWidget != nullptr && LabelWidget->ColorText.R == 1 && LabelWidget->ColorText.G == 2 &&
               LabelWidget->ColorText.B == 3 && LabelWidget->ColorText.A == 255,
           "GetRef(...).SetColor(...) reached the same real Label's ColorText field");
    Expect(LabelWidget != nullptr && !LabelWidget->GetIsVisible(),
           "GetRef(...).SetVisible(false) reached the same real Label's WidgetBase::IsVisible field");
}

void TestNoNyxHostMeansNoGetRefCapabilityEvenWithARealNyxLifecycle() {
    TempProject       Project;
    const std::string AppPath = Project.Write("NoHostTicker.irisx",
                                               "void NoHostTicker() {\n"
                                               "    auto OnTick = (float dt) -> {\n"
                                               "        auto handle = GetRef(\"label\");\n"
                                               "        handle.SetText(\"ticked\");\n"
                                               "    };\n"
                                               "\n"
                                               "    render {\n"
                                               "        <Text ref=\"label\">idle</Text>\n"
                                               "    }\n"
                                               "}\n");

    Iris::IrisNyxDriver Driver(UmbraConfig(), Project.RootPath());
    const Component      Root = Driver.MountRoot(AppPath, "NoHostTicker");
    Expect(Driver.Errors().empty(), "the NoHostTicker fixture compiles and mounts with no errors");

    BuildContext Context; // NyxHost left null, the default
    PenumbraUiBackend::RefMap Refs;
    const auto                Built = BuildWidgetTree(Root, Context, /*OutTags=*/nullptr, &Refs);

    auto* LabelWidget = Refs.count("label") != 0 ? dynamic_cast<Label*>(Refs.at("label")) : nullptr;
    Expect(LabelWidget != nullptr, "the ref=\"label\" node still builds normally with no NyxHost set");

    // GetRef was never defined into this instance's own RenderScope Environment, so the
    // OnTick body's own GetRef(...) call hits an ordinary Nyx-level "undefined variable"
    // RuntimeError -- caught and converted to an error Value inside NyxRuntime::
    // EvaluateInScope, not a C++ exception escaping this call, and not a crash.
    Root.Instance->Lifecycle->OnTick(Umbra::TickInfo{0.5f});

    Expect(LabelWidget != nullptr && LabelWidget->Text == "idle",
           "Context.NyxHost == nullptr (the default) means GetRef doesn't exist for this component at "
           "all -- OnTick's own call to it fails at the Nyx level and never reaches the real Label, "
           "exactly pre-wiring behavior");
}

} // namespace

void RunWalkerTests() {
    TestNoneProducesNoWidget();
    TestFrameBuildsABoxWithClassName();
    TestFrameChildrenAreAttachedAndNoneChildrenAreSkipped();
    TestFramePressEventReachesWidgetBase();
    TestGridBuildsABoxWithHorizontalStackLayout();
    TestInlineBuildsAnInlineContainer();
    TestTextBuildsALabelWithContent();
    TestTextPicksUpFontFromBuildContext();
    TestImageBuildsWithoutLoadingWhenNoBackendProvided();
    TestIconBuildsWithIconNameEvenWithoutBackendProvided();
    TestIconPicksUpIconBackendFromBuildContext();
    TestIconSizeOverridesTheDefault();
    TestIconWithNoSizePropKeepsTheDefault();
    TestScrollBuildsAScrollablePanelWithChildrenAndWheelStep();
    TestScrollWithNoWheelStepKeepsTheDefault();
    TestInputBuildsATextInputWithTextAndPreferredWidth();
    TestInputPicksUpFocusAndClipboardFromBuildContext();
    TestInputOnTextChangeReachesTextInputOnTextChanged();
    TestSplitBuildsASplitPanelWithBothPanesAndProps();
    TestSplitWithNoPropsKeepsDefaults();
    TestNativeUnwrapsAPenumbraWidgetToItsRealWidgetBase();
    TestNativeWithNoBuilderProducesNoWidget();
    TestNativeBuildReturningNonPenumbraWidgetProducesNoWidget();
    TestNestedTreeBuildsRecursively();
    TestRefTaggedNodeIsRecordedInOutRefs();
    TestNoRefLeavesOutRefsEmpty();
    TestLifecycleRegistersOnBuildWhenLifecycleHostIsSet();
    TestLifecycleUnregistersWhenTheBuiltWidgetIsDestroyed();
    TestUmbraLifecycleBridgeAbortsWhenInnerLifecycleIsDestroyedFirst();
    TestNoLifecycleHostMeansNoRegistrationEvenWithALiveInstance();
    TestNoComponentInstanceMeansNoRegistrationEvenWithALifecycleHost();
    TestNestedNonSlotComponentInvocationAlsoRegistersItsOwnLifecycle();
    TestApplicationRegisterLifecycleStillForwardsToItsOwnLifecycleRegistry();
    TestNyxOnTickCanLookUpAndMutateARefdWidgetViaGetRef();
    TestNoNyxHostMeansNoGetRefCapabilityEvenWithARealNyxLifecycle();
}

void RunPenumbraWidgetAdapterTests();      // tests/PenumbraWidgetAdapterTests.cpp
void RunSlotWiringTests();                 // tests/SlotWiringTests.cpp
void RunLustreStyleApplierTests();         // tests/LustreStyleApplierTests.cpp
void RunStyleWiringTests();                // tests/StyleWiringTests.cpp
void RunStyleMismatchDiagnosticTests();    // tests/StyleMismatchDiagnosticTests.cpp
void RunStylesheetLoaderTests();           // tests/StylesheetLoaderTests.cpp
void RunNyxApplicationBridgeTests();       // tests/NyxApplicationBridgeTests.cpp

int main() {
    RunNyxApplicationBridgeTests();
    RunWalkerTests();
    RunPenumbraWidgetAdapterTests();
    RunSlotWiringTests();
    RunLustreStyleApplierTests();
    RunStyleWiringTests();
    RunStyleMismatchDiagnosticTests();
    RunStylesheetLoaderTests();

    std::printf("\n%d failure(s)\n", Failures);
    return Failures == 0 ? 0 : 1;
}
