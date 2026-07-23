#include "PenumbraUiBackend/Walker.h"

#include "Penumbra/Backends/IIconBackend.h"
#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/IconWidget.h"
#include "Penumbra/Widgets/ImageWidget.h"
#include "Penumbra/Widgets/InlineContainer.h"
#include "Penumbra/Widgets/Label.h"
#include "Penumbra/Widgets/ScrollablePanel.h"
#include "Penumbra/Widgets/TextInput.h"

#include <cstdio>
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
using Penumbra::Widgets::Box;
using Penumbra::Widgets::IconWidget;
using Penumbra::Widgets::ImageWidget;
using Penumbra::Widgets::InlineContainer;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::LayoutMode;
using Penumbra::Widgets::ScrollablePanel;
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
    TestNestedTreeBuildsRecursively();
}

void RunPenumbraWidgetAdapterTests();      // tests/PenumbraWidgetAdapterTests.cpp
void RunSlotWiringTests();                 // tests/SlotWiringTests.cpp
void RunLustreStyleApplierTests();         // tests/LustreStyleApplierTests.cpp
void RunStyleWiringTests();                // tests/StyleWiringTests.cpp
void RunStyleMismatchDiagnosticTests();    // tests/StyleMismatchDiagnosticTests.cpp

int main() {
    RunWalkerTests();
    RunPenumbraWidgetAdapterTests();
    RunSlotWiringTests();
    RunLustreStyleApplierTests();
    RunStyleWiringTests();
    RunStyleMismatchDiagnosticTests();

    std::printf("\n%d failure(s)\n", Failures);
    return Failures == 0 ? 0 : 1;
}
