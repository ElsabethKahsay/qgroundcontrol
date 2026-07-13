# ============================================================================
# Preflight Custom Build Configuration Overrides
# ============================================================================

# ----------------------------------------------------------------------------
# Application Branding
# ----------------------------------------------------------------------------
set(QGC_APP_NAME "PreflightQGroundControl" CACHE STRING "App Name" FORCE)
set(QGC_APP_DESCRIPTION "Skywin Aeronautical Ground Control Station" CACHE STRING "Application description" FORCE)
set(QGC_APP_COPYRIGHT "Copyright (C) 2026 Skywin Aeronautical. All rights reserved." CACHE STRING "Copyright notice" FORCE)
set(QGC_ORG_NAME "UAVPreflight" CACHE STRING "Organization name" FORCE)
set(QGC_ORG_DOMAIN "org.uavpreflight" CACHE STRING "Organization domain" FORCE)
set(QGC_PACKAGE_NAME "org.uavpreflight.qgroundcontrol" CACHE STRING "Package identifier" FORCE)

# ----------------------------------------------------------------------------
# Custom Icons and Graphics
# ----------------------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/custom/res/icons/custom_qgroundcontrol.icns")
    set(QGC_MACOS_ICON_PATH "${CMAKE_SOURCE_DIR}/custom/res/icons/custom_qgroundcontrol.icns" CACHE FILEPATH "MacOS Icon Path" FORCE)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/custom/res/icons/custom_qgroundcontrol.svg")
    set(QGC_APPIMAGE_ICON_SCALABLE_PATH "${CMAKE_SOURCE_DIR}/custom/res/icons/custom_qgroundcontrol.svg" CACHE FILEPATH "AppImage Icon SVG Path" FORCE)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/custom/deploy/windows/installheader.bmp")
    set(QGC_WINDOWS_INSTALL_HEADER_PATH "${CMAKE_SOURCE_DIR}/custom/deploy/windows/installheader.bmp" CACHE FILEPATH "Windows Install Header Path" FORCE)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/custom/deploy/windows/WindowsQGC.ico")
    set(QGC_WINDOWS_ICON_PATH "${CMAKE_SOURCE_DIR}/custom/deploy/windows/WindowsQGC.ico" CACHE FILEPATH "Windows Icon Path" FORCE)
endif()

# ----------------------------------------------------------------------------
# Qt6LocationPrivate stub for Qt 6.8.x
# ----------------------------------------------------------------------------
# Qt 6.8.x open-source does not ship Qt6LocationPrivate cmake config, but
# QGC's CMakeLists.txt lists it as REQUIRED. Our stub cmake module provides
# the target. Add our cmake dir to the prefix path so it can be found.
list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/custom/cmake")

# ----------------------------------------------------------------------------
# Feature Set Customization
# ----------------------------------------------------------------------------
set(QGC_DISABLE_APM_MAVLINK OFF CACHE BOOL "Disable APM Dialect" FORCE)
set(QGC_DISABLE_APM_PLUGIN ON CACHE BOOL "Disable APM Plugin" FORCE)
set(QGC_DISABLE_APM_PLUGIN_FACTORY ON CACHE BOOL "Disable APM Plugin Factory" FORCE)
set(QGC_DISABLE_PX4_PLUGIN_FACTORY ON CACHE BOOL "Disable PX4 Plugin Factory" FORCE)

# ----------------------------------------------------------------------------
# Qt Version Configuration
# ----------------------------------------------------------------------------
# Override the minimum Qt version from .github/build-config.json (6.10.0).
# Change this to match your actual Qt installation (e.g., 6.5.0 for Qt 6.8.3).
set(QGC_QT_MINIMUM_VERSION "6.5.0" CACHE STRING "Minimum supported Qt version" FORCE)


