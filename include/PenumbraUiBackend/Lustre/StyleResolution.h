#pragma once

#include "Lustre/Resolver.h"
#include "Lustre/ResolvedStyle.h"

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace PenumbraUiBackend::Lustre {

// Resolves Target's style against Sheets. A thin pass-through to
// `::Lustre::ResolveStyle` (lustre's own §1.3 two-layer cascade composition
// plus §1.7 color/font inheritance) -- this used to hand-roll the two-layer
// composition itself (two separate `Resolver::Resolve()` calls plus a local
// `MergeInto`), since `::Lustre::Resolver::Resolve()` alone can't express
// "global unbounded, component bounded" (§1.2's asymmetric boundary rule) in
// one call. That composition, and inheritance built on top of it, now lives
// in `lustre` itself so any consumer of the library gets both, not just this
// one -- kept here only as the optional-pointer-diagnostics convenience this
// repo's call sites (`Walker.cpp`, `PenumbraWidgetAdapter.cpp`) already rely
// on.
::Lustre::ResolvedStyle ResolveStyle(const ::Lustre::IStyleTarget& Target, const ::Lustre::StylesheetSet& Sheets,
                                       std::vector<::Lustre::ResolveDiagnostic>* OutDiagnostics = nullptr);

// The element's number prop called Name, or nullopt when it has none.
using NumberPropLookup = std::function<std::optional<float>(std::string_view Name)>;

// Fills in each `attr()` binding in Style, and in its Hover/Active/Disabled/Starting
// overlays, from Lookup: `flex-grow: attr(fill)` sets FlexGrow to the element's `fill`
// prop, a length in logical pixels. A binding whose prop the element lacks leaves the
// property unset, as CSS treats an attr() with no attribute and no fallback.
void BindAttributes(::Lustre::ResolvedStyle& Style, const NumberPropLookup& Lookup);

} // namespace PenumbraUiBackend::Lustre
