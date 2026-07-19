#include "PenumbraUiBackend/Lustre/StyleResolution.h"

namespace PenumbraUiBackend::Lustre {

namespace {

// Copies every property Overlay actually set into Base, leaving anything
// Overlay didn't touch as Base already had it -- the merge Base <- Overlay
// direction matches §1.3's "the component's own file overrides global.lustre
// for anything both define," applied here across the two ResolveStyle()
// calls this file composes rather than within a single Resolver::Resolve().
void MergeInto(::Lustre::ResolvedStyle& Base, const ::Lustre::ResolvedStyle& Overlay) {
    if (Overlay.BackgroundColor) Base.BackgroundColor = Overlay.BackgroundColor;
    if (Overlay.BorderColor) Base.BorderColor = Overlay.BorderColor;
    if (Overlay.BorderWidth) Base.BorderWidth = Overlay.BorderWidth;
    if (Overlay.BorderRadius) Base.BorderRadius = Overlay.BorderRadius;
    if (Overlay.Padding) Base.Padding = Overlay.Padding;
    if (Overlay.Margin) Base.Margin = Overlay.Margin;
    if (Overlay.TextColor) Base.TextColor = Overlay.TextColor;
    if (Overlay.Font) Base.Font = Overlay.Font;
    if (Overlay.DisplayMode) Base.DisplayMode = Overlay.DisplayMode;
    if (Overlay.FlexDirectionMode) Base.FlexDirectionMode = Overlay.FlexDirectionMode;
    if (Overlay.Gap) Base.Gap = Overlay.Gap;
    if (Overlay.AlignItems) Base.AlignItems = Overlay.AlignItems;
    if (Overlay.Transition) Base.Transition = Overlay.Transition;
    if (Overlay.WidthLogical) Base.WidthLogical = Overlay.WidthLogical;
    if (Overlay.HeightLogical) Base.HeightLogical = Overlay.HeightLogical;
    if (Overlay.TransformScale) Base.TransformScale = Overlay.TransformScale;

    auto MergeOverlayBlock = [](std::shared_ptr<::Lustre::ResolvedStyle>&       BaseBlock,
                                 const std::shared_ptr<::Lustre::ResolvedStyle>& OverlayBlock) {
        if (!OverlayBlock) {
            return;
        }
        if (!BaseBlock) {
            BaseBlock = std::make_shared<::Lustre::ResolvedStyle>();
        }
        MergeInto(*BaseBlock, *OverlayBlock);
    };
    MergeOverlayBlock(Base.Hover, Overlay.Hover);
    MergeOverlayBlock(Base.Active, Overlay.Active);
    MergeOverlayBlock(Base.Disabled, Overlay.Disabled);
}

} // namespace

::Lustre::ResolvedStyle ResolveStyle(const ::Lustre::IStyleTarget& Target, const ::Lustre::StylesheetSet& Sheets,
                                       std::vector<::Lustre::ResolveDiagnostic>* OutDiagnostics) {
    static const ::Lustre::Resolver Resolver;
    std::vector<::Lustre::ResolveDiagnostic> LocalDiagnostics;
    std::vector<::Lustre::ResolveDiagnostic>& Diagnostics = OutDiagnostics ? *OutDiagnostics : LocalDiagnostics;

    ::Lustre::ResolvedStyle Result;
    if (Sheets.Global) {
        const auto GlobalStyle =
            Resolver.Resolve(Target, ::Lustre::StylesheetSet{Sheets.Global, nullptr}, /*Unbounded=*/true, Diagnostics);
        MergeInto(Result, GlobalStyle);
    }
    if (Sheets.Component) {
        const auto ComponentStyle = Resolver.Resolve(Target, ::Lustre::StylesheetSet{nullptr, Sheets.Component},
                                                       /*Unbounded=*/false, Diagnostics);
        MergeInto(Result, ComponentStyle);
    }
    return Result;
}

} // namespace PenumbraUiBackend::Lustre
