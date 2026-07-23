#include "PenumbraUiBackend/Lustre/StyleApplier.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/Button.h"
#include "Penumbra/Widgets/Checkbox.h"
#include "Penumbra/Widgets/IconWidget.h"
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
using Penumbra::Widgets::IconWidget;
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

void TestHoverOverlayReachesAButtonWidget() {
    Button                  WidgetButton;
    ::Lustre::ResolvedStyle Style = MakeStyle();
    Style.Hover = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Hover->BackgroundColor = ::Lustre::Color{0x66, 0xBB, 0x6A, 0xFF};

    LustreStyleApplier Applier;
    Applier.Apply(WidgetButton, Style);

    Expect(WidgetButton.Style.ColorBackgroundHovered.R == 0x66 && WidgetButton.Style.ColorBackgroundHovered.G == 0xBB,
           ":hover background-color reaches Button::Style.ColorBackgroundHovered");
    // The base (non-hover) style still applies too -- overlays add to the
    // base rather than replacing what Apply() already did for it.
    Expect(WidgetButton.Style.ColorBackground.R == 0xE8, "the base style still applies alongside the hover overlay");
}

void TestHoverOverlayReachesAPlainBoxToo() {
    // BoxStyle::ColorBackgroundHovered/Pressed/Disabled are universal (not
    // Button-only) since Penumbra dd1d6ab, and Box::BackgroundForState
    // already renders them for any Box -- Apply()'s pseudo-class overlay is
    // no longer gated to Button, see docs/pseudo_class_plain_box_decision.md.
    Box                      WidgetBox;
    ::Lustre::ResolvedStyle  Style;
    Style.Hover = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Hover->BackgroundColor = ::Lustre::Color{0x66, 0xBB, 0x6A, 0xFF};

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Style.ColorBackgroundHovered.R == 0x66 && WidgetBox.Style.ColorBackgroundHovered.G == 0xBB,
           ":hover background-color reaches a plain Box::Style.ColorBackgroundHovered too");
}

void TestHoverAndActiveGradientOverlaysReachBoxStyle() {
    Box                     WidgetBox;
    ::Lustre::ResolvedStyle Style = MakeStyle();
    Style.Hover = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Hover->BackgroundGradientStart = ::Lustre::Color{0x4A, 0x90, 0xFF, 0xFF};
    Style.Hover->BackgroundGradientEnd = ::Lustre::Color{0x2A, 0x5A, 0xDD, 0xFF};
    Style.Active = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Active->BackgroundGradientStart = ::Lustre::Color{0x1A, 0x40, 0xAA, 0xFF};
    Style.Active->BackgroundGradientEnd = ::Lustre::Color{0x0A, 0x20, 0x66, 0xFF};

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Style.GradientTopHovered.R == 0x4A && WidgetBox.Style.GradientTopHovered.B == 0xFF,
           ":hover background-gradient-start reaches Box::Style.GradientTopHovered");
    Expect(WidgetBox.Style.GradientBottomHovered.R == 0x2A && WidgetBox.Style.GradientBottomHovered.B == 0xDD,
           ":hover background-gradient-end reaches Box::Style.GradientBottomHovered");
    Expect(WidgetBox.Style.GradientTopPressed.R == 0x1A && WidgetBox.Style.GradientTopPressed.B == 0xAA,
           ":active background-gradient-start reaches Box::Style.GradientTopPressed");
    Expect(WidgetBox.Style.GradientBottomPressed.R == 0x0A && WidgetBox.Style.GradientBottomPressed.B == 0x66,
           ":active background-gradient-end reaches Box::Style.GradientBottomPressed");
}

void TestNoGradientOverlayLeavesPerStateGradientFieldsAtDefault() {
    Box                     WidgetBox;
    ::Lustre::ResolvedStyle Style = MakeStyle();
    Style.Hover = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Hover->BackgroundColor = ::Lustre::Color{0x66, 0xBB, 0x6A, 0xFF}; // no gradient in the overlay

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Style.GradientTopHovered.A == 0,
           "a :hover overlay with no gradient leaves Box::Style.GradientTopHovered at its zero-alpha default");
}

