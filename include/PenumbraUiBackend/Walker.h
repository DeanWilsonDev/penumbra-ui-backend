#pragma once

#include "Lustre/Resolver.h"
#include "PenumbraUiBackend/Lustre/StyleApplier.h"

#include "Iris/Component.h"
#include "Iris/IrisNyxDriver.h"
#include "Penumbra/Backends/IIconBackend.h"
#include "Penumbra/Backends/IImageBackend.h"
#include "Penumbra/LifecycleRegistry.h"
#include "Penumbra/Platform/IClipboard.h"
#include "Penumbra/Render/IFontBackend.h"
#include "Penumbra/Widgets/FocusState.h"
#include "Penumbra/Widgets/WidgetBase.h"

#include <SDL3/SDL.h>

#include <memory>
#include <unordered_map>

namespace PenumbraUiBackend {

// Maps a built widget's raw pointer to its real Lustre primitive tag
// ("Frame"/"Inline"/"Grid"/"Image"/"Text") -- the piece of information that
// BuildWidgetTree's own IrisElementTag switch knows but a plain
// `Penumbra::Widgets::WidgetBase` tree can't represent on its own (`Frame`
// and `Grid` both build to a plain `Box`, per BuildGrid's own comment).
// Optional: only populated when a caller passes a non-null map to
// BuildWidgetTree, so PenumbraWidgetAdapter.cpp can later thread the exact
// original tag onto each `PenumbraWidget` wrapper at wrap time, rather than
// re-inferring it from C++ type on a class change (see
// docs/penumbra_ui_backend_lustre_bridge_decision.md's "Fixing the
// primitive-tag limitation" section for why that re-inference was
// unreliable for Frame/Grid specifically).
using PrimitiveTagMap = std::unordered_map<const Penumbra::Widgets::WidgetBase*, std::string>;

// Maps a `ref`-tagged node's ref name (docs/next_steps.md's "GetByRef lookup on a
// mounted tree" ask -- `iris`'s own `Component::Ref` field, `vendor/iris/include/
// Iris/Component.h`) to the widget built from it. Same "optional, populated only when
// a caller passes a non-null map" convention as PrimitiveTagMap above, and populated at
// exactly the same point in BuildWidgetTreeInternal's recursion for the same reason: by
// the time PenumbraWidgetAdapter.cpp wraps the already-built tree, the originating
// `Component` nodes (and their `Ref` fields) are gone. PenumbraWidgetAdapter.cpp's
// WrapExistingTree consumes this to populate PenumbraWidget::GetByRef, keyed on the
// same ref name.
using RefMap = std::unordered_map<std::string, Penumbra::Widgets::WidgetBase*>;

// Resources BuildWidgetTree needs beyond what an Component tree itself carries.
// Penumbra's own primitives don't take these through their fluent Builder: `Label`'s
// `FontBackend`/`Font` are plain public fields set after construction (there's no
// Builder method for either — confirmed against penumbra-proto's own demo, which sets
// them the same way), and `ImageWidget` needs an explicit `LoadFrom()` call after
// `build()` since loading was deliberately kept out of the Builder chain
// (docs/iris_core_spec.md §3.1's `<Image>` entry). Any field left null/default here
// just means that resource-dependent step is skipped — a `<Text>` widget still builds
// successfully with `FontBackend == nullptr`, it just can't measure/draw real text
// content yet; useful for structural tests that don't need a real font/SDL context.
//
// `Style`/`StyleApplier` are the same kind of optional resource: if either is null, no
// Lustre resolution happens at all and every widget builds exactly as it did before this
// wiring existed. Both pointers must outlive every widget built with this Context, since
// PenumbraWidgetAdapter.cpp threads them onto live `PenumbraWidget` wrappers for later
// re-resolution on a class change, not just the initial mount here.
struct BuildContext {
    Penumbra::Render::IFontBackend*    FontBackend{nullptr};
    Penumbra::Render::FontHandle       Font{0};
    Penumbra::Backends::IImageBackend* ImageBackend{nullptr};
    Penumbra::Backends::IIconBackend*  IconBackend{nullptr};
    SDL_Renderer*                      SdlRenderer{nullptr};
    // <Input> (docs/iris_core_spec.md §3.1) -- app-owned runtime state a built
    // TextInput needs to actually be focusable/editable. Left null means the
    // same "build succeeds, nothing loaded" tolerance ImageBackend/IconBackend
    // already have: the widget still builds, it's just inert (unfocusable, no
    // clipboard) until a caller wires these up.
    Penumbra::Widgets::FocusState*  Focus{nullptr};
    Penumbra::Platform::IClipboard* Clipboard{nullptr};

