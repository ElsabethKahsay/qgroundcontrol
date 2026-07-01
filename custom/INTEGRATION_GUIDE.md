# UAV Preflight Plugin — QGC v5.0 Integration Guide

*For AI agents and developers integrating this custom plugin into a fresh fork of QGroundControl v5.0.*

---

## Table of Contents

- [Overview](#overview)
- [Step 1: Fork & Checkout QGC v5.0](#step-1-fork--checkout-qgc-v50)
- [Step 2: Copy the Plugin](#step-2-copy-the-plugin)
- [Step 3: Configure CMake Build](#step-3-configure-cmake-build)
- [Step 4: Apply Upstream Source Fixes](#step-4-apply-upstream-source-fixes)
- [Step 5: Verify CMake Configuration](#step-5-verify-cmake-configuration)
- [Step 6: Build](#step-6-build)
- [Step 7: Verify Runtime](#step-7-verify-runtime)
- [Troubleshooting](#troubleshooting)
- [Reference: Key Files & Their Roles](#reference-key-files--their-roles)
- [Reference: Upstream Files Modified](#reference-upstream-files-modified)
- [Appendix: Full CMake Output Checks](#appendix-full-cmake-output-checks)

---

## Overview

This plugin extends QGC via the `QGC_CUSTOM_DIR` mechanism — the canonical extension point supported by the upstream build system. The entire plugin lives in a `custom/` directory at the QGC source root.

**What the plugin does:**
- Replaces QGC's core plugin (`QGCCorePlugin`/`QGCOptions`) with a custom `PreflightPlugin`
- Adds 91 preflight checks across 9 categories
- Implements an arming gate that intercepts MAV_CMD_COMPONENT_ARM_DISARM
- Provides a telemetry bridge from C++ Vehicle to QML singleton
- Replaces several stock QML FlyView/Toolbar components

**Dependency:** QGC v5.0. The plugin calls QGC internal APIs extensively. It is NOT standalone.

---

## Step 1: Fork & Checkout QGC v5.0

```bash
git clone https://github.com/mavlink/qgroundcontrol.git qgroundcontrol-v5
cd qgroundcontrol-v5
git checkout v5.0.0
git submodule update --init --recursive
```

**Qt version:** The plugin is developed against **Qt 6.10.3**, but works with **Qt 6.8.x+** with a one-line configuration change (see step 3).

QGC v5.0's upstream CMake requires `Qt6 6.10.0+`. To use Qt 6.8.3, override the minimum version check in `custom/cmake/CustomOverrides.cmake` — this is done automatically by the bundled overrides file.

> **Why 6.10.3 was used for development:** Qt 6.10.3 introduced stricter constexpr requirements for `QMetaType::fromType<T*>()` with forward-declared types. The bundled `include/qt6_meta_fixes.h` resolves this. On Qt 6.8.x, the constexpr evaluator is more lenient and the fix header is harmless (the `is_complete` trait exists in all Qt 6 versions).

---

## Step 2: Copy the Plugin

Place the entire `custom/` directory from this bundle into the QGC source root:

```bash
# From the bundle root
cp -r custom/ /path/to/qgroundcontrol-v5/custom/
```

**Verify the file structure:**

```bash
ls /path/to/qgroundcontrol-v5/custom/
# Should show: android/ cmake/ CMakeLists.txt custom.pri custom.qrc ...
# Should NOT show: CustomBuild.cmake (was removed — dead file with Qt5 refs)
```

Critical files that MUST be present:

```
custom/
├── CMakeLists.txt                  # Primary CMake build integration
├── cmake/
│   └── CustomOverrides.cmake       # Overrides app name, disables APM/PX4, force-includes qt6_meta_fixes.h
├── include/
│   └── qt6_meta_fixes.h            # Qt 6.10.3 constexpr fix — NEVER OMIT THIS
├── src/
│   ├── PreflightPlugin.cpp/.h      # Entry point — CUSTOMCLASS point
│   ├── QmlModuleInit.cpp           # QML module registration force-linking
│   ├── APMPluginStub.cpp           # APM symbol stubs
│   ├── core/                       # Check framework + 43 check implementations
│   ├── adapters/                   # Telemetry bridge
│   ├── controllers/                # Hardware test controllers
│   ├── mission/                    # Waypoint math
│   ├── ui/                         # QML model adapters
│   └── utils/                      # Utilities
├── qml/                            # 50 QML files
│   ├── cpts/                       # Checklist card components
│   ├── pages/                      # Full-screen pages
│   ├── singletons/                 # QML singletons
│   └── *.qml                       # Toolbar/FlyView overlays
├── custom.qrc                      # Qt resource bindings
├── SRS_AUDIT.md                    # Requirements traceability
└── INTEGRATION_GUIDE.md            # This file
```

---

## Step 3: Configure CMake Build

### 3.1 Prerequisites: Remove dead files

Delete `custom/CustomBuild.cmake` if present. This is a dead file (not referenced by CMake or qmake) that contains Qt5 references and a hardcoded `/tmp/opencode/location-fix` path that will not exist in a fresh fork.

```bash
rm -f custom/CustomBuild.cmake
```

### 3.2 Set up the CMake configuration

From the QGC source root:

```bash
cmake -S . -B build_custom \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DQGC_CUSTOM_DIR=custom \
    -DQGC_BUILD_TESTING=OFF \
    -DQGC_ENABLE_GST_VIDEOSTREAMING=OFF \
    -DQGC_UNITY_BUILD=OFF \
    -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
```

**Explanation of options:**

| Option | Value | Why |
|---|---|---|
| `QGC_CUSTOM_DIR` | `custom` | Tells QGC build system to load this plugin |
| `QGC_BUILD_TESTING` | `OFF` | Disables test compilation (saves ~30% build time; turn ON for development) |
| `QGC_ENABLE_GST_VIDEOSTREAMING` | `OFF` | Disables GStreamer. Required because the QtLocation private header workaround conflicts with GStreamer's include paths. If you need video, you must resolve the QtLocation headers separately (see Troubleshooting) |
| `QGC_UNITY_BUILD` | `OFF` | Unity builds merge multiple .cpp files into one compilation unit, which can cause ODR violations with the plugin's includes. Keep OFF for reliability |
| `CMAKE_PREFIX_PATH` | Your Qt path | Points to Qt 6.10.3 installation. Adjust for your system |

**Important:** The target executable name becomes `PreflightQGroundControl` (not `QGroundControl`). This is set by `CustomOverrides.cmake` via `set(QGC_APP_NAME "PreflightQGroundControl" ...)`.

### 3.2 Verify CMake output

On first configure, look for these messages:

```
-- QGC: Adding UAVPreflight Plugin
-- UAVPreflight plugin configured successfully
-- Custom build found: applying overrides
```

If you see `-- QGC_CUSTOM_DIR is set but not found`, the path to custom/ is wrong.

If you see `CMake Error at cmake/CustomOptions.cmake` — your CMake version is too old (need 3.25+).

### 3.3 Verify that qt6_meta_fixes.h is force-included

Check the compile commands:

```bash
grep "qt6_meta_fixes" build_custom/build.ninja | head -3
```

Should output lines like:

```
FLAGS = ... -include /path/to/custom/include/qt6_meta_fixes.h ...
```

If this flag is missing, the build WILL fail with constexpr-related errors about incomplete types in QMetaType::fromType().

**Why this is needed:** Qt 6.10.3's QMetaType::fromType<T*>() is constexpr and requires T to be fully defined at the point of instantiation. QGC's Vehicle and VehicleComponent are self-referencing types in Q_OBJECT macros and are only forward-declared in many compilation units. The fix header specializes `QtPrivate::is_complete<Vehicle>::value = true_type`, bypassing the incomplete-type check.

---

## Step 4: Apply Upstream Source Fixes

The plugin requires two source file modifications in QGC itself. You MUST apply these — the build will either fail or produce a broken binary without them.

### Fix 1: MavlinkCameraControlInterface.h — Missing MAVLink include

**File:** `src/Camera/MavlinkCameraControlInterface.h`

**Problem:** This file uses MAVLink types (mavlink_message_t, etc.) but does not include any MAVLink header. With Qt 6's stricter include checking and/or unity builds, this causes "unknown type" compilation errors.

**Fix — add `#include "MAVLinkLib.h"`:**

```cpp
// File: src/Camera/MavlinkCameraControlInterface.h
// Lines 5-9:
#include <QtCore/QLoggingCategory>
#include <QtCore/QSizeF>

#include "FactGroup.h"
#include "MAVLinkLib.h"          // ← ADD THIS LINE

#include <QtQmlIntegration/QtQmlIntegration>
```

**Why this wasn't caught before:** The MAVLink types were being pulled in transitively through other headers. With certain build configurations (unity builds, different include orderings), the transitive include chain breaks.

### Fix 2: FactValueGrid.h — Vehicle* QML type resolution

**File:** `src/QmlControls/FactValueGrid.h`

**Problem:** This class has `Q_PROPERTY(Vehicle * specificVehicleForCard ...)`. The original code forward-declares `class Vehicle;` which is insufficient for Qt 6's QML meta-type system. When the QML engine encounters a property assignment to this property, it cannot resolve `Vehicle*` as a valid QML type, producing:

```
Invalid property assignment: unsupported type "Vehicle*"
```

**Fix — replace forward declaration with full include:**

```cpp
// File: src/QmlControls/FactValueGrid.h
// Lines 7-12 — change from:
#include "QGCMAVLinkTypes.h"

class InstrumentValueData;
class QmlObjectListModel;
class Vehicle;                  // ← REMOVE THIS LINE

// To:
#include "Vehicle/Vehicle.h"    // ← ADD THIS LINE
#include "QGCMAVLinkTypes.h"

class InstrumentValueData;
class QmlObjectListModel;
```

**Why this works:** When MOC processes `Q_PROPERTY(Vehicle * specificVehicleForCard ...)`, it evaluates `std::is_base_of<QObject, Vehicle>` to determine the pointer's meta-type flags. With only a forward declaration, this trait evaluates to `false`, causing the pointer to be registered as opaque/untyped. Including the full Vehicle.h header makes Vehicle's QObject ancestry visible, so MOC correctly generates `PointerToQObject | IsPointer` flags. The QML engine then accepts Vehicle* as a valid property type.

**What happens without this fix:** The QML engine at RUNTIME (not compile time) rejects any assignment — even `null` — to the `specificVehicleForCard` property. Every FlyView that uses FactValueGrid or TelemetryValuesBar becomes unavailable.

### Verify all fixes applied

```bash
# Check Fix 1
grep -n "MAVLinkLib.h" src/Camera/MavlinkCameraControlInterface.h
# Expected output: line with #include "MAVLinkLib.h"

# Check Fix 2
grep -n "Vehicle/Vehicle.h" src/QmlControls/FactValueGrid.h
# Expected output: line with #include "Vehicle/Vehicle.h"
grep -n "class Vehicle;" src/QmlControls/FactValueGrid.h
# Expected output: NOTHING (the forward declaration must be removed)
```

---

## Step 5: Verify CMake Configuration

Run a dry build check to confirm all files are discovered:

```bash
# From build_custom directory
ninja -t targets all 2>&1 | grep Preflight
```

Expected output:

```
PreflightQGroundControl
PreflightQGroundControl_autogen
```

Check that custom source files are in the build graph:

```bash
ninja -t targets all 2>&1 | grep -i "custom/src" | head -5
# Should show PreflightPlugin.cpp, QmlModuleInit.cpp, etc.
```

---

## Step 6: Build

### 6.1 First build

```bash
cmake --build build_custom --target PreflightQGroundControl -j$(nproc)
```

**Expected build time:** 15-30 minutes (Release, -j8) on modern hardware. On 2-core constrained systems, 3-5 hours.

**Expected artifact:** `build_custom/Release/PreflightQGroundControl` (or `build_custom/Debug/` for debug builds)

### 6.2 Verify binary

```bash
file build_custom/Release/PreflightQGroundControl
# Expected: ELF 64-bit LSB executable, dynamically linked
```

### 6.3 Build error checklist

| Error | Likely Cause | Fix |
|---|---|---|
| `'mavlink_message_t' does not name a type` | Missing Fix 1 | Apply Fix 1 to MavlinkCameraControlInterface.h |
| `'Vehicle' is incomplete` / constexpr errors | Missing `qt6_meta_fixes.h` | Verify `-include` flag is in compile commands (Step 3.3) |
| `Qt5::Sql` not found | Old build directory from different config | Delete `build_custom/` and reconfigure |
| `QGC_CUSTOM_DIR is set but not found` | Wrong path | Set `-DQGC_CUSTOM_DIR=/absolute/path/to/custom` |
| `multiple definition of 'qml_register_types_*'` | Unity build conflict | Set `-DQGC_UNITY_BUILD=OFF` |
| `APMParameterMetaData` undefined | APM plugin not disabled correctly | Check `QGC_DISABLE_APM_PLUGIN=ON` in CustomOverrides |
| `cannot open shared object file` at runtime | LD_LIBRARY_PATH not set | `export LD_LIBRARY_PATH=/path/to/Qt/6.10.3/gcc_64/lib:$LD_LIBRARY_PATH` |

### 6.4 Incremental builds

After the first full build, subsequent builds are fast. Only alter `custom/` files and the two fixed upstream files. Do NOT touch `src/pch.h` — modifying the precompiled header forces a full rebuild of all 479+ translation units.

---

## Step 7: Verify Runtime

### 7.1 Basic launch test

```bash
export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH=/path/to/Qt/6.10.3/gcc_64/lib
timeout 15 ./build_custom/Release/PreflightQGroundControl --simple-boot-test 2>&1
```

**Expected (success):** Application starts, QML engine loads all modules, then exits. Look for these log lines:

```
PreflightManager: 95 unique checks registered
```

**Expected (headless failures — NOT problematic):**
```
Critical: Failed To Init Video Manager - mainWindow is NULL
Critical: Unable to start the client: "org.freedesktop.DBus.Error.AccessDenied" ...
```

These are expected in headless/CI environments. They do not indicate a functional problem.

### 7.2 Critical errors that indicate a problem

**FATAL:** `QQmlApplicationEngine failed to create component`
**FATAL:** `Invalid property assignment: unsupported type "Vehicle*"`
**FATAL:** `Type FlyView unavailable`

If you see any of these, the `FactValueGrid.h` fix (Fix 2) was not applied correctly. Confirm both changes from Step 4 are present.

### 7.3 Verify with QML import trace

```bash
export QML_IMPORT_TRACE=1
timeout 15 ./build_custom/Release/PreflightQGroundControl 2>&1 | grep "importExtension"
```

Should show:

```
importExtension: loaded ":/qml/QGC/qmldir"
importExtension: loaded ":/qml/QGroundControl/qmldir"
importExtension: loaded ":/qml/QGroundControl/FlyView/qmldir"
```

If module imports fail, the QML import paths are wrong.

### 7.4 Verify MultiVehicleList vehicle card

The `MultiVehicleList.qml` in FlyView uses `specificVehicleForCard: _vehicle` (a real Vehicle object reference, not null). With Fix 2 applied, this should work. If it fails with "unsupported type Vehicle*", the fix was not applied.

### 7.5 Smoke test

If `QGC_BUILD_TESTING=ON` was set at configure time:

```bash
./build_custom/Release/PreflightQGroundControl --unittest:CheckSmokeTest
```

---

## Troubleshooting

### Build fails: constexpr/incomplete type errors for Vehicle

**Symptom:**
```
error: constexpr variable 'moc_metatype_0' must be initialized by a constant expression
  ... QMetaType::fromType<Vehicle*>() ...
  ... incomplete type 'Vehicle' ...
```

**Root cause:** Qt 6.10.3's QMetaType::fromType<T*>() is constexpr and requires T to be fully defined. QGC uses Vehicle* in Q_OBJECT signal/slot signatures with only a forward declaration.

**Fix:** Ensure `-include ${CMAKE_CURRENT_SOURCE_DIR}/include/qt6_meta_fixes.h` is in the compile flags. Verify with:

```bash
grep "qt6_meta_fixes" build_custom/build.ninja | head -1
```

The fix header (`include/qt6_meta_fixes.h`) specializes `QtPrivate::is_complete<Vehicle, void>` to `std::true_type`, which satisfies the constexpr evaluator without making Vehicle* opaque.

### Build fails: MavlinkCameraControlInterface missing types

**Symptom:**
```
src/Camera/MavlinkCameraControlInterface.h: error: 'mavlink_message_t' does not name a type
```

**Fix:** Apply Fix 1 from Step 4.

### Runtime: QML engine won't load FlyView

**Symptom:**
```
Type FlyView unavailable
qrc:/qml/QGroundControl/FlyView/FlyViewBottomRightRowLayout.qml:13:33: Invalid property assignment: unsupported type "Vehicle*"
```

**Fix:** Apply Fix 2 from Step 4. The `FactValueGrid.h` file MUST include Vehicle.h rather than forward-declaring Vehicle.

**Do NOT attempt to work around this by removing the `specificVehicleForCard: null` assignment in the QML file.** That workaround only masks the symptom for the null case — it does NOT fix the underlying type issue. Other QML files (MultiVehicleList.qml) assign real Vehicle objects to the same property and would still fail.

### Runtime: GStreamer-related crashes

**Symptom:**
```
GStreamer-CRITICAL: gst_element_get_state: assertion 'GST_IS_ELEMENT (element)' failed
```

**Fix:** Build with `-DQGC_ENABLE_GST_VIDEOSTREAMING=OFF`. This is already the default when using `CustomOverrides.cmake`.

If you need video streaming, you must provide the QtLocation private headers. The plugin uses `CustomOverrides.cmake` to apply a workaround:

```cmake
set(QT_LOCATION_FIX_DIR "/tmp/opencode/location-fix")
if(EXISTS ${QT_LOCATION_FIX_DIR}/QtLocation/private/qgeomaptype_p.h)
    include_directories(BEFORE SYSTEM ${QT_LOCATION_FIX_DIR})
endif()
```

This workaround path is environment-specific. In your deployment, either:
1. Install the QtLocation private headers to a known path and update `CustomOverrides.cmake`
2. Or just keep GStreamer disabled (sufficient for preflight checklist use cases)

### Build takes forever

**Cause:** Modifying `src/pch.h` (the precompiled header) invalidates ALL 479+ object files.

**Prevention:** Never modify `src/pch.h`. The plugin's Qt 6.10.3 fixes are handled by `qt6_meta_fixes.h` (force-included via `-include`), which does NOT invalidate the precompiled header.

If you already modified pch.h and are facing a full rebuild, revert the file with `git checkout src/pch.h` and rebuild from the partially-built state — ninja will only rebuild the files that changed since the last clean build.

### Plugin not loaded (stock QGC starts instead)

**Symptom:** Binary is named `QGroundControl` (not `PreflightQGroundControl`), and the custom UI doesn't appear.

**Fix:** Check that `CustomOverrides.cmake` is being loaded:

```bash
grep "CustomOverrides" build_custom/build.ninja | head -3
```

If not found, the cmake module path is wrong. The `cmake/` directory in the QGC source root must be able to find `custom/cmake/CustomOverrides.cmake`. This is normally handled by `cmake/CustomOptions.cmake`:

```cmake
set(QGC_CUSTOM_DIR "custom" CACHE STRING "...")
```

If you renamed or moved the `custom/` directory, update `QGC_CUSTOM_DIR` accordingly.

### Qt5::Sql errors in CMake

**Symptom:**
```
CMake Error at CustomBuild.cmake:37 (find_package):
  By not providing "FindQt5.cmake" in CMAKE_MODULE_PATH
```

**Fix:** `CustomBuild.cmake` was a dead file and should have been removed (see Step 2). If it's still present, delete it and reconfigure. The correct CMake integration is in `custom/CMakeLists.txt` which uses `Qt6::Sql`.

### Cmake configure fails: Qt6 6.10.0+ not found

**Symptom:**
```
CMake Error at CMakeLists.txt:199 (message):
  Qt6 6.10.0+ not found.
```

**Root cause:** QGC v5.0 requires Qt 6.10.0 minimum. Your installed Qt (e.g., 6.8.3) is too old.

**Fix:** The bundled `custom/cmake/CustomOverrides.cmake` automatically overrides the minimum to `6.5.0` via:
```cmake
set(QGC_QT_MINIMUM_VERSION "6.5.0" CACHE STRING "" FORCE)
```
If cmake still fails with this error, the overrides file is not being loaded. Verify:
```bash
grep "QGC_QT_MINIMUM_VERSION" build_custom/CMakeCache.txt
# Should show: QGC_QT_MINIMUM_VERSION:STRING=6.5.0
```
If it still shows `6.10.0`, delete `build_custom/CMakeCache.txt` and reconfigure.

**Do NOT attempt to symlink an older Qt version (e.g., `ln -s 6.8.3 6.10.3`):**
- The symlink will bypass cmake's version check, but Qt's cmake config files report the actual version (6.8.3), so the check will still fail (needs 6.5.0, finds 6.8.3 — this succeeds only if the override is in place)
- Even if cmake configures, the include directory structure between Qt 6.8 and 6.10 may differ, causing `#include <QtCore/QMetaType>` to fail in `qt6_meta_fixes.h`
- Use the cmake override instead — it's the supported path

---

## Reference: Key Files & Their Roles

| File | Role | Critical? |
|---|---|---|
| `custom/CMakeLists.txt` | Build integration — globs sources, sets include paths, force-includes qt6_meta_fixes.h | YES |
| `custom/cmake/CustomOverrides.cmake` | App branding override, feature toggles, force-includes qt6_meta_fixes.h | YES |
| `custom/include/qt6_meta_fixes.h` | Specializes is_complete<Vehicle> for Qt 6.10.3 constexpr | YES |
| `custom/src/PreflightPlugin.cpp` | Plugin constructor — must force-link QML module registrations | YES |
| `custom/src/PreflightPlugin.h` | CUSTOMCLASS header point | YES |
| `custom/src/QmlModuleInit.cpp` | Symbols that force-link qml_register_types_* and qInitResources_qmlcache_* | YES |
| `custom/src/core/PreflightManager.cpp` | Registers all 91 checks | YES |
| `custom/src/core/ArmingGate.cpp` | MAV_CMD intercept for arming | Conditional |
| `custom/src/adapters/TelemetryBridge.cpp` | Vehicle → QML telemetry bridge | YES |
| `custom/custom.qrc` | Qt resource bindings | YES |
| `custom/qgroundcontrol.exclusion` | Excludes stock QML files from resource bundle | YES |
| `custom/custom.pri` | QMake build (NOT used by CMake) | No (CMake-only deployment) |
| `custom/updateqrc.py` | QRC regeneration script | No (maintenance only) |
| `custom/updateinstrumentqrc.py` | Instrument icon QRC regeneration | No (maintenance only) |
| `custom/custom_deploy.pri` | QMake deploy helper | No (CMake-only deployment) |

## Reference: Upstream Files Modified

Exactly 3 upstream files are modified from stock QGC v5.0:

| File | Change | Purpose |
|---|---|---|
| `CMakeLists.txt` (line 236) | Moved `LocationPrivate` from `COMPONENTS` to `OPTIONAL_COMPONENTS` | Qt 6.8.x open-source lacks this private module |
| `src/Camera/MavlinkCameraControlInterface.h` | Added `#include "MAVLinkLib.h"` after `#include "FactGroup.h"` | Fixes missing MAVLink type in Qt6 builds |
| `src/QmlControls/FactValueGrid.h` | Replaced `class Vehicle;` with `#include "Vehicle/Vehicle.h"` | Fixes Vehicle* QML type resolution |

These changes are safe for upstream merge — they do not alter behavior, only fix type visibility.

---

## Appendix: Full CMake Output Checks

### Verify CUSTOM_SOURCES is populated

```bash
cmake --build build_custom --target help 2>&1 | grep -c "custom/src"
# Should be > 0 (all 223+ source files discovered)
```

### Verify CUSTOM_INCLUDE_DIRECTORIES

```bash
grep "CUSTOM_INCLUDE_DIRECTORIES" build_custom/CMakeCache.txt
# Should show all custom/src subdirectories
```

### Verify target name

```bash
grep "CMAKE_MAKEFILE_PRODUCTS" build_custom/CMakeCache.txt | head -1
# Should reference PreflightQGroundControl
```

### Verify GStreamer is disabled

```bash
grep "QGC_ENABLE_GST_VIDEOSTREAMING" build_custom/CMakeCache.txt
# Should show: QGC_ENABLE_GST_VIDEOSTREAMING:BOOL=OFF
```

---

*Document v1.0 — covers Qt 6.10.3 / QGC v5.0 integration. Update for newer Qt or QGC versions as needed.*
