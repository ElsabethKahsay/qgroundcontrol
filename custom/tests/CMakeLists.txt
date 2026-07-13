# ============================================================================
# Custom Preflight Plugin — Test Infrastructure
# ============================================================================
# Test source files are glob'd by custom/CMakeLists.txt and compiled into the
# main binary using Qt Test framework. This file adds mock include directories
# and test-specific build configuration.
# ============================================================================

if(NOT QGC_BUILD_TESTING)
    return()
endif()

# Mock include path added by custom/CMakeLists.txt via CUSTOM_INCLUDE_DIRECTORIES
message(STATUS "QGC: PreflightPlugin test infrastructure ready (mocks in ${CMAKE_CURRENT_SOURCE_DIR}/mocks)")
