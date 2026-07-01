# ============================================================================
# UAVPreflight Plugin - Qt6 CMake Build
# ============================================================================

message(STATUS "QGC: Adding UAVPreflight Plugin")

# ============================================================================
# Custom Android Package Template
# ============================================================================

if(ANDROID)
    set(CUSTOM_ANDROID_DIR "${CMAKE_SOURCE_DIR}/custom/android")
    if(EXISTS "${CUSTOM_ANDROID_DIR}")
        file(GLOB CUSTOM_ANDROID_FILES "${CUSTOM_ANDROID_DIR}/*")
        if(CUSTOM_ANDROID_FILES)
            message(STATUS "QGC: Custom Android package template found. Overlaying custom files...")
            set(DEFAULT_ANDROID_DIR "${CMAKE_SOURCE_DIR}/android")
            set(FINAL_ANDROID_DIR "${CMAKE_BINARY_DIR}/custom/android")
            file(COPY "${DEFAULT_ANDROID_DIR}/." DESTINATION "${FINAL_ANDROID_DIR}")
            execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory "${CUSTOM_ANDROID_DIR}" "${FINAL_ANDROID_DIR}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                "${DEFAULT_ANDROID_DIR}/"
                "${CUSTOM_ANDROID_DIR}/"
            )
            set(QGC_ANDROID_PACKAGE_SOURCE_DIR "${FINAL_ANDROID_DIR}" CACHE PATH "Path to a custom Android package template" FORCE)
            message(STATUS "QGC: Android package template path set to: ${QGC_ANDROID_PACKAGE_SOURCE_DIR}")
        else()
            message(STATUS "QGC: Custom Android package template empty. Using default.")
        endif()
    else()
        message(STATUS "QGC: No custom Android package template found. Using default.")
    endif()
endif()

# ============================================================================
# Custom Resources and Import Paths
# ============================================================================

list(APPEND CUSTOM_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/custom.qrc
)
set(QGC_RESOURCES ${QGC_RESOURCES} ${CUSTOM_RESOURCES} CACHE STRING "Paths to .qrc Resources" FORCE)

set(QML_IMPORT_PATH ${QML_IMPORT_PATH}
    "${CMAKE_CURRENT_SOURCE_DIR}/res"
    "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    CACHE STRING "Extra qml import paths" FORCE
)

# ============================================================================
# Custom Plugin Sources (auto-discovered via GLOB)
# ============================================================================

file(GLOB_RECURSE _custom_src CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

file(GLOB_RECURSE _custom_h CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

set(CUSTOM_SOURCES
    ${_custom_src}
    ${_custom_h}
    CACHE INTERNAL "" FORCE
)

# ============================================================================
# Custom Build Configuration
# ============================================================================

set(CUSTOM_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters
    ${CMAKE_CURRENT_SOURCE_DIR}/src/controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mission
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_DEFINITIONS
    QGC_CUSTOM_BUILD
    CUSTOMHEADER="PreflightPlugin.h"
    CUSTOMCLASS=PreflightPlugin
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_QT_COMPONENTS
    Core
    Sql
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_LIBRARIES
    Qt6::Sql
    CACHE INTERNAL "" FORCE
)

# ============================================================================
# Custom Tests
# ============================================================================

if(QGC_BUILD_TESTING)
    file(GLOB_RECURSE _custom_test_src CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h
    )
    if(_custom_test_src)
        list(APPEND CUSTOM_SOURCES ${_custom_test_src})
        set(CUSTOM_SOURCES ${CUSTOM_SOURCES} CACHE INTERNAL "" FORCE)
    endif()
    message(STATUS "QGC: PreflightPlugin tests enabled")
endif()

message(STATUS "QGC: UAVPreflight plugin configured successfully")
