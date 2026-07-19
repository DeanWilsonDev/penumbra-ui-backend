#pragma once

#include "Lustre/Resolver.h"
#include "Lustre/ResolvedStyle.h"

#include <vector>

namespace PenumbraUiBackend::Lustre {

// Resolves Target's style against Sheets, correctly handling
// docs/lustre_core_spec.md §1.2's asymmetric boundary rule when a global and
// a component-scoped stylesheet are supplied together: global.lustre's own
// selectors reach however far they naturally imply (Unbounded), while the
// component's own file stays bounded to Target's own component root (§1.2's
// "component-boundary rule"). `::Lustre::Resolver::Resolve()` takes a single
// `Unbounded` flag applied to both layers in one call, which is correct when
// resolving only one layer at a time but can't express "global unbounded,
// component bounded" simultaneously -- so this composes two separate
// `Resolve()` calls (global alone with Unbounded=true, component alone with
// Unbounded=false) and merges the results, component fields winning over
// global for anything both set, matching §1.3's two-layer cascade.
::Lustre::ResolvedStyle ResolveStyle(const ::Lustre::IStyleTarget& Target, const ::Lustre::StylesheetSet& Sheets,
                                       std::vector<::Lustre::ResolveDiagnostic>* OutDiagnostics = nullptr);

} // namespace PenumbraUiBackend::Lustre