    const ::Lustre::StylesheetSet* Style{nullptr};
    const Lustre::IStyleApplier*    StyleApplier{nullptr};

    // docs/next_steps.md's "reconciler-side wiring for a framework-owned component
    // lifecycle system" ask. When set, every built widget whose originating Component
    // carries a live `Instance->Lifecycle` (iris::RegisterLifecycle, `Iris/
    // ComponentInstance.h`) gets registered against this registry's own
    // RegisterLifecycle/UnregisterLifecycle pair automatically -- see Walker.cpp's
    // BuildWidgetTreeInternal for the actual wiring. Left null (the default) means
    // exactly pre-wiring behavior: no registration happens, matching every other
    // optional BuildContext resource above.
    //
    // Typed as the standalone Penumbra::LifecycleRegistry rather than
    // Penumbra::Application (docs/next_steps.md's "widen BuildContext::LifecycleHost"
    // entry) so a consumer that isn't itself Application-based -- e.g. a hand-rolled
    // Platform::PlatformWindow-owning app -- can construct a LifecycleRegistry directly
    // and pass it here; an Application-based consumer passes
    // &App->GetLifecycleRegistry() instead. Only RegisterLifecycle/UnregisterLifecycle
    // are ever called on this pointer (see RegisterLifecycleIfPresent in Walker.cpp),
    // both unchanged in signature/behavior on the new type.
    Penumbra::LifecycleRegistry* LifecycleHost{nullptr};

