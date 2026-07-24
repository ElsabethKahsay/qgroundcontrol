/**
 * @file HardwareTestProfile.h
 * @brief Data structures for profile-driven hardware test sequences.
 *
 * A HardwareTestProfile defines a complete servo/motor test sequence for a
 * specific vehicle type (e.g. "fixed_wing", "quadcopter").  Profiles are
 * loaded from JSON template files on disk and consist of an ordered list
 * of TestStep entries.  Each step commands a specific servo or motor to a
 * target PWM and validates the feedback response.
 */

#pragma once

#include <QString>
#include <QVector>

/**
 * @brief TestStep - Individual servo actuator test command
 *
 * Represents a single step in a servo sweep sequence.
 * Each step sends a PWM command to a specific servo output and validates
 * the feedback response within a tolerance range.
 */
struct TestStep {
    QString name;               // Display name: "Aileron Left"
    QString testType;           // "motor" for DO_MOTOR_TEST, "servo" for DO_SET_SERVO sweep
    int servoInstance;          // 1-based servo output number (1-16) - for servo_test
    int motorInstance;          // 1-based motor instance (1-8, 0=all) - for motor_test
    int targetPwm;              // Commanded PWM in microseconds (1000-2000)
    int expectedMin;            // Minimum acceptable feedback PWM
    int expectedMax;            // Maximum acceptable feedback PWM
    int durationMs;             // How long to hold the servo command
    int settleMs;               // Time to wait for physical movement before reading feedback
    bool needsVisualConfirm;    // If true, pause for operator YES/NO after feedback validation
    bool useRcOverride = false; // If true, use RC_CHANNELS_OVERRIDE instead of DO_SET_SERVO
    int throttlePct = -1;       // Throttle percentage (0-100) for motor_test; -1 = use targetPwm
};

/**
 * @brief HardwareTestProfile - Container for a complete servo test sequence
 *
 * Loaded from JSON template files (fixed_wing.json, quadcopter.json, etc.)
 * Contains all TestStep definitions for a specific vehicle type.
 * Also includes throttle handling instructions (manual vs. motor test).
 */
struct HardwareTestProfile {
    QString profileName;        // e.g., "fixed_wing_survey_v2"
    QString vehicleType;        // "fixed_wing", "quadcopter", "hexacopter"
    QString throttleHandling;   // "manual_fixed_wing", "motor_test_copter", "prearm_brief"
    QString throttleNote;       // Safety instructions if applicable
    int launchServo = 5;        // Servo instance for launch mechanism (from JSON)
    int launchPwm = 2000;       // PWM value for launch mechanism (from JSON)
    QVector<TestStep> steps;    // Ordered list of servo test commands

    /**
     * @brief isValid - Check if profile has required fields
     * @return true if steps.size() > 0 and all step PWMs are in valid range
     */
    bool isValid() const;

    /**
     * @brief loadFromJsonFile - Parse a hardware test profile from JSON on disk
     * @param filePath Absolute or relative path to profile JSON
     * @param errorMessage Optional output describing parse failures
     * @return Populated profile, or empty profile on failure
     */
    static HardwareTestProfile loadFromJsonFile(const QString &filePath,
                                              QString *errorMessage = nullptr);
};

// HARDWARETESTPROFILE_H
