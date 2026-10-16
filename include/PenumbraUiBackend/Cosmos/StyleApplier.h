#pragma once

#include "Cosmos/ResolvedStyle.h"

#include "Penumbra/Render/IFontBackend.h"
#include "Penumbra/Widgets/TextArea.h"
#include "Penumbra/Widgets/WidgetBase.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace PenumbraUiBackend::Cosmos {

class IStyleApplier {
public:
    virtual ~IStyleApplier() = default;

    virtual void Apply(Penumbra::Widgets::WidgetBase& Widget, const ::Cosmos::ResolvedStyle& Style) const = 0;
    virtual void ApplySpan(Penumbra::Widgets::TextAreaSpan& Span, const ::Cosmos::ResolvedStyle& Style) const = 0;
};

class CosmosStyleApplier : public IStyleApplier {
public:
    explicit CosmosStyleApplier(Penumbra::Render::IFontBackend* FontBackend = nullptr,
                                 float                            DpiScaleFactor = 1.0F);

    void Apply(Penumbra::Widgets::WidgetBase& Widget, const ::Cosmos::ResolvedStyle& Style) const override;
    void ApplySpan(Penumbra::Widgets::TextAreaSpan& Span, const ::Cosmos::ResolvedStyle& Style) const override;

    void  SetDpiScaleFactor(float DpiScaleFactor);
    float DpiScaleFactor() const { return DpiScaleFactor_; }

    void SetDefaultFont(::Cosmos::FontRequest Font) { DefaultFont_ = std::move(Font); }
    const std::optional<::Cosmos::FontRequest>& DefaultFont() const { return DefaultFont_; }

private:
    std::optional<::Cosmos::FontRequest> EffectiveFont(const ::Cosmos::ResolvedStyle& Style) const;
    Penumbra::Render::FontHandle         ResolveFont(const ::Cosmos::FontRequest& Request,
                                                     Penumbra::Render::FontStyle   FontStyle) const;

    Penumbra::Render::IFontBackend*      FontBackend_;
    float                                DpiScaleFactor_;
    std::optional<::Cosmos::FontRequest> DefaultFont_;

    mutable std::unordered_map<std::string, Penumbra::Render::FontHandle> FontCache_;
};

} // namespace PenumbraUiBackend::Cosmos
