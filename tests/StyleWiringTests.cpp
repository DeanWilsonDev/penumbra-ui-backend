#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"
#include "PenumbraUiBackend/Walker.h"

#include "Chaos/Reconciler.h"
#include "Chaos/Signal.h"
#include "Chaos/SlotResolution.h"
#include "Cosmos/Parser.h"

#include "Penumbra/Render/Renderer.h"
#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/IconWidget.h"
#include "Penumbra/Widgets/Label.h"
#include "Penumbra/Widgets/LayoutPass.h"

#include <SDL3/SDL.h>

#include <cmath>

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

extern int Failures; // defined in WalkerTests.cpp

namespace {

void Expect(bool Condition, const std::string& Description) {
    if (Condition) {
        std::printf("[PASS] %s\n", Description.c_str());
    } else {
        std::printf("[FAIL] %s\n", Description.c_str());
        ++Failures;
    }
}

using Chaos::Component;
using Chaos::ChaosElementTag;
using Chaos::ChaosProps;
using Chaos::ChaosPropValue;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::BuildWidgetTree;
using PenumbraUiBackend::PenumbraWidget;
using PenumbraUiBackend::PrimitiveTagMap;
using PenumbraUiBackend::WrapExistingTree;
using PenumbraUiBackend::Cosmos::CosmosStyleApplier;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::IconWidget;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::WidgetBase;

Component MakeNode(ChaosElementTag Tag, ChaosProps Props = {}, std::vector<Component> Children = {}) {
    return Component(Tag, std::move(Props), std::move(Children), nullptr);
}

ChaosProps WithClass(const std::string& ClassName) {
    ChaosProps Props;
    Props["class"] = ChaosPropValue{ClassName};
    return Props;
}

::Cosmos::Stylesheet ParseOrDie(const std::string& Source, const std::string& FilePath) {
    ::Cosmos::Parser Parser(Source, FilePath);
    ::Cosmos::ParseResult Result = Parser.Parse();
    Expect(Result.Errors.empty() && Result.Sheet.has_value(), "test fixture stylesheet `" + FilePath + "` parses cleanly");
    return Result.Sheet.has_value() ? std::move(*Result.Sheet) : ::Cosmos::Stylesheet{};
}

void TestMountAppliesResolvedStyleToABuiltWidget() {
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(R"(
.bar-critical {
    background-color: #E8593C;
    border-radius: 8px;
}
)",
                                                            "HealthBar.cosmos");

    const CosmosStyleApplier Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(ChaosElementTag::Frame, WithClass("bar-critical"));
    const auto Built = BuildWidgetTree(Node, Context);

    if (Built == nullptr) {
        Expect(false, "<Frame class=\"bar-critical\"> builds a widget");
        return;
    }
    const auto* AsBox = dynamic_cast<Box*>(Built.get());
    Expect(AsBox != nullptr, "<Frame> still builds a Box with style wiring in place");
    Expect(AsBox != nullptr && AsBox->Style.ColorBackground.R == 0xE8 && AsBox->Style.ColorBackground.G == 0x59,
           "background-color from the matching class rule reaches Box::Style at mount time");
    Expect(AsBox != nullptr && AsBox->Style.BorderRadius == 8.0F, "border-radius from the same rule also reaches Box::Style");
}

void TestMountLeavesStyleUntouchedWhenNoContextIsSupplied() {
    const auto Node = MakeNode(ChaosElementTag::Frame, WithClass("bar-critical"));
    const auto Built = BuildWidgetTree(Node, BuildContext{}); // Style/StyleApplier both null

    const auto* AsBox = dynamic_cast<Box*>(Built.get());
    Expect(AsBox != nullptr && AsBox->Style.ColorBackground.A == 0,
           "no BuildContext.Style/StyleApplier means no style resolution at all -- unchanged pre-wiring behavior");
}

