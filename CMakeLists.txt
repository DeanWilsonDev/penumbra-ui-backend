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
# The backend itself: walks a parsed, props-resolved IrisComponent IR tree
# (from `iris`) and builds a real Penumbra widget tree via Penumbra's
# fluent Builder API (from `penumbra`) — docs/iris_core_spec.md §2.5–§2.6,
# §3 on the `iris` side; `docs/iris_stage2_decision_doc.md` for the Stage 2
# planning decisions this implements.
# ---------------------------------------------------------------------------
add_library(penumbra_ui_backend STATIC
    src/PenumbraUiBackend/Walker.cpp
    src/PenumbraUiBackend/PenumbraWidgetAdapter.cpp)

target_include_directories(penumbra_ui_backend PUBLIC include)
target_compile_features(penumbra_ui_backend PUBLIC cxx_std_23)
target_link_libraries(penumbra_ui_backend PUBLIC iris penumbra)

# ---------------------------------------------------------------------------
# The Lustre style bridge: applies a Lustre::ResolvedStyle (from `lustre`,
# which knows nothing about Iris and never will) onto a real Penumbra
# widget's style structs. Deliberately its own target, independent of
# `penumbra_ui_backend` above — it needs nothing from `iris` (it operates
# on plain Penumbra::Widgets::WidgetBase&, not IrisComponent), and nothing
# in `penumbra_ui_backend` needs it either. This is what keeps Iris and
# Lustre from ever depending on each other even indirectly: this repo is the
# one place allowed to know about both, and even here the two connect only
# through this narrow, swappable seam (see
# docs/penumbra_ui_backend_lustre_bridge_decision.md) — IStyleApplier is
# the interface a future, non-Lustre styling language would implement
# instead, without touching Walker.cpp/PenumbraWidgetAdapter.cpp at all.
# ---------------------------------------------------------------------------
add_library(penumbra_ui_backend_lustre STATIC
    src/PenumbraUiBackend/Lustre/StyleApplier.cpp)

target_include_directories(penumbra_ui_backend_lustre PUBLIC include)
target_compile_features(penumbra_ui_backend_lustre PUBLIC cxx_std_23)
target_link_libraries(penumbra_ui_backend_lustre PUBLIC lustre penumbra)

# ---------------------------------------------------------------------------
# Tests — plain hand-rolled Expect()/[PASS]/[FAIL] executable, matching
# `iris`'s own test style (Cimmerian, the ecosystem's intended long-term test
# framework, isn't wired into either repo yet).
# ---------------------------------------------------------------------------
add_executable(penumbra_ui_backend_tests
    tests/WalkerTests.cpp
    tests/PenumbraWidgetAdapterTests.cpp
    tests/SlotWiringTests.cpp
    tests/LustreStyleApplierTests.cpp)

target_link_libraries(penumbra_ui_backend_tests PRIVATE penumbra_ui_backend penumbra_ui_backend_lustre)
set_target_properties(penumbra_ui_backend_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
