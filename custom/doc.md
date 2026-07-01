# UAV Preflight Plugin

Preflight checklist and arming-gate system extending QGroundControl via the `QGC_CUSTOM_DIR` mechanism.

---

## Table of Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Build System](#build-system)
  - [Qt 6.8.x Compatibility](#qt-68x-compatibility)
  - [Build Configuration](#build-configuration)
- [Architecture](#architecture)
  - [Plugin Layer](#plugin-layer)
  - [Check Framework](#check-framework)
  - [Arming Gate](#arming-gate)
  - [Telemetry Bridge](#telemetry-bridge)
  - [QML Layer](#qml-layer)
- [Key Fixes & Workarounds](#key-fixes--workarounds)
  - [Vehicle* QML Type Resolution](#vehicle-qml-type-resolution)
  - [Qt 6.10.3 Constexpr Meta-Type](#qt-6103-constexpr-meta-type)
  - [Missing MAVLink Includes](#missing-mavlink-includes)
- [Files of Interest](#files-of-interest)
- [Migration Guide](#migration-guide)
- [SRS Coverage](#srs-coverage)

---

## Overview

This plugin replaces QGC's stock core plugin (`QGCCorePlugin`) with a custom `PreflightPlugin` that adds:

- **91 preflight checks** (67 automatic + 23 manual + 1 specialized) across 9 SRS categories
- **Arming gate** — intercepts `MAV_CMD_COMPONENT_ARM_DISARM` and gates arming on checklist state
- **State machine** — `DISCONNECTED → CONNECTING → PARAM_LOADING → CHECKLIST_IN_PROGRESS → PREFLIGHT_PASS → ARMING_ALLOWED → ARMED`
- **Telemetry bridge** — QML-accessible vehicle telemetry via singleton `VehicleTelemetry`
- **Custom QML UI** — replaces several stock QGC FlyView/Toolbar components

---

## Quick Start

```bash
# Configure with custom build
cmake -S . -B build_custom \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DQGC_CUSTOM_DIR=custom \
    -DQGC_BUILD_TESTING=OFF \
    -DCMAKE_PREFIX_PATH=~/Qt/6.8.3/gcc_64   # Adjust to your Qt path

# Build
cmake --build build_custom --target PreflightQGroundControl -j$(nproc)

# Run
./build_custom/Release/PreflightQGroundControl
```

### Prerequisites

- **Qt 6.8.x+** (developed against 6.10.3; works on 6.8.x with overrides in `custom/cmake/CustomOverrides.cmake`)
- CMake 3.25+
- Ninja or Make
- C++20 compiler (GCC 12+, Clang 16+, MSVC 2022+)

---

## Build System

### Integration via QGC_CUSTOM_DIR

The plugin lives in `custom/` and is loaded by QGC's build system via the `QGC_CUSTOM_DIR` mechanism:

1. **`custom/CMakeLists.txt`** — sets `CUSTOM_SOURCES`, `CUSTOM_INCLUDE_DIRECTORIES`, `CUSTOM_DEFINITIONS`, `CUSTOM_RESOURCES`, `CUSTOM_QT_COMPONENTS`
2. **`custom/cmake/CustomOverrides.cmake`** — overrides app branding (name `PreflightQGroundControl`, org `UAVPreflight`), disables APM/PX4 plugins, force-includes `qt6_meta_fixes.h`, overrides minimum Qt version to 6.5.0
3. **`cmake/CustomOptions.cmake`** (upstream) — sets `QGC_CUSTOM_DIR` default to `custom`

### File Layout

```
custom/
├── cmake/CustomOverrides.cmake   # CMake brand/feature overrides
├── include/qt6_meta_fixes.h      # Qt 6.10.3 constexpr workaround
├── src/                          # C++ source (223 files)
│   ├── PreflightPlugin.h/.cpp    # Core plugin entry point
│   ├── QmlModuleInit.cpp         # QML module registration force-linking
│   ├── APMPluginStub.cpp         # Stub for APM symbols
│   ├── adapters/                 # QGC→Telemetry bridge
│   ├── controllers/              # Hardware test controllers
│   ├── core/                     # Check framework + 43 check implementations
│   ├── mission/                  # Waypoint math helpers
│   ├── ui/                       # QML model adapters
│   └── utils/                    # Utilities (weather, DB, export)
├── qml/                          # QML UI (50 files)
│   ├── cpts/                     # Reusable check card components
│   ├── pages/                    # Full-screen pages
│   ├── singletons/               # QML singletons (VehicleTelemetry, Colors, Config)
│   └── *.qml                     # Toolbar/FlyView overlays
├── res/                          # Icons, images
├── tests/                        # Smoke tests
├── custom.qrc                    # Qt resource bindings for custom assets
├── SRS_AUDIT.md                  # Requirements traceability
└── doc.md                        # This file
```

### Qt 6.8.x Compatibility

The plugin supports **Qt 6.8.x through 6.10.x**. The following upstream changes are required:

| Issue | Fix | Location |
|---|---|---|
| Minimum Qt version check enforces 6.10.0 | Override to `6.5.0` via `set(QGC_QT_MINIMUM_VERSION ... FORCE)` | `custom/cmake/CustomOverrides.cmake` |
| `Qt6LocationPrivate` REQUIRED component | Moved to `OPTIONAL_COMPONENTS` (not shipped with open-source Qt) | `CMakeLists.txt` (upstream) |
| Constexpr `QMetaType::fromType<T*>()` needs complete type | `is_complete<Vehicle>::value = true_type` specialization | `include/qt6_meta_fixes.h` (force-included via `-include`) |
| `Q_DECLARE_OPAQUE_POINTER` causes QML type rejection | Avoid it — use `is_complete` specialization instead | See [Vehicle* fix](#vehicle-qml-type-resolution) |

### Build Configuration

Key CMake options:

| Option | Default | Notes |
|---|---|---|
| `QGC_CUSTOM_DIR` | `custom` | Path to custom plugin root |
| `QGC_BUILD_TESTING` | ON (Debug), OFF (Release) | Enables unit tests |
| `QGC_DISABLE_APM_PLUGIN` | ON | ArduPilot disabled |
| `QGC_DISABLE_PX4_PLUGIN_FACTORY` | ON | PX4 factory disabled |
| `QGC_ENABLE_GST_VIDEOSTREAMING` | OFF | GStreamer disabled (avoids QtLocation issues) |

---

## Architecture

### Plugin Layer

```
QGCCorePlugin  ←  PreflightPlugin  ←  PreflightOptions (QGCOptions)
                       │
                       ├── PreflightManager (91 checks)
                       ├── PreflightStateMachine (7 states)
                       ├── ArmingGate (MAV_CMD interception)
                       └── TelemetryBridge → VehicleTelemetry (QML singleton)
```

`PreflightPlugin` is injected into QGC via `CUSTOMHEADER`/`CUSTOMCLASS` preprocessor defines. It replaces the entire core plugin, including options, branding, and FlyView behavior.

### Check Framework

Every check inherits from `AbstractCheck`:

```cpp
class BatteryVoltageCheck : public AbstractCheck {
    Q_OBJECT
public:
    CheckResult evaluate(Vehicle* vehicle) override;
    // Auto-detected as AutoCheck or ManualCheck based on type
};
```

**Check types:**
- **AutoCheck** — evaluates automatically after parameter load
- **ManualCheck** — requires user confirmation
- **ActionCheck** — initiates an action (e.g., motor test)

**Registration** happens in `PreflightManager` via `registerCheck<T>()` template calls in `PreflightManager.cpp`.

### Arming Gate

`ArmingGate` attaches to `LinkInterface::incomingMessage` and intercepts `MAV_CMD_COMPONENT_ARM_DISARM`. Depending on mode:
- **PASSIVE** — logs attempt, passes through
- **ACTIVE** — blocks arming unless checklist state permits
- **HYBRID** — blocks arming for guided arm, passes for RC arm

### Telemetry Bridge

```
QGC Vehicle (MAVLink) → QgcVehicleAdapter → TelemetryBridge → VehicleTelemetry singleton (QML)
```

Extracts key telemetry fields (battery, GPS, attitude, ESC temps, etc.) and exposes them as QML `property var` via the `VehicleTelemetry` singleton.

### QML Layer

Custom QML replaces several stock QGC components:

| Stock QGC File | Custom Replacement |
|---|---|
| `MainStatusIndicator.qml` | `custom/qml/MainStatusIndicator.qml` |
| `MainToolBarIndicators.qml` | `custom/qml/MainToolBarIndicators.qml` |
| `ArmedIndicator.qml` | `custom/qml/ArmedIndicator.qml` |
| `FlyViewCustomLayer.qml` | `custom/qml/FlyViewCustomLayer.qml` |
| `FlyViewPreFlightChecklistPopup.qml` | `custom/qml/FlyViewPreFlightChecklistPopup.qml` |

Exclusion rules in `qgroundcontrol.exclusion` prevent stock versions from being compiled into the resource bundle.

---

## Key Fixes & Workarounds

### Vehicle* QML Type Resolution

**Problem:** Qt 6.10.3's QML engine rejects `Vehicle*` as a property type for assignments (error: `Invalid property assignment: unsupported type "Vehicle*"`).

**Root cause:** When `Vehicle` is only forward-declared (via `class Vehicle;`) in files that use `Q_PROPERTY(Vehicle * ...)`, the Qt meta-type system at compile time does not have enough information to properly register the pointer type. The pointer becomes opaque (or unresolved) in the QML type system, causing runtime type-check failures even when the actual type registration (via `qml_register_types_QGC()`) has completed.

**Fix:** In `src/QmlControls/FactValueGrid.h`, replace the forward declaration with a full include:

```cpp
// Before:
class Vehicle;

// After:
#include "Vehicle/Vehicle.h"
```

This ensures `Vehicle` is fully defined when the `Q_PROPERTY(Vehicle * specificVehicleForCard ...)` macro is processed by MOC, allowing Qt to generate correct meta-type information for the pointer type.

**Why this works:** When MOC processes `Q_PROPERTY(Vehicle * ...)`, it needs to know that `Vehicle` derives from `QObject` so that the pointer is registered as `PointerToQObject` rather than as an opaque pointer. A forward declaration (`class Vehicle;`) alone does not provide this information. Including the full header allows the type trait `is_base_of<QObject, Vehicle>` to be evaluated, which sets the correct meta-type flags.

**Additional context:** The `qt6_meta_fixes.h` workaround (specializing `is_complete<Vehicle>`) handles a related but distinct issue — the constexpr evaluation of `QMetaType::fromType<Vehicle*>()` in Qt 6.10.3. Both fixes together ensure clean compilation and correct QML type resolution.

Only one QML property assignment (`specificVehicleForCard: null` in `FlyViewBottomRightRowLayout.qml`) was affected by this issue. After the fix, the assignment works correctly without modification.

### Qt 6.10.3 Constexpr Meta-Type

**Problem:** Qt 6.10.3 requires `QMetaType::fromType<T*>()` to be constexpr-evaluable, which in turn requires `T` to be either fully defined or declared opaque at the point of evaluation. Forward-declared types used in `Q_OBJECT` signals/slots cause compile errors.

**Fix:** Specialize `QtPrivate::is_complete` for the affected types in `include/qt6_meta_fixes.h`:

```cpp
#include <QtCore/QMetaType>

class Vehicle;
class VehicleComponent;

namespace QtPrivate {
template<> struct is_complete<Vehicle, void> : std::true_type {};
template<> struct is_complete<VehicleComponent, void> : std::true_type {};
}
```

This header is force-included into every translation unit via `-include` (set in both `custom/CMakeLists.txt` and `custom/cmake/CustomOverrides.cmake`).

**Why NOT `Q_DECLARE_OPAQUE_POINTER`:** While `Q_DECLARE_OPAQUE_POINTER(Vehicle*)` also resolves the constexpr issue, it registers `Vehicle*` as an opaque pointer type, which breaks QML's ability to recognize it as a QObject-derived pointer. This causes the QML engine to reject all property assignments to `Vehicle*` properties at runtime. The `is_complete` specialization avoids this by providing the compiler with the needed completeness information without altering the pointer's type semantics.

### Missing MAVLink Includes

`src/Camera/MavlinkCameraControlInterface.h` uses MAVLink types (e.g., `mavlink_message_t`) without including the MAVLink header. In Qt 6 builds with unity/build optimizations, this can cause "unknown type" errors. Fix: added `#include "MAVLinkLib.h"`.

---

## Files of Interest

| File | Purpose |
|---|---|
| `custom/src/PreflightPlugin.cpp` | Core plugin; QML module registration force-linking; constructor init |
| `custom/src/PreflightPlugin.h` | Plugin header; CUSTOMCLASS point |
| `custom/src/core/PreflightManager.cpp` | Registers all 91 checks, runs evaluation |
| `custom/src/core/PreflightStateMachine.cpp` | 7-state state machine |
| `custom/src/core/AbstractCheck.h` | Base class for all checks |
| `custom/src/core/ArmingGate.cpp` | MAV_CMD arm/disarm interception |
| `custom/src/adapters/TelemetryBridge.cpp` | Vehicle → QML telemetry bridge |
| `custom/include/qt6_meta_fixes.h` | Qt 6.10.3 constexpr workaround |
| `custom/cmake/CustomOverrides.cmake` | Build configuration/branding overrides |
| `custom/custom.qrc` | Qt resource bindings |
| `custom/SRS_AUDIT.md` | Requirements traceability (109 reqs) |

---

## Migration Guide

### To extract this plugin from the QGC monorepo

1. **Copy the `custom/` directory** to your standalone repo
2. **Add QGC as a submodule** or external dependency:
   ```bash
   git submodule add https://github.com/mavlink/qgroundcontrol.git third_party/qgroundcontrol
   ```
3. **Create a top-level CMakeLists.txt** that:
   - Calls `add_subdirectory(third_party/qgroundcontrol)`
   - Sets `QGC_CUSTOM_DIR` to the custom directory path
   - Applies the same CMake options as above
4. **Update `cmake/CustomOverrides.cmake`** — the `${QGC_CUSTOM_DIR}` variable should resolve to the correct path in your repo layout
5. **Update `custom.qrc` paths** — verify all resource paths are relative to the new repo root
6. **Keep `include/qt6_meta_fixes.h`** — required for Qt 6.10.3+ compatibility
7. **Remove unused files** if desired:
   - `CustomBuild.cmake` (dead file; not referenced anywhere)
   - `custom_deploy.pri` (qmake deploy helper; only needed if using qmake)
   - `updateqrc.py`, `updateinstrumentqrc.py` (regeneration scripts for QRC files)

### QRC resource paths

The custom plugin's QRC files reference upstream QGC resources via relative paths (`../resources/`, `../src/`, `../custom/`). After migration, these must be updated:

| QRC File | References | Migrated Approach |
|---|---|---|
| `custom.qrc` | `custom/qml/*`, `custom/res/*` | Keep as-is (all paths within `custom/`) |
| `qgroundcontrol.qrc` | `../src/*`, `../resources/*` | Add QGC source as resource or symlink |
| `qgcresources.qrc` | `../resources/*` | Same as above |
| `InstrumentValueIcons.qrc` | `../resources/InstrumentValueIcons/*` | Same as above |

The `qgroundcontrol.exclusion` and `qgcresources.exclusion` files control which upstream files are excluded from the custom QRC bundles.

---

## SRS Coverage

109 requirements across 9 categories, **94.3% coverage** (100/106 checkable). See `SRS_AUDIT.md` for the full traceability matrix.

| Category | Code | Reqs | Covered | Missing |
|---|---|---|---|---|
| System | SYS | 15 | 14 | 1 (ParameterWatchlist) |
| Propulsion | PROP | 11 | 11 | 0 |
| Power | POW | 10 | 8 | 1 (rail voltage), 1 partial |
| Navigation | NAV | 13 | 13 | 0 |
| Communications | COM | 14 | 12 | 2 partial |
| Airframe | AIR | 10 | 10 | 0 |
| Safety | SAF | 17 | 15 | 1 (mission energy), 1 partial |
| Environment | ENV | 8 | 7 | 1 partial |
| Arming Gate | ARM | 11 | 11 | 0 |

---

*Generated for Qt 6.10.3 / QGC master (a8a6cc4)*