void TestMountResolvesADescendantSelectorAcrossRealAncestry() {
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(R"(
.card {
    .card-title {
        color: #FFFFFF;
    }
}
)",
                                                            "Card.cosmos");

    const CosmosStyleApplier Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    std::vector<Component> Children;
    Children.push_back(MakeNode(ChaosElementTag::Text, WithClass("card-title")));
    const auto Node = MakeNode(ChaosElementTag::Frame, WithClass("card"), std::move(Children));

    const auto Built = BuildWidgetTree(Node, Context);
    const auto* AsBox = dynamic_cast<Box*>(Built.get());
    const WidgetBase* ChildWidget =
        (AsBox != nullptr && AsBox->GetChildCount() == 1) ? AsBox->GetChildAt(0) : nullptr;
    const auto* AsLabel = dynamic_cast<const Label*>(ChildWidget);

    Expect(AsLabel != nullptr && AsLabel->ColorText.R == 0xFF && AsLabel->ColorText.G == 0xFF,
           "`.card .card-title` resolves across the real Component ancestry built in one BuildWidgetTree call");
}

void TestMountMergesGlobalAndComponentLayers() {
    const ::Cosmos::Stylesheet GlobalSheet = ParseOrDie(".card { background-color: #000000; }", "global.cosmos");
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(".card { border-radius: 4px; }", "Card.cosmos");

    const CosmosStyleApplier Applier;
    const ::Cosmos::StylesheetSet Sheets{&GlobalSheet, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(ChaosElementTag::Frame, WithClass("card"));
    const auto Built = BuildWidgetTree(Node, Context);
    const auto* AsBox = dynamic_cast<Box*>(Built.get());

    Expect(AsBox != nullptr && AsBox->Style.ColorBackground.A == 0xFF,
           "global.cosmos's background-color still applies even though the component file doesn't set it");
    Expect(AsBox != nullptr && AsBox->Style.BorderRadius == 4.0F,
           "the component file's own border-radius applies alongside global's background-color");
}

void TestMountMergesGradientAcrossGlobalAndComponentLayers() {
    // Regression test for the MergeInto() gap where BackgroundGradientStart/
    // End weren't copied field-by-field alongside BackgroundColor et al., so a
    // gradient set only in global.cosmos silently vanished once a component
    // sheet was also merged in.
    const ::Cosmos::Stylesheet GlobalSheet =
        ParseOrDie(".card { background-gradient-start: #4A90FF; background-gradient-end: #000000; }", "global.cosmos");
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(".card { border-radius: 4px; }", "Card.cosmos");

    const CosmosStyleApplier Applier;
    const ::Cosmos::StylesheetSet Sheets{&GlobalSheet, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(ChaosElementTag::Frame, WithClass("card"));
    const auto Built = BuildWidgetTree(Node, Context);
    const auto* AsBox = dynamic_cast<Box*>(Built.get());

    Expect(AsBox != nullptr && AsBox->Style.GradientTop.R == 0x4A && AsBox->Style.GradientTop.B == 0xFF,
           "global.cosmos's background-gradient-start still applies once merged with a component layer");
    Expect(AsBox != nullptr && AsBox->Style.BorderRadius == 4.0F,
           "the component file's own border-radius applies alongside global's gradient");
}

// cosmos_core_spec.md §1.7: color/font inherit from the nearest ancestor that sets them.
// Proves inheritance reaches a real Penumbra widget through an actual BuildWidgetTree
// call (not just a synthetic cosmos-level Resolver test) -- an <Icon> reuses the same
// `color` property Label's ColorText does (docs/next_steps.md's 2026-07-23 second-pass
// entry), so it's the concrete scenario a follow-up question about this session's
// <Split> handle-color work raised: is a nested leaf's foreground color tied to an
// ancestor's, or independently controllable? Answer: independently controllable by
// default (its own `color` rule always wins), but it now inherits when it sets none.
void TestColorInheritsFromAnAncestorFrameToANestedIconWithNoClass() {
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(".row { color: #FFFFFF; }", "test.cosmos");

    const CosmosStyleApplier Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    std::vector<Component> Children;
    Children.push_back(MakeNode(ChaosElementTag::Icon)); // no class of its own
    const auto Node = MakeNode(ChaosElementTag::Frame, WithClass("row"), std::move(Children));

    const auto Built = BuildWidgetTree(Node, Context);
    const auto* AsBox = dynamic_cast<Box*>(Built.get());
    const WidgetBase* ChildWidget =
        (AsBox != nullptr && AsBox->GetChildCount() == 1) ? AsBox->GetChildAt(0) : nullptr;
    const auto* AsIcon = dynamic_cast<const IconWidget*>(ChildWidget);

    Expect(AsIcon != nullptr && AsIcon->ColorLogical.R == 0xFF && AsIcon->ColorLogical.G == 0xFF &&
               AsIcon->ColorLogical.B == 0xFF,
           "an <Icon> with no class of its own inherits color from its ancestor <Frame class=\"row\">");
}

// Companion to the test above -- proves inheritance is scoped to color/font only (§1.7),
// not every property, through the same real BuildWidgetTree path.
void TestBackgroundColorDoesNotInheritToANestedChildWithNoClass() {
    const ::Cosmos::Stylesheet ComponentSheet =
        ParseOrDie(".row { background-color: #FF0000; color: #FFFFFF; }", "test.cosmos");

    const CosmosStyleApplier Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    std::vector<Component> Children;
    Children.push_back(MakeNode(ChaosElementTag::Frame)); // a plain child Box, no class
    const auto Node = MakeNode(ChaosElementTag::Frame, WithClass("row"), std::move(Children));

    const auto Built = BuildWidgetTree(Node, Context);
    const auto* AsBox = dynamic_cast<Box*>(Built.get());
    const WidgetBase* ChildWidget =
        (AsBox != nullptr && AsBox->GetChildCount() == 1) ? AsBox->GetChildAt(0) : nullptr;
    const auto* ChildAsBox = dynamic_cast<const Box*>(ChildWidget);

    Expect(ChildAsBox != nullptr && ChildAsBox->Style.ColorBackground.A == 0,
           "background-color does not inherit to a nested child with no class of its own");
}

void TestClassChangeReResolvesAndAppliesTheNewStyle() {
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(R"(
.bar-normal {
    background-color: #4CAF50;
    padding: 8px;
}

.bar-critical {
    background-color: #E8593C;
}
)",
                                                            "HealthBar.cosmos");

    const CosmosStyleApplier Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(ChaosElementTag::Frame, WithClass("bar-normal"));
    auto Built = BuildWidgetTree(Node, Context);
    auto Wrapper = WrapExistingTree(std::move(Built), nullptr, nullptr, Context.Style, Context.StyleApplier);

    auto* AsBoxBefore = dynamic_cast<Box*>(Wrapper->RawWidget());
    Expect(AsBoxBefore != nullptr && AsBoxBefore->Style.ColorBackground.G == 0xAF,
           "mount resolves .bar-normal's own green background");
    Expect(AsBoxBefore != nullptr && AsBoxBefore->Style.Padding.Left == 8.0F, "and .bar-normal's own padding");

    Umbra::ChaosPropDiff Diff;
    Diff.ClassName = "bar-critical";
    Wrapper->ApplyPropDiff(Diff);

    auto* AsBoxAfter = dynamic_cast<Box*>(Wrapper->RawWidget());
    Expect(AsBoxAfter != nullptr && AsBoxAfter->Style.ColorBackground.R == 0xE8 && AsBoxAfter->Style.ColorBackground.G == 0x59,
           "a class change re-resolves and applies .bar-critical's own background-color");
    Expect(AsBoxAfter != nullptr && AsBoxAfter->Style.Padding.Left == 0.0F,
           ".bar-critical doesn't set padding, so the stale value from .bar-normal is cleared, not left lingering");
}

void TestClassChangeIsANoOpWithoutStyleContext() {
    const auto Node = MakeNode(ChaosElementTag::Frame, WithClass("bar-normal"));
    auto Built = BuildWidgetTree(Node, BuildContext{});
    auto Wrapper = WrapExistingTree(std::move(Built), nullptr, nullptr); // no style context at all

    Umbra::ChaosPropDiff Diff;
    Diff.ClassName = "bar-critical";
    Wrapper->ApplyPropDiff(Diff); // must not crash

    auto* AsBox = dynamic_cast<Box*>(Wrapper->RawWidget());
    Expect(AsBox != nullptr && AsBox->ClassName == "bar-critical",
           "ClassName itself still updates even with no style context configured");
    Expect(AsBox != nullptr && AsBox->Style.ColorBackground.A == 0,
           "and no style resolution is attempted -- unchanged pre-wiring behavior");
}

void TestClassChangeOnANestedChildRespectsRealAncestryThroughTheWrapperTree() {
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(R"(
.card {
    .card-title {
        color: #FFFFFF;
    }
}
)",
                                                            "Card.cosmos");

    const CosmosStyleApplier Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    std::vector<Component> Children;
    Children.push_back(MakeNode(ChaosElementTag::Text));
    const auto Node = MakeNode(ChaosElementTag::Frame, WithClass("card"), std::move(Children));

    auto Built = BuildWidgetTree(Node, Context);
    auto Wrapper = WrapExistingTree(std::move(Built), nullptr, nullptr, Context.Style, Context.StyleApplier);

    Umbra::IWidget* ChildWrapper = Wrapper->GetChildCount() == 1 ? Wrapper->GetChildAt(0) : nullptr;
    if (ChildWrapper == nullptr) {
        Expect(false, "the nested <Text> has a wrapper");
        return;
    }

    Umbra::ChaosPropDiff Diff;
    Diff.ClassName = "card-title";
    ChildWrapper->ApplyPropDiff(Diff);

    const auto* AsLabel = dynamic_cast<const Label*>(dynamic_cast<PenumbraWidget*>(ChildWrapper)->RawWidget());
    Expect(AsLabel != nullptr && AsLabel->ColorText.R == 0xFF,
           "a class change on a wrapped child still resolves `.card .card-title` via the wrapper tree's real Parent_ chain");
}

