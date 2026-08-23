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
# Custom Plugin Sources (listed explicitly to avoid VerifyGlobs loop)
# ============================================================================

set(CUSTOM_SOURCE_REL
    src/PreflightPlugin.cpp
    src/PreflightPlugin.h
    src/QmlModuleInit.cpp
    src/adapters/QgcParameterAdapter.cpp
    src/adapters/QgcParameterAdapter.h
    src/adapters/QgcVehicleAdapter.cpp
    src/adapters/QgcVehicleAdapter.h
    src/adapters/TelemetryBridge.cpp
    src/adapters/TelemetryBridge.h
    src/controllers/HardwareTestController.cpp
    src/controllers/HardwareTestController.h
    src/controllers/ControlSurfaceTestController.cpp
    src/controllers/ControlSurfaceTestController.h
    src/controllers/HardwareTestProfile.cpp
    src/controllers/HardwareTestProfile.h
    src/core/AbstractCheck.cpp
    src/core/AbstractCheck.h
    src/core/AccelConsistencyCheck.cpp
    src/core/AccelConsistencyCheck.h
    src/core/AirspeedCheck.cpp
    src/core/AirspeedCheck.h
    src/core/ArmingGate.cpp
    src/core/ArmingGate.h
    src/core/AttitudeCheck.cpp
    src/core/AttitudeCheck.h
    src/core/BatteryFailsafeCheck.cpp
    src/core/BatteryFailsafeCheck.h
    src/core/BatteryFailsafeParamCheck.cpp
    src/core/BatteryFailsafeParamCheck.h
    src/core/BatteryTemperatureCheck.cpp
    src/core/BatteryTemperatureCheck.h
    src/core/BatteryVoltageCheck.cpp
    src/core/BatteryVoltageCheck.h
    src/core/CellConfigCheck.cpp
    src/core/CellConfigCheck.h
    src/core/ChecklistEngine.cpp
    src/core/ChecklistEngine.h
    src/core/ChecklistItemModel.cpp
    src/core/ChecklistItemModel.h
    src/core/EkfFailsafeCheck.cpp
    src/core/EkfFailsafeCheck.h
    src/core/GcsFailsafeCheck.cpp
    src/core/GcsFailsafeCheck.h
    src/core/GimbalLinkCheck.cpp
    src/core/GimbalLinkCheck.h
    src/core/GpsFixCheck.cpp
    src/core/GpsFixCheck.h
    src/core/GpsSpeedAccuracyCheck.cpp
    src/core/GpsSpeedAccuracyCheck.h
    src/core/HeartbeatCheck.cpp
    src/core/HeartbeatCheck.h
    src/core/HomePositionCheck.cpp
    src/core/HomePositionCheck.h
    src/core/ImuTemperatureCheck.cpp
    src/core/ImuTemperatureCheck.h
    src/core/LevelCalibrationCheck.cpp
    src/core/LevelCalibrationCheck.h
    src/core/ManualConfirmCheck.cpp
    src/core/ManualConfirmCheck.h
    src/core/MavlinkProtocolCheck.cpp
    src/core/MavlinkProtocolCheck.h
    src/core/MissionEnergyCheck.cpp
    src/core/MissionEnergyCheck.h
    src/core/MotorCountCheck.cpp
    src/core/MotorCountCheck.h
    src/core/MotorSpinCheck.cpp
    src/core/MotorSpinCheck.h
    src/core/MotorTemperatureCheck.cpp
    src/core/MotorTemperatureCheck.h
    src/core/ParameterWatchlist.h
    src/core/PowerModel.cpp
    src/core/PowerModel.h
    src/core/PreflightManager.cpp
    src/core/PreflightManager.h
    src/core/PreflightStateMachine.cpp
    src/core/PreflightStateMachine.h
    src/core/RadioFailsafeCheck.cpp
    src/core/RadioFailsafeCheck.h
    src/core/RcArmingSwitchCheck.cpp
    src/core/RcArmingSwitchCheck.h
    src/core/RcCalibrationCheck.cpp
    src/core/RcCalibrationCheck.h
    src/core/RcFailsafeCheck.cpp
    src/core/RcFailsafeCheck.h
    src/core/RcModeSwitchCheck.cpp
    src/core/RcModeSwitchCheck.h
    src/core/RcThrottleMinCheck.cpp
    src/core/RcThrottleMinCheck.h
    src/core/RtlAltParamCheck.cpp
    src/core/RtlAltParamCheck.h
    src/core/TelemetryDropRateCheck.cpp
    src/core/TelemetryDropRateCheck.h
    src/core/UavParameterManager.cpp
    src/core/UavParameterManager.h
    src/core/VehicleProfileManager.cpp
    src/core/VehicleProfileManager.h
    src/core/ZoneComplianceCheck.cpp
    src/core/ZoneComplianceCheck.h
    src/core/VideoFeedCheck.cpp
    src/core/VideoFeedCheck.h
    src/mission/WaypointMath.cpp
    src/mission/WaypointMath.h
    src/mission/WaypointMathHelper.cpp
    src/mission/WaypointMathHelper.h
    src/ui/PreflightChecklistFilterModel.cpp
    src/ui/PreflightChecklistFilterModel.h
    src/ui/PreflightChecklistModel.cpp
    src/ui/PreflightChecklistModel.h
    src/utils/AlertManager.cpp
    src/utils/AlertManager.h
    src/utils/AutopilotInfoDetector.cpp
    src/utils/AutopilotInfoDetector.h
    src/utils/ClipboardHelper.cpp
    src/utils/ClipboardHelper.h
    src/utils/Config.h
    src/utils/DatabaseManager.cpp
    src/utils/DatabaseManager.h
    src/utils/ExportHelper.cpp
    src/utils/ExportHelper.h
    src/utils/Hysteresis.h
    src/utils/MaintenanceTracker.cpp
    src/utils/MaintenanceTracker.h
    src/utils/PreflightSettingsManager.cpp
    src/utils/PreflightSettingsManager.h
    src/utils/TemplateManager.cpp
    src/utils/TemplateManager.h
    src/utils/WeatherProvider.cpp
    src/utils/WeatherProvider.h
    src/detection/VehicleRegistry.cpp
    src/detection/VehicleRegistry.h
    src/managers/OperatorManager.cpp
    src/managers/OperatorManager.h
    src/managers/FlightSession.cpp
    src/managers/FlightSession.h
    src/managers/TelemetryEventLogger.cpp
    src/managers/TelemetryEventLogger.h
    src/models/FlightHistoryModel.cpp
    src/models/FlightHistoryModel.h
    src/models/NoFlyZoneModel.cpp
    src/models/NoFlyZoneModel.h
    src/models/VehicleListModel.cpp
    src/models/VehicleListModel.h
)

set(CUSTOM_SOURCES "")

foreach(_src IN LISTS CUSTOM_SOURCE_REL)
    list(APPEND CUSTOM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
endforeach()

set(CUSTOM_SOURCES ${CUSTOM_SOURCES} CACHE INTERNAL "" FORCE)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/detection
    ${CMAKE_CURRENT_SOURCE_DIR}/src/managers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/models
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
    file(GLOB_RECURSE _custom_test_src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h
    )
    if(_custom_test_src)
        list(APPEND CUSTOM_SOURCES ${_custom_test_src})
        set(CUSTOM_SOURCES ${CUSTOM_SOURCES} CACHE INTERNAL "" FORCE)
        message(STATUS "QGC: PreflightPlugin tests enabled (${_custom_test_src} source files)")
    endif()
    list(APPEND CUSTOM_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/tests/mocks)
    set(CUSTOM_INCLUDE_DIRECTORIES ${CUSTOM_INCLUDE_DIRECTORIES} CACHE INTERNAL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()

message(STATUS "QGC: UAVPreflight plugin configured successfully")
