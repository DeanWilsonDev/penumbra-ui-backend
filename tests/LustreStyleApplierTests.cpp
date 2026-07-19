#include "PenumbraUiBackend/Lustre/StyleApplier.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/Button.h"
#include "Penumbra/Widgets/Checkbox.h"
#include "Penumbra/Widgets/Label.h"

#include <cstdio>
#include <string>

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

using PenumbraUiBackend::Lustre::LustreStyleApplier;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::Button;
using Penumbra::Widgets::Checkbox;
using Penumbra::Widgets::Label;

::Lustre::ResolvedStyle MakeStyle() {
    ::Lustre::ResolvedStyle Style;
    Style.BackgroundColor = ::Lustre::Color{0xE8, 0x59, 0x3C, 0xFF};
    Style.BorderRadius = 8.0F;
    Style.Padding = ::Lustre::EdgeInsets{4.0F, 4.0F, 4.0F, 4.0F};
    return Style;
}

void TestBoxStyleReachesAPlainBox() {
    Box              WidgetBox;
    const auto       Style = MakeStyle();
    LustreStyleApplier Applier;

    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Style.ColorBackground.R == 0xE8 && WidgetBox.Style.ColorBackground.G == 0x59 &&
               WidgetBox.Style.ColorBackground.B == 0x3C,
           "background-color reaches Box::Style.ColorBackground");
    Expect(WidgetBox.Style.BorderRadius == 8.0F, "border-radius reaches Box::Style.BorderRadius");
    Expect(WidgetBox.Style.Padding.Left == 4.0F, "padding reaches Box::Style.Padding");
}

void TestUnsetPropertiesLeaveExistingFieldsUntouched() {
    Box WidgetBox;
    WidgetBox.Style.BorderWidth = 2.0F; // pre-existing value the style doesn't mention

    ::Lustre::ResolvedStyle Style;
    Style.BackgroundColor = ::Lustre::Color{0, 0, 0, 255};

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Style.BorderWidth == 2.0F,
           "a property the resolved style never set is left untouched, not reset to a default");
}

void TestHoverOverlayReachesButtonOnlyOnButtonWidgets() {
    Button                  WidgetButton;
    ::Lustre::ResolvedStyle Style = MakeStyle();
    Style.Hover = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Hover->BackgroundColor = ::Lustre::Color{0x66, 0xBB, 0x6A, 0xFF};

    LustreStyleApplier Applier;
    Applier.Apply(WidgetButton, Style);

    Expect(WidgetButton.ColorBackgroundHovered.R == 0x66 && WidgetButton.ColorBackgroundHovered.G == 0xBB,
           ":hover background-color reaches Button::ColorBackgroundHovered");
    // The base (non-hover) style still applies too -- overlays add to the
    // base rather than replacing what Apply() already did for it.
    Expect(WidgetButton.Style.ColorBackground.R == 0xE8, "the base style still applies alongside the hover overlay");
}

void TestHoverOverlayIsStubbedOnAPlainBox() {
    // §2/lustre_style_gaps_requirements.md §1: BoxStyle has no
    // interaction-state fields at all yet -- a :hover rule on a Frame-classed
    // element resolves into the IR (Lustre's job) but has nowhere to land in
    // Penumbra today. Apply() should simply not crash or misapply it.
    Box                      WidgetBox;
    ::Lustre::ResolvedStyle  Style;
    Style.Hover = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Hover->BackgroundColor = ::Lustre::Color{0x66, 0xBB, 0x6A, 0xFF};

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Style.ColorBackground.A == 0, "a :hover-only style with no base leaves Box::Style at its default");
}

void TestTextColorReachesALabel() {
    Label                    WidgetLabel;
    ::Lustre::ResolvedStyle  Style;
    Style.TextColor = ::Lustre::Color{0xFF, 0xFF, 0xFF, 0xFF};

    LustreStyleApplier Applier;
    Applier.Apply(WidgetLabel, Style);

    Expect(WidgetLabel.ColorText.R == 0xFF && WidgetLabel.ColorText.G == 0xFF && WidgetLabel.ColorText.B == 0xFF,
           "color reaches Label::ColorText");
}

void TestDisplayStackWithRowFlexDirectionMapsToHorizontalStack() {
    Box                     WidgetBox;
    ::Lustre::ResolvedStyle Style;
    Style.DisplayMode = ::Lustre::Display::Stack;
    Style.FlexDirectionMode = ::Lustre::FlexDirection::Row;

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Layout == Penumbra::Widgets::LayoutMode::HorizontalStack,
           "display: stack + flex-direction: row reaches Box::Layout as HorizontalStack");
}

void TestDisplayStackWithColumnFlexDirectionMapsToVerticalStack() {
    Box                     WidgetBox;
    ::Lustre::ResolvedStyle Style;
    Style.DisplayMode = ::Lustre::Display::Stack;
    Style.FlexDirectionMode = ::Lustre::FlexDirection::Column;

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Layout == Penumbra::Widgets::LayoutMode::VerticalStack,
           "display: stack + flex-direction: column reaches Box::Layout as VerticalStack");
}

void TestDisplayInlineMapsToLayoutNone() {
    Box                     WidgetBox;
    WidgetBox.Layout = Penumbra::Widgets::LayoutMode::HorizontalStack; // pre-existing, should be overwritten
    ::Lustre::ResolvedStyle Style;
    Style.DisplayMode = ::Lustre::Display::Inline;

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Layout == Penumbra::Widgets::LayoutMode::None, "display: inline reaches Box::Layout as None");
}

void TestNoDisplaySetLeavesLayoutUntouched() {
    Box WidgetBox;
    WidgetBox.Layout = Penumbra::Widgets::LayoutMode::HorizontalStack;
    const auto Style = MakeStyle(); // never sets DisplayMode

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Layout == Penumbra::Widgets::LayoutMode::HorizontalStack,
           "a style that never sets display leaves Box::Layout exactly as it was");
}

void TestGapAndAlignItemsReachBox() {
    Box                     WidgetBox;
    ::Lustre::ResolvedStyle Style;
    Style.Gap = 12.0F;
    Style.AlignItems = ::Lustre::Align::Center;

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.ChildGap == 12.0F, "gap reaches Box::ChildGap");
    Expect(WidgetBox.CrossAlignment == Penumbra::Widgets::CrossAlign::Center,
           "align-items reaches Box::CrossAlignment");
}

void TestCheckboxStillReceivesItsBoxStyleSlice() {
    Checkbox                 WidgetCheckbox;
    const auto                Style = MakeStyle();

    LustreStyleApplier Applier;
    Applier.Apply(WidgetCheckbox, Style);

    Expect(WidgetCheckbox.Style.ColorBackground.R == 0xE8,
           "a Checkbox still receives the universal BoxStyle slice via its Box base");
}

} // namespace

void RunLustreStyleApplierTests() {
    TestBoxStyleReachesAPlainBox();
    TestUnsetPropertiesLeaveExistingFieldsUntouched();
    TestHoverOverlayReachesButtonOnlyOnButtonWidgets();
    TestHoverOverlayIsStubbedOnAPlainBox();
    TestTextColorReachesALabel();
    TestDisplayStackWithRowFlexDirectionMapsToHorizontalStack();
    TestDisplayStackWithColumnFlexDirectionMapsToVerticalStack();
    TestDisplayInlineMapsToLayoutNone();
    TestNoDisplaySetLeavesLayoutUntouched();
    TestGapAndAlignItemsReachBox();
    TestCheckboxStillReceivesItsBoxStyleSlice();
}