// Regression test for the Frame/Grid primitive-tag gap: both build to a plain Box, so
// re-resolving style on a class change used to guess "Frame" for both (InferPrimitiveTag's
// dynamic_cast fallback), silently breaking a `grid { }` primitive-element selector on
// anything past the initial mount. BuildWidgetTree's PrimitiveTagMap out-param plus
// WrapExistingTree threading it onto each PenumbraWidget fixes this -- verified here by
// giving `grid { }` and `frame { }` deliberately different colors and confirming the
// *grid* one still applies after a class change forces re-resolution.
void TestClassChangeStillResolvesAGridPrimitiveSelectorCorrectly() {
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(R"(
grid {
    background-color: #00FF00;
}

frame {
    background-color: #FF0000;
}
)",
                                                            "Grid.cosmos");

    const CosmosStyleApplier Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto    Node = MakeNode(ChaosElementTag::Grid);
    PrimitiveTagMap Tags;
    auto Built = BuildWidgetTree(Node, Context, &Tags);
    auto Wrapper = WrapExistingTree(std::move(Built), nullptr, nullptr, Context.Style, Context.StyleApplier, &Tags);

    auto* AsBoxBefore = dynamic_cast<Box*>(Wrapper->RawWidget());
    Expect(AsBoxBefore != nullptr && AsBoxBefore->Style.ColorBackground.G == 0xFF,
           "mount resolves the grid { } primitive selector (green, not frame's red)");
    Expect(Wrapper->GetPrimitiveTag() == "Grid", "the wrapper's own GetPrimitiveTag() reports the real tag");

    Umbra::ChaosPropDiff Diff;
    Diff.ClassName = ""; // trigger re-resolution -- a primitive selector doesn't care what it is
    Wrapper->ApplyPropDiff(Diff);

    auto* AsBoxAfter = dynamic_cast<Box*>(Wrapper->RawWidget());
    Expect(AsBoxAfter != nullptr && AsBoxAfter->Style.ColorBackground.G == 0xFF && AsBoxAfter->Style.ColorBackground.R == 0,
           "a class change still resolves grid { } correctly, not frame { }'s color -- the primitive-tag gap is fixed");
}

