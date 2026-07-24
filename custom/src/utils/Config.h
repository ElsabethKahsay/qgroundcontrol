#pragma once

#include <cstdint>

// ============================================================================
// Config.h — Central constants for the Skywin GCS preflight plugin.
// Grouped by subsystem. These are compile-time defaults; some are overridden
// at runtime by vehicle-specific configuration loaded from the database.
// ============================================================================

// ── Telemetry / MAVSDK ──────────────────────────────────────────────────────
// Timing constants for MAVSDK communication and heartbeat monitoring.
constexpr int kSimulationIntervalMs = 1000;
constexpr int kHeartbeatTimeoutMs = 5000;
constexpr int kMavsdkDiscoverTimeoutMs = 8000;

// ── MAVLink commands ─────────────────────────────────────────────────────────
// MAVLink command IDs used for vehicle communication.
constexpr uint16_t kMavCmdSetMessageInterval = 511;

// ── Servo / PWM ─────────────────────────────────────────────────────────────
// Servo output limits and PWM signal ranges. Used by hardware test controllers
// and motor output validation.
constexpr int kServoMinInstance = 1;
constexpr int kServoMaxInstance = 16;
constexpr int kServoMinPwm = 800;
constexpr int kServoMaxPwm = 2200;
constexpr int kServoOutputCount = 8;
constexpr int kServoDefaultMinPwm = 1000;
constexpr int kServoDefaultMaxPwm = 2000;

// ── Hardware Test ───────────────────────────────────────────────────────────
// Timeouts and result codes for motor/servo hardware test sequences.
constexpr int kMavResultAccepted = 0;
constexpr int kFeedbackTimeoutMs = 2000;
constexpr int kFeedbackTimeoutMarginMs = 1000;
constexpr int kAckTimeoutMs = 1000;

// ── Checklist / Thresholds ──────────────────────────────────────────────────
// Default pass/fail thresholds for preflight checks. These are fallback values;
// per-vehicle configs stored in the database can override them.
constexpr double kBatteryMinVoltage = 15.0;
constexpr double kBatteryTolerance = 0.5;
constexpr int kGpsMinSatellites = 8;
constexpr double kAirspeedNominal = 0.0;
constexpr double kAirspeedTolerance = 1.0;
constexpr double kMotorTestThrottleDefault = 0.20;
constexpr int kMotorTestDurationDefaultMs = 3000;
constexpr double kMotorTestThrottleVtol = 0.20;

// ── Launch Mechanism ────────────────────────────────────────────────────────
// Servo instance and PWM value used to trigger the physical launch mechanism.
constexpr int kLaunchServoInstance = 5;
constexpr int kLaunchServoPwm = 2000;

// ── Serial / Connection ─────────────────────────────────────────────────────
// Default baud rates for serial connections to flight controllers.
constexpr int kSerialBaudDefault = 57600;
constexpr int kSerialBaudPixhawk = 115200;

// ── Gimbal / Mount ──────────────────────────────────────────────────────────
// Gimbal component ID, angle limits, and MAVLink mount modes.
constexpr uint8_t kMavCompIdGimbal = 154;
constexpr float kGimbalPitchUp = -45.0f;
constexpr float kGimbalPitchDown = 45.0f;
constexpr float kGimbalCenterPitch = 0.0f;
constexpr float kGimbalCenterRoll = 0.0f;
constexpr float kGimbalCenterYaw = 0.0f;
constexpr int kGimbalMountRetract = 0;
constexpr int kGimbalMountNeutral = 1;
constexpr int kGimbalMountRcTargeting = 2;
constexpr int kGimbalMountGpsPoint = 3;
constexpr int kGimbalDetectionTimeoutMs = 5000;

// ── MAVLink Types ────────────────────────────────────────────────────────────
// MAVLink vehicle type constants used to branch behavior by airframe.
constexpr int kMavTypeQuad = 2;
constexpr int kMavTypePlane = 3;
constexpr int kMavTypeVtolTiltrotor = 21;
constexpr int kMavTypeVtolQuad = 22;

