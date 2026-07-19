#pragma once

#include "Lustre/ResolvedStyle.h"

#include "Penumbra/Render/IFontBackend.h"
#include "Penumbra/Widgets/WidgetBase.h"

#include <string>
#include <unordered_map>
#include <utility>

namespace IrisPenumbraBackend::Lustre {

// The seam a future, non-Lustre styling language would implement instead of
// LustreStyleApplier below, without touching anything else in this repo
// (Walker.cpp, PenumbraWidgetAdapter.cpp) — see
// docs/iris_penumbra_backend_lustre_bridge_decision.md. The real contract a
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

// Applies a Lustre::ResolvedStyle (docs/lustre_core_spec.md §3, from the
// `lustre` repo — backend-agnostic, knows nothing about Penumbra or Iris)
// onto a real Penumbra widget. Dispatches on the widget's concrete runtime
// type (Button/Checkbox/Label/plain Box) via dynamic_cast, since which
// concrete style struct (BoxStyle/ButtonStyle/CheckboxStyle) and which
// ApplyStyle() overload applies depends on that, the same distinction §2's
// "Pseudo-class-scoped variants" note draws (only Button has real fields for
// a hover/press/disabled background today).
//
// Font resolution (font-family + font-size -> a real Penumbra FontHandle)
// needs an IFontBackend and is cached by (path, size) per §2's font-request
// note — LustreStyleApplier owns that cache so repeated Apply() calls for
// the same font don't re-request a handle from the backend every time.
class LustreStyleApplier : public IStyleApplier {
public:
    // FontBackend may be null -- font-family/font-size are simply skipped
    // then, the same "resource-dependent step is skipped" treatment
    // Walker.h's BuildContext already uses for Label's font.
    explicit LustreStyleApplier(Penumbra::Render::IFontBackend* FontBackend = nullptr,
                                 float                            DpiScaleFactor = 1.0F);

    void Apply(Penumbra::Widgets::WidgetBase& Widget, const ::Lustre::ResolvedStyle& Style) const override;

private:
    Penumbra::Render::FontHandle ResolveFont(const ::Lustre::FontRequest& Request) const;

    Penumbra::Render::IFontBackend* FontBackend_;
    float                            DpiScaleFactor_;

    // (path, size) -> handle. Apply() is logically const (it doesn't mutate
    // *this* style-mapping policy, only the widget it's given), so this
    // cache is mutable -- matches FontBackend_ itself already being a
    // stateful resource reached through a const method.
    mutable std::unordered_map<std::string, Penumbra::Render::FontHandle> FontCache_;
};

} // namespace IrisPenumbraBackend::Lustre
