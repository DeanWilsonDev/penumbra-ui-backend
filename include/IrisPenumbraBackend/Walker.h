#pragma once

#include "Iris/IrisComponent.h"
#include "Penumbra/Backends/IImageBackend.h"
#include "Penumbra/Render/IFontBackend.h"
#include "Penumbra/Widgets/WidgetBase.h"

#include <SDL3/SDL.h>

#include <memory>

namespace IrisPenumbraBackend {

// Resources BuildWidgetTree needs beyond what an IrisComponent tree itself carries.
// Penumbra's own primitives don't take these through their fluent Builder: `Label`'s
// `FontBackend`/`Font` are plain public fields set after construction (there's no
// Builder method for either — confirmed against penumbra-proto's own demo, which sets
// them the same way), and `ImageWidget` needs an explicit `LoadFrom()` call after
// `build()` since loading was deliberately kept out of the Builder chain
// (docs/iris_core_spec.md §3.1's `<Image>` entry). Any field left null/default here
// just means that resource-dependent step is skipped — a `<Text>` widget still builds
// successfully with `FontBackend == nullptr`, it just can't measure/draw real text
// content yet; useful for structural tests that don't need a real font/SDL context.
struct BuildContext {
    Penumbra::Render::IFontBackend*    FontBackend{nullptr};
    Penumbra::Render::FontHandle       Font{0};
    Penumbra::Backends::IImageBackend* ImageBackend{nullptr};
    SDL_Renderer*                      SdlRenderer{nullptr};
};

// Walks a single `IrisComponent` IR node (docs/iris_core_spec.md §2.5, from the `iris`
// preprocessor — never a Penumbra type) and builds the equivalent real Penumbra widget
// via that primitive's own fluent `Builder`, recursing into `Node.Children`. This is
// Stage 2 only: a one-shot tree build, no diffing, no identity tracking — `IrisComponent`
// does carry a `Key` now (docs/iris_stage3_implementation_decision.md, in the `iris`
// repo), but this function never reads it; matching by key across two trees is Stage 3's
// reconciler's job (`PenumbraWidgetAdapter.h`), built on top of this, not part of it.
//
// Two IrisElementTag values get special handling rather than a Builder call:
//   - `None` (the `<Slot>`-callable-returned-`nullptr` sentinel, docs/iris_core_spec.md
//     §8) returns `nullptr` — "no widget here", not an error. A `None` child is simply
//     omitted from its parent's built children, never passed to a child()/children()
//     Builder call.
//   - `Slot` should never reach this function at all: the Iris runtime is specified to
//     resolve every `<Slot>` (invoking its callable, substituting the result) before
//     any backend-mapping pass runs (docs/iris_core_spec.md §2.5). Encountering one
//     here is a precondition violation, not a normal "no widget" case — asserts in
//     debug builds, returns `nullptr` in release ones (same fallback as `None`, since
//     there's nothing sound to build).
std::unique_ptr<Penumbra::Widgets::WidgetBase> BuildWidgetTree(const Iris::IrisComponent& Node,
                                                                 const BuildContext&        Context);

} // namespace IrisPenumbraBackend
