#include "PenumbraUiBackend/Cosmos/StyleResolution.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace PenumbraUiBackend::Cosmos {

namespace {

class SpanStyleTarget final : public ::Cosmos::IStyleTarget {
public:
    SpanStyleTarget(std::string ClassName, const ::Cosmos::IStyleTarget& Field)
        : ClassName_(std::move(ClassName)), Field_(Field) {}

    std::string                   ClassName() const override { return ClassName_; }
    std::string                   PrimitiveTag() const override { return "text"; }
    bool                          IsComponentRoot() const override { return false; }
    const ::Cosmos::IStyleTarget* Parent() const override { return &Field_; }

private:
    std::string                   ClassName_;
    const ::Cosmos::IStyleTarget& Field_;
};

std::optional<std::uint8_t> ChannelValue(const ::Cosmos::ColorChannel& Channel, const NumberPropLookup& Lookup) {
    if (!Channel.Attribute) {
        return Channel.Value;
    }
    const std::optional<float> Value = Lookup(*Channel.Attribute);
    if (!Value || std::isnan(*Value)) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(std::clamp(std::round(*Value), 0.0F, 255.0F));
}

std::optional<::Cosmos::Color>* ColorField(::Cosmos::ResolvedStyle& Style, std::string_view Property) {
    if (Property == "background-color") {
        return &Style.BackgroundColor;
    }
    if (Property == "background-gradient-start") {
        return &Style.BackgroundGradientStart;
    }
    if (Property == "background-gradient-end") {
        return &Style.BackgroundGradientEnd;
    }
    if (Property == "border-color") {
        return &Style.BorderColor;
    }
    if (Property == "color") {
        return &Style.TextColor;
    }
    return nullptr;
}

void BindColors(::Cosmos::ResolvedStyle& Style, const NumberPropLookup& Lookup) {
    for (const ::Cosmos::ColorAttrBinding& Binding : Style.ColorAttrBindings) {
        std::optional<::Cosmos::Color>* Field = ColorField(Style, Binding.Property);
        if (Field == nullptr) {
            continue;
        }
        const std::optional<std::uint8_t> R = ChannelValue(Binding.R, Lookup);
        const std::optional<std::uint8_t> G = ChannelValue(Binding.G, Lookup);
        const std::optional<std::uint8_t> B = ChannelValue(Binding.B, Lookup);
        const std::optional<std::uint8_t> A = ChannelValue(Binding.A, Lookup);
        if (R && G && B && A) {
            *Field = ::Cosmos::Color{*R, *G, *B, *A};
        } else {
            Field->reset();
        }
    }
    // The applier draws a gradient only from both stops, so a stop left unset by a
    // missing prop leaves no gradient at all.
    if (Style.BackgroundGradientStart.has_value() != Style.BackgroundGradientEnd.has_value()) {
        Style.BackgroundGradientStart.reset();
        Style.BackgroundGradientEnd.reset();
    }
}

} // namespace

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
        } else if (Property == "top") {
            Style.Top = ::Cosmos::Offset{.Auto = false, .Value = Pixels};
        } else if (Property == "right") {
            Style.Right = ::Cosmos::Offset{.Auto = false, .Value = Pixels};
        } else if (Property == "bottom") {
            Style.Bottom = ::Cosmos::Offset{.Auto = false, .Value = Pixels};
        } else if (Property == "left") {
            Style.Left = ::Cosmos::Offset{.Auto = false, .Value = Pixels};
        }
    }
    BindColors(Style, Lookup);
    for (const std::shared_ptr<::Cosmos::ResolvedStyle>& Overlay :
         {Style.Hover, Style.Active, Style.Disabled, Style.Starting}) {
        if (Overlay) {
            BindAttributes(*Overlay, Lookup);
        }
    }
}

void StyleTextAreaSpans(Penumbra::Widgets::TextArea& Field, const ::Cosmos::IStyleTarget& FieldTarget,
                        const ::Cosmos::StylesheetSet& Sheets, const IStyleApplier& Applier) {
    std::vector<Penumbra::Widgets::TextAreaSpan> Spans = Field.GetSpans();
    if (Spans.empty()) {
        return;
    }
    std::unordered_map<std::string, ::Cosmos::ResolvedStyle> ByClass;
    for (Penumbra::Widgets::TextAreaSpan& Span : Spans) {
        auto It = ByClass.find(Span.ClassName);
        if (It == ByClass.end()) {
            It = ByClass.emplace(Span.ClassName, ResolveStyle(SpanStyleTarget(Span.ClassName, FieldTarget), Sheets)).first;
        }
        Applier.ApplySpan(Span, It->second);
    }
    Field.SetSpans(std::move(Spans));
}

} // namespace PenumbraUiBackend::Cosmos