// The fallback (no PrimitiveTagMap given -- e.g. a widget wrapped some other way) still
// degrades to the old dynamic_cast-based guess rather than crashing or leaving the tag
// empty; documented as "Frame" for a plain Box, matching InferPrimitiveTag's own comment.
void TestClassChangeFallsBackToFrameGuessWithoutAPrimitiveTagMap() {
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie("frame { background-color: #4CAF50; }", "test.cosmos");

    const CosmosStyleApplier Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(ChaosElementTag::Grid); // really a Grid, but no map to say so
    auto       Built = BuildWidgetTree(Node, Context); // no OutTags
    auto       Wrapper = WrapExistingTree(std::move(Built), nullptr, nullptr, Context.Style, Context.StyleApplier); // no Tags

    Expect(Wrapper->GetPrimitiveTag().empty(), "with no PrimitiveTagMap, GetPrimitiveTag() is empty, not guessed eagerly");

    Umbra::ChaosPropDiff Diff;
    Diff.ClassName = "";
    Wrapper->ApplyPropDiff(Diff);

    auto* AsBox = dynamic_cast<Box*>(Wrapper->RawWidget());
    Expect(AsBox != nullptr && AsBox->Style.ColorBackground.G == 0xAF,
           "re-resolution still falls back to treating an untagged plain Box as \"Frame\", matching frame { }");
}

} // namespace

