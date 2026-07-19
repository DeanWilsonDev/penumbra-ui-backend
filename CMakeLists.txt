cmake_minimum_required(VERSION 3.24)

project(PenumbraUiBackend
    VERSION 0.1.0
    DESCRIPTION "The Penumbra backend for Iris and Lustre — maps IrisComponent IR onto a real Penumbra widget tree and applies Lustre::ResolvedStyle to it"
    LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ---------------------------------------------------------------------------
# Iris, Penumbra, and Lustre are independent projects that don't know about
# each other — each is vendored here as its own pinned git submodule, and
# each subdirectory sets its own C++ standard (Iris/Lustre: C++23, Penumbra:
# C++20), which is fine — CMAKE_CXX_STANDARD is directory-scoped, not
# global, once you're inside an add_subdirectory().
# ---------------------------------------------------------------------------
add_subdirectory(vendor/iris)
add_subdirectory(vendor/penumbra)
add_subdirectory(vendor/lustre)

# ---------------------------------------------------------------------------
# The Lustre style bridge: applies a Lustre::ResolvedStyle (from `lustre`,
# which knows nothing about Iris and never will) onto a real Penumbra
# widget's style structs, plus ResolveStyle() (StyleResolution.cpp), the
# glue that composes Lustre::Resolver's two-layer cascade correctly (§1.2's
# global-unbounded/component-bounded asymmetry) for a single element. Kept
# as its own target so a consumer of `penumbra_ui_backend` below that
# doesn't want styling can still avoid pulling in `lustre` --
# `penumbra_ui_backend` itself now links this (see that target's own
# comment) since Walker.cpp/PenumbraWidgetAdapter.cpp call ResolveStyle()
# directly, but nothing in *this* target needs anything from `iris`. This is
# what keeps Iris and Lustre from ever depending on each other even
# indirectly: this repo is the one place allowed to know about both, and
# even here the two connect only through this narrow, swappable seam (see
# docs/penumbra_ui_backend_lustre_bridge_decision.md) — IStyleApplier is
# the interface a future, non-Lustre styling language would implement
# instead.
# ---------------------------------------------------------------------------
add_library(penumbra_ui_backend_lustre STATIC
    src/PenumbraUiBackend/Lustre/StyleApplier.cpp
    src/PenumbraUiBackend/Lustre/StyleResolution.cpp)

target_include_directories(penumbra_ui_backend_lustre PUBLIC include)
target_compile_features(penumbra_ui_backend_lustre PUBLIC cxx_std_23)
target_link_libraries(penumbra_ui_backend_lustre PUBLIC lustre penumbra)

# ---------------------------------------------------------------------------
# The backend itself: walks a parsed, props-resolved IrisComponent IR tree
# (from `iris`) and builds a real Penumbra widget tree via Penumbra's
# fluent Builder API (from `penumbra`) — docs/iris_core_spec.md §2.5–§2.6,
# §3 on the `iris` side; `docs/iris_stage2_decision_doc.md` for the Stage 2
# planning decisions this implements. Also resolves and applies Lustre
# styles at mount (Walker.cpp) and on every class change
# (PenumbraWidgetAdapter.cpp) when a caller supplies a StylesheetSet/
# IStyleApplier via BuildContext — both fields are nullable, so a consumer
# that never sets them gets exactly the pre-styling behavior, unchanged.
# ---------------------------------------------------------------------------
add_library(penumbra_ui_backend STATIC
    src/PenumbraUiBackend/Walker.cpp
    src/PenumbraUiBackend/PenumbraWidgetAdapter.cpp)

target_include_directories(penumbra_ui_backend PUBLIC include)
target_compile_features(penumbra_ui_backend PUBLIC cxx_std_23)
target_link_libraries(penumbra_ui_backend PUBLIC iris penumbra penumbra_ui_backend_lustre)

# ---------------------------------------------------------------------------
# Tests — plain hand-rolled Expect()/[PASS]/[FAIL] executable, matching
# `iris`'s own test style (Cimmerian, the ecosystem's intended long-term test
# framework, isn't wired into either repo yet).
# ---------------------------------------------------------------------------
add_executable(penumbra_ui_backend_tests
    tests/WalkerTests.cpp
    tests/PenumbraWidgetAdapterTests.cpp
    tests/SlotWiringTests.cpp
    tests/LustreStyleApplierTests.cpp
    tests/StyleWiringTests.cpp)

target_link_libraries(penumbra_ui_backend_tests PRIVATE penumbra_ui_backend penumbra_ui_backend_lustre)
set_target_properties(penumbra_ui_backend_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

# ---------------------------------------------------------------------------
# demo/main.cpp — a small, real running app (real window, real Renderer, real
# click handling) exercising both wiring points from
# docs/penumbra_ui_backend_lustre_bridge_decision.md: BuildWidgetTree/
# Walker.cpp resolving Lustre style at mount, and PenumbraWidgetAdapter::
# ApplyPropDiff re-resolving it on a live class change. Not part of the
# library itself — purely for visually confirming the wiring against a real
# window/renderer rather than test assertions alone.
# ---------------------------------------------------------------------------
add_executable(penumbra_ui_backend_demo demo/main.cpp)
target_link_libraries(penumbra_ui_backend_demo PRIVATE penumbra_ui_backend)
target_compile_definitions(penumbra_ui_backend_demo PRIVATE
    DEMO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vendor/penumbra/demo/assets")
set_target_properties(penumbra_ui_backend_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demo")