void TestHoverActiveAndDisabledBorderColorOverlaysReachBoxStyle() {
    // Mirrors TestHoverOverlayReachesAPlainBoxToo's ColorBackgroundHovered
    // precedent: BoxStyle::ColorBorderHovered/Pressed/Disabled are universal
    // (penumbra@7fad4dc), so a plain Box picks these up too.
    Box                     WidgetBox;
    ::Lustre::ResolvedStyle Style = MakeStyle();
    Style.Hover = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Hover->BorderColor = ::Lustre::Color{0x66, 0xBB, 0x6A, 0xFF};
    Style.Active = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Active->BorderColor = ::Lustre::Color{0x1A, 0x40, 0xAA, 0xFF};
    Style.Disabled = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Disabled->BorderColor = ::Lustre::Color{0x80, 0x80, 0x80, 0xFF};

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Style.ColorBorderHovered.R == 0x66 && WidgetBox.Style.ColorBorderHovered.G == 0xBB,
           ":hover border-color reaches Box::Style.ColorBorderHovered");
    Expect(WidgetBox.Style.ColorBorderPressed.R == 0x1A && WidgetBox.Style.ColorBorderPressed.B == 0xAA,
           ":active border-color reaches Box::Style.ColorBorderPressed");
    Expect(WidgetBox.Style.ColorBorderDisabled.R == 0x80 && WidgetBox.Style.ColorBorderDisabled.G == 0x80,
           ":disabled border-color reaches Box::Style.ColorBorderDisabled");
}

void TestNoBorderColorOverlayLeavesPerStateBorderFieldsAtDefault() {
    Box                     WidgetBox;
    ::Lustre::ResolvedStyle Style = MakeStyle();
    Style.Hover = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Hover->BackgroundColor = ::Lustre::Color{0x66, 0xBB, 0x6A, 0xFF}; // no border-color in the overlay

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Style.ColorBorderHovered.A == 0,
           "a :hover overlay with no border-color leaves Box::Style.ColorBorderHovered at its zero-alpha default");
}

void TestBoxShadowReachesBoxStyle() {
    Box                     WidgetBox;
    ::Lustre::ResolvedStyle Style = MakeStyle();
    Style.ShadowColor = ::Lustre::Color{0x00, 0x00, 0x00, 0xAA};
    Style.ShadowBlurRadiusLogical = 12.0F;

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Style.ShadowColor.A == 0xAA, "box-shadow color reaches Box::Style.ShadowColor");
    Expect(WidgetBox.Style.ShadowBlurRadiusLogical == 12.0F,
           "box-shadow blur radius reaches Box::Style.ShadowBlurRadiusLogical");
}

void TestNoBoxShadowLeavesBoxStyleShadowFieldsAtDefault() {
    Box        WidgetBox;
    const auto Style = MakeStyle(); // sets BackgroundColor, no box-shadow

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Style.ShadowBlurRadiusLogical == 0.0F,
           "no box-shadow in the style leaves Box::Style.ShadowBlurRadiusLogical at its zero default");
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

void TestColorReachesAnIconWidget() {
    // IconWidget isn't a Box (same as ImageWidget) -- this exercises the
    // dynamic_cast<IconWidget*> branch that has to run before Apply()'s
    // Box-only early return.
    IconWidget               WidgetIcon;
    ::Lustre::ResolvedStyle  Style;
    Style.TextColor = ::Lustre::Color{0xFF, 0xFF, 0xFF, 0xFF};

    LustreStyleApplier Applier;
    Applier.Apply(WidgetIcon, Style);

    Expect(WidgetIcon.ColorLogical.R == 0xFF && WidgetIcon.ColorLogical.G == 0xFF && WidgetIcon.ColorLogical.B == 0xFF,
           "color reaches IconWidget::ColorLogical");
}

void TestHoverActiveAndDisabledColorOverlaysReachAnIconWidget() {
    IconWidget               WidgetIcon;
    ::Lustre::ResolvedStyle  Style;
    Style.Hover = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Hover->TextColor = ::Lustre::Color{0x66, 0xBB, 0x6A, 0xFF};
    Style.Active = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Active->TextColor = ::Lustre::Color{0x1A, 0x40, 0xAA, 0xFF};
    Style.Disabled = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Disabled->TextColor = ::Lustre::Color{0x80, 0x80, 0x80, 0xFF};

    LustreStyleApplier Applier;
    Applier.Apply(WidgetIcon, Style);

    Expect(WidgetIcon.ColorLogicalHovered.R == 0x66 && WidgetIcon.ColorLogicalHovered.G == 0xBB,
           ":hover color reaches IconWidget::ColorLogicalHovered");
    Expect(WidgetIcon.ColorLogicalPressed.R == 0x1A && WidgetIcon.ColorLogicalPressed.B == 0xAA,
           ":active color reaches IconWidget::ColorLogicalPressed");
    Expect(WidgetIcon.ColorLogicalDisabled.R == 0x80 && WidgetIcon.ColorLogicalDisabled.G == 0x80,
           ":disabled color reaches IconWidget::ColorLogicalDisabled");
}

