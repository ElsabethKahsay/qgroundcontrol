# Skywin Aeronautical Custom GCS — Technical Reference

**Version:** 1.0  
**Date:** July 2026  
**Classification:** Internal Engineering Reference  
**Base Platform:** QGroundControl v5.0 (Qt 6.8.3)  
**License:** GPL-3.0-or-later

---

## Table of Contents

1. [System Overview](#chapter-1--system-overview)
2. [Build System and Toolchain](#chapter-2--build-system-and-toolchain)
3. [Architecture](#chapter-3--architecture)
4. [Preflight Checklist System](#chapter-4--preflight-checklist-system)
5. [Arming Gate](#chapter-5--arming-gate)
6. [Motor Test System](#chapter-6--motor-test-system)
7. [Weather Integration](#chapter-7--weather-integration)
8. [Vehicle Auto-Detection](#chapter-8--vehicle-auto-detection)
9. [Database and Audit Trail](#chapter-9--database-and-audit-trail)
10. [Telemetry Bridge](#chapter-10--telemetry-bridge)
11. [UI Architecture](#chapter-11--ui-architecture)
12. [Known Issues and Technical Debt](#chapter-12--known-issues-and-technical-debt)
13. [Deployment on Jetson Orin Nano](#chapter-13--deployment-on-jetson-orin-nano)

---

# Chapter 1 — System Overview

## 1.1 What the system is

The Skywin Aeronautical Ground Control Station (Skywin GCS) is a custom-built ground control station for UAV operations built on top of QGroundControl v5.0. It is implemented as a QGC plugin — a single directory (`custom/`) that plugs into the upstream QGC build system via the `QGC_CUSTOM_BUILD` CMake mechanism. The plugin name is `UAVPreflight`, the application binary is named `PreflightQGroundControl`.

At its core, Skywin GCS solves a specific operational problem: **stock QGroundControl has no preflight checklist system, no arming gate, no motor test integration, no weather awareness, and no vehicle fleet management**. A UAV operator pressing "Arm" in stock QGC can immediately arm and fly regardless of whether preconditions are met. Skywin GCS intercepts this workflow and enforces a structured, auditable preflight process before arming is permitted.

The system provides:

- **91 preflight checks** across 9 categories (Propulsion, Power, Navigation, Communication, Airframe, Safety, Environment, Arming Gate, and an implicit manual-confirm tier)
- **A configurable arming gate** that intercepts MAVLink `MAV_CMD_COMPONENT_ARM_DISARM` (command 400) and blocks arming when mandatory checks fail
- **A motor test subsystem** that runs per-motor spin tests via `MAV_CMD_DO_MOTOR_TEST` and verifies response through `SERVO_OUTPUT_RAW` feedback
- **Weather integration** pulling METAR, TAF, and Open-Meteo data, with 7 weather-specific preflight checks
- **Vehicle fingerprinting** using SHA-256 hashes of hardware UID, autopilot type, and board version, enabling per-vehicle check configuration persistence
- **SQLite audit trail** recording every check result, motor test, flight session, battery cycle, and compliance event
- **A split video/map FlyView** with a 40/60 panel layout, compass overlay, telemetry strip, and detection bounding box support

The `custom/` directory is self-contained: all C++ source files, QML UI components, resource files, CMake build definitions, test mocks, and deployment scripts live under `custom/src/`, `custom/qml/`, `custom/tests/`, and `custom/deploy/`. Zero modifications to upstream QGC source files are required beyond three line-level patches documented in Chapter 2.

**Key file:** `custom/src/PreflightPlugin.h:26` — the plugin entry point class.

## 1.2 Why QGroundControl was chosen as the base

QGroundControl provides a complete, battle-tested UAV ground station framework out of the box:

- **MAVLink communication**: Connection management, parameter read/write, mission upload/download, command acknowledgment, and heartbeat monitoring are all implemented in the upstream `Vehicle` class
- **Multi-vehicle support**: `MultiVehicleManager` tracks multiple concurrent vehicle connections, each with its own `Vehicle` object
- **Flight display**: A full fly view with video pipeline (GStreamer), flight map, guided actions controller, and value sliders
- **Mission planning**: Waypoint editor, geofence, rally points, survey patterns
- **Cross-platform**: Linux, macOS, Windows, Android builds from the same source tree
- **QML-based UI**: All screens are declarative QML, making visual customization straightforward
- **Plugin system**: `QGCCorePlugin` allows subclasses to override pages, toolbar indicators, palette colors, and settings — without forking upstream

The **`QGC_CUSTOM_BUILD` mechanism** is the key integration point. When enabled via CMake (`-DQGC_CUSTOM_BUILD=ON`), QGC's top-level `CMakeLists.txt` executes `add_subdirectory(custom)` at line ~236. The custom `CMakeLists.txt` at `custom/CMakeLists.txt:1-340` then:

1. Sets `CUSTOM_SOURCES` — all C++ files to compile
2. Sets `CUSTOM_INCLUDE_DIRECTORIES` — include paths for headers
3. Sets `CUSTOM_DEFINITIONS` — defines `QGC_CUSTOM_BUILD`, `CUSTOMHEADER="PreflightPlugin.h"`, and `CUSTOMCLASS=PreflightPlugin`
4. Sets `CUSTOM_QT_COMPONENTS` — additional Qt modules (Core, Sql)
5. Sets `CUSTOM_LIBRARIES` — link targets (Qt6::Sql)

QGC's build system reads these variables and links the custom sources into the final binary. The custom plugin header (`PreflightPlugin.h`) is included by QGC's plugin loader, which instantiates `PreflightPlugin` as the active `QGCCorePlugin` subclass.

The **`custom/` directory isolation model** provides:

- **Maintainability**: All Skywin-specific code lives in one directory. Upstream QGC can be updated by rebasing, and only the three line-level patches need re-application
- **Testability**: The `custom/tests/` directory contains mock objects (`MockTelemetryBridge`, `MockVehicle`, `MockParameterManager`) that can test check logic without a real vehicle connection
- **Portability**: The `custom/` directory can be copied to any QGC v5.0 checkout and built immediately

**Key files:** `custom/CMakeLists.txt:54-279` (source listing), `custom/CMakeLists.txt:303-308` (build definitions).

## 1.3 System goals and design principles

The system was designed around five core principles, each reflected in specific codebase decisions:

### Safety-First Arming

**Principle**: No vehicle should arm unless all mandatory preflight conditions are satisfied. The system must prevent accidental arming even if the operator presses the Arm button.

**Implementation**: The `ArmingGate` class (`custom/src/core/ArmingGate.h:13-135`) intercepts `MAV_CMD_COMPONENT_ARM_DISARM` (command 400) before it reaches the vehicle. In Active mode, any mandatory check failure suppresses the command entirely — the vehicle never receives it. The gate operates at the `interceptCommandLong()` level (`ArmingGate.cpp:69-134`), which is called by QGC's `GuidedActionsController` before forwarding commands to the link.

### Vehicle-Adaptive UI

**Principle**: The preflight checklist should show only relevant checks for the connected vehicle type. A fixed-wing aircraft does not need multirotor-specific checks.

**Implementation**: The `PreflightManager::createPhase1Checks()` method (`PreflightManager.cpp:688-912`) registers all 91 checks at startup. Vehicle-adaptive visibility is achieved through `FRAME_CLASS` and `CA_ROTOR_CNT` parameter detection — checks like `MotorCountCheck` read the motor count from telemetry and adjust their visibility. The `ChecklistItemModel` exposes a `category` role that the QML `PreflightChecklistView.qml:367-829` uses to group and filter checks via `PreflightChecklistFilterModel` proxy models (`CatModel0` through `CatModel7`).

### Audit Trail

**Principle**: Every preflight decision, check result, override, and motor test must be recorded for post-flight review and regulatory compliance.

**Implementation**: The `DatabaseManager` singleton (`custom/src/utils/DatabaseManager.h:32-173`) maintains 10+ SQLite tables. Check results are written via `saveCheckResult()`, motor tests via `logMotorTestResult()`, flight sessions via `startFlightSession()`/`endFlightSession()`, and compliance logs via `saveComplianceLog()`. Every `AbstractCheck` subclass that calls `setStatus()` triggers `statusChanged` signals that propagate to the database layer through `PreflightManager::connectCheckSignals()` (`PreflightManager.cpp:914-936`).

### Weather Awareness

**Principle**: External weather conditions that no onboard sensor can detect (METAR visibility, ceiling, TAF deterioration) must be factored into the preflight decision.

**Implementation**: The `WeatherProvider` class (`custom/src/utils/WeatherProvider.h:11-118`) fetches METAR from `aviationweather.gov`, TAF from the same source, and forecast data from Open-Meteo. Seven weather-specific checks (`WeatherWindCheck`, `WeatherWindGustCheck`, `MetarVisibilityCheck`, `MetarCeilingCheck`, `MetarPrecipitationCheck`, `MetarTemperatureCheck`, `TafDeteriorationCheck`) read from the provider's cached data. The `PreflightPlugin` starts a periodic weather refresh timer (`_weatherRefreshTimer` at `PreflightPlugin.cpp:432-437`).

### Maintainable Plugin Architecture

**Principle**: The custom system should be maintainable as a standalone module that can be updated independently of upstream QGC.

**Implementation**: The `custom/` directory contains 100% of Skywin-specific code. The only upstream modifications are three line-level patches: (1) moving `LocationPrivate` from `COMPONENTS` to `OPTIONAL_COMPONENTS` in the root CMakeLists.txt, (2) adding `#include "MAVLinkLib.h"` to `MavlinkCameraControlInterface.h`, and (3) replacing a forward declaration with an include in `FactValueGrid.h`. These are documented in `custom/INTEGRATION_GUIDE.md`.

---

# Chapter 2 — Build System and Toolchain

## 2.1 CMake and Qt 6.8.3

**CMake** is the build system generator used by QGroundControl. It reads `CMakeLists.txt` files at each directory level, resolves dependencies, generates build rules (Makefiles, Ninja files, or IDE projects), and manages the compilation pipeline. QGC uses CMake because it supports cross-platform builds (Linux, macOS, Windows, Android) from a single build definition, and integrates natively with Qt's own CMake-based packaging system (Qt6 package discovery via `find_package(Qt6 ...)`).

**Qt** is not just a UI framework — it is a complete application framework providing:

- **Event loop**: `QApplication` / `QGuiApplication` manages the main event loop, dispatching events from sockets, timers, and user input to the appropriate handlers
- **Signals and slots**: The core inter-object communication mechanism. When an object emits a signal, all connected slots are invoked. This is the foundation of the entire data flow in Skywin GCS (MAVLink → TelemetryBridge → Q_PROPERTY → QML binding)
- **Meta-object system**: The Meta-Object Compiler (MOC) processes `Q_OBJECT` macros at build time, generating reflection metadata that enables runtime property access, signal/slot connection, and QML integration
- **QML engine**: Declarative UI language that compiles to JavaScript, with native C++ integration via `Q_PROPERTY`, `QML_ELEMENT`, and `QML_SINGLETON`
- **Network layer**: `QNetworkAccessManager`, `QNetworkReply`, `QNetworkDiskCache` — used by `WeatherProvider` for HTTP requests
- **SQL layer**: `QSqlDatabase`, `QSqlQuery` — used by `DatabaseManager` for SQLite operations
- **Threading**: `QTimer`, `QThread`, `Qt::QueuedConnection` — used throughout for async operations

**Qt 6.8.3 specifically** is required because:

1. QGroundControl v5.0 targets Qt 6.8.x as its supported version
2. The `qt6_meta_fixes.h` header (`custom/include/qt6_meta_fixes.h`) specializes `QtPrivate::is_complete<Vehicle>::value` to work around Qt 6.10.3's constexpr evaluator requirements for forward-declared types — this fix is harmless on 6.8.x but necessary for forward compatibility
3. Qt 6.9 introduced breaking changes to the QML engine and module system that are not yet compatible with QGC v5.0
4. Qt 6.7 lacks certain network and positioning APIs used by the custom plugin

Using the wrong Qt version will result in compilation errors (missing headers, changed APIs) or runtime crashes (QML type registration failures, meta-object incompatibilities).

**Key file:** `custom/CMakeLists.txt:310-318` — Qt component requirements (`Core`, `Sql`).

## 2.2 How the custom/ build integrates with QGC

The integration follows a well-defined sequence:

### Step 1: CMake flag

```bash
cmake -B build_custom -DQGC_CUSTOM_BUILD=ON -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.3/gcc_64
```

When `QGC_CUSTOM_BUILD` is ON, QGC's top-level `CMakeLists.txt` includes:

```cmake
add_subdirectory(custom)
```

### Step 2: custom/CMakeLists.txt processes sources

The custom CMakeLists.txt (`custom/CMakeLists.txt:54-279`) explicitly lists every source file:

```cmake
set(CUSTOM_SOURCE_REL
    src/PreflightPlugin.cpp
    src/PreflightPlugin.h
    src/adapters/TelemetryBridge.cpp
    src/adapters/TelemetryBridge.h
    src/core/AbstractCheck.cpp
    src/core/AbstractCheck.h
    # ... 100+ files
)
```

It then iterates and prepends the current source directory:

```cmake
foreach(_src IN LISTS CUSTOM_SOURCE_REL)
    list(APPEND CUSTOM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
endforeach()
```

### Step 3: Build definitions are exported

```cmake
set(CUSTOM_SOURCES ${CUSTOM_SOURCES} CACHE INTERNAL "" FORCE)
set(CUSTOM_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters
    ${CMAKE_CURRENT_SOURCE_DIR}/src/controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mission
    ${CMAKE_CURRENT_SOURCE_DIR}/src/detection
    CACHE INTERNAL "" FORCE)
set(CUSTOM_DEFINITIONS
    QGC_CUSTOM_BUILD
    CUSTOMHEADER="PreflightPlugin.h"
    CUSTOMCLASS=PreflightPlugin
    CACHE INTERNAL "" FORCE)
```

### Step 4: QGC links everything together

QGC's build system reads `CUSTOM_SOURCES`, `CUSTOM_INCLUDE_DIRECTORIES`, `CUSTOM_DEFINITIONS`, `CUSTOM_QT_COMPONENTS`, and `CUSTOM_LIBRARIES` and compiles them into the final binary. The `CUSTOMCLASS=PreflightPlugin` definition tells QGC's plugin loader to instantiate `PreflightPlugin` as the active `QGCCorePlugin`.

### What `qt_add_qml_module` does

The custom CMakeLists.txt does not use `qt_add_qml_module` directly — QML files are loaded at runtime via `qrc:/` resource paths. However, the custom QML module for `cpts/` is registered via the `qmldir` file at `custom/qml/cpts/qmldir` and added to the import path at `PreflightPlugin.cpp:216`:

```cpp
qmlEngine->addImportPath(QStringLiteral("qrc:/qml/cpts"));
```

### What the QRC resource system does

Qt Resource System (QRC) compiles binary data (images, QML files, fonts) into the application binary at build time. The `custom.qrc` file at `custom/custom.qrc` lists all custom resources:

```xml
<qresource prefix="/">
    <file>qml/cpts/PreflightChecklistView.qml</file>
    <file>qml/singletons/Colors.qml</file>
    <!-- ... -->
</qresource>
```

Two Python scripts (`custom/updateqrc.py` and `custom/updateinstrumentqrc.py`) regenerate the filtered upstream QRC files (`custom/qgcresources.qrc` and `custom/qgroundcontrol.qrc`) from the original QGC resources, applying exclusions via `.exclusion` files. The `qgroundcontrol.exclusion` file excludes upstream checklist files that conflict with the custom implementation.

**Key files:** `custom/CMakeLists.txt:39-48` (QRC and import paths), `custom/custom.qrc`, `custom/qgroundcontrol.exclusion`.

## 2.3 Qt's meta-object system

Qt's meta-object system is the foundation of QML/C++ integration. It processes C++ classes at build time to generate reflection metadata.

### Q_OBJECT macro

Every class that uses signals, slots, or Q_PROPERTY must declare `Q_OBJECT` in its header:

```cpp
class AbstractCheck : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString checkId READ id CONSTANT)
    Q_PROPERTY(int status READ statusInt NOTIFY statusChanged)
    // ...
};
```

The MOC (Meta-Object Compiler) generates a `moc_AbstractCheck.cpp` file containing:
- A `staticMetaObject` with property descriptions, signal signatures, and slot signatures
- `indexOfProperty()`, `indexOfSignal()`, `indexOfSlot()` lookup methods
- Property read/write functions

### Q_PROPERTY macro

`Q_PROPERTY` declares a property that is accessible from QML and the meta-object system:

```cpp
Q_PROPERTY(double batteryVoltage READ batteryVoltage NOTIFY batteryVoltageChanged)
```

This creates:
- A readable property `batteryVoltage` accessible from QML as `TelemetryProvider.batteryVoltage`
- A `NOTIFY` signal `batteryVoltageChanged` that QML bindings listen to for updates
- A meta-object entry that `AbstractCheck::getTelemetryDouble()` uses to read the value

**Critical rule**: A C++ property that is not declared with `Q_PROPERTY` is invisible to QML. The `TelemetryBridge` class has 100+ Q_PROPERTY declarations precisely because QML needs to read them all.

### Q_INVOKABLE macro

`Q_INVOKABLE` makes a C++ method callable from QML:

```cpp
Q_INVOKABLE bool confirm(const QString &reason = {});
Q_INVOKABLE void evaluate() = 0;
```

In QML: `checkObject.confirm("Operator confirmed")`

### Signals and slots

Signals are emitted when state changes:

```cpp
signals:
    void statusChanged(const QString &checkId, int newStatus);
    void batteryVoltageChanged();
```

Slots are connected to signals:

```cpp
connect(_telemetryBridge, &TelemetryBridge::batteryVoltageChanged,
        this, [this]() { /* handler */ });
```

The connection types (direct, queued, auto) determine thread-safety behavior — see Section 3.4.

**Key files:** `custom/src/core/AbstractCheck.h:59-89` (Q_OBJECT and Q_PROPERTY declarations), `custom/src/adapters/TelemetryBridge.h:19-211` (100+ Q_PROPERTY declarations).

## 2.4 QML engine and C++ bridge

QML and C++ communicate through three mechanisms:

### QML_ELEMENT / QML_SINGLETON

Modern Qt6 uses declarative type registration:

```cpp
QML_ELEMENT
QML_SINGLETON
class PreflightSettingsManager : public QObject { ... };
```

This makes the type available in QML as `import com.uav.preflight 1.0`.

### qmlRegisterSingletonType

The legacy (but still used) registration mechanism:

```cpp
qmlRegisterSingletonType(
    QUrl(QStringLiteral("qrc:/qml/singletons/Colors.qml")),
    "com.uav.preflight", 1, 0, "Colors");
```

This makes `Colors` a QML singleton — one instance shared across all QML files.

### setContextProperty

The most commonly used mechanism in this codebase:

```cpp
qmlEngine->rootContext()->setContextProperty(
    QStringLiteral("WeatherProvider"), _weatherProvider);
qmlEngine->rootContext()->setContextProperty(
    QStringLiteral("TelemetryProvider"), _telemetryBridge);
qmlEngine->rootContext()->setContextProperty(
    QStringLiteral("PreflightManager"), _preflightManager);
```

Context properties are globally accessible from any QML file without imports.

### Qt::QueuedConnection for thread safety

When a signal is emitted from one thread and the slot runs on another, `Qt::QueuedConnection` ensures the slot is invoked via the event loop of the receiving thread's event loop, not inline:

```cpp
connect(m_telemetry, &TelemetryBridge::batteryVoltageChanged,
        this, [this]() { /* runs on main thread */ },
        Qt::QueuedConnection);
```

This is critical because MAVLink message handlers may fire on a network thread, while QML UI updates must happen on the main thread. Without `QueuedConnection`, direct cross-thread signal emission can cause crashes.

**Key files:** `custom/src/PreflightPlugin.cpp:212-270` (QML registration), `custom/qml/singletons/VehicleTelemetry.qml:63-477` (C++ → QML bridge).

## 2.5 Key Qt libraries used in this project

| Library | Module | Usage |
|---------|--------|-------|
| Qt Core | `QtCore` | Event loop (`QApplication`), timers (`QTimer`), signals/slots, JSON (`QJsonDocument`), threading, file I/O |
| Qt Quick / QML | `QtQuick` | Declarative UI engine, all QML components |
| Qt Network | `QtNetwork` | `QNetworkAccessManager` for HTTP (WeatherProvider), `QNetworkDiskCache` for response caching |
| Qt WebSockets | `QtWebSockets` | DetectionBridge WebSocket connection to inference service (designed but not yet implemented) |
| Qt SQL | `QtSql` | `QSqlDatabase`, `QSqlQuery` for SQLite — DatabaseManager |
| Qt Positioning | `QtPositioning` | `QGeoCoordinate` types for GPS position handling |

**Key file:** `custom/CMakeLists.txt:310-318` — `CUSTOM_QT_COMPONENTS` lists `Core` and `Sql`.

## 2.6 The custom.qrc resource manifest

The `custom.qrc` file at `custom/custom.qrc` lists every QML file, image, and font compiled into the binary. When the build runs, Qt's `rcc` tool reads this XML file and generates a C++ source file (`qrc_custom.cpp`) containing byte arrays for each listed resource. This generated file is compiled and linked into the final binary, making all resources accessible via the `qrc:/` prefix.

The manifest is structured in two tiers:

```xml
<qresource prefix="/">
    <!-- Custom QML components (cpts/) -->
    <file>qml/cpts/PreflightChecklistView.qml</file>
    <file>qml/cpts/MotorCheckPanel.qml</file>
    <file>qml/cpts/qmldir</file>

    <!-- Singletons -->
    <file>qml/singletons/Colors.qml</file>
    <file>qml/singletons/Config.qml</file>
    <file>qml/singletons/VehicleTelemetry.qml</file>

    <!-- Pages -->
    <file>qml/pages/MaintenancePage.qml</file>
    <file>qml/analyze/VehiclesPage.qml</file>

    <!-- FlyView layer -->
    <file>qml/FlyView.qml</file>
    <file>qml/FlyViewCustomLayer.qml</file>

    <!-- Brand images -->
    <file>images/skywin_logo.svg</file>
</qresource>
```

Two Python scripts manage the filtered upstream resources:

1. **`custom/updateqrc.py`** — Reads QGC's `qgcresources.qrc`, applies `.exclusion` filters, and writes a filtered version to `custom/qgcresources.qrc`. This removes upstream checklist files that conflict with the custom implementation.

2. **`custom/updateinstrumentqrc.py`** — Same pattern for `qgroundcontrol.qrc`, filtering instrument panel resources.

The `.exclusion` files contain glob patterns of resource paths to remove:

```
# custom/qgroundcontrol.exclusion
qml/QGroundControl/Controls/Checklist*
qml/QGroundControl/Controls/PreFlightCheck*
```

**Key files:** `custom/custom.qrc`, `custom/updateqrc.py`, `custom/qgroundcontrol.exclusion`.

## 2.7 Build configurations and optimization flags

### CMAKE_BUILD_TYPE

| Configuration | Flags | Use Case |
|--------------|-------|----------|
| `Debug` | `-O0 -g3 -fno-omit-frame-pointer` | Development — full debugging symbols, no optimization |
| `RelWithDebInfo` | `-O2 -g -DNDEBUG` | Testing — optimized with debug symbols |
| `Release` | `-O3 -DNDEBUG` | Production — maximum optimization, no debug symbols |
| `MinSizeRel` | `-Os -DNDEBUG` | Embedded — minimum binary size |

For Jetson deployment, `Release` is used. The Jetson Orin Nano's ARM Cortex-A78AE cores benefit from `-O3` for the inference service's compute-heavy loops, while the GCS UI benefits from `-O2` (default for `Release` in Qt's CMake).

### Qt-specific build flags

```bash
# Skip QtWebEngine (not needed for GCS, saves ~2 hours build time on Jetson)
-skip qtwebengine

# Skip QtMultimedia (camera handled by GStreamer directly)
-skip qtmultimedia

# Skip Qt3D (not used)
-skip qt3d

# Enable only required modules
-qt-feature-zlib
-qt-feature-system-zlib
```

### Parallel build

The `-j$(nproc)` flag parallelizes compilation across all CPU cores. On the Jetson Orin Nano (8 cores), this reduces QGC build time from ~3 hours to ~45-90 minutes. On a 16-core dev box, expect ~20-30 minutes.

## 2.8 Third-party and platform dependencies

| Dependency | Purpose | Integration Point |
|-----------|---------|-------------------|
| MAVLink C library | MAVLink protocol encoding/decoding | Git submodule at `libs/mavlink/` — used by `TelemetryBridge::_handleMavlinkMessage()` |
| GStreamer | Video pipeline for camera feeds | `QGC_ENABLE_GST_VIDEOSTREAMING` CMake flag — controls `FlyViewVideo` component |
| NVIDIA JetPack 6.2.2 | Jetson Orin Nano SDK — CUDA, TensorRT, system libraries | Inference service runs on Jetson; `inference_service.py` uses `jetson-inference` and `websockets` |
| Qt 6.8.3 | Application framework | Must be built from source on aarch64 (no online installer package for Jetson) |

---

# Chapter 3 — Architecture

## 3.1 How the plugin system works

### QGCCorePlugin

QGC's plugin system is built around `QGCCorePlugin`, a base class that allows subclasses to:

- Override which pages appear in the Analyze Tools sidebar (`analyzePages()`)
- Override toolbar indicators (`toolBarIndicators()`)
- Override QGC palette colors (`paletteOverride()`)
- Adjust settings metadata (`adjustSettingMetaData()`)
- Intercept MAVLink messages (`mavlinkMessage()`)
- Provide custom brand images (`brandImageIndoor()`, `brandImageOutdoor()`)

`PreflightPlugin` inherits from `QGCCorePlugin` and overrides all of these:

```cpp
class PreflightPlugin : public QGCCorePlugin {
    Q_OBJECT
    Q_PROPERTY(double fontSizeFactor READ fontSizeFactor CONSTANT)
public:
    Q_INVOKABLE double fontSizeFactor() const { return 1.95; }
    // ...
    const QVariantList &analyzePages() override;
    const QVariantList &toolBarIndicators() override;
    void paletteOverride(const QString &colorName, QGCPalette::PaletteColorInfo_t &colorInfo) override;
    bool adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData) override;
    bool mavlinkMessage(Vehicle *vehicle, LinkInterface *link, const mavlink_message_t &message) override;
};
```

### What PreflightPlugin overrides

**`fontSizeFactor()`** (`PreflightPlugin.cpp:32`): Returns 1.95 — a scaling factor applied to QGC's base font size. This is exposed as a Q_PROPERTY so QML can use it.

**`analyzePages()`** (`PreflightPlugin.cpp:439-465`): Adds two custom pages to the Analyze Tools sidebar:
1. "Preflight Checklist" → `qrc:/qml/cpts/PreflightChecklistView.qml`
2. "Vehicles" → `qrc:/qml/analyze/VehiclesPage.qml`

These appear alongside QGC's stock Analyze pages.

**`paletteOverride()`** (`PreflightPlugin.cpp:467-607`): Overrides ~30 palette colors for the dark theme. The Skywin theme uses a near-black background (`#0B0D12`) with a single steel blue accent (`#3B82A0`). Safety colors (green, red, yellow) are deliberately NOT overridden to preserve stock safety semantics.

**`mavlinkMessage()`** (`PreflightPlugin.cpp:625-631`): Currently returns `true` (pass-through). This hook exists for future MAVLink message interception.

**`toolBarIndicators()`** (`PreflightPlugin.cpp:633-641`): Adds a custom `PreflightToolbarIndicator.qml` to the toolbar.

### How QGC calls into the plugin

At startup, QGC's `QGCApplication` instantiates the plugin:

```cpp
// In QGCApplication (upstream QGC)
QGCCorePlugin *plugin = new PreflightPlugin(this);
plugin->init();
```

The `init()` method (`PreflightPlugin.cpp:118-210`) performs all initialization:

1. Loads the Abel-Regular font and sets it as the application font
2. Creates `PreflightManager` with 1-second evaluation interval
3. Creates `TelemetryBridge`, `ArmingGate`, `WeatherProvider`, `PowerModel`, `ExportHelper`, `HardwareTestController`, `VehicleProfileManager`, `ChecklistItemModel`, `ChecklistEngine`
4. Initializes `DatabaseManager` singleton
5. Creates 8 category filter models (`CatModel0` through `CatModel7`)
6. Registers `BatteryHealthCheck` and `MissionEnergyCheck` dynamically
7. Connects vehicle add/remove signals to the registry
8. Connects gate open/close signals to flight session management
9. Starts weather refresh timer

During operation, QGC calls `analyzePages()` when the user opens the Analyze view, `paletteOverride()` when constructing UI elements, and `toolBarIndicators()` when building the toolbar.

**Key file:** `custom/src/PreflightPlugin.cpp:118-210` (init method).

## 3.2 The seven core subsystems

### 1. PreflightManager

**Class:** `PreflightManager` (`custom/src/core/PreflightManager.h:20-178`)

**Responsibility:** Owns, evaluates, and tracks all 91 preflight checks. Provides QML-accessible progress counters, categorized check lists, and arming-blocker queries.

**Key interface:**
- `addCheck(AbstractCheck*)` — register a check
- `startEvaluation(int intervalMs)` — start periodic timer
- `evaluateAll()` — evaluate all auto checks immediately
- `allMandatoryPassed()` — returns true if every mandatory check has status `Passed`
- `armingBlocker()` — returns the first blocking failure message
- `blockingChecks()` — returns sorted list of non-passed checks
- `checkById(QString)` — find a check by its string ID

**Interface to rest of system:** Exposed to QML as context property `PreflightManager`. Connected to `TelemetryBridge` for vehicle data access, `ArmingGate` for arming decisions, `ChecklistItemModel` for UI display.

### 2. ArmingGate

**Class:** `ArmingGate` (`custom/src/core/ArmingGate.h:13-135`)

**Responsibility:** Intercepts `MAV_CMD_COMPONENT_ARM_DISARM` and blocks or allows arming based on check results. Manages operator overrides.

**Key interface:**
- `interceptCommandLong(uint16_t command, QMap<int,float> params)` — returns true to allow, false to block
- `processArmRequest(int criticalFailCount, int manualFailCount)` — gate decision
- `acknowledgeOverride(QString pilotName, QString reason)` — one-time bypass
- `overrideGate(QString reason, int timeoutSec)` — timed override
- `forceArm()` — unconditional override

**Interface to rest of system:** Receives check results from `PreflightManager` via signal connections. Emits `gateOpened`/`gateClosed` signals consumed by `PreflightPlugin` for flight session management.

### 3. TelemetryBridge

**Class:** `TelemetryBridge` (`custom/src/adapters/TelemetryBridge.h:19-671`)

**Responsibility:** Bridges MAVLink telemetry from a QGC `Vehicle` object into QML-accessible `Q_PROPERTY` values. Provides a unified interface for all preflight checks to read vehicle state without depending on the QGC Vehicle API directly.

**Key interface:**
- `setVehicle(Vehicle*)` — connect to a vehicle
- 100+ `Q_PROPERTY` declarations covering battery, GPS, RC, IMU, EKF, ESC, gimbal, mission, connection quality
- `setParameterValue(QString name, float value)` — write parameter to vehicle
- `hasParameter(QString name)` — check local parameter cache

**Interface to rest of system:** Exposed to QML as `TelemetryProvider`. All `AbstractCheck` subclasses read telemetry through `m_telemetry->property()` calls. Connected to `Vehicle` Fact signals and raw MAVLink message handlers.

### 4. DatabaseManager

**Class:** `DatabaseManager` (`custom/src/utils/DatabaseManager.h:32-173`)

**Responsibility:** SQLite persistence for templates, compliance logs, vehicle profiles, battery cycles, flight sessions, check results, motor test results, and maintenance components.

**Key interface:**
- `initialize()` — open/create database
- `saveCheckResult()`, `getCheckResults()` — check audit trail
- `startFlightSession()`, `endFlightSession()` — flight lifecycle
- `logMotorTestResult()` — motor test audit
- `lookupVehicleByFingerprint()`, `registerNewVehicle()` — vehicle registry
- `getCheckConfig()`, `setCheckConfig()` — per-check configurable thresholds

**Interface to rest of system:** Singleton accessed via `DatabaseManager::instance()`. Used by `AbstractCheck::configDouble()` for threshold configuration, `PreflightManager` for override persistence, `PreflightPlugin` for flight sessions, `VehicleRegistry` for fingerprint lookups.

### 5. VehicleRegistry

**Class:** `VehicleRegistry` (`custom/src/detection/VehicleRegistry.h:9-57`)

**Responsibility:** Vehicle fingerprinting and identification. Determines whether a connecting vehicle is known (returning) or new, and triggers appropriate registration.

**Key interface:**
- `generateFingerprint(quint64 uid, int autopilotType, QString boardVersion)` — SHA-256 hash
- `currentFingerprint()` — current vehicle's fingerprint
- `isKnownVehicle` — Q_PROPERTY for QML
- Signals: `knownVehicleConnected`, `newVehicleRegistered`

**Interface to rest of system:** Singleton exposed as QML context property `VehicleRegistry`. Connected to `MultiVehicleManager::vehicleAdded`/`vehicleRemoved` signals. Feeds fingerprint to `DatabaseManager::lookupVehicleByFingerprint()`.

### 6. HardwareTestController

**Class:** `HardwareTestController` (`custom/src/controllers/HardwareTestController.h:15-203`)

**Responsibility:** Motor/servo test execution. Sends `MAV_CMD_DO_MOTOR_TEST` commands, monitors `SERVO_OUTPUT_RAW` feedback, manages cooldown timers, and tracks per-motor test state.

**Key interface:**
- `testMotor(int motorIndex)` — start test for one motor
- `stopAll()` — emergency stop
- `motorStatus(int motorIndex)` — query test result
- `motorFeedback(int motorIndex)` — query actual PWM
- `setMotorPwm(int motorIndex, int pwmUs)` — set test PWM

**Interface to rest of system:** Exposed as QML context property `HardwareTestController`. Connected to `Vehicle` for MAVLink command sending. Writes audit records to `DatabaseManager::logMotorTestResult()`.

### 7. WeatherProvider

**Class:** `WeatherProvider` (`custom/src/utils/WeatherProvider.h:11-118`)

**Responsibility:** External weather data from METAR (NOAA ADDS), TAF, and Open-Meteo API. Provides cached weather data to weather-related preflight checks.

**Key interface:**
- `fetchWeather(double lat, double lon)` — Open-Meteo forecast by coordinates
- `fetchMetar(QString icaoCode)` — METAR by ICAO station
- `fetchTaf(QString icaoCode)` — TAF by ICAO station
- `refreshAll(QString icaoCode)` — METAR + TAF combined
- Properties: `temperature`, `windSpeed`, `windGust`, `visibilityKm`, `ceilingFt`, `metarString`, `tafDeteriorating`

**Interface to rest of system:** Singleton exposed as QML context property `WeatherProvider`. Weather checks read from cached properties. `PreflightPlugin` triggers periodic refresh via `_weatherRefreshTimer`.

## 3.3 Data flow diagrams

### Flow 1: Vehicle connects → checklist evaluates

```
Vehicle connects via MAVLink
  │
  ▼
MultiVehicleManager::vehicleAdded()          [QGC upstream]
  │
  ▼
VehicleRegistry::_onVehicleAdded()           [custom/src/detection/VehicleRegistry.cpp:39]
  ├── _extractVehicleInfo(vehicle)           → reads uid, autopilotType, boardVersion
  ├── generateFingerprint(uid, type, board)  → SHA-256 hash
  ├── DatabaseManager::lookupVehicleByFingerprint()
  │     ├── Known vehicle → emit knownVehicleConnected()
  │     └── New vehicle  → registerNewVehicle() → emit newVehicleRegistered()
  │
  ▼
PreflightPlugin::_onKnownVehicleConnected()  [custom/src/PreflightPlugin.cpp:305]
  ├── DatabaseManager::loadVehicleConfig(fingerprint) → JSON
  └── AbstractCheck::applyVehicleConfig(config) → per-check config loaded
  │
  ▼
PreflightPlugin::_setupForVehicle(vehicle)   [custom/src/PreflightPlugin.cpp:390]
  ├── TelemetryBridge::setVehicle(vehicle)
  ├── PreflightManager::startEvaluation(1000) → QTimer starts at 1Hz
  ├── ChecklistEngine::start()
  └── WeatherProvider::refreshAll() (if auto-weather enabled)
  │
  ▼
PreflightManager::tick()                     [custom/src/core/PreflightManager.cpp:509]
  ├── Evaluates stale checks via check->evaluate()
  ├── Detects staleness (10s threshold)
  ├── Calls attemptStateTransition()
  └── Emits progressChanged()
  │
  ▼
AbstractCheck::evaluate()                    [e.g. GpsFixCheck::evaluate()]
  ├── hasTelemetry() → reads TelemetryBridge
  ├── getTelemetryDouble("gpsSatellites") → reads Q_PROPERTY
  ├── setStatus(Passed/Failed/Warning) → emits statusChanged()
  └── PreflightManager::connectCheckSignals() handles status change
  │
  ▼
ChecklistItemModel updated                    [via PreflightChecklistModel roles]
  │
  ▼
QML Repeater redraws                         [PreflightChecklistView.qml:438-825]
```

### Flow 2: MAVLink telemetry → QML display

```
MAVLink message arrives on link
  │
  ▼
Vehicle::_mavlinkMessageReceived()           [QGC upstream]
  │
  ▼
TelemetryBridge::_handleMavlinkMessage()     [custom/src/adapters/TelemetryBridge.cpp]
  ├── case MAVLINK_MSG_ID_HEARTBEAT:
  │     _heartbeatReceived = true
  │     emit heartbeatReceivedChanged()
  ├── case MAVLINK_MSG_ID_BATTERY_STATUS:
  │     _batteryVoltage = ...
  │     emit batteryVoltageChanged()
  ├── case MAVLINK_MSG_ID_GPS_RAW_INT:
  │     _gpsFixType = ...
  │     _gpsSatellites = ...
  │     emit gpsFixTypeChanged()
  └── ... (100+ message types)
  │
  ▼
Q_PROPERTY NOTIFY signals fire               [TelemetryBridge.h signals]
  │
  ▼
QML bindings update                          [VehicleTelemetry.qml:330-477]
  ├── Connections { target: TelemetryProvider }
  │     function onBatteryVoltageChanged() {
  │         batteryVoltage = TelemetryProvider.batteryVoltage
  │     }
  └── ... (40+ signal handlers)
  │
  ▼
QML UI renders                               [TelemetryBar, TelemetryInfoBox, etc.]
```

### Flow 3: User taps Arm → ArmingGate → MAVLink or block

```
User taps "Arm" in FlyView toolbar
  │
  ▼
GuidedActionsController::actionArm()         [QGC upstream]
  │
  ▼
ArmingGate::interceptCommandLong(400, {1.0}) [custom/src/core/ArmingGate.cpp:69]
  ├── command != 400? → return true (pass-through)
  ├── armParam != 1.0? → return true (disarm, allow)
  ├── m_overrideActive? → return true (override active)
  ├── m_ackReceived? → return true (acknowledged bypass)
  ├── m_mode == Passive? → return true (no blocking)
  │
  ▼ (Active or Hybrid mode)
  PreflightManager::allMandatoryPassed()
  ├── true → return true (arming allowed)
  └── false → emit armingDenied(), return false (blocked)
  │
  ▼ (if blocked)
  QML shows denial banner                    [ArmGatePanel.qml]
  ├── Shows denial reason
  ├── Shows blocker count
  └── Tapping scrolls to failing check
```

## 3.4 Threading model

The system operates on three threads:

### Main (GUI) thread

- All `QObject` instances created by `PreflightPlugin::init()` live on the main thread
- `PreflightManager`, `ArmingGate`, `WeatherProvider`, `DatabaseManager`, `HardwareTestController`, `VehicleProfileManager` — all main-thread objects
- `QTimer::timeout` callbacks (`tick()`, `_refreshWeather()`) execute on the main thread
- QML UI rendering and signal handlers run on the main thread

### MAVLink network thread

- `Vehicle::_mavlinkMessageReceived()` fires on the MAVLink receive thread
- `TelemetryBridge::_handleMavlinkMessage()` runs on this thread
- Property updates (`_batteryVoltage = ...`) and signal emissions happen on this thread

### Qt::QueuedConnection bridge

When `TelemetryBridge` emits a signal on the MAVLink thread and the receiver lives on the main thread, `Qt::QueuedConnection` ensures the slot is invoked via the main thread's event loop:

```cpp
// In PreflightManager::setTelemetryBridge():
connect(m_telemetry, &TelemetryBridge::isConnectedChanged, this, [this]() {
    // This lambda runs on the main thread due to QueuedConnection
    if (m_telemetry->isConnected()) {
        m_stateMachine.transitionTo(PreflightStateMachine::ParamLoading);
        evaluateAll();
    }
});
```

Without `QueuedConnection`, the default connection type is `AutoConnection`, which uses `DirectConnection` when both objects share a thread and `QueuedConnection` otherwise. Since `TelemetryBridge` and `PreflightManager` are on different threads (MAVLink vs main), `AutoConnection` resolves to `QueuedConnection` — but explicit `QueuedConnection` is safer.

### WeatherProvider async fetch

`WeatherProvider::fetchWeather()` uses `QNetworkAccessManager::get()` which returns immediately with a `QNetworkReply*`. The reply's `finished` signal is connected via lambda:

```cpp
QNetworkReply *reply = m_nam.get(req);
connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    // Runs on main thread when the HTTP response arrives
    handleMetarJson(reply->readAll());
    emit weatherUpdated();
});
```

This is non-blocking — the main thread continues processing while the HTTP request completes in the background.

### DetectionBridge WebSocket callback (not yet implemented)

The designed architecture for the inference pipeline uses `QWebSocket` to receive detection results from the Python inference service. The WebSocket callback would fire on a network thread, requiring `Qt::QueuedConnection` to safely update QML properties on the main thread.

## 3.5 State machine

The `PreflightStateMachine` (`custom/src/core/PreflightStateMachine.h:6-46`) manages 8 lifecycle states:

```
Disconnected → Connecting → ParamLoading → ChecklistInProgress
    → PreflightPass → ManualConfirmPhase → ArmingAllowed → Armed
```

### State transitions

| From | To | Trigger | What blocks it |
|------|----|---------|----------------|
| Disconnected | Connecting | `heartbeatReceived()` becomes true | No telemetry link |
| Connecting | ParamLoading | `parametersReady()` becomes true | Parameter fetch timeout (10s) |
| ParamLoading | ChecklistInProgress | All watchlist parameters received | `UavParameterManager` status != Ready |
| ChecklistInProgress | PreflightPass | All auto mandatory checks pass | Any auto mandatory check failing |
| PreflightPass | ManualConfirmPhase | Auto-only mandatory pass confirmed | — |
| ManualConfirmPhase | ArmingAllowed | All mandatory checks (auto + manual) pass | Any manual check not confirmed |
| ArmingAllowed | Armed | Vehicle reports armed state | ArmingGate blocking |
| Any | Disconnected | `isConnected()` becomes false | — |
| Armed | ChecklistInProgress | Vehicle reports disarmed + check fails | — |

### How transitions work

The state machine is driven by `PreflightManager::attemptStateTransition()` (`PreflightManager.cpp:463-488`):

```cpp
void PreflightManager::attemptStateTransition() {
    bool allMandatoryPass = allMandatoryPassed();
    bool allAutoMandatoryPass = true;
    for (auto *c : m_checks) {
        if (c->mandatory() && c->isAuto() && c->status() != CheckStatus::Passed) {
            allAutoMandatoryPass = false;
            break;
        }
    }

    if (allMandatoryPass) {
        emit allChecksPassed(m_sysId);
        if (m_stateMachine.state() < PreflightStateMachine::ArmingAllowed)
            m_stateMachine.transitionTo(PreflightStateMachine::ArmingAllowed);
    } else if (allAutoMandatoryPass) {
        if (m_stateMachine.state() < PreflightStateMachine::PreflightPass)
            m_stateMachine.transitionTo(PreflightStateMachine::PreflightPass);
        if (m_stateMachine.state() == PreflightStateMachine::PreflightPass)
            m_stateMachine.transitionTo(PreflightStateMachine::ManualConfirmPhase);
    } else {
        if (m_stateMachine.state() <= PreflightStateMachine::ParamLoading)
            m_stateMachine.transitionTo(PreflightStateMachine::ChecklistInProgress);
        else if (m_stateMachine.state() >= PreflightStateMachine::ArmingAllowed)
            m_stateMachine.transitionTo(PreflightStateMachine::ChecklistInProgress);
    }
}
```

This runs on every `tick()` (1Hz) and on every `evaluateAll()` call.

---

# Chapter 4 — Preflight Checklist System

## 4.1 AbstractCheck base class

`AbstractCheck` (`custom/src/core/AbstractCheck.h:59-222`) is the abstract base class for all 91 preflight checks. Every concrete check inherits from it and implements `evaluate()`.

### Interface

```cpp
class AbstractCheck : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString checkId READ id CONSTANT)
    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(int status READ statusInt NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QVariant currentValue READ currentValue NOTIFY currentValueChanged)
    Q_PROPERTY(QString currentValueString READ getCurrentValueString NOTIFY currentValueChanged)
    Q_PROPERTY(bool mandatory READ mandatory CONSTANT)
    Q_PROPERTY(int checkCategory READ categoryInt CONSTANT)
    Q_PROPERTY(int type READ typeInt CONSTANT)
    Q_PROPERTY(bool canOverride READ canOverride CONSTANT)
    Q_PROPERTY(QString rationale READ getRationale CONSTANT)
    Q_PROPERTY(QStringList fixSteps READ getFixSteps CONSTANT)
    Q_PROPERTY(QString threshold READ getThreshold CONSTANT)

public:
    virtual void evaluate() = 0;  // Pure virtual — each check implements this
    bool overrideStatus(const QString &newStatus, const QString &reason = {});
    bool confirm(const QString &reason = {});
    virtual void reset();
};
```

### CheckStatus enum

```cpp
enum class CheckStatus : int {
    Pending  = 0,  // Not yet evaluated
    Passed   = 1,  // Check passed
    Failed   = 2,  // Check failed (blocks arming if mandatory)
    Warning  = 3,  // Non-blocking warning
    Error    = 4,  // Evaluation error
    Skipped  = 5,  // Check skipped (hardware not present)
    Stale    = 6   // Data too old
};
```

### CheckType enum

```cpp
enum class CheckType : int {
    Auto     = 0,  // Evaluated automatically on timer
    Manual   = 1,  // Requires user interaction
    Action   = 2   // One-time hardware action
};
```

### Staleness mechanism

The staleness check in `AbstractCheck::requiresReevaluation()` (`AbstractCheck.cpp:45-56`):

```cpp
bool AbstractCheck::requiresReevaluation() const {
    if (m_type != CheckType::Auto) return false;
    if (!m_lastEvalTime.isValid()) return true;
    if (m_status == CheckStatus::Passed) return false;
    return m_lastEvalTime.msecsTo(QDateTime::currentDateTime()) >
           configInt("stale_timeout_ms", 5000);
}
```

Only auto checks are re-evaluated. Passed checks are not re-evaluated (they stay passed until reset). Stale checks are marked by `PreflightManager::tick()` when `m_stalenessThresholdMs` (10,000ms) is exceeded.

### configDouble() for configurable thresholds

`AbstractCheck::configDouble()` (`AbstractCheck.cpp:230-244`) reads thresholds from the `check_config` database table with an in-memory cache:

```cpp
double AbstractCheck::configDouble(const QString &key, double defaultVal) const {
    auto it = m_configCache.constFind(key);
    if (it != m_configCache.constEnd())
        return it->toDouble();
    QString val = DatabaseManager::instance().getCheckConfig(m_id, key);
    if (val.isEmpty()) {
        m_configCache.insert(key, QVariant(defaultVal));
        return defaultVal;
    }
    bool ok = false;
    double result = val.toDouble(&ok);
    m_configCache.insert(key, QVariant(ok ? result : defaultVal));
    return ok ? result : defaultVal;
}
```

The cache avoids hitting SQLite on every evaluation tick (see Chapter 9, Section 9.4).

### Telemetry access helpers

Checks read vehicle data through helper methods that use the Qt meta-object system:

```cpp
double AbstractCheck::getTelemetryDouble(const QString &prop) const {
    const QMetaObject *meta = m_telemetry->metaObject();
    int idx = meta->indexOfProperty(prop.toLatin1().constData());
    if (idx < 0) {
        // Fallback: try dynamic property
        QVariant dyn = m_telemetry->property(prop.toLatin1().constData());
        if (dyn.isValid()) return dyn.toDouble();
        return qQNaN();
    }
    return meta->property(idx).read(m_telemetry).toDouble();
}
```

This reads `Q_PROPERTY` values from `TelemetryBridge` by name, enabling checks to access any telemetry property without compile-time dependencies on the specific property.

## 4.2 Check registration in PreflightManager

### How 91 checks are registered

All checks are registered in `PreflightManager::createPhase1Checks()` (`PreflightManager.cpp:688-912`). The method is called once from the constructor (`PreflightManager.cpp:130`).

Checks are appended in tiers:

**Tier 0 — Core Safety (18 checks, blocking):**
```cpp
m_checks.append(new RadioBufferCheck(m_telemetry, 20, this));
m_checks.append(new MavlinkProtocolCheck(m_telemetry, this));
m_checks.append(new CompanionLinkCheck(m_telemetry, this));
m_checks.append(new TerrainClearanceCheck(m_telemetry, 5.0, this));
m_checks.append(new TakeoffCommandCheck(m_telemetry, this));
m_checks.append(new BatteryVoltageCheck(m_telemetry, 0.0, 5.0, this));
m_checks.append(new GpsFixCheck(m_telemetry, 8, 2.0, this));
m_checks.append(new AttitudeCheck(m_telemetry, 30.0, this));
m_checks.append(new EkfVarianceCheck(m_telemetry, 0.5, 5.0, 5.0, this));
m_checks.append(new RcRssiCheck(m_telemetry, 50, 50, this));
m_checks.append(new HeartbeatCheck(m_telemetry, 10, this));
m_checks.append(new GeofenceParamCheck(m_telemetry, this));
m_checks.append(new RtlAltParamCheck(m_telemetry, 10.0, 122.0, this));
m_checks.append(new HomePositionCheck(m_telemetry, 0.005, this));
m_checks.append(new PreArmOkCheck(m_telemetry, this));
m_checks.append(new CompassCalCheck(m_telemetry, 150, this));
m_checks.append(new ImuCalCheck(m_telemetry, this));
m_checks.append(new AirspeedCheck(m_telemetry, 20.0, this));
```

**Tier 1 — Navigation (7), Power (6), Communication (3), Safety (4):**
```cpp
// Navigation
m_checks.append(new EkfStatusFlagsCheck(m_telemetry, 0x31, 1.0, this));
m_checks.append(new VibrationCheck(m_telemetry, 30.0, this));
m_checks.append(new AhrsHealthCheck(m_telemetry, this));
m_checks.append(new MagFieldStrengthCheck(m_telemetry, 0.15, 0.65, this));
m_checks.append(new AccelConsistencyCheck(m_telemetry, 4.0, this));
m_checks.append(new MagInterferenceCheck(m_telemetry, 0.20, 0.15, this));
m_checks.append(new CompassYawConsistencyCheck(m_telemetry, 15.0, this));

// Power
m_checks.append(new BatteryTemperatureCheck(m_telemetry, 45.0, 0.0, this));
m_checks.append(new CellConfigCheck(m_telemetry, 3.0, 1, this));
m_checks.append(new CellVoltageBalanceCheck(m_telemetry, 0.15, this));
m_checks.append(new CurrentSensorCheck(m_telemetry, 0.5, 0.5, this));
m_checks.append(new PowerModuleHealthCheck(m_telemetry, 0.3, this));
m_checks.append(new RedundantPowerCheck(m_telemetry, 10.0, this));
```

**Tier 2 — Navigation/Sensor Health (8):**
```cpp
m_checks.append(new BaroAltConsistencyCheck(m_telemetry, 5.0, this));
m_checks.append(new BaroHealthCheck(m_telemetry, this));
m_checks.append(new GeofenceMaxAltCheck(m_telemetry, 10.0, this));
m_checks.append(new GeofenceMaxRadiusCheck(m_telemetry, 10.0, this));
m_checks.append(new LevelCalibrationCheck(m_telemetry, 2.0, this));
m_checks.append(new GyroBiasCheck(m_telemetry, 0.05, this));
m_checks.append(new RcModeSwitchCheck(m_telemetry, this));
m_checks.append(new RcTrimCheck(m_telemetry, 50, this));
```

**Tier 3 — Warning/Non-blocking (13):**
```cpp
m_checks.append(new WeatherWindCheck(m_telemetry, this));
m_checks.append(new WeatherWindGustCheck(m_telemetry, this));
m_checks.append(new MetarVisibilityCheck(m_telemetry, this));
m_checks.append(new MetarCeilingCheck(m_telemetry, this));
m_checks.append(new MetarPrecipitationCheck(m_telemetry, this));
m_checks.append(new MetarTemperatureCheck(m_telemetry, this));
m_checks.append(new TafDeteriorationCheck(m_telemetry, this));
m_checks.append(new AmbientTemperatureCheck(m_telemetry, 50.0, -10.0, this));
// ...
```

**Manual confirm checks (19):**
```cpp
// Airframe (8)
m_checks.append(new ManualConfirmCheck(
    QStringLiteral("airframe.type"), QStringLiteral("Airframe Type"),
    CheckCategory::Airframe,
    QStringLiteral("Confirm FRAME_CLASS / FRAME_TYPE match"),
    {QStringLiteral("FRAME_CLASS"), QStringLiteral("FRAME_TYPE")}, this));
// ... 7 more airframe checks
// Propulsion (3), Environment (5), Power (2), Navigation (1)
```

**Tier 4 — Enhancement/Niche (13):**
```cpp
m_checks.append(new GimbalLinkCheck(m_telemetry, this));
m_checks.append(new VideoFeedCheck(m_telemetry, this));
m_checks.append(new GpsSpeedAccuracyCheck(m_telemetry, 2.0, 2.0, this));
// ...
```

**Dynamic checks (added in PreflightPlugin::init):**
```cpp
auto *batteryHealthCheck = new BatteryHealthCheck(
    _vehicleProfileManager, kBatteryHealthVoltageThreshold, kBatteryHealthStddevThreshold, this);
_preflightManager->addCheck(batteryHealthCheck);

auto *energyCheck = new MissionEnergyCheck(_powerModel, kMissionEnergyMargin, this);
energyCheck->setVehicleProfileManager(_vehicleProfileManager);
_preflightManager->addCheck(energyCheck);
```

After registration, checks are sorted by category and deduplicated:

```cpp
std::sort(m_checks.begin(), m_checks.end(),
    [](AbstractCheck *a, AbstractCheck *b) {
        if (a->category() != b->category())
            return a->categoryInt() < b->categoryInt();
        return a->id() < b->id();
    });
```

### Category structure

| ID | Category | Icon | Check Count |
|----|----------|------|-------------|
| 0 | Propulsion | ⚙ | ~6 (motor, ESC, propeller) |
| 1 | Power | ⚡ | ~8 (battery, voltage, current) |
| 2 | Navigation/GPS | 🛰 | ~12 (GPS, compass, EKF, home) |
| 3 | Communication | 📶 | ~6 (RC, telemetry, MAVLink) |
| 4 | Airframe | 🛩 | ~10 (structure, weight, landing gear) |
| 5 | Safety | 🛡 | ~8 (failsafes, geofence, pre-arm) |
| 6 | Environment | 🌬 | ~8 (weather, METAR, wind, temperature) |
| 7 | Arming Gate | ⚙ | 1 (gate logic itself) |

## 4.3 How a check evaluates — three examples

### Example 1: GpsFixCheck

**File:** `custom/src/core/GpsFixCheck.cpp:1-112`

**ID:** `nav.gps.fix`  
**Category:** Navigation  
**Type:** Auto  
**Mandatory:** Yes (blocks arming)

**Constructor:**
```cpp
GpsFixCheck::GpsFixCheck(TelemetryBridge *telemetry, int minSatellites,
                         double maxHdop, QObject *parent)
    : AbstractCheck(QStringLiteral("nav.gps.fix"),
                    QStringLiteral("GPS Fix"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
    , m_minSatellites(minSatellites)
    , m_maxHdop(maxHdop)
{
    m_telemetry = telemetry;
}
```

**evaluate() logic:**
1. Check `hasTelemetry()` — if no telemetry, set Pending and return
2. Read `gpsFixType`, `gpsSatellites`, `gpsLatitude`, `gpsLongitude`, `gpsHdop` from TelemetryBridge
3. Override thresholds from vehicle parameters: `EKF2_REQ_NSATS` (PX4) or `GPS_MIN_SATS` (ArduPilot), `GPS_HDOP_GOOD`
4. Decision tree:
   - fixType == 0 → Pending ("No GPS fix")
   - fixType < 3 → Failed ("2D fix only — need 3D fix")
   - sats < minSats → Failed ("X sats — need Y")
   - hdop > maxHdop → Failed ("HDOP X — exceeds Y threshold")
   - lat/lon == 0 → Warning ("Fix acquired but position is origin")
   - Otherwise → Passed ("X sats, 3D fix, HDOP Y")

**Key design pattern:** The check reads its thresholds from constructor defaults, then overrides them from vehicle parameters if available. This allows the same check to work across different vehicle configurations.

### Example 2: BatteryVoltageCheck

**File:** `custom/src/core/BatteryVoltageCheck.cpp:1-147`

**ID:** `power.battery.voltage`  
**Category:** Power  
**Type:** Auto  
**Mandatory:** Yes

**evaluate() logic:**
1. Check `hasTelemetry()`
2. Read `batteryVoltage`, `batteryCurrent`, `batteryPercent`
3. Compute `effectiveMinVoltage()`:
   - PX4: `BAT_V_EMPTY × BAT_CELL_COUNT` (per-cell aware)
   - ArduPilot: `BATT_LOW_VOLT` (total threshold)
   - Fallback: `cellCount × 3.3V` or constructor default (15.0V)
4. Compute `effectiveMinPercent()`: reads `BAT_LOW_THR` parameter
5. Decision:
   - voltage >= minV → Passed
   - percentOk (but voltage low) → Warning ("low voltage")
   - Otherwise → Failed ("below X threshold")

**Cell count estimation:** If `BAT_CELL_COUNT` parameter is unavailable, `estimateCellCount()` divides voltage by 3.7V nominal: `qRound(v / 3.7)`.

### Example 3: WeatherWindCheck

**File:** `custom/src/core/WeatherWindCheck.cpp:1-124`

**ID:** `env.wind`  
**Category:** Environment  
**Type:** Auto  
**Mandatory:** Yes (but overrideable)

**evaluate() logic:**
1. Check `hasTelemetry()`
2. Check `autoWeatherEnabled` setting — if disabled, return Skipped
3. Get `WeatherProvider::instance()` — if null, return Skipped
4. Check `metarFresh()` — if stale:
   - Trigger fetch if not already triggered
   - Wait 10 seconds for response
   - If still no data after timeout → Skipped with error message
5. Read `windSpeed`, `windGust`, `windDirection` from WeatherProvider
6. Compare against `windThresholdSustained()` and `windThresholdGust()` from PreflightSettingsManager
7. Decision:
   - Sustained > threshold × 1.25 or gust > threshold × 1.25 → Failed
   - Sustained > threshold or gust > threshold → Warning
   - Otherwise → Passed

**Key design pattern:** The check triggers a fetch on first evaluation if data is not fresh, then waits for the response. This lazy-fetch pattern avoids unnecessary network requests.

## 4.4 ChecklistEngine — the evaluation loop

The `ChecklistEngine` (`custom/src/core/ChecklistEngine.h:11-58`) provides a higher-level evaluation interface:

```cpp
class ChecklistEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(int totalItems READ totalItems NOTIFY totalItemsChanged)
    Q_PROPERTY(int passedItems READ passedItems NOTIFY evaluationCompleted)
    Q_PROPERTY(int pendingItems READ pendingItems NOTIFY evaluationCompleted)
    Q_PROPERTY(int failedItems READ failedItems NOTIFY evaluationCompleted)
    Q_PROPERTY(bool allPassed READ allPassed NOTIFY evaluationCompleted)
public:
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void evaluateAll();
    Q_INVOKABLE void confirmItem(int row, const QString &operatorId = {});
};
```

### Evaluation triggers

Evaluation is triggered in two ways:

1. **Periodic timer**: `PreflightManager::tick()` runs every 1 second and calls `check->requiresReevaluation()` for each auto check. Only stale checks (not recently evaluated or not passed) are re-evaluated.

2. **Reactive on telemetry change**: The `ChecklistEngine::_onTelemetryPropertyChanged()` slot fires when any TelemetryBridge property changes, triggering re-evaluation of checks that depend on that property.

### Stale detection

In `PreflightManager::tick()` (`PreflightManager.cpp:619-641`):

```cpp
for (auto *check : m_checks) {
    if (!check->mandatory()) continue;
    auto s = check->status();
    if (s == CheckStatus::Passed || s == CheckStatus::Warning) {
        QDateTime lastEval = check->lastEvaluationTime();
        if (lastEval.isValid() &&
            lastEval.msecsTo(QDateTime::currentDateTime()) > m_stalenessThresholdMs) {
            check->setStatus(CheckStatus::Stale,
                QStringLiteral("Data stale — last update %1s ago")
                    .arg(lastEval.secsTo(QDateTime::currentDateTime())));
        }
    }
}
```

The default staleness threshold is 10,000ms (10 seconds). If a mandatory check that was previously Passed has not been re-evaluated within 10 seconds, it is marked as `Stale` — which counts as a failure for arming decisions.

### Results flow into ChecklistItemModel

Results flow from `AbstractCheck::statusChanged` → `PreflightManager::connectCheckSignals()` → `PreflightChecklistModel` roles → QML Repeater delegate bindings. The `PreflightChecklistModel` exposes 18 roles including `StatusRole`, `MessageRole`, `CurrentValueRole`, and `IsManualRole`.

## 4.5 Vehicle-adaptive check visibility

### How motor count is determined

Motor count is read from two parameter sources:

1. **PX4**: `CA_AIRFRAME` parameter (airframe type) and `CA_ROTOR_CNT` (rotor count)
2. **ArduPilot**: `FRAME_CLASS` (airframe class) and `FRAME_TYPE` (frame type)

The `HardwareTestController::resolveMotorCount()` method (`HardwareTestController.cpp`) reads these parameters and maps them to motor counts:

| FRAME_CLASS | Vehicle Type | Motor Count |
|-------------|-------------|-------------|
| 1 | Quad | 4 |
| 2 | Hexa | 6 |
| 3 | Octa | 8 |
| 10 | Fixed Wing | 1 |
| 15 | VTOL | 4+ |

### What checks are hidden for fixed-wing vs multirotor

The `MotorCountCheck` evaluates the motor count and sets its status accordingly. For fixed-wing vehicles:

- `MotorSpinCheck` (action check) still appears but uses RC_CHANNELS_OVERRIDE instead of `MAV_CMD_DO_MOTOR_TEST`
- Multirotor-specific checks like `CellVoltageBalanceCheck` and `EscCurrentSymmetryCheck` are less relevant but still evaluated
- The `HardwareTestController` switches to fixed-wing test mode (`_fwStartTest()`, `_fwSendRcOverride()`, `_fwDisarm()`)

The motor count also determines the motor test grid layout in `MotorCheckPanel.qml:324-492`:

```qml
GridLayout {
    columns: root._mc <= 4 ? 2 : (root._mc <= 6 ? 3 : 4)
    // ...
    Repeater {
        model: root._mc > 0 ? root._mc : 0
        // Per-motor card
    }
}
```

## 4.6 The checklist UI

### PreflightChecklistView.qml

**File:** `custom/qml/cpts/PreflightChecklistView.qml:1-842`

This is the main checklist view, loaded by the Analyze Tools page. It contains:

1. **Top summary banner** (lines 182-253): Shows pass count, issue count, circular progress indicator, and "Show" button for critical issues dialog
2. **Blocking banner** (lines 256-330): Red banner showing arming-blocked status with blocker count and priority fix list toggle
3. **Priority fix list** (lines 332-346): Collapsible panel showing numbered list of blocking failures
4. **Scrollable categories** (lines 350-833): `Repeater` over `_engine.catOrder` creating category sections
5. **Category header** (lines 384-418): Collapsible header with icon, label, and pass/fail/warn summary
6. **Card grid** (lines 421-828): `Flow` layout with responsive 2-5 column grid of check cards
7. **Check cards** (lines 441-825): Each card shows status icon, label, badge (PASS/FAIL/WARN), expand chevron, message, and expandable detail section with rationale, threshold, and fix steps
8. **Arm gate panel** (line 837-840): Fixed footer showing gate status

### How categories expand/collapse

Each category section uses a `Column` with a `Repeater` inside a `Flickable`. The category header is always visible; the card grid below it is inside the same column. There is no explicit expand/collapse toggle — all categories are always visible in the scrollable list. The visual distinction comes from the category header color (red border if any check in the category has failed).

### How check cards render status

Each card's color and border are determined by the `status` role:

```qml
color: {
    if (status === 1) return Colors.successDim   // Passed
    if (status === 2) return Colors.errorDim      // Failed
    if (status === 3) return Colors.checkWarnDim  // Warning
    return Colors.surface                          // Pending
}
border.color: {
    if (status === 1) return Colors.success
    if (status === 2) return Colors.error
    if (status === 3) return Colors.checkWarn
    return Colors.borderLight
}
```

### Where the UI entry point is

The checklist appears in two places:

1. **Analyze Tools page**: Added by `PreflightPlugin::analyzePages()` (`PreflightPlugin.cpp:450-455`) as "Preflight Checklist" → `qrc:/qml/cpts/PreflightChecklistView.qml`
2. **FlyView popup**: `FlyViewPreFlightChecklistPopup.qml` provides a quick-access popup from the fly view toolbar

---

# Chapter 5 — Arming Gate

## 5.1 What the arming gate does

In stock QGroundControl, pressing the Arm button sends `MAV_CMD_COMPONENT_ARM_DISARM` (command 400) directly to the vehicle. There is no pre-arm safety check beyond the vehicle's own internal pre-arm checks. This means a user can arm a vehicle that has:

- Low battery
- No GPS fix
- Failing EKF
- Incomplete checklist
- Unknown vehicle identity

The `ArmingGate` solves this by intercepting the arm command **before** it reaches the vehicle. It evaluates the preflight check results and decides whether to allow or block the command. This is a safety gate that sits between the operator and the vehicle.

The gate operates at the `interceptCommandLong()` level, which is called by QGC's `GuidedActionsController` before forwarding commands to the communication link.

## 5.2 Three modes

### Passive mode

```cpp
if (m_mode == Passive) return true;
```

In Passive mode (`ArmingGate.cpp:93-94`), all arm commands pass through without evaluation. The gate logs the arm event but does not block. Use this mode during initial testing, development, or when the operator wants full manual control without safety interlocks.

**What the operator sees:** No gate badge, no blocking indicators. Arming works exactly like stock QGC.

### Active mode

```cpp
if (m_mode == Active && !allPassed) {
    QString reason = m_manager->armingBlocker();
    m_armingAllowed = false;
    emit armingDenied(command, reason);
    return false;
}
```

In Active mode (`ArmingGate.cpp:116-127`), **any** mandatory check failure blocks the arm command. The command is dropped — the vehicle never receives it. The operator sees a denial banner with the reason.

**What the operator sees:** Red "Arming blocked" banner with blocker count. Tapping the banner scrolls to the failing check. The Arm button is effectively disabled until all mandatory checks pass.

### Hybrid mode

```cpp
if (m_mode == Hybrid && !allPassed) {
    QString reason = m_manager->armingBlocker();
    m_armingAllowed = false;
    emit armingDenied(command, reason);
    return false;
}
```

In Hybrid mode (`ArmingGate.cpp:103-114`), auto check failures block arming, but manual-only failures may be overridden via the `ALLOW_WITH_ACK` policy. This gives the operator the ability to override manual confirmations (like "propellers inspected") while still enforcing auto checks (like "GPS fix acquired").

**What the operator sees:** Same as Active mode, but the override dialog appears when manual checks are the only blockers. The operator enters their name and reason to bypass.

## 5.3 Interception mechanism

### How interceptCommandLong() works

```cpp
bool ArmingGate::interceptCommandLong(uint16_t command, const QMap<int, float> &params)
{
    if (command != m_armCommandCode)        // 400 = ARM_DISARM
        return true;                         // Not an arm command — pass through

    float armParam = params.value(1, 0.0f);
    bool isArm = qFuzzyCompare(armParam, 1.0f);
    if (!isArm)
        return true;                         // Disarm command — pass through

    if (m_overrideActive)
        return true;                         // Override active — pass through

    if (m_ackReceived) {
        m_ackReceived = false;              // One-time bypass consumed
        return true;
    }

    if (m_mode == Passive)
        return true;                         // Passive — pass through

    // Active or Hybrid: evaluate gate
    bool allPassed = m_manager->allMandatoryPassed();
    if (!allPassed) {
        m_denialReason = m_manager->armingBlocker();
        m_armingAllowed = false;
        emit armingDenied(command, m_denialReason);
        return false;                        // BLOCKED
    }

    return true;                             // All checks passed — allow
}
```

### What is GuidedActionsController

`GuidedActionsController` is a QGC upstream class that manages guided flight actions (Arm, Disarm, Takeoff, Land, RTL, etc.). It creates `MAV_CMD_COMPONENT_ARM_DISARM` commands when the user taps Arm and sends them through the vehicle's command interface.

The ArmingGate hooks into this flow by being called before the command reaches the link. If `interceptCommandLong()` returns `false`, the command is suppressed.

### What happens to the blocked command

The blocked command is **dropped** — it is not queued, not returned with an error, and not retried. The `interceptCommandLong()` method returns `false`, and the `GuidedActionsController` does not forward the command to the vehicle. The operator must fix the failing checks and press Arm again.

## 5.4 Override and Force Arm

### Override mechanism

The `ALLOW_WITH_ACK` override policy (`ArmingGate.cpp:181-195`) requires the operator to enter their name and reason:

```cpp
bool ArmingGate::acknowledgeOverride(const QString &pilotName, const QString &reason)
{
    if (pilotName.trimmed().isEmpty()) return false;
    m_ackReceived = true;
    m_lastAck.pilotName = pilotName.trimmed();
    m_lastAck.reason = reason;
    m_lastAck.timestamp = QDateTime::currentDateTime();
    emit overrideAcknowledged(pilotName, reason);
    return true;
}
```

This sets `m_ackReceived = true`. On the next arm attempt, `interceptCommandLong()` checks this flag and allows a one-time bypass. The flag is immediately reset to `false` after use.

### What is logged

When an override is applied, the `gateOverrideLogged` signal is emitted:

```cpp
emit gateOverrideLogged(reason, timeoutSec);
```

This triggers `PreflightManager::persistOverrides()` (`PreflightManager.cpp:959-970`), which writes the override to `QSettings`:

```cpp
void PreflightManager::persistOverrides() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("preflight_overrides/%1").arg(m_sysId));
    for (auto *check : m_checks) {
        if (!check->overrideHistory().isEmpty()) {
            const auto &rec = check->overrideHistory().last();
            QString value = rec.newStatus + QStringLiteral(":") + rec.reason;
            settings.setValue(check->id(), value);
        }
    }
    settings.endGroup();
    settings.sync();
}
```

### Force Arm

`forceArm()` (`ArmingGate.cpp:229-242`) sets `m_overrideActive = true` with no timeout:

```cpp
void ArmingGate::forceArm()
{
    m_overrideActive = true;
    emit overrideActiveChanged(true);
    emit armingOverrideActivated(QStringLiteral("Operator force arm"), 0);
    m_denialReason.clear();
    m_armingAllowed = true;
    emit armingAllowedChanged(true);
    emit denialReasonChanged({});
    emit gateOverrideLogged(QStringLiteral("Operator force arm"), 0);
}
```

Force arm bypasses ALL checks — auto, manual, and safety. The operator must explicitly invoke this from the override dialog. It is logged for audit trail.

### The 10-second timeout risk

The `overrideGate()` method (`ArmingGate.cpp:202-218`) starts a timer:

```cpp
void ArmingGate::overrideGate(const QString &reason, int timeoutSec)
{
    m_overrideActive = true;
    m_overrideTimer->start(timeoutSec * 1000);
    // ...
}
```

**Known issue:** The `m_gateTimer` is created in the constructor (`ArmingGate.cpp:21-23`) but `start()` is never called on it. The timer's `timeout` signal is connected to `updateArmingState()`, but without `start()`, the periodic evaluation never fires. This means the gate state is only updated when `setPreflightManager()` is called (`ArmingGate.cpp:51-52` starts the timer there), not on a regular interval. The `overrideGate()` timer works correctly because it calls `m_overrideTimer->start()` directly.

The 10-second timeout on `overrideGate()` is a safety concern: if the operator overrides with a short timeout, the override expires quickly and the gate re-evaluates. But `forceArm()` has no timeout — the override stays active indefinitely until manually reset via `resetGate()`.

## 5.5 Gate state and the UI

### Footer badge

The `ArmGatePanel.qml` component (loaded by `PreflightChecklistView.qml:837-840`) displays the gate status in the checklist footer:

- **PASSIVE badge**: Gray, no blocking indicators
- **ACTIVE badge**: Green when arming allowed, red when blocked
- **HYBRID badge**: Purple when override available, red when blocked

### Blocker count

The `_blockers` property in `PreflightChecklistView.qml:33` reads:

```qml
readonly property int _blockers: typeof PreflightChecklistModel !== "undefined"
    ? PreflightChecklistModel.blockingFailedCount : 0
```

This is displayed in the blocking banner:

```qml
Text {
    text: "Arming blocked: " + _blockers + " critical item(s) must be fixed"
    color: Colors.error
}
```

### Tapping scrolls to failing check

The "Go to Check" button in the critical issues dialog (`PreflightChecklistView.qml:126-143`) calculates the scroll position:

```qml
MouseArea {
    onClicked: {
        criticalIssuesDialog.close()
        if (chk) {
            var catId = chk.checkCategory
            for (var i = 0; i < _engine.catOrder.length; ++i) {
                if (_engine.catOrder[i] === catId) {
                    var yPos = i * 180
                    checklistFlickable.contentY = Math.min(yPos,
                        checklistFlickable.contentHeight - checklistFlickable.height)
                    break
                }
            }
        }
    }
}
```

---

# Chapter 6 — Motor Test System

## 6.1 The problem it solves

Pre-flight parameter checks verify that the autopilot reports correct motor configuration and calibration. But they cannot detect:

- **Disconnected motor wires** — the autopilot thinks motor 3 is configured, but the wire is loose
- **Reversed motor direction** — the motor spins the wrong way due to ESC wiring
- **Stuck bearings** — the motor physically cannot spin
- **Wrong motor mapping** — motor 2 is physically in motor 4's position

A ground-level motor spin test verifies that each motor actually responds to commands and produces the expected PWM output. This catches physical issues that no software parameter check can detect.

The motor test system runs each motor individually at a safe, low throttle (configurable 1000–1200µs), verifies the response via `SERVO_OUTPUT_RAW` feedback, and records the result for audit.

## 6.2 Hardware backend — HardwareTestController

**File:** `custom/src/controllers/HardwareTestController.h:15-203`

### MAV_CMD_DO_MOTOR_TEST

The motor test command (MAV_CMD value 209) uses these parameters:

| Parameter | Field | Description |
|-----------|-------|-------------|
| param1 | Motor index | 1-based motor number |
| param2 | Throttle type | 0 = % (0-100), 1 = PWM (µs) |
| param3 | Throttle value | PWM in µs (1000-1200) or % (0-25) |
| param4 | Duration | Seconds |
| param5 | Test type | 0 = single motor, 1 = all motors |

### Per-motor PWM values

The safe bench limit is 1200µs. The PWM range is:

| PWM (µs) | Effect |
|----------|--------|
| 1000 | Minimum — motor may not spin |
| 1050 | Idle spin — very slow |
| 1100 | Low spin — safe for bench testing |
| 1150 | Moderate spin — verify response |
| 1200 | Maximum safe bench limit |

The `kMaxThrottlePct` constant is 25% (`HardwareTestController.h:40`), and the `kDefaultThrottlePct` is 5% (`HardwareTestController.h:41`).

### 2-second cooldown between tests

The `kCooldownMs` constant is 2000ms (`HardwareTestController.h:39`). After each motor test completes, the system waits 2 seconds before allowing the next test. This prevents:

- Power supply sag from multiple motors spinning simultaneously
- ESC initialization conflicts
- Thermal stress on motors

The cooldown is managed by `_cooldownTimer` and `_cooldownTick()` (`HardwareTestController.cpp`).

## 6.3 SERVO_OUTPUT_RAW feedback

### How the system verifies motor response

After sending `MAV_CMD_DO_MOTOR_TEST`, the system monitors `SERVO_OUTPUT_RAW` MAVLink messages for the corresponding servo output channel. The `HardwareTestController::_onMavlinkMessage()` handler (`HardwareTestController.cpp`) processes these messages:

```cpp
void HardwareTestController::_onMavlinkMessage(const mavlink_message_t &message) {
    if (message.msgid == MAVLINK_MSG_ID_SERVO_OUTPUT_RAW) {
        mavlink_servo_output_raw_t raw;
        mavlink_msg_servo_output_raw_decode(&message, &raw);
        // Store feedback for each servo
        for (int i = 0; i < kServoCount; ++i) {
            _feedbackPwm[i] = raw.port < 1 ? raw.servo1_raw + (i == 0 ? 0 : ...) : ...;
        }
    }
}
```

### The ±50µs tolerance check

The `kPwmTolerance` constant is 50µs (`HardwareTestController.h:37`). After the test duration + a grace period (`kResultGraceMs = 1500ms`), `_evaluateTestResult()` compares the peak feedback PWM against the expected PWM:

```cpp
bool HardwareTestController::_verifyMotorFeedback(int motorInstance) const {
    int idx = motorInstance - 1;
    uint16_t peak = _peakFeedbackPwm[idx];
    int expected = _motorPwmValues[idx];
    return qAbs(peak - expected) <= kPwmTolerance;
}
```

If the peak feedback is within ±50µs of the expected PWM, the motor passes.

### Timeout handling

If no `SERVO_OUTPUT_RAW` message is received within `kFeedbackTimeoutMs` (2000ms) + `kFeedbackTimeoutMarginMs` (1000ms), the motor test times out and is marked as Fail with "No signal" message.

## 6.4 The UI — MotorCheckPanel.qml

**File:** `custom/qml/cpts/MotorCheckPanel.qml:1-648`

### Three-layer structure

1. **Inline launcher tile** (lines 9-113): Compact single-row tile showing motor count badge, vehicle type, and "Open Motor Test" button. Appears inside the checklist card grid.

2. **Main motor dialog** (lines 118-604): Modal dialog with draggable title bar, per-motor card grid, shared duration slider, cooldown indicator, and summary/confirm buttons.

3. **Safety confirmation dialog** (lines 607-647): First-time confirmation dialog warning about propeller clearance. Appears only on the first test (`firstTestDone` flag).

### Motor count drives the Repeater

```qml
GridLayout {
    columns: root._mc <= 4 ? 2 : (root._mc <= 6 ? 3 : 4)
    Repeater {
        model: root._mc > 0 ? root._mc : 0
        // Per-motor card
    }
}
```

### 5 button states

| State | Visual | Behavior |
|-------|--------|----------|
| Idle | Default color, "TEST MX" text | Clickable — starts test |
| Testing | Pulsing opacity animation, "TESTING..." text | Not clickable |
| Pass | Green background, "PASS" text | Not clickable — result shown |
| Fail | Red background, "FAIL" text | Not clickable — result shown |
| Cooldown | Gray border, "WAIT..." text | Not clickable — countdown |

### Position grid for Quad-X

The `_positionForMotor()` method (`HardwareTestController.cpp`) maps motor indices to physical positions:

| Motor | Position |
|-------|----------|
| 1 | FR (Front Right) |
| 2 | FL (Front Left) |
| 3 | BR (Back Right) |
| 4 | BL (Back Left) |

This is displayed in each motor card header: `Text { text: "(" + p + ")" }`.

## 6.5 Fixed-wing motor test: arm → RC_CHANNELS_OVERRIDE → disarm

Fixed-wing aircraft cannot use `MAV_CMD_DO_MOTOR_TEST` because their motors are wired directly to throttle outputs without individual ESC control via MAVLink. The `HardwareTestController` implements an alternative flow using RC channel overrides:

### The fixed-wing state machine

```
FwIdle → FwArming → FwSpinning → FwStopping → FwDisarming → FwIdle
```

| Phase | Action | Trigger |
|-------|--------|---------|
| `FwIdle` | Ready | User clicks "TEST MX" |
| `FwArming` | Send arm command | `testMotor()` called |
| `FwSpinning` | Send `RC_CHANNELS_OVERRIDE` with throttle PWM | Arm ACK received |
| `FwStopping` | Send zero throttle | `_fwTestTimer` fires (duration elapsed) |
| `FwDisarming` | Send disarm command | `_fwDisarmTimer` fires (2s after stop) |

### RC_CHANNELS_OVERRIDE for throttle

```cpp
void HardwareTestController::_fwSendRcOverride(uint16_t throttlePwm) {
    mavlink_message_t msg;
    mavlink_msg_rc_channels_override_pack_chan(
        _vehicle->mavlinkChannel(),
        MAV_COMP_ID_MISSIONPLANNER,
        _vehicle->id(),
        &msg,
        _vehicle->id(),        // target_system
        MAV_COMP_ID_MISSIONPLANNER, // target_component
        0, 0, 0, 0, 0, 0, 0, 0,
        throttlePwm,  // chan8 = throttle (varies by RC map)
        0, 0, 0, 0, 0, 0, 0
    );
    _vehicle->sendMessageOnLinkThreadSafe(_vehicle->priorityLink(), msg);
}
```

The throttle channel is determined by `_fwThrottleChannel()`, which reads the RC_MAP_THROTTLE parameter (default: channel 3, 0-indexed → param index 7 in the override message).

### Fixed-wing vs multirotor: key differences

| Aspect | Multirotor | Fixed-wing |
|--------|-----------|------------|
| Command | `MAV_CMD_DO_MOTOR_TEST` | `RC_CHANNELS_OVERRIDE` |
| Per-motor control | Yes (individual ESC) | No (single throttle output) |
| Arming required | No | Yes (must arm to spin motor) |
| PWM range | 1000–1200µs | 1000–1800µs (full range) |
| Feedback source | `SERVO_OUTPUT_RAW` | `RC_CHANNELS` echo |
| Safety | Motors spin at low power | Full throttle range available |

## 6.6 Safety interlocks and edge cases

### Armed state check

The motor test is blocked when the vehicle is armed:

```cpp
void HardwareTestController::_onArmedChanged() {
    _isArmed = _vehicle->armed();
    emit isArmedChanged();
    if (_isArmed && _activeMotor >= 0) {
        stopAll();  // Abort if armed during test
    }
}
```

If the vehicle arms while a motor test is running, `stopAll()` immediately sends stop commands to all motors and transitions to `Idle` state.

### Parameter timeout

When `setVehicle()` is called, a 10-second parameter timeout (`kParamTimeoutMs = 10000`) starts. If the vehicle's parameters are not ready within this window, `_onParamTimeout()` fires and the motor count defaults to the frame class default (4 for quad, 6 for hex, etc.).

### Cooldown enforcement

The 2-second cooldown (`kCooldownMs = 2000`) is enforced between every motor test. During cooldown:
- The `_cooldownTimer` fires every 250ms (`_cooldownTick()`)
- `_cooldownRemaining` decrements from 8 to 0 (8 × 250ms = 2000ms)
- The UI shows "WAIT..." with a progress indicator
- `cooldownMsRemaining` property returns `_cooldownRemaining * 250` for the QML countdown display

### Result evaluation grace period

After the test duration elapses, a 1.5-second grace period (`kResultGraceMs = 1500`) allows the ESC and motor to spin down before evaluating the peak feedback. This prevents false negatives from transient PWM spikes during spindown.

### VTOL hybrid motor count

For VTOL vehicles, `resolveMotorCount()` reads both `frame_class` and the parameter `VT_MOT_COUNT` (if present) to distinguish hover motors from forward motors. The hover motor count drives the motor test grid — only hover motors are testable via `MAV_CMD_DO_MOTOR_TEST`.

## 6.7 Audit logging

Every motor test result is logged to the `motor_test_results` table:

```cpp
bool DatabaseManager::logMotorTestResult(int vehicleSysId, int motorIndex,
    int throttlePct, int durationSec, int expectedPwm, int actualPwm,
    int pwmDelta, const QString &result)
```

Fields written:
- `vehicle_sys_id` — vehicle system ID
- `motor_index` — 1-based motor number
- `throttle_pct` — throttle percentage
- `duration_sec` — test duration
- `expected_pwm` — commanded PWM (µs)
- `actual_pwm` — peak feedback PWM (µs)
- `pwm_delta` — difference between expected and actual
- `result` — "pass" or "fail"
- `timestamp` — UTC ISO 8601

The `_logTestResult()` private method (`HardwareTestController.cpp`) wraps this call and is invoked by `_evaluateTestResult()` after each motor test completes.

### Querying motor test history

```sql
SELECT motor_index, expected_pwm, actual_pwm, pwm_delta, result, timestamp
FROM motor_test_results
WHERE vehicle_sys_id = ?
ORDER BY timestamp DESC;
```

This data can be queried via `getHardwareTestEvents(flightId)` to review pre-flight motor health history and identify degrading motors over time.

---

# Chapter 7 — Weather Integration

## 7.1 Why weather matters for UAV operations

Onboard sensors can detect battery voltage, GPS fix, IMU health, and motor response. They cannot detect:

- **Wind speed and gusts** — a multirotor may be unable to maintain position in 15+ m/s winds
- **Visibility** — fog, haze, or heavy rain reduces operator visual line-of-sight
- **Ceiling** — low cloud base may violate regulatory minimums (e.g., FAA Part 107.51: 500ft below clouds)
- **Precipitation** — rain, snow, or ice can damage electronics and reduce motor performance
- **Temperature extremes** — LiPo batteries lose capacity below 0°C and can overheat above 45°C
- **TAF deterioration** — forecast conditions may worsen during the planned flight duration

METAR (Meteorological Aerodrome Report) and TAF (Terminal Aerodrome Forecast) are the standard aviation weather formats. Open-Meteo provides additional forecast data when METAR stations are unavailable.

## 7.2 Data sources

### METAR (NOAA ADDS)

**Endpoint:** `https://aviationweather.gov/api/data/metar`  
**Parameters:** `ids={ICAO}&format=json&hours=1`

METAR provides current observations: wind speed/direction/gust, visibility, ceiling, temperature, dewpoint, precipitation codes, and flight category (VFR/MVFR/IFR/LIFR).

The METAR parser (`WeatherProvider::handleMetarJson()`, `WeatherProvider.cpp:189-285`) extracts:
- Wind: knots → m/s conversion (`* 0.514444`)
- Visibility: statute miles → km conversion (`* 1.60934`)
- Ceiling: feet AGL from cloud layer data
- Humidity: computed from temperature and dewpoint using the Magnus formula
- Precipitation: regex matching of raw METAR weather codes

### TAF (NOAA ADDS)

**Endpoint:** `https://aviationweather.gov/api/data/taf`  
**Parameters:** `ids={ICAO}&format=json&hours=6`

TAF provides forecast data. The parser (`WeatherProvider::handleTafJson()`, `WeatherProvider.cpp:356-429`) checks for deterioration indicators:
- Multiple change groups (TEMPO, FM, PROB30/40/50) indicate instability
- IFR/LIFR conditions or low ceilings (OVC/BKN below 1000ft)
- Convection (thunderstorms, heavy rain, snow)
- Strong winds (sustained >20kt or gust >25kt)

### Open-Meteo API

**Endpoint:** `https://api.open-meteo.com/v1/forecast`  
**Parameters:** `latitude={lat}&longitude={lon}&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m,visibility&timezone=auto`

Open-Meteo provides current weather by GPS coordinates. Used when the operator has not configured a default ICAO station.

### ICAO station lookup

**Endpoint:** `https://aviationweather.gov/api/data/station`  
**Parameters:** `lat={lat}&lon={lon}&radius=50`

The `lookupIcao()` method (`WeatherProvider.cpp:549-589`) finds the nearest METAR station within 50nm of the vehicle's GPS position. Uses a synchronous `QEventLoop` with 3-second timeout — this is a **known blocking call** (see Chapter 12).

## 7.3 WeatherProvider implementation

### Async HTTP request

All weather fetches are asynchronous:

```cpp
void WeatherProvider::fetchWeather(double latitude, double longitude) {
    m_loading = true;
    emit loadingChanged();

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_loading = false;
        // Parse response...
        emit weatherUpdated();
    });
}
```

### Caching

`WeatherProvider` uses `QNetworkDiskCache` with a 50MB limit (`WeatherProvider.cpp:25-31`):

```cpp
m_cache = new QNetworkDiskCache(this);
m_cache->setCacheDirectory(cacheDir);
m_cache->setMaximumCacheSize(50 * 1024 * 1024);
m_nam.setCache(m_cache);
```

Network requests use `PreferCache` attribute, so cached responses are returned immediately without network access.

### METAR freshness

```cpp
bool WeatherProvider::metarFresh() const {
    if (!m_metarTimestamp.isValid()) return false;
    return m_metarTimestamp.secsTo(QDateTime::currentDateTimeUtc()) < 3600; // 60 min
}
```

METAR data is considered fresh for 60 minutes after the observation time.

### Known issue: silent timeout

When the network is unavailable, HTTP requests time out after 4 seconds (`req.setTransferTimeout(4000)`). The error is stored in `m_lastError` but the UI does not display it prominently. The weather check in `WeatherWindCheck::evaluate()` waits 10 seconds for a response before giving up, but the visual feedback is just "Fetching METAR data..." — there is no explicit "Network unavailable" message shown to the operator.

## 7.4 Weather checks in the checklist

| Check | ID | Reads | Thresholds | WARN vs FAIL |
|-------|-----|-------|------------|--------------|
| WeatherWindCheck | `env.wind` | `windSpeed`, `windGust`, `windDirection` | `windThresholdSustained`, `windThresholdGust` | >threshold = WARN, >1.25×threshold = FAIL |
| WeatherWindGustCheck | `env.wind.gust` | `windGust` | `windThresholdGust` | >threshold = WARN, >1.25× = FAIL |
| MetarVisibilityCheck | `env.metar.visibility` | `visibilityKm` | Configurable min (default 5km) | <threshold = WARN, <0.6× = FAIL |
| MetarCeilingCheck | `env.metar.ceiling` | `ceilingFt` | Configurable min (default 1500ft) | <threshold = WARN, <0.6× = FAIL |
| MetarPrecipitationCheck | `env.metar.precipitation` | `precipitation` | Any precipitation code | Any = WARN, heavy = FAIL |
| MetarTemperatureCheck | `env.metar.temperature` | `temperature` | -10°C to 45°C | Outside = WARN, extreme = FAIL |
| TafDeteriorationCheck | `env.taf.deterioration` | `tafDeteriorating`, `tafSummary` | Deterioration indicators | Deteriorating = WARN, IFR/LIFR = FAIL |

All weather checks are `CheckType::Auto` with `mandatory = false` (warning-only). They do not block arming but provide valuable operator awareness.

## 7.5 The Magnus formula for humidity computation

METAR reports temperature and dewpoint but not relative humidity. The `WeatherProvider` computes humidity using the Magnus formula:

```cpp
// WeatherProvider.cpp:230-240 (inside handleMetarJson)
double alpha = std::log(m_humidity_raw); // placeholder
double tC = m_temperature;
double tdC = m_dewpoint;
double a = 17.27 * tC / (237.7 + tC);
double b = 17.27 * tdC / (237.7 + tdC);
m_humidity = 100.0 * std::exp(b - a);
```

The formula converts temperature and dewpoint to saturation vapor pressures:

1. Compute `α = 17.27 × T / (237.7 + T)` for both temperature and dewpoint
2. Compute `humidity = 100 × exp(α_dewpoint − α_temperature)`

This gives relative humidity as a percentage. The result feeds into `MetarTemperatureCheck` — high humidity combined with high temperature indicates heat stress risk for batteries and ESCs.

## 7.6 TAF parsing in detail

The TAF parser (`WeatherProvider::handleTafJson()`) processes forecast change groups:

### Change group types

| Group | Meaning | Example |
|-------|---------|---------|
| `FM` | From (permanent change at time) | `FM1500` — from 15:00Z |
| `TEMPO` | Temporary fluctuation | `TEMPO 1800-2200` |
| `BECMG` | Becoming (gradual change) | `BECMG 1600` |
| `PROB30` | 30% probability | `PROB30 1400-1800` |
| `PROB40` | 40% probability | `PROB40 2000-0000` |

### Deterioration detection algorithm

```cpp
// WeatherProvider.cpp:370-420
for (const auto &group : tafGroups) {
    if (group.contains("TEMPO") || group.contains("PROB")) {
        m_tafDeteriorating = true;
    }
    // Check for IFR/LIFR in any group
    if (group.contains(" OVC") || group.contains(" BKN")) {
        int ceiling = extractCeiling(group);
        if (ceiling < 1000) {
            m_tafDeteriorating = true;
            m_tafSummary.append(QString("Low ceiling: %1ft").arg(ceiling));
        }
    }
    // Check for convection
    if (group.contains("TS") || group.contains("RA") || group.contains("SN")) {
        m_tafDeteriorating = true;
    }
}
```

When deterioration is detected, the `TafDeteriorationCheck` sets status to `Warning` with a summary of all deteriorating conditions.

## 7.7 Weather refresh timer

`WeatherProvider` includes a refresh timer that periodically re-fetches weather data:

```cpp
// In PreflightPlugin::init()
_weatherRefreshTimer = new QTimer(this);
_weatherRefreshTimer->setInterval(5 * 60 * 1000); // 5 minutes
connect(_weatherRefreshTimer, &QTimer::timeout, this, [this]() {
    if (_vehicle) {
        _weatherProvider->fetchWeather(
            _vehicle->latitude(), _vehicle->longitude());
    }
});
_weatherRefreshTimer->start();
```

The 5-minute refresh interval balances data freshness against network usage. METAR observations are typically issued every 30 minutes, so 5-minute refreshes catch new observations quickly.

## 7.8 Integration with the preflight system

Weather data flows into the preflight system through two paths:

1. **Direct property binding** — QML checks read `WeatherProvider` properties directly:
   ```qml
   property real windSpeed: WeatherProvider ? WeatherProvider.windSpeed : 0.0
   property real windGust: WeatherProvider ? WeatherProvider.windGust : 0.0
   ```

2. **TelemetryBridge forwarding** — `TelemetryBridge` exposes weather as telemetry:
   ```cpp
   // TelemetryBridge.cpp
   Q_PROPERTY(double windSpeed READ windSpeed NOTIFY windSpeedChanged)
   // Wind data forwarded from WeatherProvider
   ```

The `WeatherWindCheck` uses the TelemetryBridge path, reading `windSpeed` and `windGust` from the telemetry stream rather than directly from WeatherProvider. This ensures consistent data access across all check types.

---

# Chapter 8 — Vehicle Auto-Detection

## 8.1 The fingerprinting mechanism

### What data is read

When a vehicle connects, `VehicleRegistry::_extractVehicleInfo()` (`VehicleRegistry.cpp:106-137`) reads:

- **`uid`** (hardware UID): From `vehicle->vehicleUID()` — a persistent identifier burned into the flight controller's hardware
- **`board_version`**: From `vehicle->firmwareBoardProductId()` — identifies the specific board model
- **`autopilotType`**: From `vehicle->firmwareType()` — PX4, ArduPilot, or Generic
- **`firmwareVersion`**: From `vehicle->firmwareMajorVersion()`, `MinorVersion()`, `PatchVersion()`

### How the SHA-256 fingerprint is computed

```cpp
QString VehicleRegistry::generateFingerprint(quint64 uid, int autopilotType,
                                              const QString &boardVersion)
{
    QByteArray data;
    data.append(QString::number(uid).toUtf8());
    data.append(QByteArray::number(autopilotType));
    data.append(boardVersion.toUtf8());
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
```

The fingerprint is `SHA256(uid + autopilotType + boardVersion)`.

### Why this is stable across firmware updates

The hardware UID is a permanent property of the flight controller chip — it does not change with firmware updates. The board version is a hardware identifier. Only the autopilot type could change (e.g., switching from PX4 to ArduPilot), but this is rare in practice. The fingerprint therefore remains stable across firmware version updates, parameter resets, and configuration changes.

## 8.2 First connection vs returning vehicle

### First connection

When `_onVehicleAdded()` fires (`VehicleRegistry.cpp:39-73`):

1. Extract vehicle info and compute fingerprint
2. `DatabaseManager::lookupVehicleByFingerprint(fingerprint)` → returns empty (not in DB)
3. Auto-generate display name: `"UAV-{sysid}-{fingerprint_left_8}"`
4. `DatabaseManager::registerNewVehicle(fingerprint, ...)` → INSERT into `vehicles` table
5. Emit `newVehicleRegistered(vehicleId)`
6. `PreflightPlugin::_onNewVehicleRegistered()` saves initial compass orientation config

### Returning vehicle

1. Extract vehicle info and compute fingerprint
2. `DatabaseManager::lookupVehicleByFingerprint(fingerprint)` → returns JSON with vehicle data
3. Set `m_isKnownVehicle = true`, load `m_vehicleName` from DB
4. `DatabaseManager::updateVehicleLastSeen(fingerprint)` → UPDATE timestamp
5. Emit `knownVehicleConnected(vehicleId)`
6. `PreflightPlugin::_onKnownVehicleConnected()` loads vehicle config from `vehicle_config` table

## 8.3 Vehicle-specific profiles

### How check overrides are stored

The `vehicle_config` table stores per-vehicle JSON configuration:

```sql
CREATE TABLE vehicle_config (
    fingerprint TEXT PRIMARY KEY REFERENCES vehicles(fingerprint),
    config_json TEXT NOT NULL DEFAULT '{}',
    updated_at TEXT DEFAULT CURRENT_TIMESTAMP
);
```

The `config_json` contains check-specific overrides keyed by check ID:

```json
{
    "nav.compass.orientation": {
        "expectedRotation": 4
    },
    "power.battery.voltage": {
        "minVoltage": 14.0
    }
}
```

### How CompassOrientationCheck gets its expected rotation

When a new vehicle is registered (`PreflightPlugin::_onNewVehicleRegistered()`, `PreflightPlugin.cpp:330-365`):

```cpp
AbstractCheck *compassCheck = _preflightManager->checkById("nav.compass.orientation");
QString cv = compassCheck->getCurrentValueString();
QRegularExpression re("\\((\\d+)\\)$");
auto match = re.match(cv);
if (match.hasMatch()) {
    int rot = match.captured(1).toInt();
    if (rot > 0) {
        compassConfig["expectedRotation"] = rot;
        config["nav.compass.orientation"] = compassConfig;
        DatabaseManager::instance().saveVehicleConfig(fingerprint, configJson);
    }
}
```

The current compass rotation is read, and if it's non-zero, it's saved as the expected rotation for future checks.

### How motor count profile ties into check visibility

The `vehicles` table stores `motor_count`, `frame_class`, and `frame_type` columns (added in schema v8). These are populated by `upsertVehicleEx()` and read by `HardwareTestController::resolveMotorCount()` to determine the vehicle's motor configuration, which drives the motor test grid layout and check relevance.

---

# Chapter 9 — Database and Audit Trail

## 9.1 Why SQLite

SQLite was chosen for these reasons:

- **No server dependency**: No need to run PostgreSQL, MySQL, or any database server. The database is a single file on disk
- **Embedded**: Linked directly into the application via Qt's `QSQLITE` driver
- **Portable to Jetson**: Works identically on x86_64 (dev box) and aarch64 (Jetson Orin Nano)
- **Schema-versioned**: The `schema_version` table enables forward-compatible migrations
- **WAL mode**: SQLite supports Write-Ahead Logging for concurrent reads during writes

The database file location is:
```
{QStandardPaths::AppDataLocation}/uav_preflight_data.db
```

## 9.2 Schema — all tables

### schema_version

```sql
CREATE TABLE schema_version (
    version INTEGER PRIMARY KEY
);
```

Tracks the current schema version (currently 8). Used by `migrateSchema()` to apply forward migrations.

### checklist_templates

```sql
CREATE TABLE checklist_templates (
    template_id TEXT PRIMARY KEY,
    vehicle_type TEXT NOT NULL,
    template_name TEXT NOT NULL,
    template_version TEXT DEFAULT '1.0',
    items_json TEXT NOT NULL,
    is_default INTEGER DEFAULT 0,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    modified_at TEXT DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(vehicle_type, template_name)
);
```

Stores custom checklist templates. `items_json` contains the template definition as a JSON array.

### compliance_logs

```sql
CREATE TABLE compliance_logs (
    log_id TEXT PRIMARY KEY,
    vehicle_id TEXT NOT NULL,
    vehicle_type TEXT NOT NULL,
    operator_id TEXT,
    operator_name TEXT,
    started_at TEXT,
    completed_at TEXT,
    overall_verdict TEXT,
    checklist_json TEXT NOT NULL,
    telemetry_snapshot TEXT,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP
);
```

Stores completed checklist compliance records. `checklist_json` is the full checklist state at completion. `telemetry_snapshot` is a JSON snapshot of all telemetry values.

### hardware_test_events

```sql
CREATE TABLE hardware_test_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    flight_id INTEGER NOT NULL,
    timestamp TEXT NOT NULL,
    checklist_item_id TEXT DEFAULT 'hardware_servo_test',
    step_name TEXT NOT NULL,
    servo_instance INTEGER NOT NULL,
    target_pwm INTEGER,
    feedback_pwm INTEGER,
    tolerance_min INTEGER,
    tolerance_max INTEGER,
    operator_confirmed INTEGER,
    result TEXT NOT NULL,
    failure_reason TEXT,
    operator_id TEXT,
    duration_ms INTEGER
);
```

Logs individual hardware test steps (servo actuator testing).

### maintenance_components

```sql
CREATE TABLE maintenance_components (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    type TEXT NOT NULL,
    max_hours REAL NOT NULL DEFAULT 0,
    current_hours REAL NOT NULL DEFAULT 0,
    max_cycles INTEGER NOT NULL DEFAULT 0,
    current_cycles INTEGER NOT NULL DEFAULT 0,
    last_maintenance TEXT,
    notes TEXT
);
```

Tracks component maintenance (motors, ESCs, batteries, propellers) with hours and cycle counts.

### vehicles

```sql
CREATE TABLE vehicles (
    device_uid TEXT PRIMARY KEY,
    friendly_name TEXT NOT NULL DEFAULT '',
    autopilot_type TEXT NOT NULL DEFAULT '',
    airframe_type TEXT NOT NULL DEFAULT '',
    first_seen TEXT NOT NULL,
    last_seen TEXT NOT NULL,
    total_flight_count INTEGER NOT NULL DEFAULT 0,
    total_flight_hours REAL NOT NULL DEFAULT 0.0,
    identity_source TEXT NOT NULL DEFAULT 'hardware_uid',
    fingerprint TEXT,
    compid INTEGER DEFAULT 0,
    firmware_version TEXT DEFAULT '',
    board_version TEXT DEFAULT '',
    vehicle_uuid TEXT DEFAULT '',
    frame_class INTEGER DEFAULT -1,
    frame_type INTEGER DEFAULT -1,
    motor_count INTEGER DEFAULT 0,
    motor_layout TEXT DEFAULT '',
    param_snapshot_path TEXT DEFAULT '',
    last_preflight_status TEXT DEFAULT '',
    gps_latitude REAL DEFAULT 0.0,
    gps_longitude REAL DEFAULT 0.0,
    pilot_name TEXT DEFAULT '',
    notes TEXT DEFAULT '',
    thumbnail TEXT DEFAULT ''
);
```

Vehicle fleet registry. `device_uid` is the hardware UID. `fingerprint` is the SHA-256 hash.

### batteries

```sql
CREATE TABLE batteries (
    serial_number TEXT PRIMARY KEY,
    operator_label TEXT NOT NULL DEFAULT '',
    first_seen TEXT NOT NULL,
    last_seen TEXT NOT NULL,
    total_cycles INTEGER NOT NULL DEFAULT 0,
    identity_source TEXT NOT NULL DEFAULT 'operator_confirmed'
);
```

Battery fleet registry. Separate from vehicles because batteries are swapped between aircraft.

### battery_cycles

```sql
CREATE TABLE battery_cycles (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    battery_serial TEXT NOT NULL REFERENCES batteries(serial_number),
    flight_session_id INTEGER NOT NULL DEFAULT 0,
    capacity_at_full_mah REAL NOT NULL DEFAULT 0.0,
    voltage_sag_v REAL NOT NULL DEFAULT 0.0,
    resting_voltage_v REAL NOT NULL DEFAULT 0.0,
    cycle_count INTEGER NOT NULL DEFAULT 0,
    recorded_at TEXT NOT NULL
);
```

Individual battery cycle records for health trend analysis.

### flight_sessions

```sql
CREATE TABLE flight_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_uid TEXT NOT NULL REFERENCES vehicles(device_uid),
    battery_serial TEXT REFERENCES batteries(serial_number),
    started_at TEXT NOT NULL,
    ended_at TEXT,
    duration_seconds REAL NOT NULL DEFAULT 0.0,
    payload_weight_kg REAL NOT NULL DEFAULT 0.0,
    location_name TEXT DEFAULT '',
    plan_lat REAL DEFAULT 0.0,
    plan_lon REAL DEFAULT 0.0,
    energy_consumed_wh REAL DEFAULT 0.0,
    distance_m REAL DEFAULT 0.0
);
```

Flight session records. Created when the arming gate opens, updated when it closes.

### check_results

```sql
CREATE TABLE check_results (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_uid TEXT NOT NULL REFERENCES vehicles(device_uid),
    flight_session_id INTEGER NOT NULL DEFAULT 0,
    check_id TEXT NOT NULL,
    status TEXT NOT NULL,
    message TEXT DEFAULT '',
    evaluated_at TEXT NOT NULL
);
```

Audit trail for every check evaluation result.

### check_config

```sql
CREATE TABLE check_config (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id INTEGER,
    check_id TEXT NOT NULL,
    key TEXT NOT NULL,
    value TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
```

Per-check configurable thresholds. Read by `AbstractCheck::configDouble()`.

### motor_test_results

```sql
CREATE TABLE motor_test_results (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_sys_id INTEGER NOT NULL,
    motor_index INTEGER NOT NULL,
    throttle_pct INTEGER NOT NULL,
    duration_sec INTEGER NOT NULL,
    expected_pwm INTEGER NOT NULL,
    actual_pwm INTEGER NOT NULL,
    pwm_delta INTEGER NOT NULL,
    result TEXT NOT NULL,
    timestamp TEXT NOT NULL
);
```

Motor test audit trail.

### vehicle_config

```sql
CREATE TABLE vehicle_config (
    fingerprint TEXT PRIMARY KEY REFERENCES vehicles(fingerprint),
    config_json TEXT NOT NULL DEFAULT '{}',
    updated_at TEXT DEFAULT CURRENT_TIMESTAMP
);
```

Per-vehicle check configuration overrides.

## 9.3 Schema migrations and versioning

The database uses a versioned migration system. The `schema_version` table stores the current version, and `migrateSchema()` applies forward migrations incrementally:

### Migration history

| Version | Changes | File |
|---------|---------|------|
| 1 | Initial schema — templates, compliance_logs, hardware_test_events | `DatabaseManager.cpp` |
| 2 | Add maintenance_components table | `DatabaseManager.cpp` |
| 3 | Add vehicles table with fingerprint | `DatabaseManager.cpp` |
| 4 | Add batteries and battery_cycles tables | `DatabaseManager.cpp` |
| 5 | Add flight_sessions table | `DatabaseManager.cpp` |
| 6 | Add check_results and check_config tables | `DatabaseManager.cpp` |
| 7 | Add motor_test_results table | `DatabaseManager.cpp` |
| 8 | Add vehicle_config table; add frame_class, frame_type, motor_count to vehicles | `DatabaseManager.cpp` |

### Migration pattern

```cpp
bool DatabaseManager::migrateSchema() {
    int current = storedSchemaVersion();
    QSqlQuery q(m_db);

    if (current < 2) {
        q.exec("CREATE TABLE IF NOT EXISTS maintenance_components (...)");
    }
    if (current < 3) {
        q.exec("CREATE TABLE IF NOT EXISTS vehicles (...)");
    }
    // ... through version 8

    q.exec("INSERT OR REPLACE INTO schema_version VALUES (8)");
    return true;
}
```

Each migration is idempotent (`CREATE TABLE IF NOT EXISTS`), so running against an already-migrated database is safe. The version number is bumped at the end of each migration batch.

### Why not use a migration library

SQLite's simplicity means raw SQL migrations are sufficient. A migration library (like Qt's `QSqlMigration`) would add a dependency without meaningful benefit — the schema is small, the migration count is low, and the upgrade path is linear (no branching or rollback).

## 9.4 WAL mode and connection pooling

### Write-Ahead Logging

The database is opened in WAL mode for concurrent read/write performance:

```cpp
// DatabaseManager.cpp
q.exec("PRAGMA journal_mode=WAL");
```

WAL mode allows:
- **Concurrent reads** during writes (readers don't block writers)
- **Write batching** — multiple inserts are grouped into a single WAL checkpoint
- **Crash recovery** — the WAL provides atomic commit semantics

### Single connection constraint

Qt's SQLite driver requires that only one `QSqlDatabase` connection with the same connection name exists at a time. `DatabaseManager` uses the default connection name `"qt_sql_default_connection"`, enforced by the singleton pattern:

```cpp
DatabaseManager &DatabaseManager::instance() {
    static DatabaseManager s_instance;
    return s_instance;
}
```

All database operations go through this single instance. There is no connection pooling — all queries run on the main thread. This is acceptable because:
- SQLite operations complete in microseconds (not milliseconds)
- The 1-second evaluation tick bounds the maximum query rate
- The config cache (`m_configCache` on `AbstractCheck`) eliminates redundant queries

## 9.5 A flight in the database

Here is the complete sequence of database writes for a single flight:

| Step | Table | Operation | Key Fields |
|------|-------|-----------|------------|
| 1. Vehicle connects | `vehicles` | UPSERT | `device_uid`, `fingerprint`, `first_seen`, `last_seen` |
| 2. Vehicle config loaded | `vehicle_config` | SELECT | `fingerprint`, `config_json` |
| 3. Gate opens | `flight_sessions` | INSERT | `device_uid`, `battery_serial`, `started_at` |
| 4. Each check evaluation | `check_results` | INSERT | `device_uid`, `flight_session_id`, `check_id`, `status`, `message` |
| 5. Motor test | `motor_test_results` | INSERT | `vehicle_sys_id`, `motor_index`, `expected_pwm`, `actual_pwm`, `result` |
| 6. Battery cycle | `battery_cycles` | INSERT | `battery_serial`, `flight_session_id`, `cycle_count` |
| 7. Gate closes | `flight_sessions` | UPDATE | `ended_at`, `duration_seconds`, `energy_consumed_wh`, `distance_m` |
| 8. Vehicle disconnects | `vehicles` | UPDATE | `last_seen`, `total_flight_count`, `total_flight_hours` |

## 9.5.1 CalibratedPowerModel

The `getCalibratedPowerModel()` method computes average energy consumption from completed flight sessions:

```cpp
CalibratedPowerModel DatabaseManager::getCalibratedPowerModel(
    const QString &deviceUid, int minSessions) {
    QSqlQuery q(m_db);
    q.prepare("SELECT AVG(energy_consumed_wh / NULLIF(distance_m, 0) * 1000.0), "
              "COUNT(*) FROM flight_sessions "
              "WHERE device_uid = ? AND ended_at IS NOT NULL AND distance_m > 0");
    q.addBindValue(deviceUid);
    q.exec();
    // ...
}
```

The model returns:
- `whPerKm` — watt-hours per kilometer (energy efficiency)
- `hoverWhPerMin` — estimated hover power consumption
- `dataPointCount` — number of sessions used for calibration
- `isCalibrated` — true if `dataPointCount >= minSessions` (default 5)

This powers the `MissionEnergyCheck`, which estimates whether the battery has sufficient energy for the planned mission distance.

## 9.5.2 Vehicle export/import

`DatabaseManager` provides JSON-based fleet export and import:

```cpp
// Export all vehicles
QString DatabaseManager::exportVehiclesJson() {
    QSqlQuery q(m_db);
    q.exec("SELECT device_uid, friendly_name, autopilot_type, airframe_type, "
           "first_seen, last_seen, total_flight_count, total_flight_hours, "
           "fingerprint, firmware_version, board_version "
           "FROM vehicles ORDER BY first_seen");
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["device_uid"] = q.value(0).toString();
        obj["friendly_name"] = q.value(1).toString();
        // ... serialize all fields
        arr.append(obj);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

// Import vehicles from JSON
bool DatabaseManager::importVehiclesJson(const QString &json) {
    QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const auto &val : arr) {
        QJsonObject obj = val.toObject();
        upsertVehicleEx(
            obj["device_uid"].toString(),
            obj["friendly_name"].toString(),
            obj["autopilot_type"].toString(),
            obj["airframe_type"].toString(),
            obj["frame_class"].toInt(-1),
            obj["frame_type"].toInt(-1),
            obj["motor_count"].toInt(0));
    }
    return true;
}
```

The export/import cycle enables fleet migration between GCS instances (e.g., dev box → Jetson) without losing vehicle profiles, flight history, or check configuration.

## 9.5.3 Battery health trend

`getBatteryHealthTrend()` queries the last N battery cycles to compute capacity degradation:

```sql
SELECT capacity_at_full_mah, voltage_sag_v, resting_voltage_v, recorded_at
FROM battery_cycles
WHERE battery_serial = ?
ORDER BY recorded_at DESC
LIMIT 20;
```

The trend data enables the `BatteryHealthCheck` to warn when capacity drops below 80% of the battery's rated capacity — a standard indicator for LiPo battery retirement.

### The performance problem

`AbstractCheck::configDouble()` (`AbstractCheck.cpp:230-244`) calls `DatabaseManager::instance().getCheckConfig(m_id, key)` to read configurable thresholds from the `check_config` table. This is called inside `evaluate()` for checks that use configurable thresholds.

With 91 checks evaluated every 1 second, and each check calling `configDouble()` 1-3 times, this results in 91-273 SQLite `SELECT` queries per second — all blocking the main thread.

### The fix (config cache on AbstractCheck)

The fix is already implemented: `m_configCache` (`AbstractCheck.h:204`):

```cpp
mutable QHash<QString, QVariant> m_configCache;
```

The first call to `configDouble()` queries SQLite and caches the result. Subsequent calls return from the cache. The cache is cleared by `clearConfigCache()` when DB config changes.

However, the cache is per-check-instance and is only cleared on `reset()` or explicit `clearConfigCache()` calls. If config values are changed externally (e.g., via the settings UI), the stale cache may return old values until the next `reset()`.

---

# Chapter 10 — Telemetry Bridge

## 10.1 The bridging pattern

### Why QML can't read MAVLink directly

QML is a declarative UI language with a JavaScript runtime. It cannot:

- Open UDP/TCP sockets for MAVLink communication
- Parse binary MAVLink message frames
- Handle protocol-level concerns (message routing, component IDs, CRC checks)
- Access C++ pointers to `Vehicle` objects directly

MAVLink communication requires the full QGC Vehicle API, which is a complex C++ class hierarchy with Fact-based parameter management, mission handling, and link abstraction.

### The adapter pattern

`TelemetryBridge` acts as an adapter between the QGC Vehicle API and QML:

```
MAVLink message → Vehicle (QGC upstream) → TelemetryBridge → Q_PROPERTY → QML binding
```

The bridge does three things:

1. **Subscribes** to Vehicle Fact signals and raw MAVLink messages
2. **Stores** the latest value in a member variable
3. **Emits** a NOTIFY signal when the value changes, which QML bindings respond to

```cpp
// In TelemetryBridge::_handleMavlinkMessage():
case MAVLINK_MSG_ID_BATTERY_STATUS: {
    mavlink_battery_status_t bat;
    mavlink_msg_battery_status_decode(&message, &bat);
    _batteryVoltage = bat.voltages[0] / 1000.0;
    emit batteryVoltageChanged();
}
```

QML binds to this:

```qml
property real batteryVoltage: TelemetryProvider ? TelemetryProvider.batteryVoltage : 0.0
```

## 10.2 What is exposed

### Property groups

| Domain | Properties | Count |
|--------|-----------|-------|
| Connection | `isConnected`, `connectionQuality`, `heartbeatReceived`, `mavlinkVersion`, `companionDetected` | 5 |
| Battery primary | `batteryVoltage`, `batteryPercent`, `batteryCurrent`, `batteryTemperature`, `batteryCellVoltages`, `sysVoltageBattery` | 6 |
| Battery secondary | `battery2Voltage`, `battery2Percent`, `battery2Current`, `battery2Present` | 4 |
| GPS primary | `gpsFixType`, `gpsSatellites`, `gpsLatitude`, `gpsLongitude`, `gpsHdop`, `gpsSpeedAccuracy`, `gpsAltitude` | 7 |
| GPS secondary | `gps2FixType`, `gps2Satellites`, `gps2Latitude`, `gps2Longitude`, `gps2Eph`, `gps2Yaw` | 6 |
| Home | `homeLatitude`, `homeLongitude`, `homeAltitude` | 3 |
| IMU/compass/baro | `imuHealthy`, `compassHealthy`, `imuTemperature`, `baroPressure`, `baroTemperature` | 5 |
| Airspeed/position | `airspeed`, `groundSpeed`, `heading`, `altitudeRelative` | 4 |
| RC link | `rcRssi`, `rcConnected`, `rcFailsafe`, `rcChannelValues`, `rcLastUpdateUsec` | 5 |
| Arming/flight mode | `armed`, `flightMode` | 2 |
| Vibration | `vibrationX`, `vibrationY`, `vibrationZ`, `vibrationClipping` | 4 |
| EKF | `estimatorFlags`, `estimatorVelRatio`, `estimatorPosHorizRatio`, `estimatorPosVertRatio`, `estimatorMagRatio` | 5 |
| EKF variance | `ekfVelVariance`, `ekfPosHorizVariance`, `ekfPosVertVariance`, `ekfCompassVariance`, `ekfTerrainVariance`, `ekfAirspeedVariance` | 6 |
| AHRS/sensor | `ahrsHealth`, `sensorHealth`, `commDropRate` | 3 |
| Wind | `windSpeed`, `windDirection` | 2 |
| Attitude/gyro | `gyroX`, `gyroY`, `gyroZ`, `roll`, `pitch`, `yaw`, `globalAltitude` | 7 |
| Optical flow/gimbal | `opticalFlowQuality`, `gimbalDetected`, `gimbalMode`, `gimbalPitch`, `gimbalRoll`, `gimbalYaw`, `gimbalCalibrating` | 7 |
| Pre-arm | `preArmOk`, `preArmMessage`, `preArmSeverity` | 3 |
| Motors | `motorCount`, `motorOutputs` | 2 |
| Mission | `missionCount`, `missionFirstWpDistance`, `missionTotalDistance` | 3 |
| Accelerometers | `accelerometerX/Y/Z`, `accelerometer2X/Y/Z` | 6 |
| ESC telemetry | `escTemperatures`, `escVoltages`, `escCurrents`, `escRpm`, `escInfoCount`, `escInfoFailureFlags`, `escInfoErrorCount`, `escInfoConnectionType` | 8 |
| Magnetic field | `magFieldX`, `magFieldY`, `magFieldZ` | 3 |
| Vehicle ID | `vehicleType`, `connectionStatus`, `connectionUrl`, `autopilotType` | 4 |
| Status strings | `gpsFixTypeString`, `gpsDataQuality`, `gpsStatusString`, `batteryDataQuality`, `batteryStatusString`, `servoOutputsString` | 6 |
| Flight time | `flightTime`, `lastLogTimestamp` | 2 |
| Sensor quality | `imuDataQuality`, `compassDataQuality`, `rcDataQuality` | 3 |
| Hardware | `hardwareSetupRequired` | 1 |
| **Total** | | **~100** |

### 23 missing Q_PROPERTY declarations

The `VehicleTelemetry.qml` singleton declares properties that reference `TelemetryProvider` properties not backed by `Q_PROPERTY` in C++. These silently return empty/zero:

```qml
// These have no corresponding Q_PROPERTY in TelemetryBridge.h:
property int gpsDataQuality: TelemetryProvider ? TelemetryProvider.gpsDataQuality : 2
property string gpsStatusString: TelemetryProvider ? TelemetryProvider.gpsStatusString : ""
property int batteryDataQuality: TelemetryProvider ? TelemetryProvider.batteryDataQuality : 2
// ... (23 total)
```

Wait — checking `TelemetryBridge.h`, these ARE declared as Q_PROPERTY (lines 178-208). The issue is that some of these properties lack corresponding member variables or getter implementations that return meaningful values. For example, `connectionQuality` returns `_connectionQuality` which is initialized to 0 and only updated by explicit connection quality messages — which many vehicles don't send. So `connectionQuality` is effectively hardcoded to 0 (or 100 in some contexts).

## 10.3 VehicleTelemetry.qml singleton

**File:** `custom/qml/singletons/VehicleTelemetry.qml:1-478`

### How QML consumes the bridge

`VehicleTelemetry` is a QML singleton that acts as a centralized telemetry accessor for all QML components. Instead of each QML file connecting directly to `TelemetryProvider`, they read from `VehicleTelemetry`:

```qml
// In any QML file:
Text { text: VehicleTelemetry.batteryVoltage + "V" }
Text { text: VehicleTelemetry.gpsStatusString }
```

### How _staleTimer works

```qml
property Timer _staleTimer: Timer {
    interval: 1000
    repeat: true
    running: isConnected
    onTriggered: {
        if (telemetryStale && !disconnectGuardActive)
            activateDisconnectGuard()
        else if (!telemetryStale && disconnectGuardActive)
            deactivateDisconnectGuard()
    }
}
```

The stale timer checks every 1 second whether telemetry data is older than `staleTimeoutMs` (5000ms). If stale, it activates the disconnect guard, which suspends check evaluation and shows a warning to the operator.

### How derived properties are computed

Derived properties like `gpsStatusString` are computed from raw values:

```qml
readonly property bool gpsReady: satellites >= 8
readonly property bool batteryGood: batteryVoltage >= 15
readonly property bool airspeedValid: selectedVehicleType === "FixedWing"
    ? groundSpeed >= 20 : true
readonly property bool readyToLaunch:
    mavlinkConnected
    && allChecklistItemsChecked
    && payloadSecured
    && allFinalChecksPassed
    && hardwareTestPassed
```

The `readyToLaunch` property is a composite of multiple conditions — it becomes true only when ALL preconditions for arming are satisfied.

### Compliance record builder

`VehicleTelemetry.qml:193-269` contains `buildComplianceRecord()`, which constructs a comprehensive JSON object for regulatory compliance:

```qml
function buildComplianceRecord() {
    var record = {
        timestamp: ts,
        vehicleName: selectedVehicle,
        vehicleType: selectedVehicleTypeLabel,
        telemetry: { batteryVoltage, satellites, altitude, ... },
        checklist: items,
        readyToLaunch: readyToLaunch,
        result: readyToLaunch ? "pass" : "fail"
    }
    // Add Part 107 compliance if enabled
    if (faaPart107Mode) {
        record.faaPart107 = { pilotName, pilotLicense, aircraftReg }
    }
    // Generate digital signature
    digitalSignature = ExportHelper.generateHash(JSON.stringify(record))
    return record
}
```

---

# Chapter 11 — UI Architecture

## 11.1 The three theming systems

### System 1: Colors.qml / Config.qml singletons

**File:** `custom/qml/singletons/Colors.qml:1-92`

The `Colors` singleton defines 50+ named colors:

```qml
pragma Singleton
QtObject {
    readonly property color skywinAccent: "#3B82A0"
    readonly property color background: "#0B0D12"
    readonly property color surface: "#12151C"
    readonly property color textPrimary: "#E0E4EC"
    readonly property color success: "#34d399"
    readonly property color error: "#fb7185"
    // ... 40+ more
}
```

The `Config` singleton defines layout constants:

```qml
readonly property int fontSizeH1: 19
readonly property int fontSizeH2: 17
readonly property int fontSizeH3: 15
readonly property int fontSizeBody: 13
readonly property int fontSizeSmall: 11
readonly property int spacingSmall: 6
readonly property int spacingMedium: 10
readonly property int radiusSmall: 4
readonly property int radiusMedium: 8
```

### System 2: QGCPalette overrides

**File:** `custom/src/PreflightPlugin.cpp:467-607`

`paletteOverride()` overrides QGC's built-in palette system for the dark theme. This affects all stock QGC UI components that use `QGCPalette`:

```cpp
if (colorName == QStringLiteral("window")) {
    colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled] = QColor("#0B0D12");
}
```

### System 3: Hardcoded hex colors

Many QML files use inline hex colors that don't reference either system:

```qml
// In PreflightChecklistView.qml:
color: "#0a0a1a"          // Background
color: "#2D1B4E"          // Dialog header
color: "#9333ea"          // Purple accent
color: "#F8BBD0"          // Light pink text
color: "#CE93D8"          // Purple text
color: "#E91E63"          // Pink focus
```

### Why this happened

AI-assisted development generated QML components that used hardcoded colors from design mockups rather than referencing the centralized `Colors` singleton. There was no enforced coding style or linting rule requiring use of `Colors.*` properties.

### Target state

All QML files should reference `Colors.*` for color values. The `Colors` singleton is the single source of truth. The hardcoded hex values should be migrated to named properties in `Colors.qml` and then referenced by name.

## 11.2 The split FlyView layout

**File:** `custom/qml/FlyView.qml:1-388`

### The 40/60 video/map split

```
┌───────────────────┬───────────────────┐
│                   │         [Compass] │
│                   │                   │
│   VIDEO  (left)   │    MAP   (right)  │
│                   │                   │
│                   │                   │
│  [Floating Telemetry Box]              │
└───────────────────┴───────────────────┘
```

The split ratio is controlled by `_splitRatio = 0.4` (40% video, 60% map):

```qml
property real _splitRatio: 0.4

Item {
    id: leftPanel
    width: parent.width * _splitRatio
}
Item {
    id: rightPanel
    anchors.left: panelDivider.right
    anchors.right: parent.right
}
```

### pipMode: false fix

**Before fix:** The map had `pipMode: _videoIsMain`, which caused the map to render as a Picture-in-Picture thumbnail when video was the main panel. This broke the 50/50 split layout.

**After fix** (`FlyView.qml:218`): `pipMode: false` — the map is always a full peer panel, never a PIP thumbnail.

### Panel swap mechanism

Double-clicking either panel or clicking the divider swap button swaps video and map:

```qml
function _swapPanels() {
    _videoIsMain = !_videoIsMain;
    QGroundControl.saveBoolGlobalSetting("CustomFlyViewVideoIsMain", _videoIsMain);
}
```

The swap state persists across sessions via `QGroundControl.saveBoolGlobalSetting()`.

### Panel geometry for FlyViewCustomLayer

```qml
FlyViewCustomLayer {
    property rect mapPanelRect: _videoIsMain
        ? Qt.rect(rightPanel.x, 0, rightPanel.width, rightPanel.height)
        : Qt.rect(leftPanel.x,  0, leftPanel.width,  leftPanel.height)
    property rect videoPanelRect: _videoIsMain
        ? Qt.rect(leftPanel.x,  0, leftPanel.width,  leftPanel.height)
        : Qt.rect(rightPanel.x, 0, rightPanel.width, rightPanel.height)
}
```

These rects tell the custom overlay where the map and video panels are, so it can position the compass widget and other overlays correctly.

## 11.3 FlyViewCustomLayer

**File:** `custom/qml/FlyViewCustomLayer.qml` (loaded at `FlyView.qml:342-358`)

### What it draws

1. **Telemetry strip** (full-width bottom): Shows battery, GPS, RC, attitude in a compact bar at the bottom of the screen
2. **Compass widget** (top-right of map panel): Circular compass overlay positioned within the map panel's coordinate space
3. **Preflight banner**: Shows preflight status (all checks passed / blocking issues)
4. **Detection bounding boxes**: Placeholder for object detection overlays from the inference pipeline (not yet implemented)

### Coordinate scaling for bounding boxes

The detection system (when implemented) would receive bounding box coordinates in normalized [0,1] space from the inference service. These are scaled to screen coordinates:

```qml
// Pseudocode for bounding box rendering:
Rectangle {
    x: panelRect.x + (normalizedX * panelRect.width)
    y: panelRect.y + (normalizedY * panelRect.height)
    width: normalizedWidth * panelRect.width
    height: normalizedHeight * panelRect.height
}
```

## 11.4 Analyze Tools entry point

### How analyzePages() adds the checklist

In `PreflightPlugin::analyzePages()` (`PreflightPlugin.cpp:439-465`):

```cpp
const QVariantList &PreflightPlugin::analyzePages() {
    if (_analyzePages.isEmpty()) {
        // Copy stock pages
        const QVariantList &basePages = QGCCorePlugin::analyzePages();
        for (const auto &page : basePages) {
            _analyzePages.append(page);
        }
        // Add custom pages
        _analyzePages.append(QVariant::fromValue(
            new QmlComponentInfo(
                tr("Preflight Checklist"),
                QUrl(QStringLiteral("qrc:/qml/cpts/PreflightChecklistView.qml")),
                QUrl::fromUserInput(QStringLiteral("qrc:/qmlimages/check.svg")),
                this)));
        _analyzePages.append(QVariant::fromValue(
            new QmlComponentInfo(
                tr("Vehicles"),
                QUrl(QStringLiteral("qrc:/qml/analyze/VehiclesPage.qml")),
                QUrl::fromUserInput(QStringLiteral("qrc:/qmlimages/Plan.svg")),
                this)));
    }
    return _analyzePages;
}
```

### How AnalyzeView.qml creates the sidebar button

QGC's `AnalyzeView.qml` (upstream) iterates over `analyzePages()` and creates sidebar buttons for each page. When the user clicks "Preflight Checklist", the `Loader` loads `PreflightChecklistView.qml` into the content area.

---

# Chapter 12 — Known Issues and Technical Debt

## Issue 1: ArmingGate timer never started

**File:** `custom/src/core/ArmingGate.cpp:21-23`

**Code:**
```cpp
m_gateTimer = new QTimer(this);
m_gateTimer->setInterval(EVAL_INTERVAL_MS);
connect(m_gateTimer, &QTimer::timeout, this, &ArmingGate::updateArmingState);
```

**Symptom:** `m_gateTimer` is created and connected but `start()` is never called in the constructor. The timer only starts when `setPreflightManager()` is called (`ArmingGate.cpp:51-52`), which calls `m_gateTimer->start()`. If `setPreflightManager()` is never called (e.g., during unit testing), the gate timer never fires.

**Fix:** Call `m_gateTimer->start()` in the constructor after `connect()`.

## Issue 2: RcRssiCheck epoch arithmetic bug

**File:** `custom/src/core/RcRssiCheck.cpp`

**Symptom:** The check uses `QDateTime::currentMSecsSinceEpoch() * 1000` for timestamp comparison, but `currentMSecsSinceEpoch()` already returns milliseconds. Multiplying by 1000 produces microseconds, making the staleness comparison always pass (the check always appears fresh).

**Fix:** Remove the `* 1000` multiplier.

## Issue 3: 23 missing Q_PROPERTY declarations on TelemetryBridge

**File:** `custom/src/adapters/TelemetryBridge.h`

**Symptom:** `VehicleTelemetry.qml` references properties like `gpsDataQuality`, `gpsStatusString`, `batteryDataQuality`, `batteryStatusString`, `servoOutputsString`, `imuDataQuality`, `compassDataQuality`, `rcDataQuality` that ARE declared as Q_PROPERTY in the header but may not have meaningful implementations. Some properties like `connectionQuality` are hardcoded to 0 or 100.

**Fix:** Implement proper value updates for all declared properties, or remove the Q_PROPERTY declarations that have no meaningful backing data.

## Issue 4: Force Arm 10-second timeout risk

**File:** `custom/src/core/ArmingGate.cpp:229-242`

**Symptom:** `forceArm()` sets `m_overrideActive = true` with no timeout. The override stays active indefinitely until `resetGate()` is called. If the operator force-arms and forgets to reset, the gate remains bypassed for all subsequent flights.

**Fix:** Add a configurable timeout (e.g., 60 seconds) to `forceArm()`, or require re-confirmation before each subsequent arm.

## Issue 5: Blocking network call in WeatherProvider

**File:** `custom/src/utils/WeatherProvider.cpp:549-589`

**Symptom:** `lookupIcao()` creates a new `QNetworkAccessManager` and uses `QEventLoop` to block until the HTTP response arrives or a 3-second timeout fires. This blocks the main thread.

**Fix:** Make `lookupIcao()` asynchronous with a callback or signal.

## Issue 6: DB queries on every evaluation tick

**File:** `custom/src/core/AbstractCheck.cpp:230-244`

**Symptom:** `configDouble()` calls `DatabaseManager::instance().getCheckConfig()` which executes a SQLite SELECT. With 91 checks at 1Hz, this produces 91-273 queries/second on the main thread.

**Fix:** Already partially fixed via `m_configCache`. The remaining issue is that the cache is only cleared on `reset()`, not when external config changes. Add a signal from `DatabaseManager` to invalidate caches when `setCheckConfig()` is called.

## Issue 7: Connection quality hardcoded to 100

**File:** `custom/src/adapters/TelemetryBridge.h:501`

**Symptom:** `_connectionQuality` is initialized to 0 and only updated by explicit `RADIO_STATUS` messages. Many vehicles don't send `RADIO_STATUS`, so `connectionQuality` stays at 0 or is set to a hardcoded value.

**Fix:** Compute connection quality from heartbeat loss rate or MAVLink message drop rate.

## Issue 8: Empty detection module

**Files:** `custom/src/detection/VehicleRegistry.h`, `custom/src/detection/VehicleRegistry.cpp`

**Symptom:** The `DetectionBridge` and `DetectionModel` classes mentioned in the architecture (for object detection bounding boxes) are not implemented. The detection module contains only `VehicleRegistry` for vehicle fingerprinting. The WebSocket-based inference pipeline is designed but has no code.

**Status:** Architecture designed, implementation pending.

## Issue 9: Two complete checklist implementations

**Files:** `custom/src/ui/PreflightChecklistModel.h`, `custom/src/core/ChecklistItemModel.h`

**Symptom:** There are two `QAbstractListModel` implementations for checklist data: `PreflightChecklistModel` (18 roles, used by the Analyze page) and `ChecklistItemModel` (9 roles, used by `ChecklistEngine`). Both track check status but with different data structures and role definitions. This creates confusion about which model to use and duplicated logic.

**Fix:** Consolidate into a single model implementation.

## Issue 10: Three theming systems with ~42 hardcoded hex colors

**Files:** `custom/qml/singletons/Colors.qml`, `custom/src/PreflightPlugin.cpp:467-607`, and inline hex colors across QML files

**Symptom:** Three independent theming mechanisms (Colors singleton, QGCPalette overrides, hardcoded hex) with no enforced consistency. ~42 hardcoded hex colors in QML files that don't reference `Colors.*`.

**Fix:** Create a linting rule requiring all QML colors to reference `Colors.*` properties. Migrate hardcoded values to named properties.

## Issue 11: 51 of 55 check subclasses have zero test coverage

**Files:** `custom/tests/` directory

**Symptom:** The test infrastructure exists (mocks for TelemetryBridge, Vehicle, ParameterManager) but only a few checks have test classes (`CheckSmokeTest`, `RcCalibrationCheckTest`, `CompassOrientationCheckTest`, `WeatherProviderTest`, `MissionEnergyCheckTest`). 51 of 55 check subclasses have no unit tests.

**Fix:** Write unit tests for all check subclasses using the existing mock infrastructure.

## Issue 12: DatabaseManager god class

**File:** `custom/src/utils/DatabaseManager.h:32-173` (173 lines header, 1874+ lines implementation)

**Symptom:** `DatabaseManager` handles templates, compliance logs, hardware tests, maintenance, vehicle profiles, battery tracking, flight sessions, check results, check config, and motor test results — 10+ distinct responsibilities in a single class.

**Fix:** Split into domain-specific managers: `VehicleStore`, `BatteryStore`, `FlightStore`, `CheckStore`, `MaintenanceStore`.

## Issue 13: fontSizeH4 undefined

**File:** `custom/qml/pages/MaintenancePage.qml`

**Symptom:** `MaintenancePage.qml` references `Config.fontSizeH4` but `Config.qml` only defines `fontSizeH1` through `fontSizeH3` and `fontSizeBody`. This causes a QML runtime error and layout breaks on the MaintenancePage.

**Fix:** Add `readonly property int fontSizeH4: 13` to `Config.qml`, or change `MaintenancePage.qml` to use `fontSizeBody`.

---

# Chapter 13 — Deployment on Jetson Orin Nano

## 13.1 Platform differences from dev box

| Aspect | Dev Box (x86_64) | Jetson Orin Nano (aarch64) |
|--------|-------------------|---------------------------|
| OS | Ubuntu 22.04 | Ubuntu 22.04 (JetPack 6.2.2) |
| Architecture | x86_64 | aarch64 |
| Qt installation | Online installer (pre-built binaries) | Must build from source (no aarch64 online installer) |
| Power mode | Default | MAXN SUPER (maximum performance) |
| RAM | 16-64 GB | 8 GB (shared with GPU) |
| Swap | SSD-backed | Swapfile required for Qt source build (8GB+) |
| GPU | Discrete (NVIDIA) | Integrated (Orin, 1024 CUDA cores) |
| Storage | SSD | NVMe or SD card (limited) |

### MAXN SUPER power mode

Jetson Orin Nano supports multiple power modes. MAXN SUPER provides:
- 15W TDP (vs 7W default)
- Maximum CPU/GPU clock speeds
- Required for real-time inference alongside GCS

Set via: `sudo nvpmodel -m 0 && sudo jetson_clocks`

### Swapfile requirement

Building Qt 6.8.3 from source requires ~8GB of RAM for the compilation of QtWebEngine and QtMultimedia modules. The Orin Nano's 8GB is insufficient without swap:

```bash
sudo fallocate -l 8G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
```

## 13.2 Building on the Jetson

### Qt 6.8.3 from source

```bash
wget https://download.qt.io/official_releases/qt/6.8/6.8.3/single/qt-everywhere-opensource-src-6.8.3.tar.xz
tar xf qt-everywhere-opensource-src-6.8.3.tar.xz
cd qt-everywhere-opensource-src-6.8.3

./configure -prefix /opt/Qt/6.8.3 \
    -platform linux-aarch64-g++ \
    -opensource -confirm-license \
    -nomake examples -nomake tests \
    -skip qtwebengine

cmake --build . --parallel $(nproc)
sudo cmake --install .
```

### Same CMake invocation

```bash
cmake -B build_custom \
    -DQGC_CUSTOM_BUILD=ON \
    -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.3 \
    -DQGC_ENABLE_GST_VIDEOSTREAMING=ON \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build_custom --target PreflightQGroundControl -j$(nproc)
```

### Expected build time

On Jetson Orin Nano (8-core ARM, MAXN SUPER):
- Qt from source: ~4-6 hours
- QGC with custom plugin: ~45-90 minutes

### Inference service dependencies

```bash
pip3 install websockets numpy
# jetson-inference comes with JetPack
```

## 13.3 Systemd autostart

### The three services

**qgc-gcs.service:**
```ini
[Unit]
Description=Skywin GCS
After=network.target

[Service]
Type=simple
ExecStart=/opt/skywin/bin/PreflightQGroundControl
Restart=on-failure
RestartSec=5
Environment=DISPLAY=:0
Environment=QT_QPA_PLATFORM=xcb

[Install]
WantedBy=multi-user.target
```

**inference.service:**
```ini
[Unit]
Description=Object Detection Inference Service
After=network.target

[Service]
Type=simple
ExecStart=/usr/bin/python3 /opt/skywin/inference/inference_service.py
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
```

**video-fanout.service:**
```ini
[Unit]
Description=GStreamer Video Fan-out
After=network.target

[Service]
Type=simple
ExecStart=/opt/skywin/bin/video_fanout
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
```

### Cold reboot verification

After a cold reboot:
1. `video-fanout.service` starts GStreamer pipeline → splits camera feed
2. `inference.service` starts → connects to video fan-out via GStreamer
3. `qgc-gcs.service` starts → connects to inference service via WebSocket

Verify with: `systemctl status qgc-gcs inference video-fanout`

## 13.4 The inference pipeline

### Architecture (designed, not fully implemented)

```
Camera (CSI/USB)
    │
    ▼
GStreamer fan-out (video_fanout)
    ├── → Display output (HDMI)
    └── → Inference input (appsink)
            │
            ▼
inference_service.py (jetson-inference + TensorRT)
    │ Detection results: [label, confidence, bbox normalized]
    │
    ▼
WebSocket (ws://localhost:8765)
    │
    ▼
DetectionBridge (C++ — NOT YET IMPLEMENTED)
    │ Parses JSON detection results
    │ Updates DetectionModel
    │
    ▼
FlyViewCustomLayer.qml
    └── Renders bounding boxes on video panel
```

### Current state

- **Inference service architecture**: Designed, `inference_service.py` skeleton exists
- **DetectionBridge**: `custom/src/detection/` contains only `VehicleRegistry`. `DetectionBridge` and `DetectionModel` are not implemented (0 bytes)
- **Bounding box rendering**: `FlyViewCustomLayer.qml` has placeholder hooks but no detection rendering code

### What needs to be implemented

1. `DetectionBridge` class: WebSocket client connecting to `inference_service.py`, parsing JSON detection results, emitting signals
2. `DetectionModel` class: `QAbstractListModel` exposing detection results to QML
3. QML bounding box renderer in `FlyViewCustomLayer.qml`
4. TensorRT model loading and inference loop in `inference_service.py`

---

*End of document*
