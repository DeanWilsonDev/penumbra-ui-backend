#include "PenumbraUiBackend/Lustre/StyleApplier.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/Checkbox.h"
#include "Penumbra/Widgets/IconWidget.h"
#include "Penumbra/Widgets/Label.h"

namespace PenumbraUiBackend::Lustre {

namespace {

Penumbra::Render::Color ToPenumbraColor(const ::Lustre::Color& C) { return {C.R, C.G, C.B, C.A}; }

Penumbra::Widgets::EdgeInsets ToPenumbraEdgeInsets(const ::Lustre::EdgeInsets& E) {
    return {E.Left, E.Top, E.Right, E.Bottom};
}

Penumbra::Widgets::CrossAlign ToPenumbraCrossAlign(::Lustre::Align A) {
    switch (A) {
        case ::Lustre::Align::Start: return Penumbra::Widgets::CrossAlign::Start;
        case ::Lustre::Align::Center: return Penumbra::Widgets::CrossAlign::Center;
        case ::Lustre::Align::End: return Penumbra::Widgets::CrossAlign::End;
        case ::Lustre::Align::Stretch: return Penumbra::Widgets::CrossAlign::Stretch;
    }
    return Penumbra::Widgets::CrossAlign::Start;
}

// §2: display/flex-direction/gap/align-items map onto Box's own layout
// fields directly -- siblings of Style (BoxStyle), not part of it, and
// Box::Builder has no method for any of them either, the same "public
// field, not a Builder chain method" pattern Walker.cpp's own BuildGrid
// stub already uses for LayoutMode::HorizontalStack. Box::Arrange skips
// laying out children entirely when Layout == LayoutMode::None ("footgun
// accepted", per Penumbra's own Box.cpp) -- any container that should
// actually size around its children needs `display: stack` for that reason,
// not just for visual flex-direction control.
void ApplyLayout(Penumbra::Widgets::Box& Target, const ::Lustre::ResolvedStyle& Style) {
    if (Style.DisplayMode) {
        if (*Style.DisplayMode == ::Lustre::Display::Inline) {
            Target.Layout = Penumbra::Widgets::LayoutMode::None;
        } else {
            const bool Row = !Style.FlexDirectionMode || *Style.FlexDirectionMode == ::Lustre::FlexDirection::Row;
            Target.Layout = Row ? Penumbra::Widgets::LayoutMode::HorizontalStack
                                 : Penumbra::Widgets::LayoutMode::VerticalStack;
        }
    }
    if (Style.Gap) {
        Target.ChildGap = *Style.Gap;
    }
    if (Style.AlignItems) {
        Target.CrossAlignment = ToPenumbraCrossAlign(*Style.AlignItems);
    }
}

// Fills in the universal box-model slots every widget type shares (§2:
// background-color, border-color/width/radius, padding, margin). Existing
// field values are left untouched wherever Style doesn't set the
// corresponding property, so applying a style never clobbers something the
// widget's constructor or a previous Apply() already set intentionally.
void ApplyBoxStyle(Penumbra::Widgets::BoxStyle& Target, const ::Lustre::ResolvedStyle& Style) {
    if (Style.BackgroundColor) {
        Target.ColorBackground = ToPenumbraColor(*Style.BackgroundColor);
    }
    // Resolver::Resolve only ever sets these as a pair (either both or
    // neither) -- checking Start alone here is enough, no need to guard
    // End separately.
    if (Style.BackgroundGradientStart) {
        Target.GradientTop = ToPenumbraColor(*Style.BackgroundGradientStart);
        Target.GradientBottom = ToPenumbraColor(*Style.BackgroundGradientEnd);
    }
    if (Style.BorderColor) {
        Target.ColorBorder = ToPenumbraColor(*Style.BorderColor);
    }
    if (Style.BorderWidth) {
        Target.BorderWidth = *Style.BorderWidth;
    }
    if (Style.BorderRadius) {
        Target.BorderRadius = *Style.BorderRadius;
    }
    if (Style.Padding) {
        Target.Padding = ToPenumbraEdgeInsets(*Style.Padding);
    }
    if (Style.Margin) {
        Target.Margin = ToPenumbraEdgeInsets(*Style.Margin);
    }
    // Same pair convention as BackgroundGradientStart/End above: the resolver
    // only ever sets both halves of `box-shadow` together (or neither), so
    // checking ShadowColor alone is enough.
    if (Style.ShadowColor) {
        Target.ShadowColor = ToPenumbraColor(*Style.ShadowColor);
        Target.ShadowBlurRadiusLogical = Style.ShadowBlurRadiusLogical.value_or(0.0f);
    }
}

} // namespace

LustreStyleApplier::LustreStyleApplier(Penumbra::Render::IFontBackend* FontBackend, float DpiScaleFactor)
    : FontBackend_(FontBackend), DpiScaleFactor_(DpiScaleFactor) {}

Penumbra::Render::FontHandle LustreStyleApplier::ResolveFont(const ::Lustre::FontRequest& Request) const {
    const std::string CacheKey = Request.Path + "@" + std::to_string(Request.SizeLogical);
    if (auto It = FontCache_.find(CacheKey); It != FontCache_.end()) {
        return It->second;
    }
    const Penumbra::Render::FontHandle Handle =
        FontBackend_->LoadFont(Request.Path.c_str(), Request.SizeLogical, DpiScaleFactor_);
    FontCache_.emplace(CacheKey, Handle);
    return Handle;
}

void LustreStyleApplier::Apply(Penumbra::Widgets::WidgetBase& Widget, const ::Lustre::ResolvedStyle& Style) const {
    using Penumbra::Widgets::Box;
    using Penumbra::Widgets::Checkbox;
    using Penumbra::Widgets::IconWidget;
    using Penumbra::Widgets::Label;

    // <Icon> (IconWidget) is a WidgetBase direct subclass, not a Box -- same
    // as ImageWidget below -- so it has to be handled before the Box-only
    // early return rather than after it. No dedicated `icon-color` Lustre
    // property exists (or needs to): this reuses the same `color` property
    // Label's ColorText branch below already resolves from TextColor, same
    // "foreground color" concept CSS's own `color` (and `currentColor`)
    // applies to both text and icons. Per-state overrides mirror
    // Box::BorderForState's convention -- Style.Hover/Active/Disabled are
    // full recursive ResolvedStyle blocks, so their own TextColor already
    // covers this with no new field needed anywhere.
    if (auto* AsIcon = dynamic_cast<IconWidget*>(&Widget)) {
        if (Style.TextColor) {
            AsIcon->ColorLogical = ToPenumbraColor(*Style.TextColor);
        }
        if (Style.Hover && Style.Hover->TextColor) {
            AsIcon->ColorLogicalHovered = ToPenumbraColor(*Style.Hover->TextColor);
        }
        if (Style.Active && Style.Active->TextColor) {
            AsIcon->ColorLogicalPressed = ToPenumbraColor(*Style.Active->TextColor);
        }
        if (Style.Disabled && Style.Disabled->TextColor) {
            AsIcon->ColorLogicalDisabled = ToPenumbraColor(*Style.Disabled->TextColor);
        }
    }

    // Every widget type in Penumbra's hierarchy is otherwise a Box
    // (WidgetBase's other direct subclass, ImageWidget, has no BoxStyle at
    // all -- a pre-existing Penumbra gap, not something this applier can
    // paper over; background-color/border/padding/margin simply don't reach
    // an <Image>-backed widget yet).
    auto* AsBox = dynamic_cast<Box*>(&Widget);
    if (!AsBox) {
        return;
    }
    ApplyBoxStyle(AsBox->Style, Style);
    ApplyLayout(*AsBox, Style);

    // §2's "Pseudo-class-scoped variants": background-color is universal
    // (BoxStyle::ColorBackgroundHovered/Pressed/Disabled, not Button-only --
    // penumbra@dd1d6ab moved these off Button onto BoxStyle specifically so
    // any classed Box renders them, see Box::BackgroundForState), so this
    // reuses AsBox rather than re-narrowing to Button.
    if (Style.Hover && Style.Hover->BackgroundColor) {
        AsBox->Style.ColorBackgroundHovered = ToPenumbraColor(*Style.Hover->BackgroundColor);
    }
    if (Style.Active && Style.Active->BackgroundColor) {
        AsBox->Style.ColorBackgroundPressed = ToPenumbraColor(*Style.Active->BackgroundColor);
    }
    if (Style.Disabled && Style.Disabled->BackgroundColor) {
        AsBox->Style.ColorBackgroundDisabled = ToPenumbraColor(*Style.Disabled->BackgroundColor);
    }

    // border-color's per-state overlay -- same universal-on-BoxStyle treatment
    // as background-color above (penumbra@7fad4dc: ColorBorderHovered/Pressed/
    // Disabled plus Box::BorderForState).
    if (Style.Hover && Style.Hover->BorderColor) {
        AsBox->Style.ColorBorderHovered = ToPenumbraColor(*Style.Hover->BorderColor);
    }
    if (Style.Active && Style.Active->BorderColor) {
        AsBox->Style.ColorBorderPressed = ToPenumbraColor(*Style.Active->BorderColor);
    }
    if (Style.Disabled && Style.Disabled->BorderColor) {
        AsBox->Style.ColorBorderDisabled = ToPenumbraColor(*Style.Disabled->BorderColor);
    }

    // Per-state gradient overrides -- same pair convention as the flat
    // GradientTop/Bottom above, and no Disabled variant for the same reason
    // (penumbra@89216b4: every known consumer falls back to a flat
    // ColorBackgroundDisabled fill when disabled, not a disabled gradient).
    if (Style.Hover && Style.Hover->BackgroundGradientStart) {
        AsBox->Style.GradientTopHovered = ToPenumbraColor(*Style.Hover->BackgroundGradientStart);
        AsBox->Style.GradientBottomHovered = ToPenumbraColor(*Style.Hover->BackgroundGradientEnd);
    }
    if (Style.Active && Style.Active->BackgroundGradientStart) {
        AsBox->Style.GradientTopPressed = ToPenumbraColor(*Style.Active->BackgroundGradientStart);
        AsBox->Style.GradientBottomPressed = ToPenumbraColor(*Style.Active->BackgroundGradientEnd);
    }

    if (auto* AsLabel = dynamic_cast<Label*>(&Widget)) {
        if (Style.TextColor) {
            AsLabel->ColorText = ToPenumbraColor(*Style.TextColor);
        }
        if (Style.Font && FontBackend_) {
            AsLabel->FontBackend = FontBackend_;
            AsLabel->Font = ResolveFont(*Style.Font);
        }
        if (Style.MaxWidthLogical) {
            AsLabel->MaxWidthLogical = *Style.MaxWidthLogical;
        }
        if (Style.TextOverflowMode) {
            AsLabel->TruncateWithEllipsis = (*Style.TextOverflowMode == ::Lustre::TextOverflow::Ellipsis);
        }
    }

    // Checkbox's own style-specific fields (ColorCheckMark/ColorBoxChecked)
    // have no corresponding Lustre v1 property yet (§2's property table has
    // no check-mark-color concept) -- nothing to apply beyond the BoxStyle
    // slice already handled above.
}

} // namespace PenumbraUiBackend::Lustre