void TestNoColorOverlayLeavesIconWidgetPerStateFieldsAtDefault() {
    IconWidget               WidgetIcon;
    ::Lustre::ResolvedStyle  Style;
    Style.Hover = std::make_shared<::Lustre::ResolvedStyle>();
    Style.Hover->BackgroundColor = ::Lustre::Color{0x66, 0xBB, 0x6A, 0xFF}; // no color in the overlay

    LustreStyleApplier Applier;
    Applier.Apply(WidgetIcon, Style);

    Expect(WidgetIcon.ColorLogicalHovered.A == 0,
           "a :hover overlay with no color leaves IconWidget::ColorLogicalHovered at its zero-alpha default");
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

void TestGradientPairReachesBoxStyle() {
    Box                     WidgetBox;
    ::Lustre::ResolvedStyle Style;
    Style.BackgroundGradientStart = ::Lustre::Color{0x4A, 0x90, 0xFF, 0xFF};
    Style.BackgroundGradientEnd = ::Lustre::Color{0x2A, 0x5A, 0xDD, 0xFF};

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Style.GradientTop.R == 0x4A && WidgetBox.Style.GradientTop.G == 0x90 &&
               WidgetBox.Style.GradientTop.B == 0xFF,
           "background-gradient-start reaches Box::Style.GradientTop");
    Expect(WidgetBox.Style.GradientBottom.R == 0x2A && WidgetBox.Style.GradientBottom.G == 0x5A &&
               WidgetBox.Style.GradientBottom.B == 0xDD,
           "background-gradient-end reaches Box::Style.GradientBottom");
}

void TestNoGradientLeavesBoxStyleGradientFieldsAtDefault() {
    Box        WidgetBox;
    const auto Style = MakeStyle(); // sets BackgroundColor, no gradient

    LustreStyleApplier Applier;
    Applier.Apply(WidgetBox, Style);

    Expect(WidgetBox.Style.GradientTop.A == 0,
           "no background-gradient-* in the style leaves Box::Style.GradientTop at its zero-alpha default");
}

void TestMaxWidthAndEllipsisReachALabel() {
    Label                   WidgetLabel;
    ::Lustre::ResolvedStyle Style;
    Style.MaxWidthLogical = 220.0F;
    Style.TextOverflowMode = ::Lustre::TextOverflow::Ellipsis;

    LustreStyleApplier Applier;
    Applier.Apply(WidgetLabel, Style);

    Expect(WidgetLabel.MaxWidthLogical.has_value() && *WidgetLabel.MaxWidthLogical == 220.0F,
           "max-width reaches Label::MaxWidthLogical");
    Expect(WidgetLabel.TruncateWithEllipsis, "text-overflow: ellipsis reaches Label::TruncateWithEllipsis as true");
}

void TestTextOverflowClipReachesALabelAsFalse() {
    Label                   WidgetLabel;
    ::Lustre::ResolvedStyle Style;
    Style.MaxWidthLogical = 100.0F;
    Style.TextOverflowMode = ::Lustre::TextOverflow::Clip;

    LustreStyleApplier Applier;
    Applier.Apply(WidgetLabel, Style);

    Expect(!WidgetLabel.TruncateWithEllipsis, "text-overflow: clip reaches Label::TruncateWithEllipsis as false");
}

void TestNoMaxWidthLeavesLabelUnconstrained() {
    Label      WidgetLabel;
    const auto Style = MakeStyle(); // no max-width/text-overflow at all

    LustreStyleApplier Applier;
    Applier.Apply(WidgetLabel, Style);

    Expect(!WidgetLabel.MaxWidthLogical.has_value(),
           "no max-width in the style leaves Label::MaxWidthLogical unset");
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
    TestHoverOverlayReachesAButtonWidget();
    TestHoverOverlayReachesAPlainBoxToo();
    TestHoverAndActiveGradientOverlaysReachBoxStyle();
    TestNoGradientOverlayLeavesPerStateGradientFieldsAtDefault();
    TestHoverActiveAndDisabledBorderColorOverlaysReachBoxStyle();
    TestNoBorderColorOverlayLeavesPerStateBorderFieldsAtDefault();
    TestBoxShadowReachesBoxStyle();
    TestNoBoxShadowLeavesBoxStyleShadowFieldsAtDefault();
    TestTextColorReachesALabel();
    TestColorReachesAnIconWidget();
    TestHoverActiveAndDisabledColorOverlaysReachAnIconWidget();
    TestNoColorOverlayLeavesIconWidgetPerStateFieldsAtDefault();
    TestDisplayStackWithRowFlexDirectionMapsToHorizontalStack();
    TestDisplayStackWithColumnFlexDirectionMapsToVerticalStack();
    TestDisplayInlineMapsToLayoutNone();
    TestNoDisplaySetLeavesLayoutUntouched();
    TestGapAndAlignItemsReachBox();
    TestGradientPairReachesBoxStyle();
    TestNoGradientLeavesBoxStyleGradientFieldsAtDefault();
    TestMaxWidthAndEllipsisReachALabel();
    TestTextOverflowClipReachesALabelAsFalse();
    TestNoMaxWidthLeavesLabelUnconstrained();
    TestCheckboxStillReceivesItsBoxStyleSlice();
}
