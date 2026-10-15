#pragma once

#include "Cosmos/ResolvedStyle.h"

#include "Penumbra/Render/IFontBackend.h"
#include "Penumbra/Widgets/WidgetBase.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace PenumbraUiBackend::Cosmos {

// The seam a future, non-Cosmos styling language would implement instead of
// CosmosStyleApplier below, without touching anything else in this repo
// (Walker.cpp, PenumbraWidgetAdapter.cpp) — see
// docs/penumbra_ui_backend_cosmos_bridge_decision.md. The real contract a
// replacement commits to is producing a `::Cosmos::ResolvedStyle`-shaped
// value; this interface exists so the *application* logic (which widget
// type gets which struct, how pseudo-states map to Penumbra's fields) is
// swappable too, not just the resolver that produces the style.
class IStyleApplier {
public:
    virtual ~IStyleApplier() = default;

    // Mutates Widget's own style fields in place to match Style. Idempotent
    // and side-effect-free beyond that — safe to call again whenever a
    // class changes or (eventually) a .cosmos file hot-reloads.
    virtual void Apply(Penumbra::Widgets::WidgetBase& Widget, const ::Cosmos::ResolvedStyle& Style) const = 0;
};

class CosmosStyleApplier : public IStyleApplier {
public:
    explicit CosmosStyleApplier(Penumbra::Render::IFontBackend* FontBackend = nullptr,
                                 float                            DpiScaleFactor = 1.0F);

    void Apply(Penumbra::Widgets::WidgetBase& Widget, const ::Cosmos::ResolvedStyle& Style) const override;

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
