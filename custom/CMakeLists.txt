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
    src/controllers/HardwareTestProfile.cpp
    src/controllers/HardwareTestProfile.h
    src/core/AbstractCheck.cpp
    src/core/AbstractCheck.h
    src/core/AccelConsistencyCheck.cpp
    src/core/AccelConsistencyCheck.h
    src/core/AhrsHealthCheck.cpp
    src/core/AhrsHealthCheck.h
    src/core/AirspeedCheck.cpp
    src/core/AirspeedCheck.h
    src/core/AmbientTemperatureCheck.cpp
    src/core/AmbientTemperatureCheck.h
    src/core/ArmingGate.cpp
    src/core/ArmingGate.h
    src/core/AttitudeCheck.cpp
    src/core/AttitudeCheck.h
    src/core/BaroAltConsistencyCheck.cpp
    src/core/BaroAltConsistencyCheck.h
    src/core/BaroHealthCheck.cpp
    src/core/BaroHealthCheck.h
    src/core/BaroTemperatureCheck.cpp
    src/core/BaroTemperatureCheck.h
    src/core/BatteryFailsafeCheck.cpp
    src/core/BatteryFailsafeCheck.h
    src/core/BatteryHealthCheck.cpp
    src/core/BatteryHealthCheck.h
    src/core/BatteryTemperatureCheck.cpp
    src/core/BatteryTemperatureCheck.h
    src/core/BatteryVoltageCheck.cpp
    src/core/BatteryVoltageCheck.h
    src/core/CellConfigCheck.cpp
    src/core/CellConfigCheck.h
    src/core/CellVoltageBalanceCheck.cpp
    src/core/CellVoltageBalanceCheck.h
    src/core/ChecklistEngine.cpp
    src/core/ChecklistEngine.h
    src/core/ChecklistItemModel.cpp
    src/core/ChecklistItemModel.h
    src/core/CompanionLinkCheck.cpp
    src/core/CompanionLinkCheck.h
    src/core/CompassCalCheck.cpp
    src/core/CompassCalCheck.h
    src/core/CompassOrientationCheck.cpp
    src/core/CompassOrientationCheck.h
    src/core/CompassYawConsistencyCheck.cpp
    src/core/CompassYawConsistencyCheck.h
    src/core/CurrentSensorCheck.cpp
    src/core/CurrentSensorCheck.h
    src/core/DualGpsConsistencyCheck.cpp
    src/core/DualGpsConsistencyCheck.h
    src/core/EkfFailsafeCheck.cpp
    src/core/EkfFailsafeCheck.h
    src/core/EkfStatusFlagsCheck.cpp
    src/core/EkfStatusFlagsCheck.h
    src/core/EkfVarianceCheck.cpp
    src/core/EkfVarianceCheck.h
    src/core/EscCurrentSymmetryCheck.cpp
    src/core/EscCurrentSymmetryCheck.h
    src/core/EscFirmwareCheck.cpp
    src/core/EscFirmwareCheck.h
    src/core/EscResponsivenessCheck.cpp
    src/core/EscResponsivenessCheck.h
    src/core/EscVoltageConsistencyCheck.cpp
    src/core/EscVoltageConsistencyCheck.h
    src/core/GcsFailsafeCheck.cpp
    src/core/GcsFailsafeCheck.h
    src/core/GeofenceMaxAltCheck.cpp
    src/core/GeofenceMaxAltCheck.h
    src/core/GeofenceMaxRadiusCheck.cpp
    src/core/GeofenceMaxRadiusCheck.h
    src/core/GeofenceParamCheck.cpp
    src/core/GeofenceParamCheck.h
    src/core/GimbalLinkCheck.cpp
    src/core/GimbalLinkCheck.h
    src/core/GpsBaroAltConsistencyCheck.cpp
    src/core/GpsBaroAltConsistencyCheck.h
    src/core/GpsFixCheck.cpp
    src/core/GpsFixCheck.h
    src/core/GpsSpeedAccuracyCheck.cpp
    src/core/GpsSpeedAccuracyCheck.h
    src/core/GyroBiasCheck.cpp
    src/core/GyroBiasCheck.h
    src/core/HeartbeatCheck.cpp
    src/core/HeartbeatCheck.h
    src/core/HomePositionCheck.cpp
    src/core/HomePositionCheck.h
    src/core/ImuCalCheck.cpp
    src/core/ImuCalCheck.h
    src/core/ImuTemperatureCheck.cpp
    src/core/ImuTemperatureCheck.h
    src/core/LevelCalibrationCheck.cpp
    src/core/LevelCalibrationCheck.h
    src/core/MagFieldStrengthCheck.cpp
    src/core/MagFieldStrengthCheck.h
    src/core/MagInterferenceCheck.cpp
    src/core/MagInterferenceCheck.h
    src/core/ManualConfirmCheck.cpp
    src/core/ManualConfirmCheck.h
    src/core/MavlinkProtocolCheck.cpp
    src/core/MavlinkProtocolCheck.h
    src/core/MetarCeilingCheck.cpp
    src/core/MetarCeilingCheck.h
    src/core/MetarPrecipitationCheck.cpp
    src/core/MetarPrecipitationCheck.h
    src/core/MetarTemperatureCheck.cpp
    src/core/MetarTemperatureCheck.h
    src/core/MetarVisibilityCheck.cpp
    src/core/MetarVisibilityCheck.h
    src/core/MissionCountCheck.cpp
    src/core/MissionCountCheck.h
    src/core/MissionEnergyCheck.cpp
    src/core/MissionEnergyCheck.h
    src/core/MissionItemCheck.cpp
    src/core/MissionItemCheck.h
    src/core/MotorCountCheck.cpp
    src/core/MotorCountCheck.h
    src/core/MotorSpinCheck.cpp
    src/core/MotorSpinCheck.h
    src/core/MotorTemperatureCheck.cpp
    src/core/MotorTemperatureCheck.h
    src/core/OpticalFlowCheck.cpp
    src/core/OpticalFlowCheck.h
    src/core/ParameterWatchlist.h
    src/core/PowerModel.cpp
    src/core/PowerModel.h
    src/core/PowerModuleHealthCheck.cpp
    src/core/PowerModuleHealthCheck.h
    src/core/PreArmOkCheck.cpp
    src/core/PreArmOkCheck.h
    src/core/PreflightManager.cpp
    src/core/PreflightManager.h
    src/core/PreflightStateMachine.cpp
    src/core/PreflightStateMachine.h
    src/core/RadioBufferCheck.cpp
    src/core/RadioBufferCheck.h
    src/core/RadioFailsafeCheck.cpp
    src/core/RadioFailsafeCheck.h
    src/core/RcArmingSwitchCheck.cpp
    src/core/RcArmingSwitchCheck.h
    src/core/RcCalibrationCheck.cpp
    src/core/RcCalibrationCheck.h
    src/core/RcChannelCountCheck.cpp
    src/core/RcChannelCountCheck.h
    src/core/RcFailsafeCheck.cpp
    src/core/RcFailsafeCheck.h
    src/core/RcModeSwitchCheck.cpp
    src/core/RcModeSwitchCheck.h
    src/core/RcRssiCheck.cpp
    src/core/RcRssiCheck.h
    src/core/RcThrottleMinCheck.cpp
    src/core/RcThrottleMinCheck.h
    src/core/RcTrimCheck.cpp
    src/core/RcTrimCheck.h
    src/core/RedundantPowerCheck.cpp
    src/core/RedundantPowerCheck.h
    src/core/RtlAltParamCheck.cpp
    src/core/RtlAltParamCheck.h
    src/core/RtlTerrainCheck.cpp
    src/core/RtlTerrainCheck.h
    src/core/TafDeteriorationCheck.cpp
    src/core/TafDeteriorationCheck.h
    src/core/TakeoffCommandCheck.cpp
    src/core/TakeoffCommandCheck.h
    src/core/TelemetryDropRateCheck.cpp
    src/core/TelemetryDropRateCheck.h
    src/core/TerrainClearanceCheck.cpp
    src/core/TerrainClearanceCheck.h
    src/core/UavParameterManager.cpp
    src/core/UavParameterManager.h
    src/core/VehicleProfileManager.cpp
    src/core/VehicleProfileManager.h
    src/core/VibrationCheck.cpp
    src/core/VibrationCheck.h
    src/core/VibrationFailsafeCheck.cpp
    src/core/VibrationFailsafeCheck.h
    src/core/VideoFeedCheck.cpp
    src/core/VideoFeedCheck.h
    src/core/WeatherWindCheck.cpp
    src/core/WeatherWindCheck.h
    src/core/WeatherWindGustCheck.cpp
    src/core/WeatherWindGustCheck.h
    src/core/WindSpeedCheck.cpp
    src/core/WindSpeedCheck.h
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
    src/utils/AutopilotDetector.cpp
    src/utils/AutopilotDetector.h
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
    src/utils/VideoManager.cpp
    src/utils/VideoManager.h
    src/utils/WeatherProvider.cpp
    src/utils/WeatherProvider.h
    src/detection/VehicleRegistry.cpp
    src/detection/VehicleRegistry.h
)

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