// Lays out and draws Root through a software renderer, one frame of DeltaSeconds per
// call: Box transitions advance in Draw, on the Renderer's frame clock.
class FrameStepper {
public:
    FrameStepper() {
        Surface_ = SDL_CreateSurface(200, 40, SDL_PIXELFORMAT_RGBA8888);
        Sdl_     = Surface_ ? SDL_CreateSoftwareRenderer(Surface_) : nullptr;
        Ready_   = Sdl_ != nullptr && Renderer_.Initialise(Sdl_, 1.0F, nullptr);
    }
    ~FrameStepper() {
        if (Sdl_) SDL_DestroyRenderer(Sdl_);
        if (Surface_) SDL_DestroySurface(Surface_);
    }

    bool Ready() const { return Ready_; }

    void Step(WidgetBase& Root, float DeltaSeconds) {
        {
            const Penumbra::Widgets::LayoutPass Pass;
            Root.Measure({200.0F, 40.0F});
            Root.Arrange({0.0F, 0.0F, 200.0F, 40.0F});
        }
        Renderer_.BeginFrame({0, 0, 0, 255}, DeltaSeconds);
        Root.Draw(Renderer_);
        Renderer_.EndFrameAndPresent();
    }

private:
    SDL_Surface*               Surface_{nullptr};
    SDL_Renderer*              Sdl_{nullptr};
    Penumbra::Render::Renderer Renderer_;
    bool                       Ready_{false};
};

