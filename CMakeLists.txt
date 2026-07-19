cmake_minimum_required(VERSION 3.24)

project(IrisPenumbraBackend
    VERSION 0.1.0
    DESCRIPTION "The Penumbra backend for Iris — maps IrisComponent IR onto a real Penumbra widget tree"
    LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ---------------------------------------------------------------------------
# Iris and Penumbra are independent projects that don't know about each
# other — each is vendored here as its own pinned git submodule, and each
# subdirectory sets its own C++ standard (Iris: C++23, Penumbra: C++20),
# which is fine — CMAKE_CXX_STANDARD is directory-scoped, not global, once
# you're inside an add_subdirectory().
# ---------------------------------------------------------------------------
add_subdirectory(vendor/iris)
add_subdirectory(vendor/penumbra)

# ---------------------------------------------------------------------------
# The backend itself: walks a parsed, props-resolved IrisComponent IR tree
# (from `iris`) and builds a real Penumbra widget tree via Penumbra's
# fluent Builder API (from `penumbra`) — docs/iris_core_spec.md §2.5–§2.6,
# §3 on the `iris` side; `docs/iris_stage2_decision_doc.md` for the Stage 2
# planning decisions this implements.
# ---------------------------------------------------------------------------
add_library(iris_penumbra_backend STATIC
    src/IrisPenumbraBackend/Walker.cpp
    src/IrisPenumbraBackend/PenumbraWidgetAdapter.cpp)

target_include_directories(iris_penumbra_backend PUBLIC include)
target_compile_features(iris_penumbra_backend PUBLIC cxx_std_23)
target_link_libraries(iris_penumbra_backend PUBLIC iris penumbra)

# ---------------------------------------------------------------------------
# Tests — plain hand-rolled Expect()/[PASS]/[FAIL] executable, matching
# `iris`'s own test style (Cimmerian, the ecosystem's intended long-term test
# framework, isn't wired into either repo yet).
# ---------------------------------------------------------------------------
add_executable(iris_penumbra_backend_tests
    tests/WalkerTests.cpp
    tests/PenumbraWidgetAdapterTests.cpp)

target_link_libraries(iris_penumbra_backend_tests PRIVATE iris_penumbra_backend)
set_target_properties(iris_penumbra_backend_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
