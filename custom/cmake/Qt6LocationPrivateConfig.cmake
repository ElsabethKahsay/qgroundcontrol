# Stub for Qt6LocationPrivate (not shipped with open-source Qt 6.8.x).
# QGC's CMakeLists.txt lists it as REQUIRED but no source code uses it directly.
# This stub provides the cmake target so find_package(Qt6 COMPONENTS LocationPrivate) succeeds.

if(TARGET Qt6::LocationPrivate)
    set(Qt6LocationPrivate_FOUND TRUE)
    return()
endif()

add_library(Qt6::LocationPrivate INTERFACE IMPORTED)
set_target_properties(Qt6::LocationPrivate PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ""
)

set(Qt6LocationPrivate_FOUND TRUE)