// docs/next_steps.md's "Animate layout and colour from Cosmos" acceptance: a Frame whose
// class goes from g-0 to g-60 under `transition: flex-grow 0.6s ease-out` eases there
// over 0.6s, with nothing done per frame by the app.
void TestAClassChangeEasesFlexGrowUnderACosmosTransition() {
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(R"(
.track { display: stack; flex-direction: row; }
.grow { transition: flex-grow 0.6s ease-out; }
.g-0 { flex-grow: 0; }
.g-40 { flex-grow: 40; }
.g-60 { flex-grow: 60; }
.g-100 { flex-grow: 100; }
)",
                                                            "Stats.cosmos");
    const CosmosStyleApplier      Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext                  Context;
    Context.Style        = &Sheets;
    Context.StyleApplier = &Applier;

    PrimitiveTagMap Tags;
    const auto      Node = MakeNode(ChaosElementTag::Frame, WithClass("track"),
                                    {MakeNode(ChaosElementTag::Frame, WithClass("grow g-0")),
                                     MakeNode(ChaosElementTag::Frame, WithClass("grow g-100"))});
    auto Wrapper = WrapExistingTree(BuildWidgetTree(Node, Context, &Tags), nullptr, nullptr, &Sheets, &Applier, &Tags);

    FrameStepper Frames;
    if (!Wrapper || Wrapper->GetChildCount() != 2 || !Frames.Ready()) {
        Expect(false, "the g-0/g-100 track mounts, and a software renderer is available to step frames");
        return;
    }
    WidgetBase& Root = *Wrapper->RawWidget();
    WidgetBase* Bar  = static_cast<PenumbraWidget*>(Wrapper->GetChildAt(0))->RawWidget();

    Frames.Step(Root, 0.1F);
    Frames.Step(Root, 0.1F);
    Expect(Bar->GetArrangedRect().W == 0.0F, "the g-0 bar starts with no width");

    Umbra::ChaosPropDiff ToBar;
    ToBar.ClassName = "grow g-60";
    Wrapper->GetChildAt(0)->ApplyPropDiff(ToBar);
    Umbra::ChaosPropDiff ToRest;
    ToRest.ClassName = "grow g-40";
    Wrapper->GetChildAt(1)->ApplyPropDiff(ToRest);

    Frames.Step(Root, 0.1F); // picks up the change: still where it was
    Expect(Bar->GetArrangedRect().W == 0.0F, "on the frame the class changes, the bar hasn't jumped");
    for (int Frame = 0; Frame < 3; ++Frame) {
        Frames.Step(Root, 0.1F);
    }
    // 0.3s into 0.6s ease-out (cubic): 0.875 of the way, so the bar's share is 52.5 of 100.
    Expect(std::abs(Bar->GetArrangedRect().W - 200.0F * 0.525F) < 0.5F,
           "0.3s in, the bar is 0.875 of the way along its ease-out, with no per-frame work from the app");
    for (int Frame = 0; Frame < 4; ++Frame) {
        Frames.Step(Root, 0.1F);
    }
    Expect(std::abs(Bar->GetArrangedRect().W - 120.0F) < 0.5F, "after 0.6s the bar settles at g-60");
}

void TestAStartingStyleGrowsANewlyMountedBarIn() {
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(R"(
.track { display: stack; flex-direction: row; }
.bar {
    flex-grow: 1;
    transition: flex-grow 1s linear;
    @starting-style { flex-grow: 0; }
}
.rest { flex-grow: 1; }
)",
                                                            "Bars.cosmos");
    const CosmosStyleApplier      Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext                  Context;
    Context.Style        = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node  = MakeNode(ChaosElementTag::Frame, WithClass("track"),
                                {MakeNode(ChaosElementTag::Frame, WithClass("bar")),
                                 MakeNode(ChaosElementTag::Frame, WithClass("rest"))});
    auto       Built = BuildWidgetTree(Node, Context);
    auto*      Track = dynamic_cast<Box*>(Built.get());
    FrameStepper Frames;
    if (Track == nullptr || Track->GetChildCount() != 2 || !Frames.Ready()) {
        Expect(false, "the bar/rest track mounts, and a software renderer is available to step frames");
        return;
    }
    auto* Bar = dynamic_cast<Box*>(Track->GetChildAt(0));
    Expect(Bar != nullptr && Bar->StartingStyle.has_value() && Bar->StartingStyle->FlexGrow == 0.0F &&
               Bar->StartingStyle->Transitions.size() == 1,
           "@starting-style reaches Box::StartingStyle: the bar's own style with flex-grow 0 laid over it");

    Frames.Step(*Track, 0.1F);
    Expect(Bar != nullptr && Bar->GetArrangedRect().W == 0.0F, "the newly mounted bar appears at its starting flex-grow");
    for (int Frame = 0; Frame < 5; ++Frame) {
        Frames.Step(*Track, 0.1F); // the frame clock caps a frame at 0.1s
    }
    // 0.5s into a 1s linear ease from 0 to 1, beside a rest of 1: 0.5 / 1.5 of the width.
    Expect(Bar != nullptr && std::abs(Bar->GetArrangedRect().W - 200.0F / 3.0F) < 0.5F,
           "and grows in toward its own flex-grow");
}

