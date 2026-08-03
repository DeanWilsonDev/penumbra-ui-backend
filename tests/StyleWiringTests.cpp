#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"
#include "PenumbraUiBackend/Walker.h"

#include "Lustre/Parser.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/IconWidget.h"
#include "Penumbra/Widgets/Label.h"

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

using Iris::Component;
using Iris::IrisElementTag;
using Iris::IrisProps;
using Iris::IrisPropValue;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::BuildWidgetTree;
using PenumbraUiBackend::PenumbraWidget;
using PenumbraUiBackend::PrimitiveTagMap;
using PenumbraUiBackend::WrapExistingTree;
using PenumbraUiBackend::Lustre::LustreStyleApplier;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::IconWidget;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::WidgetBase;

Component MakeNode(IrisElementTag Tag, IrisProps Props = {}, std::vector<Component> Children = {}) {
    return Component(Tag, std::move(Props), std::move(Children), nullptr);
}

IrisProps WithClass(const std::string& ClassName) {
    IrisProps Props;
    Props["class"] = IrisPropValue{ClassName};
    return Props;
}

::Lustre::Stylesheet ParseOrDie(const std::string& Source, const std::string& FilePath) {
    ::Lustre::Parser Parser(Source, FilePath);
    ::Lustre::ParseResult Result = Parser.Parse();
    Expect(Result.Errors.empty() && Result.Sheet.has_value(), "test fixture stylesheet `" + FilePath + "` parses cleanly");
    return Result.Sheet.has_value() ? std::move(*Result.Sheet) : ::Lustre::Stylesheet{};
}

void TestMountAppliesResolvedStyleToABuiltWidget() {
    const ::Lustre::Stylesheet ComponentSheet = ParseOrDie(R"(
.bar-critical {
    background-color: #E8593C;
    border-radius: 8px;
}
)",
                                                            "HealthBar.lustre");

    const LustreStyleApplier Applier;
    const ::Lustre::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("bar-critical"));
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
    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("bar-critical"));
    const auto Built = BuildWidgetTree(Node, BuildContext{}); // Style/StyleApplier both null

    const auto* AsBox = dynamic_cast<Box*>(Built.get());
    Expect(AsBox != nullptr && AsBox->Style.ColorBackground.A == 0,
           "no BuildContext.Style/StyleApplier means no style resolution at all -- unchanged pre-wiring behavior");
}

void TestMountResolvesADescendantSelectorAcrossRealAncestry() {
    const ::Lustre::Stylesheet ComponentSheet = ParseOrDie(R"(
.card {
    .card-title {
        color: #FFFFFF;
    }
}
)",
                                                            "Card.lustre");

    const LustreStyleApplier Applier;
    const ::Lustre::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    std::vector<Component> Children;
    Children.push_back(MakeNode(IrisElementTag::Text, WithClass("card-title")));
    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("card"), std::move(Children));

    const auto Built = BuildWidgetTree(Node, Context);
    const auto* AsBox = dynamic_cast<Box*>(Built.get());
    const WidgetBase* ChildWidget =
        (AsBox != nullptr && AsBox->GetChildCount() == 1) ? AsBox->GetChildAt(0) : nullptr;
    const auto* AsLabel = dynamic_cast<const Label*>(ChildWidget);

    Expect(AsLabel != nullptr && AsLabel->ColorText.R == 0xFF && AsLabel->ColorText.G == 0xFF,
           "`.card .card-title` resolves across the real Component ancestry built in one BuildWidgetTree call");
}

void TestMountMergesGlobalAndComponentLayers() {
    const ::Lustre::Stylesheet GlobalSheet = ParseOrDie(".card { background-color: #000000; }", "global.lustre");
    const ::Lustre::Stylesheet ComponentSheet = ParseOrDie(".card { border-radius: 4px; }", "Card.lustre");

    const LustreStyleApplier Applier;
    const ::Lustre::StylesheetSet Sheets{&GlobalSheet, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("card"));
    const auto Built = BuildWidgetTree(Node, Context);
    const auto* AsBox = dynamic_cast<Box*>(Built.get());

    Expect(AsBox != nullptr && AsBox->Style.ColorBackground.A == 0xFF,
           "global.lustre's background-color still applies even though the component file doesn't set it");
    Expect(AsBox != nullptr && AsBox->Style.BorderRadius == 4.0F,
           "the component file's own border-radius applies alongside global's background-color");
}