// ── Battery Thresholds per vehicle type ─────────────────────────────────────
// Minimum voltage thresholds differ by airframe due to cell count variations.
constexpr double kQuadMinVoltage = 14.0;
constexpr double kFixedWingMinVoltage = 14.5;
constexpr double kVtolMinVoltage = 14.5;

// ── RC Override ──────────────────────────────────────────────────────────────
// Timeout and channel count for RC override commands sent to the vehicle.
constexpr int kRcOverrideTimeoutMs = 3000;
constexpr int kRcOverrideChannelCount = 8;

// ── ArduPilot FRAME_CLASS → vehicle type ─────────────────────────────────────
// Maps ArduPilot FRAME_CLASS parameter values to airframe categories.
// Used to determine motor count and flight characteristics.
constexpr int kFrameClassQuad      = 1;
constexpr int kFrameClassHexa      = 2;
constexpr int kFrameClassOcta      = 3;
constexpr int kFrameClassFixedWing = 10;
constexpr int kFrameClassVtol      = 15;
constexpr int kFrameClassTiltVtol  = 18;

// ── Motor function mapping (SERVO{N}_FUNCTION values) ────────────────────────
// ArduPilot SERVO_FUNCTION values that identify motor outputs.
// Motor N corresponds to function value (kMotorFuncBase + N).
constexpr int kMotorFuncMotor1 = 33;
constexpr int kMotorFuncMotor2 = 34;
constexpr int kMotorFuncMotor3 = 35;
constexpr int kMotorFuncMotor4 = 36;
constexpr int kMotorFuncThrottle = 70;   // Fixed-wing throttle
constexpr int kMotorFuncBase  = 32;      // Motor N = base + N

// ── Control surface SERVO{N}_FUNCTION values (ArduPilot) ─────────────────────
// SERVO_FUNCTION values for fixed-wing control surfaces and motor range.
constexpr int kServoFuncAileron  = 4;
constexpr int kServoFuncElevator = 19;
constexpr int kServoFuncRudder   = 21;
constexpr int kServoFuncThrottle = 70;   // Same as motor throttle — actual motor
constexpr int kServoFuncMotorMin = 33;    // Motor 1
constexpr int kServoFuncMotorMax = 40;    // Motor 8 (function values 33-40)

// ── Parameter names ──────────────────────────────────────────────────────────
// ArduPilot parameter key names used to query vehicle configuration.
constexpr const char* kParamFrameClass  = "FRAME_CLASS";
constexpr const char* kParamFrameType   = "FRAME_TYPE";

// ── Motor / PWM display ────────────────────────────────────────────────────
// PWM signal limits and motor counts per vehicle type. Used by the motor test
// UI to render output bars and determine how many motor slots to display.
constexpr int kPwmMin              =  800;
constexpr int kPwmMax              = 2200;
constexpr int kPwmRangeMin         = 1000;
constexpr int kPwmRangeMax         = 2000;
constexpr int kMotorCountQuad      = 4;
constexpr int kMotorCountHexa      = 6;
constexpr int kMotorCountOcta      = 8;
constexpr int kMotorCountFixedWing = 1;
constexpr int kMotorCountVtolQuad  = 4;

// ── Compliance / Part 107 ────────────────────────────────────────────────────
// Default empty values for FAA Part 107 compliance fields.
constexpr const char* kDefaultPilotName    = "";
constexpr const char* kDefaultLicense      = "";
constexpr const char* kDefaultAircraftReg  = "";

// ── Maintenance ──────────────────────────────────────────────────────────────
// Percentage thresholds for maintenance warnings vs. critical alerts.
constexpr int kMaintWarningPercent  = 80;
constexpr int kMaintCriticalPercent = 100;



// ── Layout: Telemetry / Checklist Split (35% / 65%) ─────────────────────────
// UI layout ratios and sizing for the main preflight checklist screen.
constexpr float kTelemetryPanelRatio = 0.35f;  // Left panel width ratio
constexpr float kChecklistPanelRatio = 0.65f;  // Right panel width ratio
constexpr int kMinRowHeight = 56;              // Minimum row height (56dp)
constexpr int kCategoryVerticalSpacing = 12;   // Vertical spacing between categories (12dp)
constexpr int kHealthGridColumns = 2;          // Health status grid: 2 items per row