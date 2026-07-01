#pragma once

#include <cstdint>

// ── Telemetry / MAVSDK ──────────────────────────────────────────────────────
constexpr int kSimulationIntervalMs = 1000;
constexpr int kHeartbeatTimeoutMs = 5000;
constexpr int kMavsdkDiscoverTimeoutMs = 8000;

// ── MAVLink commands ─────────────────────────────────────────────────────────
constexpr uint16_t kMavCmdSetMessageInterval = 511;

// ── Servo / PWM ─────────────────────────────────────────────────────────────
constexpr int kServoMinInstance = 1;
constexpr int kServoMaxInstance = 16;
constexpr int kServoMinPwm = 800;
constexpr int kServoMaxPwm = 2200;
constexpr int kServoOutputCount = 8;
constexpr int kServoDefaultMinPwm = 1000;
constexpr int kServoDefaultMaxPwm = 2000;

// ── Hardware Test ───────────────────────────────────────────────────────────
constexpr int kMavResultAccepted = 0;
constexpr int kFeedbackTimeoutMs = 2000;
constexpr int kFeedbackTimeoutMarginMs = 1000;
constexpr int kAckTimeoutMs = 1000;

// ── Checklist / Thresholds ──────────────────────────────────────────────────
constexpr double kBatteryMinVoltage = 15.0;
constexpr double kBatteryTolerance = 0.5;
constexpr int kGpsMinSatellites = 8;
constexpr double kAirspeedNominal = 0.0;
constexpr double kAirspeedTolerance = 1.0;
constexpr double kMotorTestThrottleDefault = 0.20;
constexpr int kMotorTestDurationDefaultMs = 3000;
constexpr double kMotorTestThrottleVtol = 0.20;

// ── Launch Mechanism ────────────────────────────────────────────────────────
constexpr int kLaunchServoInstance = 5;
constexpr int kLaunchServoPwm = 2000;

// ── Serial / Connection ─────────────────────────────────────────────────────
constexpr int kSerialBaudDefault = 57600;
constexpr int kSerialBaudPixhawk = 115200;

// ── Gimbal / Mount ──────────────────────────────────────────────────────────
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
constexpr int kMavTypeQuad = 2;
constexpr int kMavTypePlane = 3;
constexpr int kMavTypeVtolTiltrotor = 21;
constexpr int kMavTypeVtolQuad = 22;

// ── Battery Thresholds per vehicle type ─────────────────────────────────────
constexpr double kQuadMinVoltage = 14.0;
constexpr double kFixedWingMinVoltage = 14.5;
constexpr double kVtolMinVoltage = 14.5;

// ── RC Override ──────────────────────────────────────────────────────────────
constexpr int kRcOverrideTimeoutMs = 3000;
constexpr int kRcOverrideChannelCount = 8;

// ── ArduPilot FRAME_CLASS → vehicle type ─────────────────────────────────────
constexpr int kFrameClassQuad      = 1;
constexpr int kFrameClassHexa      = 2;
constexpr int kFrameClassOcta      = 3;
constexpr int kFrameClassFixedWing = 10;
constexpr int kFrameClassVtol      = 15;
constexpr int kFrameClassTiltVtol  = 18;

// ── Motor function mapping (SERVO{N}_FUNCTION values) ────────────────────────
constexpr int kMotorFuncMotor1 = 33;
constexpr int kMotorFuncMotor2 = 34;
constexpr int kMotorFuncMotor3 = 35;
constexpr int kMotorFuncMotor4 = 36;
constexpr int kMotorFuncThrottle = 70;   // Fixed-wing throttle
constexpr int kMotorFuncBase  = 32;      // Motor N = base + N

// ── Control surface SERVO{N}_FUNCTION values (ArduPilot) ─────────────────────
constexpr int kServoFuncAileron  = 4;
constexpr int kServoFuncElevator = 19;
constexpr int kServoFuncRudder   = 21;
constexpr int kServoFuncThrottle = 70;   // Same as motor throttle — actual motor
constexpr int kServoFuncMotorMin = 33;    // Motor 1
constexpr int kServoFuncMotorMax = 40;    // Motor 8 (function values 33-40)

// ── Parameter names ──────────────────────────────────────────────────────────
constexpr const char* kParamFrameClass  = "FRAME_CLASS";
constexpr const char* kParamFrameType   = "FRAME_TYPE";

// ── Motor / PWM display ────────────────────────────────────────────────────
constexpr int kPwmMin              =  800;
constexpr int kPwmMax              = 2200;
constexpr int kPwmRangeMin         = 1000;
constexpr int kPwmRangeMax         = 2000;
constexpr int kMotorCountQuad      = 4;
constexpr int kMotorCountHexa      = 6;
constexpr int kMotorCountOcta      = 8;
constexpr int kMotorCountFixedWing = 1;
constexpr int kMotorCountVtolQuad  = 4;

// ── EKF / Health thresholds ─────────────────────────────────────────────────
constexpr float kEkfVarianceMax = 1.0f;

// ── Compliance / Part 107 ────────────────────────────────────────────────────
constexpr const char* kDefaultPilotName    = "";
constexpr const char* kDefaultLicense      = "";
constexpr const char* kDefaultAircraftReg  = "";

// ── Maintenance ──────────────────────────────────────────────────────────────
constexpr int kMaintWarningPercent  = 80;
constexpr int kMaintCriticalPercent = 100;

// ── PreflightPlugin thresholds ──────────────────────────────────────────────
constexpr double kBatteryHealthVoltageThreshold = 80.0;
constexpr double kBatteryHealthStddevThreshold = 0.5;
constexpr double kMissionEnergyMargin = 0.60;

// ── EKF Flags ───────────────────────────────────────────────────────────────
constexpr uint16_t kEfkFlagVelocity  = 1 << 0;
constexpr uint16_t kEfkFlagPosHoriz  = 1 << 1;
constexpr uint16_t kEfkFlagPosVert   = 1 << 2;
constexpr uint16_t kEfkFlagMag       = 1 << 3;
constexpr uint16_t kEfkFlagBaro      = 1 << 4;
constexpr uint16_t kEfkFlagGps       = 1 << 5;

// ── New Layout Design: Flat Telemetry Grid (60% / 40% split) ─────────────────
constexpr float kTelemetryPanelRatio = 0.60f;  // Left panel width ratio
constexpr float kChecklistPanelRatio = 0.40f;  // Right panel width ratio
constexpr int kMinRowHeight = 32;              // Minimum row height (32dp)
constexpr int kCategoryVerticalSpacing = 8;    // Vertical spacing between categories (8dp)
constexpr int kHealthGridColumns = 4;          // Health status grid: 4 items per row