void TestMountMergesGradientAcrossGlobalAndComponentLayers() {
    // Regression test for the MergeInto() gap where BackgroundGradientStart/
    // End weren't copied field-by-field alongside BackgroundColor et al., so a
    // gradient set only in global.lustre silently vanished once a component
    // sheet was also merged in.
    const ::Lustre::Stylesheet GlobalSheet =
        ParseOrDie(".card { background-gradient-start: #4A90FF; background-gradient-end: #000000; }", "global.lustre");
    const ::Lustre::Stylesheet ComponentSheet = ParseOrDie(".card { border-radius: 4px; }", "Card.lustre");

    const LustreStyleApplier Applier;
    const ::Lustre::StylesheetSet Sheets{&GlobalSheet, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("card"));
    const auto Built = BuildWidgetTree(Node, Context);
    const auto* AsBox = dynamic_cast<Box*>(Built.get());

    Expect(AsBox != nullptr && AsBox->Style.GradientTop.R == 0x4A && AsBox->Style.GradientTop.B == 0xFF,
           "global.lustre's background-gradient-start still applies once merged with a component layer");
    Expect(AsBox != nullptr && AsBox->Style.BorderRadius == 4.0F,
           "the component file's own border-radius applies alongside global's gradient");
}

// lustre_core_spec.md §1.7: color/font inherit from the nearest ancestor that sets them.
// Proves inheritance reaches a real Penumbra widget through an actual BuildWidgetTree
// call (not just a synthetic lustre-level Resolver test) -- an <Icon> reuses the same
// `color` property Label's ColorText does (docs/next_steps.md's 2026-07-23 second-pass
// entry), so it's the concrete scenario a follow-up question about this session's
// <Split> handle-color work raised: is a nested leaf's foreground color tied to an
// ancestor's, or independently controllable? Answer: independently controllable by
// default (its own `color` rule always wins), but it now inherits when it sets none.
void TestColorInheritsFromAnAncestorFrameToANestedIconWithNoClass() {
    const ::Lustre::Stylesheet ComponentSheet = ParseOrDie(".row { color: #FFFFFF; }", "test.lustre");

    const LustreStyleApplier Applier;
    const ::Lustre::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    std::vector<Component> Children;
    Children.push_back(MakeNode(IrisElementTag::Icon)); // no class of its own
    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("row"), std::move(Children));

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
    const ::Lustre::Stylesheet ComponentSheet =
        ParseOrDie(".row { background-color: #FF0000; color: #FFFFFF; }", "test.lustre");

    const LustreStyleApplier Applier;
    const ::Lustre::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    std::vector<Component> Children;
    Children.push_back(MakeNode(IrisElementTag::Frame)); // a plain child Box, no class
    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("row"), std::move(Children));

    const auto Built = BuildWidgetTree(Node, Context);
    const auto* AsBox = dynamic_cast<Box*>(Built.get());
    const WidgetBase* ChildWidget =
        (AsBox != nullptr && AsBox->GetChildCount() == 1) ? AsBox->GetChildAt(0) : nullptr;
    const auto* ChildAsBox = dynamic_cast<const Box*>(ChildWidget);

    Expect(ChildAsBox != nullptr && ChildAsBox->Style.ColorBackground.A == 0,
           "background-color does not inherit to a nested child with no class of its own");
}

void TestClassChangeReResolvesAndAppliesTheNewStyle() {
    const ::Lustre::Stylesheet ComponentSheet = ParseOrDie(R"(
.bar-normal {
    background-color: #4CAF50;
    padding: 8px;
}

.bar-critical {
    background-color: #E8593C;
}
)",
                                                            "HealthBar.lustre");

    const LustreStyleApplier Applier;
    const ::Lustre::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("bar-normal"));
    auto Built = BuildWidgetTree(Node, Context);
    auto Wrapper = WrapExistingTree(std::move(Built), nullptr, nullptr, Context.Style, Context.StyleApplier);

    auto* AsBoxBefore = dynamic_cast<Box*>(Wrapper->RawWidget());
    Expect(AsBoxBefore != nullptr && AsBoxBefore->Style.ColorBackground.G == 0xAF,
           "mount resolves .bar-normal's own green background");
    Expect(AsBoxBefore != nullptr && AsBoxBefore->Style.Padding.Left == 8.0F, "and .bar-normal's own padding");

    Umbra::IrisPropDiff Diff;
    Diff.ClassName = "bar-critical";
    Wrapper->ApplyPropDiff(Diff);

    auto* AsBoxAfter = dynamic_cast<Box*>(Wrapper->RawWidget());
    Expect(AsBoxAfter != nullptr && AsBoxAfter->Style.ColorBackground.R == 0xE8 && AsBoxAfter->Style.ColorBackground.G == 0x59,
           "a class change re-resolves and applies .bar-critical's own background-color");
    Expect(AsBoxAfter != nullptr && AsBoxAfter->Style.Padding.Left == 0.0F,
           ".bar-critical doesn't set padding, so the stale value from .bar-normal is cleared, not left lingering");
}

