cmake_minimum_required(VERSION 3.30)

project(PenumbraUiBackend
    VERSION 0.1.0
    DESCRIPTION "The Penumbra backend for Chaos and Cosmos — maps Chaos component IR onto a real Penumbra widget tree and applies Cosmos::ResolvedStyle to it"
    LANGUAGES CXX)

if((CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
   OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19))
  message(FATAL_ERROR "Penumbra UI Backend needs GCC 14 or later or Clang 19 or later; found ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ---------------------------------------------------------------------------
# Chaos, Penumbra, and Cosmos are independent projects that don't know about
# each other — each is vendored here as its own pinned git submodule, and
# each subdirectory sets its own C++ standard (all three: C++26), which is
# fine — CMAKE_CXX_STANDARD is directory-scoped, not global, once you're
# inside an add_subdirectory().
# ---------------------------------------------------------------------------
include(FetchContent)
add_subdirectory(external/chaos)
add_subdirectory(external/penumbra)
add_subdirectory(external/cosmos)

# ---------------------------------------------------------------------------
# The Cosmos style bridge: applies a Cosmos::ResolvedStyle (from `cosmos`,
# which knows nothing about Chaos and never will) onto a real Penumbra
# widget's style structs, plus ResolveStyle() (StyleResolution.cpp), the
# glue that composes Cosmos::Resolver's two-layer cascade correctly (§1.2's
# global-unbounded/component-bounded asymmetry) for a single element. Kept
# as its own target so a consumer of `penumbra_ui_backend` below that
# doesn't want styling can still avoid pulling in `cosmos` --
# `penumbra_ui_backend` itself now links this (see that target's own
# comment) since Walker.cpp/PenumbraWidgetAdapter.cpp call ResolveStyle()
# directly, but nothing in *this* target needs anything from `chaos`. This is
# what keeps Chaos and Cosmos from ever depending on each other even
# indirectly: this repo is the one place allowed to know about both, and
# even here the two connect only through this narrow, swappable seam (see
# docs/penumbra_ui_backend_cosmos_bridge_decision.md) — IStyleApplier is
# the interface a future, non-Cosmos styling language would implement
# instead.
# ---------------------------------------------------------------------------
add_library(penumbra_ui_backend_cosmos STATIC
    src/PenumbraUiBackend/Cosmos/StyleApplier.cpp
    src/PenumbraUiBackend/Cosmos/StyleResolution.cpp
    src/PenumbraUiBackend/Cosmos/StylesheetLoader.cpp)

target_include_directories(penumbra_ui_backend_cosmos PUBLIC include)
target_compile_features(penumbra_ui_backend_cosmos PUBLIC cxx_std_26)
target_link_libraries(penumbra_ui_backend_cosmos PUBLIC cosmos penumbra)

# ---------------------------------------------------------------------------
# The backend itself: walks a parsed, props-resolved Chaos component IR tree
# (from `chaos`) and builds a real Penumbra widget tree via Penumbra's
# fluent Builder API (from `penumbra`); `docs/iris_stage2_decision_doc.md` for
# the Stage 2 planning decisions this implements. Also resolves and applies Cosmos
# styles at mount (Walker.cpp) and on every class change
# (PenumbraWidgetAdapter.cpp) when a caller supplies a StylesheetSet/
# IStyleApplier via BuildContext — both fields are nullable, so a consumer
# that never sets them gets exactly the pre-styling behavior, unchanged.
# ---------------------------------------------------------------------------
add_library(penumbra_ui_backend STATIC
    src/PenumbraUiBackend/NyxApplicationBridge.cpp
    src/PenumbraUiBackend/ChaosApplication.cpp
    src/PenumbraUiBackend/ScriptCanvas.cpp
    src/PenumbraUiBackend/Walker.cpp
    src/PenumbraUiBackend/ViewportHostWidget.cpp
    src/PenumbraUiBackend/PenumbraWidgetAdapter.cpp)

target_include_directories(penumbra_ui_backend PUBLIC include)
target_compile_features(penumbra_ui_backend PUBLIC cxx_std_26)
target_link_libraries(penumbra_ui_backend PUBLIC chaos penumbra penumbra_ui_backend_cosmos)

# ---------------------------------------------------------------------------
# The application host: everything an app written as one Nyx Application subclass needs from
# C++ (the bridge, nyx-proto's JSON, file and directory bindings, appInstance(), icons and the
# font), so the app's own C++ is a main() and its app-specific host functions.
# ---------------------------------------------------------------------------
add_library(penumbra_ui_backend_host STATIC
    src/PenumbraUiBackend/ChaosApplicationHost.cpp)

target_include_directories(penumbra_ui_backend_host PUBLIC include)
target_compile_features(penumbra_ui_backend_host PUBLIC cxx_std_26)
target_link_libraries(penumbra_ui_backend_host PUBLIC penumbra_ui_backend penumbra_icons nyx-json)

# ---------------------------------------------------------------------------
# Tests — plain hand-rolled Expect()/[PASS]/[FAIL] executable, matching
# `chaos`'s own test style (Cimmerian, the ecosystem's intended long-term test
# framework, isn't wired into either repo yet).
# ---------------------------------------------------------------------------
add_executable(penumbra_ui_backend_tests
    tests/NyxApplicationBridgeTests.cpp
    tests/ChaosApplicationTests.cpp
    tests/WalkerTests.cpp
    tests/PenumbraWidgetAdapterTests.cpp
    tests/SlotWiringTests.cpp
    tests/CosmosStyleApplierTests.cpp
    tests/StyleWiringTests.cpp
    tests/StyleMismatchDiagnosticTests.cpp
    tests/StylesheetLoaderTests.cpp
    tests/StackFillAndScrollbarTests.cpp
    tests/RelativeSizingTests.cpp
    tests/ErrorReportingTests.cpp
    tests/ChartTests.cpp
    tests/TextAreaSpanTests.cpp
    tests/TextSelectionTests.cpp
    tests/ChaosApplicationHostTests.cpp
    tests/PlatformInputTests.cpp)

target_link_libraries(penumbra_ui_backend_tests PRIVATE penumbra_ui_backend penumbra_ui_backend_cosmos
                      penumbra_ui_backend_host)
set_target_properties(penumbra_ui_backend_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

# ---------------------------------------------------------------------------
# demo/main.cpp — a small, real running app (real window, real Renderer, real
# click handling) exercising both wiring points from
# docs/penumbra_ui_backend_cosmos_bridge_decision.md: BuildWidgetTree/
# Walker.cpp resolving Cosmos style at mount, and PenumbraWidgetAdapter::
# ApplyPropDiff re-resolving it on a live class change. Not part of the
# library itself — purely for visually confirming the wiring against a real
# window/renderer rather than test assertions alone.
# ---------------------------------------------------------------------------
add_executable(penumbra_ui_backend_demo demo/main.cpp)
target_link_libraries(penumbra_ui_backend_demo PRIVATE penumbra_ui_backend)
target_compile_definitions(penumbra_ui_backend_demo PRIVATE
    DEMO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/external/penumbra/demo/assets")
set_target_properties(penumbra_ui_backend_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demo")

# Nyx-authored application demo. Integration belongs here rather than in the
# language-neutral Penumbra repository.
add_executable(penumbra_ui_backend_nyx_demo demo_nyx/main.cpp)
target_link_libraries(penumbra_ui_backend_nyx_demo PRIVATE penumbra_ui_backend)
target_compile_definitions(penumbra_ui_backend_nyx_demo PRIVATE
    DEMO_NYX_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/demo_nyx/assets")
set_target_properties(penumbra_ui_backend_nyx_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demo_nyx")