    // docs/next_steps.md's "a Nyx-authored OnMount/OnTick has no way to reach its own
    // component's ref'd widgets" ask. When set (alongside LifecycleHost, and only for a
    // node whose `Instance->DriverState` was actually produced by `Iris::IrisNyxDriver` --
    // see Walker.cpp's RegisterGetRefIfPresent for the exact gate), BuildWidgetTreeInternal
    // defines a Nyx-callable `GetRef(name: string)` directly into that component's own
    // `Iris::NyxDriverState::RenderScope` Environment (never onto this Runtime's own
    // `Globals()` -- see RegisterGetRefIfPresent's own comment for why per-instance scoping
    // is both necessary, so concurrently-mounted components' ref sets never collide under
    // one shared name, and achievable with zero `iris-proto`/`nyx-proto` API additions).
    // `GetRef` returns a small host-object handle (`SetText`/`SetIconName`/`SetColor`/
    // `SetVisible`, dispatched by `dynamic_cast` against whichever concrete Penumbra widget
    // type the ref actually names) for whatever the calling component's own `ref`-tagged
    // descendants are -- exactly the operation set `pharos-proto`'s own
    // `inspector_panel_native.cpp` hand-writes today per row. Left null (the default) means
    // exactly pre-wiring behavior: no `GetRef` capability exists for any component, same
    // "optional resource" convention every other BuildContext field already follows. Only
    // reaches Model 1 (free-function) components today -- Model 2 (class-based) components
    // dispatch their lifecycle hooks through the file-level shared interpreter/registry, not
    // through any per-instance Environment, so there is currently no analogous scope to
    // define `GetRef` into for that model (a class-based OnTick calling `GetRef` gets an
    // ordinary Nyx-level "undefined variable" error, not a wrong-widget bug) -- a real,
    // deliberately-left-open scope boundary, not silently broken.
    nyx::host::NyxRuntime* NyxHost{nullptr};
};

// Walks a single `Component` IR node (docs/iris_core_spec.md §2.5, from the `iris`
// preprocessor — never a Penumbra type) and builds the equivalent real Penumbra widget
// via that primitive's own fluent `Builder`, recursing into `Node.Children`. This is
// Stage 2 only: a one-shot tree build, no diffing, no identity tracking — `Component`
// does carry a `Key` now (docs/iris_stage3_implementation_decision.md, in the `iris`
// repo), but this function never reads it; matching by key across two trees is Stage 3's
// reconciler's job (`PenumbraWidgetAdapter.h`), built on top of this, not part of it.
//
// Two IrisElementTag values get special handling rather than a Builder call, and both
// return `nullptr` — "no widget here, contributes nothing to its parent's built
// children," not an error:
//   - `None` (the `<Slot>`-callable-returned-`nullptr` sentinel, docs/iris_core_spec.md
//     §8).
//   - `Slot` (docs/iris_slot_stage2_wiring_decision.md) — a `<Slot>` child's own
//     content isn't known yet at this point in the build (its callable hasn't been
//     invoked), so this function leaves its position empty, same as `None`. The real
//     content gets spliced in afterward by `iris::ResolveSlots`
//     (`Iris/SlotResolution.h`, in the `iris` repo), which walks the just-built static
//     tree and `Node`'s own `Component` tree in lockstep to find each `<Slot>`'s
//     correct position and attach its initial (and, later, every subsequent) render
//     there. A `<Slot>` at the very root of what's being built (no static wrapper at
//     all, e.g. `render { <Slot>...</Slot> }`) is a different case `ResolveSlots`
//     doesn't handle either — see its own doc comment for what to do instead.
//
// When `Context.Style`/`Context.StyleApplier` are both set, every built widget also gets
// its Lustre style resolved and applied before this function returns it — see
// docs/penumbra_ui_backend_lustre_bridge_decision.md's "Wiring into the mount and
// reconcile paths" section for the ancestor-chain/component-boundary bookkeeping this
// does internally. `Node` itself is always treated as a component-root boundary (§1.2):
// correct for every real caller today (this function is only ever invoked at a whole
// mount's root — see `MakeMountFn`/`iris::ResolveSlots`), but not detectable from
// `Component` alone if a future caller ever builds a *sub*tree containing more than
// one component's worth of composed content in a single call.
//
// When OutTags is non-null, every built widget's raw pointer is recorded there against
// its real Lustre primitive tag -- see PrimitiveTagMap's own comment above. Left null
// (the default), behaves exactly as before this parameter existed.
//
// When OutRefs is non-null, every node carrying a `ref` prop has its built widget's raw
// pointer recorded there against that ref name -- see RefMap's own comment above. Left
// null (the default), behaves exactly as before this parameter existed. A `ref` whose
// value isn't a plain string (shouldn't happen via real `.iris` codegen, `Component.h`'s
// `Ref` field always carries the string an `IIFE` wrapper set it from) is silently
// skipped, same "malformed input is simply not recorded" tolerance the rest of this
// walker already has for e.g. a wrongly-typed `class` prop.
//
// When Context.LifecycleHost is non-null, every node's `Component::Instance` is checked
// for a live `Instance->Lifecycle` (set via `iris::RegisterLifecycle` inside that
// component's own body) -- not just Node itself: `MountComponentInstance` sets
// `Instance` for *every* component invocation `Codegen.h` wraps, including a plain
// nested `<ChildComponent .../>` used as an ordinary static child with no `<Slot>`
// involved, so this check happens at the same per-node point PrimitiveTagMap/RefMap
// above already record at, not once at Node's own root. See BuildContext::
// LifecycleHost's own comment for what registering actually does.
std::unique_ptr<Penumbra::Widgets::WidgetBase> BuildWidgetTree(const Iris::Component& Node,
                                                                 const BuildContext&        Context,
                                                                 PrimitiveTagMap*           OutTags = nullptr,
                                                                 RefMap*                    OutRefs = nullptr);

} // namespace PenumbraUiBackend
