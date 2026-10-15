#include "PenumbraUiBackend/Cosmos/StyleResolution.h"

#include <algorithm>

namespace PenumbraUiBackend::Cosmos {

::Cosmos::ResolvedStyle ResolveStyle(const ::Cosmos::IStyleTarget& Target, const ::Cosmos::StylesheetSet& Sheets,
                                       std::vector<::Cosmos::ResolveDiagnostic>* OutDiagnostics) {
    std::vector<::Cosmos::ResolveDiagnostic> LocalDiagnostics;
    std::vector<::Cosmos::ResolveDiagnostic>& Diagnostics = OutDiagnostics ? *OutDiagnostics : LocalDiagnostics;
    return ::Cosmos::ResolveStyle(Target, Sheets, Diagnostics);
}

void BindAttributes(::Cosmos::ResolvedStyle& Style, const NumberPropLookup& Lookup) {
    for (const ::Cosmos::AttrBinding& Binding : Style.AttrBindings) {
        const std::optional<float> Value = Lookup(Binding.Attribute);
        if (!Value) {
            continue;
        }
        const ::Cosmos::Length Pixels{*Value, ::Cosmos::LengthUnit::Px};
        const std::string&     Property = Binding.Property;
        if (Property == "width") {
            Style.Width = Pixels;
        } else if (Property == "height") {
            Style.Height = Pixels;
        } else if (Property == "min-width") {
            Style.MinWidth = Pixels;
        } else if (Property == "max-width") {
            Style.MaxWidth = Pixels;
        } else if (Property == "flex-grow") {
            Style.FlexGrow = std::max(0.0F, *Value);
        } else if (Property == "opacity") {
            Style.Opacity = std::clamp(*Value, 0.0F, 1.0F);
        } else if (Property == "gap") {
            Style.Gap = *Value;
        } else if (Property == "border-width") {
            Style.BorderWidth = *Value;
        } else if (Property == "border-radius") {
            Style.BorderRadius = *Value;
        }
    }
    for (const std::shared_ptr<::Cosmos::ResolvedStyle>& Overlay :
         {Style.Hover, Style.Active, Style.Disabled, Style.Starting}) {
        if (Overlay) {
            BindAttributes(*Overlay, Lookup);
        }
    }
}

} // namespace PenumbraUiBackend::Cosmos
