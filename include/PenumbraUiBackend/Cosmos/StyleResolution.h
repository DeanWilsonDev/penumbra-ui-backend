#pragma once

#include "Cosmos/Resolver.h"
#include "Cosmos/ResolvedStyle.h"

#include "Penumbra/Widgets/TextArea.h"
#include "PenumbraUiBackend/Cosmos/StyleApplier.h"

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace PenumbraUiBackend::Cosmos {

// Resolves Target's style against Sheets. A thin pass-through to
// `::Cosmos::ResolveStyle` (cosmos's own §1.3 two-layer cascade composition
// plus §1.7 color/font inheritance) -- this used to hand-roll the two-layer
// composition itself (two separate `Resolver::Resolve()` calls plus a local
// `MergeInto`), since `::Cosmos::Resolver::Resolve()` alone can't express
// "global unbounded, component bounded" (§1.2's asymmetric boundary rule) in
// one call. That composition, and inheritance built on top of it, now lives
// in `cosmos` itself so any consumer of the library gets both, not just this
// one -- kept here only as the optional-pointer-diagnostics convenience this
// repo's call sites (`Walker.cpp`, `PenumbraWidgetAdapter.cpp`) already rely
// on.
::Cosmos::ResolvedStyle ResolveStyle(const ::Cosmos::IStyleTarget& Target, const ::Cosmos::StylesheetSet& Sheets,
                                       std::vector<::Cosmos::ResolveDiagnostic>* OutDiagnostics = nullptr);

// The element's number prop called Name, or nullopt when it has none.
using NumberPropLookup = std::function<std::optional<float>(std::string_view Name)>;

// Fills in each `attr()` binding in Style, and in its Hover/Active/Disabled/Starting
// overlays, from Lookup: `flex-grow: attr(fill)` sets FlexGrow to the element's `fill`
// prop, a length in logical pixels. A colour binding reads each attr() channel the same
// way, rounded and clamped to 0..255: `background-color: rgba(attr(r), attr(g), attr(b),
// 255)`. A binding whose prop the element lacks leaves the property unset, as CSS treats
// an attr() with no attribute and no fallback; a gradient missing either stop is unset.
void BindAttributes(::Cosmos::ResolvedStyle& Style, const NumberPropLookup& Lookup);

void StyleTextAreaSpans(Penumbra::Widgets::TextArea& Field, const ::Cosmos::IStyleTarget& FieldTarget,
                        const ::Cosmos::StylesheetSet& Sheets, const IStyleApplier& Applier);

} // namespace PenumbraUiBackend::Cosmos
