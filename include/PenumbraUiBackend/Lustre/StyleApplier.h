#pragma once

#include "Lustre/ResolvedStyle.h"

#include "Penumbra/Render/IFontBackend.h"
#include "Penumbra/Widgets/WidgetBase.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace PenumbraUiBackend::Lustre {

// The seam a future, non-Lustre styling language would implement instead of
// LustreStyleApplier below, without touching anything else in this repo
// (Walker.cpp, PenumbraWidgetAdapter.cpp) — see
// docs/penumbra_ui_backend_lustre_bridge_decision.md. The real contract a
// replacement commits to is producing a `::Lustre::ResolvedStyle`-shaped
// value; this interface exists so the *application* logic (which widget
// type gets which struct, how pseudo-states map to Penumbra's fields) is
// swappable too, not just the resolver that produces the style.
class IStyleApplier {
public:
    virtual ~IStyleApplier() = default;

    // Mutates Widget's own style fields in place to match Style. Idempotent
    // and side-effect-free beyond that — safe to call again whenever a
    // class changes or (eventually) a .lustre file hot-reloads.
    virtual void Apply(Penumbra::Widgets::WidgetBase& Widget, const ::Lustre::ResolvedStyle& Style) const = 0;
};

class LustreStyleApplier : public IStyleApplier {
public:
    explicit LustreStyleApplier(Penumbra::Render::IFontBackend* FontBackend = nullptr,
                                 float                            DpiScaleFactor = 1.0F);

    void Apply(Penumbra::Widgets::WidgetBase& Widget, const ::Lustre::ResolvedStyle& Style) const override;

    void  SetDpiScaleFactor(float DpiScaleFactor);
    float DpiScaleFactor() const { return DpiScaleFactor_; }

    void SetDefaultFont(::Lustre::FontRequest Font) { DefaultFont_ = std::move(Font); }
    const std::optional<::Lustre::FontRequest>& DefaultFont() const { return DefaultFont_; }

private:
    std::optional<::Lustre::FontRequest> EffectiveFont(const ::Lustre::ResolvedStyle& Style) const;
    Penumbra::Render::FontHandle         ResolveFont(const ::Lustre::FontRequest& Request) const;

    Penumbra::Render::IFontBackend*      FontBackend_;
    float                                DpiScaleFactor_;
    std::optional<::Lustre::FontRequest> DefaultFont_;

    mutable std::unordered_map<std::string, Penumbra::Render::FontHandle> FontCache_;
};

} // namespace PenumbraUiBackend::Lustre
