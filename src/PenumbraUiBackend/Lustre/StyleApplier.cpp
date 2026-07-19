#include "PenumbraUiBackend/Lustre/StyleApplier.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/Button.h"
#include "Penumbra/Widgets/Checkbox.h"
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
    using Penumbra::Widgets::Button;
    using Penumbra::Widgets::Checkbox;
    using Penumbra::Widgets::Label;

    // Every widget type in Penumbra's hierarchy is a Box (WidgetBase's only
    // other direct subclass, ImageWidget, has no BoxStyle at all -- a
    // pre-existing Penumbra gap, not something this applier can paper over;
    // background-color/border/padding/margin simply don't reach an
    // <Image>-backed widget yet).
    auto* AsBox = dynamic_cast<Box*>(&Widget);
    if (!AsBox) {
        return;
    }
    ApplyBoxStyle(AsBox->Style, Style);
    ApplyLayout(*AsBox, Style);

    // §2's "Pseudo-class-scoped variants": only background-color has a real
    // field to receive a pseudo-scoped value today, and only on Button.
    // :hover/:active/:disabled targeting anything else resolves into the IR
    // (Lustre's own job) but has nowhere to land here yet -- stubbed, see
    // penumbra-proto/docs/lustre_style_gaps_requirements.md §1.
    if (auto* AsButton = dynamic_cast<Button*>(&Widget)) {
        if (Style.Hover && Style.Hover->BackgroundColor) {
            AsButton->ColorBackgroundHovered = ToPenumbraColor(*Style.Hover->BackgroundColor);
        }
        if (Style.Active && Style.Active->BackgroundColor) {
            AsButton->ColorBackgroundPressed = ToPenumbraColor(*Style.Active->BackgroundColor);
        }
        if (Style.Disabled && Style.Disabled->BackgroundColor) {
            AsButton->ColorBackgroundDisabled = ToPenumbraColor(*Style.Disabled->BackgroundColor);
        }
    }

    if (auto* AsLabel = dynamic_cast<Label*>(&Widget)) {
        if (Style.TextColor) {
            AsLabel->ColorText = ToPenumbraColor(*Style.TextColor);
        }
        if (Style.Font && FontBackend_) {
            AsLabel->FontBackend = FontBackend_;
            AsLabel->Font = ResolveFont(*Style.Font);
        }
    }

    // Checkbox's own style-specific fields (ColorCheckMark/ColorBoxChecked)
    // have no corresponding Lustre v1 property yet (§2's property table has
    // no check-mark-color concept) -- nothing to apply beyond the BoxStyle
    // slice already handled above.
}

} // namespace PenumbraUiBackend::Lustre