void TestCosmosOpacityAndTransitionsReachBoxStyle() {
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(R"(
.faded {
    opacity: 0.5;
    transition: opacity 0.3s ease-in, all 0.2s linear, border-radius 1s;
}
)",
                                                            "Faded.cosmos");
    const CosmosStyleApplier      Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext                  Context;
    Context.Style        = &Sheets;
    Context.StyleApplier = &Applier;

    const auto  Built = BuildWidgetTree(MakeNode(ChaosElementTag::Frame, WithClass("faded")), Context);
    const auto* AsBox = dynamic_cast<const Box*>(Built.get());
    if (AsBox == nullptr) {
        Expect(false, "<Frame class=\"faded\"> builds a Box");
        return;
    }
    using Penumbra::Widgets::TransitionProperty;
    const auto& Transitions = AsBox->Style.Transitions;
    Expect(AsBox->Style.Opacity == 0.5F, "opacity reaches BoxStyle::Opacity");
    Expect(Transitions.size() == 6, "an opacity entry plus `all` expanded to Box's five easable values; border-radius, "
                                    "which Box can't ease, is left out");
    Expect(!Transitions.empty() && Transitions[0].Property == TransitionProperty::Opacity &&
               Transitions[0].DurationSeconds == 0.3F && Transitions[0].Curve == Penumbra::Anim::Easing::EaseIn,
           "a transition entry keeps its duration and easing");
    Expect(!AsBox->StartingStyle.has_value(), "no @starting-style leaves Box::StartingStyle unset");
}

ChaosProps WithClassAndFill(const std::string& ClassName, float Fill) {
    ChaosProps Props = WithClass(ClassName);
    Props["fill"]   = ChaosPropValue{Fill};
    return Props;
}

// `flex-grow: attr(fill)` follows a Frame's `fill` prop all the way through: a
// chaos::Signal changes, chaos::Tick re-renders the Slot, the reconciler's prop diff
// carries the new number, and the bar eases to it under its transition.
void TestAnAttrBindingFollowsItsPropThroughTheReconciler() {
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(R"(
.track { display: stack; flex-direction: row; }
.share { flex-grow: attr(fill); transition: flex-grow 0.6s ease-out; }
)",
                                                            "Share.cosmos");
    const CosmosStyleApplier      Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext                  Context;
    Context.Style        = &Sheets;
    Context.StyleApplier = &Applier;
    const chaos::MountFn Mount = PenumbraUiBackend::MakeMountFn(Context);

    chaos::Signal<float> Fill = 0.0F;
    const Component     RootNode =
        MakeNode(ChaosElementTag::Frame, WithClass("track"),
                 {Component(ChaosElementTag::Slot, {}, {}, Chaos::MakeSlotCallable([&]() -> Component {
                                return MakeNode(ChaosElementTag::Frame, WithClassAndFill("share", Fill.get()));
                            })),
                  Component(ChaosElementTag::Slot, {}, {}, Chaos::MakeSlotCallable([&]() -> Component {
                                return MakeNode(ChaosElementTag::Frame, WithClassAndFill("share", 100.0F - Fill.get()));
                            }))});
    std::unique_ptr<Umbra::IWidget> Root  = Mount(RootNode);
    auto                            Slots = chaos::ResolveSlots(*Root, RootNode, Mount);
    auto* Track = dynamic_cast<Box*>(static_cast<PenumbraWidget*>(Root.get())->RawWidget());

    FrameStepper Frames;
    if (Track == nullptr || Track->GetChildCount() != 2 || !Frames.Ready()) {
        Expect(false, "the attr()-bound track mounts its two Slot frames, and a software renderer is available");
        return;
    }
    WidgetBase* Bar = Track->GetChildAt(0);
    Frames.Step(*Track, 0.1F);
    Expect(Bar->GetArrangedRect().W == 0.0F, "the bar's flex-grow comes from its fill prop through attr(): 0 to start");

    Fill.set(60.0F);
    chaos::Tick();
    Frames.Step(*Track, 0.1F);
    for (int Frame = 0; Frame < 3; ++Frame) {
        Frames.Step(*Track, 0.1F);
    }
    Expect(std::abs(Bar->GetArrangedRect().W - 200.0F * 0.525F) < 0.5F,
           "a Signal change re-renders the Slot, the diff carries fill=60, and the bar eases toward it");
    for (int Frame = 0; Frame < 4; ++Frame) {
        Frames.Step(*Track, 0.1F);
    }
    Expect(std::abs(Bar->GetArrangedRect().W - 120.0F) < 0.5F, "and settles at a 60 share");
}

