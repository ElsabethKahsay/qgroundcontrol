// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: src/HardwareTestProfile.cpp
// Description: Hardware test profile validation and JSON template loading.

#include "HardwareTestProfile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Config.h"

bool HardwareTestProfile::isValid() const
{
    if (steps.isEmpty()) {
        return false;
    }

    for (const TestStep &step : steps) {
        const bool isMotor = step.testType == QStringLiteral("motor");
        if (isMotor && step.throttlePct >= 0) {
            if (step.throttlePct < 0 || step.throttlePct > 100) {
                return false;
            }
        } else {
            if (step.targetPwm < kServoDefaultMinPwm || step.targetPwm > kServoDefaultMaxPwm) {
                return false;
            }
        }
        if (step.expectedMin < kServoDefaultMinPwm || step.expectedMin > kServoDefaultMaxPwm) {
            return false;
        }
        if (step.expectedMax < kServoDefaultMinPwm || step.expectedMax > kServoDefaultMaxPwm) {
            return false;
        }
        if (step.expectedMin > step.expectedMax) {
            return false;
        }
        // For motor_test, validate motorInstance (0-8, where 0=all)
        // For servo_test, validate servoInstance (1-16)
        if (isMotor) {
            if (step.motorInstance < 0 || step.motorInstance > 8) {
                return false;
            }
            // If motorInstance is 0 (all motors), we don't need a specific servoInstance
            if (step.motorInstance > 0 && (step.servoInstance < kServoMinInstance || step.servoInstance > kServoMaxInstance)) {
                return false;
            }
        } else { // servo_test
            if (step.servoInstance < kServoMinInstance || step.servoInstance > kServoMaxInstance) {
                return false;
            }
        }
        if (step.durationMs <= 0 || step.settleMs < 0) {
            return false;
        }
    }

    return true;
}

HardwareTestProfile HardwareTestProfile::loadFromJsonFile(const QString &filePath,
                                                          QString *errorMessage)
{
    auto setError = [&](const QString &msg) {
        if (errorMessage)
            *errorMessage = msg;
        return HardwareTestProfile{};
    };

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return setError(QStringLiteral("Cannot open profile: %1").arg(filePath));
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return setError(QStringLiteral("Invalid JSON in %1").arg(filePath));
    }

    const QJsonObject root = doc.object();
    HardwareTestProfile profile;
    profile.profileName = root.value(QStringLiteral("profile_name")).toString();
    profile.vehicleType = root.value(QStringLiteral("vehicle_type")).toString();
    profile.throttleHandling = root.value(QStringLiteral("throttle_handling")).toString();
    profile.throttleNote = root.value(QStringLiteral("throttle_note")).toString();
    profile.launchServo = root.value(QStringLiteral("launch_servo")).toInt(5);
    profile.launchPwm = root.value(QStringLiteral("launch_pwm")).toInt(2000);

    const QJsonArray stepsArray = root.value(QStringLiteral("steps")).toArray();
    if (stepsArray.isEmpty()) {
        return setError(QStringLiteral("Profile has no steps: %1").arg(filePath));
    }

     for (const QJsonValue &value : stepsArray) {
         const QJsonObject stepObj = value.toObject();
         TestStep step;
         step.name = stepObj.value(QStringLiteral("name")).toString();
         step.testType = stepObj.value(QStringLiteral("test_type")).toString(QStringLiteral("servo"));
         step.servoInstance = stepObj.value(QStringLiteral("servo")).toInt();
         step.motorInstance = stepObj.value(QStringLiteral("motor_instance")).toInt(0); // Default to 0 (all motors)
         step.targetPwm = stepObj.value(QStringLiteral("pwm")).toInt();
         step.expectedMin = stepObj.value(QStringLiteral("min")).toInt();
         step.expectedMax = stepObj.value(QStringLiteral("max")).toInt();
         step.durationMs = stepObj.value(QStringLiteral("duration_ms")).toInt(2000);
         step.settleMs = stepObj.value(QStringLiteral("settle_ms")).toInt(1000);
         step.needsVisualConfirm = stepObj.value(QStringLiteral("visual")).toBool(true);
         step.useRcOverride = stepObj.value(QStringLiteral("use_rc_override")).toBool(false);
         step.throttlePct = stepObj.value(QStringLiteral("throttle_pct")).toInt(-1);
         profile.steps.append(step);
     }

    if (!profile.isValid()) {
        return setError(QStringLiteral("Profile validation failed: %1").arg(filePath));
    }

    return profile;
}