void TestClassChangeIsANoOpWithoutStyleContext() {
    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("bar-normal"));
    auto Built = BuildWidgetTree(Node, BuildContext{});
    auto Wrapper = WrapExistingTree(std::move(Built), nullptr, nullptr); // no style context at all

    Umbra::IrisPropDiff Diff;
    Diff.ClassName = "bar-critical";
    Wrapper->ApplyPropDiff(Diff); // must not crash

    auto* AsBox = dynamic_cast<Box*>(Wrapper->RawWidget());
    Expect(AsBox != nullptr && AsBox->ClassName == "bar-critical",
           "ClassName itself still updates even with no style context configured");
    Expect(AsBox != nullptr && AsBox->Style.ColorBackground.A == 0,
           "and no style resolution is attempted -- unchanged pre-wiring behavior");
}

void TestClassChangeOnANestedChildRespectsRealAncestryThroughTheWrapperTree() {
    const ::Lustre::Stylesheet ComponentSheet = ParseOrDie(R"(
.card {
    .card-title {
        color: #FFFFFF;
    }
}
)",
                                                            "Card.lustre");

    const LustreStyleApplier Applier;
    const ::Lustre::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    std::vector<Component> Children;
    Children.push_back(MakeNode(IrisElementTag::Text));
    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("card"), std::move(Children));

    auto Built = BuildWidgetTree(Node, Context);
    auto Wrapper = WrapExistingTree(std::move(Built), nullptr, nullptr, Context.Style, Context.StyleApplier);

    Umbra::IWidget* ChildWrapper = Wrapper->GetChildCount() == 1 ? Wrapper->GetChildAt(0) : nullptr;
    if (ChildWrapper == nullptr) {
        Expect(false, "the nested <Text> has a wrapper");
        return;
    }

    Umbra::IrisPropDiff Diff;
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
    const ::Lustre::Stylesheet ComponentSheet = ParseOrDie(R"(
grid {
    background-color: #00FF00;
}

frame {
    background-color: #FF0000;
}
)",
                                                            "Grid.lustre");

    const LustreStyleApplier Applier;
    const ::Lustre::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto    Node = MakeNode(IrisElementTag::Grid);
    PrimitiveTagMap Tags;
    auto Built = BuildWidgetTree(Node, Context, &Tags);
    auto Wrapper = WrapExistingTree(std::move(Built), nullptr, nullptr, Context.Style, Context.StyleApplier, &Tags);

    auto* AsBoxBefore = dynamic_cast<Box*>(Wrapper->RawWidget());
    Expect(AsBoxBefore != nullptr && AsBoxBefore->Style.ColorBackground.G == 0xFF,
           "mount resolves the grid { } primitive selector (green, not frame's red)");
    Expect(Wrapper->GetPrimitiveTag() == "Grid", "the wrapper's own GetPrimitiveTag() reports the real tag");

    Umbra::IrisPropDiff Diff;
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
    const ::Lustre::Stylesheet ComponentSheet = ParseOrDie("frame { background-color: #4CAF50; }", "test.lustre");

    const LustreStyleApplier Applier;
    const ::Lustre::StylesheetSet Sheets{nullptr, &ComponentSheet};
    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(IrisElementTag::Grid); // really a Grid, but no map to say so
    auto       Built = BuildWidgetTree(Node, Context); // no OutTags
    auto       Wrapper = WrapExistingTree(std::move(Built), nullptr, nullptr, Context.Style, Context.StyleApplier); // no Tags

    Expect(Wrapper->GetPrimitiveTag().empty(), "with no PrimitiveTagMap, GetPrimitiveTag() is empty, not guessed eagerly");

    Umbra::IrisPropDiff Diff;
    Diff.ClassName = "";
    Wrapper->ApplyPropDiff(Diff);

    auto* AsBox = dynamic_cast<Box*>(Wrapper->RawWidget());
    Expect(AsBox != nullptr && AsBox->Style.ColorBackground.G == 0xAF,
           "re-resolution still falls back to treating an untagged plain Box as \"Frame\", matching frame { }");
}

} // namespace

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
}