void TestAClassChangeKeepsAnAttrBoundValue() {
    const ::Cosmos::Stylesheet ComponentSheet = ParseOrDie(R"(
.bar { flex-grow: attr(fill); }
.bar-tall { height: 40px; }
)",
                                                            "Bar.cosmos");
    const CosmosStyleApplier      Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext                  Context;
    Context.Style        = &Sheets;
    Context.StyleApplier = &Applier;

    std::unique_ptr<Umbra::IWidget> Wrapper =
        PenumbraUiBackend::MakeMountFn(Context)(MakeNode(ChaosElementTag::Frame, WithClassAndFill("bar", 30.0F)));
    auto* AsBox = Wrapper ? dynamic_cast<Box*>(static_cast<PenumbraWidget*>(Wrapper.get())->RawWidget()) : nullptr;
    Expect(AsBox != nullptr && AsBox->Style.FlexGrow == 30.0F, "attr(fill) sets flex-grow from the prop at mount");

    Umbra::ChaosPropDiff Diff;
    Diff.ClassName = "bar bar-tall";
    Wrapper->ApplyPropDiff(Diff);
    Expect(AsBox != nullptr && AsBox->Style.FlexGrow == 30.0F && AsBox->Style.Height.IsSet(),
           "a class change re-resolves the style and still fills attr(fill) from the prop the wrapper recorded");

    Umbra::ChaosPropDiff NewFill;
    NewFill.Numbers["fill"] = 45.0F;
    Wrapper->ApplyPropDiff(NewFill);
    Expect(AsBox != nullptr && AsBox->Style.FlexGrow == 45.0F && AsBox->Style.Height.IsSet(),
           "a changed number prop alone re-resolves the style with the new value");
}

void RunStyleWiringTests() {
    TestMountAppliesResolvedStyleToABuiltWidget();
    TestMountLeavesStyleUntouchedWhenNoContextIsSupplied();
    TestMountResolvesADescendantSelectorAcrossRealAncestry();
    TestMountMergesGlobalAndComponentLayers();
    TestMountMergesGradientAcrossGlobalAndComponentLayers();
    TestColorInheritsFromAnAncestorFrameToANestedIconWithNoClass();
    TestBackgroundColorDoesNotInheritToANestedChildWithNoClass();
    TestClassChangeReResolvesAndAppliesTheNewStyle();
    TestClassChangeIsANoOpWithoutStyleContext();
    TestClassChangeOnANestedChildRespectsRealAncestryThroughTheWrapperTree();
    TestClassChangeStillResolvesAGridPrimitiveSelectorCorrectly();
    TestClassChangeFallsBackToFrameGuessWithoutAPrimitiveTagMap();
    TestCosmosOpacityAndTransitionsReachBoxStyle();
    TestAClassChangeEasesFlexGrowUnderACosmosTransition();
    TestAStartingStyleGrowsANewlyMountedBarIn();
    TestAnAttrBindingFollowsItsPropThroughTheReconciler();
    TestAClassChangeKeepsAnAttrBoundValue();
}
