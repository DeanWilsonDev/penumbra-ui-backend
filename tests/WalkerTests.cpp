#include "IrisPenumbraBackend/Walker.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/ImageWidget.h"
#include "Penumbra/Widgets/InlineContainer.h"
#include "Penumbra/Widgets/Label.h"

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

using Iris::IrisComponent;
using Iris::IrisElementTag;
using Iris::IrisProps;
using Iris::IrisPropValue;
using IrisPenumbraBackend::BuildContext;
using IrisPenumbraBackend::BuildWidgetTree;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::ImageWidget;
using Penumbra::Widgets::InlineContainer;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::LayoutMode;
using Penumbra::Widgets::WidgetBase;

IrisComponent MakeNode(IrisElementTag Tag, IrisProps Props = {}, std::vector<IrisComponent> Children = {}) {
    return IrisComponent(Tag, std::move(Props), std::move(Children), nullptr);
}

void TestNoneProducesNoWidget() {
    const auto Built = BuildWidgetTree(IrisComponent(nullptr), BuildContext{});
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
    std::vector<IrisComponent> Children;
    Children.push_back(MakeNode(IrisElementTag::Frame));
    Children.push_back(IrisComponent(nullptr)); // None — should be silently dropped
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

void TestNestedTreeBuildsRecursively() {
    // <Frame class="party-row"><HealthBar-shaped Frame/></Frame> — a small stand-in for
    // the spec §9 PartyScreen shape, since <HealthBar> itself is a component invocation
    // Codegen resolves away before this walker ever sees an IrisComponent tree.
    std::vector<IrisComponent> Inner;
    IrisProps                   TextProps;
    TextProps["text"] = IrisPropValue{std::string("42/100")};
    Inner.push_back(MakeNode(IrisElementTag::Text, TextProps));

    std::vector<IrisComponent> Outer;
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
    TestNestedTreeBuildsRecursively();
}

void RunPenumbraWidgetAdapterTests(); // tests/PenumbraWidgetAdapterTests.cpp

int main() {
    RunWalkerTests();
    RunPenumbraWidgetAdapterTests();

    std::printf("\n%d failure(s)\n", Failures);
    return Failures == 0 ? 0 : 1;
}
