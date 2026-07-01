# UAV Preflight Plugin — QGC v5.0 Integration Guide

*For developers (and AI agents) integrating this custom plugin into a fresh fork of QGroundControl v5.0.*

## Table of Contents

- [Overview](#overview)
- [Step 1: Fork & Checkout QGC v5.0](#step-1-fork--checkout-qgc-v50)
- [Step 2: Copy the Plugin](#step-2-copy-the-plugin)
- [Step 3: Configure CMake Build](#step-3-configure-cmake-build)
- [Step 4: Confirm No Upstream Files Are Modified](#step-4-confirm-no-upstream-files-are-modified)
- [Step 5: Verify CMake Configuration](#step-5-verify-cmake-configuration)
- [Step 6: Build](#step-6-build)
- [Step 7: Verify Runtime](#step-7-verify-runtime)
- [Troubleshooting](#troubleshooting)
- [Reference: Key Files & Their Roles](#reference-key-files--their-roles)
- [Reference: Upstream Files Modified](#reference-upstream-files-modified)
- [Appendix: CMake Output Checks](#appendix-cmake-output-checks)

---

## Overview

This plugin extends QGC via the `QGC_CUSTOM_BUILD` mechanism — the canonical extension point supported by the upstream CMake build (`if(QGC_CUSTOM_BUILD) add_subdirectory(custom) endif()`). The entire plugin lives in a `custom/` directory at the QGC source root and is **additive only** — it must not require edits to anything outside `custom/`.

**What the plugin does:**
- Provides a custom `PreflightPlugin` (subclassing `QGCCorePlugin`)
- Adds a set of preflight checks across multiple categories
- Implements an arming gate that intercepts `MAV_CMD_COMPONENT_ARM_DISARM`
- Provides a telemetry bridge from C++ `Vehicle` to a QML-facing model
- Adds QML overlays via the supported customization points (`FlyViewCustomLayer.qml`, toolbar customization) rather than replacing stock files outright

**Dependency:** QGC v5.0 (tag `v5.0.8` recommended — latest stable patch as of this writing; pin to a specific tag, don't float on a branch). The plugin calls QGC internal APIs and is **not standalone**.

---

## Step 1: Fork & Checkout QGC v5.0

```bash
git clone --recursive -j8 https://github.com/<your-fork>/qgroundcontrol.git
cd qgroundcontrol
git checkout tags/v5.0.8 -b custom-base-v5.0.8
git submodule update --init --recursive
```

> If `tags/v5.0.8` doesn't resolve, your fork may have been created with GitHub's "copy default branch only" option, which excludes tags. Add the real upstream and pull tags from there:
> ```bash
> git remote add upstream https://github.com/mavlink/qgroundcontrol.git
> git fetch upstream --tags
> git checkout tags/v5.0.8 -b custom-base-v5.0.8
> ```

**Qt version: 6.8.3 — exactly, no other version.** This is not a suggestion; QGC's own build docs state that other Qt versions (including newer ones like 6.10.x) carry real risk of stability/safety bugs even if the code compiles. Install it via the Qt Online Installer (Custom Installation → check "Archive" if 6.8.3 isn't shown by default → Qt 6.8.3 → Desktop gcc 64-bit on Linux), not your distro's packages — QGC needs private Qt headers that distro packages don't ship.

No version-minimum override is needed in `CustomOverrides.cmake`. If your CMake configure ever complains about a Qt version *higher* than 6.8.3 being required, that almost always means you've drifted onto the `master` branch's `CMakeLists.txt` (which currently targets a newer/unreleased Qt) rather than the `v5.0.8` tag — check your `git log` / `git describe`, don't patch around it.

---

## Step 2: Copy the Plugin

Place the entire `custom/` directory from this bundle into the QGC source root:

```bash
cp -r custom/ ~/qgroundcontrol/custom/
```

**Verify the file structure:**

```bash
ls ~/qgroundcontrol/custom/
```

Expected layout (matches upstream's own `custom-example/` scaffold, which is the supported starting point — `cp -r custom-example custom` if you're starting fresh):

```
custom/
├── CMakeLists.txt                  # Primary CMake build integration
├── cmake/
│   └── CustomOverrides.cmake       # App branding, feature toggles, MAVLink dialect/tag pin
├── src/
│   ├── PreflightPlugin.cpp/.h      # Entry point — subclasses QGCCorePlugin
│   ├── core/                       # Check framework + check implementations
│   ├── adapters/                   # Telemetry bridge
│   ├── controllers/                # Hardware test controllers
│   ├── mission/                    # Waypoint math
│   ├── ui/                         # QML model adapters
│   └── utils/                      # Utilities
├── qml/                            # Custom QML
│   ├── cpts/                       # Checklist card components
│   ├── pages/                      # Full-screen pages
│   └── singletons/                 # QML singletons
├── custom.qrc                      # Qt resource bindings
├── qgroundcontrol.exclusion        # Excludes specific stock QML/resources from the bundle, if needed
├── SRS_AUDIT.md                    # Requirements traceability
└── INTEGRATION_GUIDE.md            # This file
```

**Do not include:** `CustomBuild.cmake`, `qt6_meta_fixes.h`, or any file referencing a hardcoded local path (e.g. `/tmp/...`). If any of these exist in your bundle, they're leftovers from a prior, incorrect setup attempt and should be deleted — they have no role in a correct `QGC_CUSTOM_BUILD` integration.

`custom.pri` / `custom_deploy.pri`, if present, are qmake-era artifacts. QGC v5.0 is CMake-only (qmake support was removed), so these are inert. Fine to delete; harmless to leave if you'd rather keep them for reference.

---

## Step 3: Configure CMake Build

From the QGC source root:

```bash
~/Qt/6.8.3/gcc_64/bin/qt-cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DQGC_CUSTOM_BUILD=ON
```

(Adjust the `qt-cmake` path to wherever your Qt 6.8.3 install actually landed — confirm with `ls ~/Qt/6.8.3/gcc_64/bin/qt-cmake` first.)

**Explanation of options:**

| Option | Value | Why |
|---|---|---|
| `QGC_CUSTOM_BUILD` | `ON` | The real flag. Triggers `add_subdirectory(custom)` in the root `CMakeLists.txt`. There is no `QGC_CUSTOM_DIR` path option — the build expects `custom/` at the repo root, full stop. |
| `CMAKE_BUILD_TYPE` | `Debug` (or `Release`) | Debug for development; switch to Release for anything you're actually deploying to the Jetson. |

There is no need to disable GStreamer or unity builds to get a custom plugin working — neither is something the `custom/` mechanism conflicts with. If you hit a real, reproducible build error involving either, troubleshoot that specific error rather than disabling the feature wholesale.

### Verify CMake picked up your plugin

There's no fixed banner text to grep for (that depends on what your `CustomOverrides.cmake`/`CustomPlugin.cc` actually print, if anything). The reliable check is structural:

```bash
grep "QGC_CUSTOM_BUILD" build/CMakeCache.txt
# Expect: QGC_CUSTOM_BUILD:BOOL=ON

ninja -C build -t targets all 2>&1 | grep -i "custom/src"
# Expect: your custom .cpp files listed as build targets
```

If `custom/src` files don't show up in the target list, `add_subdirectory(custom)` isn't being reached — double check the flag name and that `custom/` actually exists at repo root (not nested under some other path).

---

## Step 4: Confirm No Upstream Files Are Modified

This step replaces v1.0's "Apply Upstream Source Fixes." There should be **zero modified files outside `custom/`**. Verify:

```bash
git status --porcelain | grep -v '^?? custom/'
```

Expect empty output. If this prints any tracked file outside `custom/`, stop and figure out why before proceeding — every upstream edit is something you'll have to manually re-apply (and re-verify) on every future `git merge`/rebase onto a newer `v5.0.x` tag. That ongoing cost is the entire reason the `QGC_CUSTOM_BUILD` mechanism exists.

If you genuinely hit something that *seems* to require an upstream change (a missing QML import, a type not resolving, etc.), the supported paths are, in order of preference:
1. **Resource override** — `qgroundcontrol.exclusion` / `qgcresources.exclusion` to swap out a specific stock QML/resource file with your own, without touching the original.
2. **A new C++ type registered via `QML_ELEMENT`** in `custom/`, rather than modifying an existing QGC class.
3. Only as a last resort, and with a clear comment explaining why: an actual upstream patch, tracked separately so you know exactly what to re-apply after every tag bump.

If you're hitting a *specific* compiler or QML error and aren't sure which of these applies, paste the actual error — don't assume a source patch is the only fix without confirming the error is real and reproducible on the documented Qt 6.8.3 / v5.0.8 combination first.

---

## Step 5: Verify CMake Configuration

```bash
ninja -C build -t targets all 2>&1 | grep -i custom
```

Should list your custom plugin's object/source targets. If the list is empty, see Step 3's verification check above before going further.

---

## Step 6: Build

```bash
cmake --build build --config Debug -j$(nproc)
```

**Expected artifact:** `build/Debug/QGroundControl` (the executable keeps the stock name unless you've deliberately set `QGC_APP_NAME` in `CustomOverrides.cmake` — that's an optional branding choice, not a requirement, and isn't load-bearing for whether the plugin itself works).

```bash
file build/Debug/QGroundControl
# Expected: ELF 64-bit LSB executable, dynamically linked
```

**If the build fails with something like:**
```
ninja: error: '.../some-submodule-path/some-file.xml', needed by '...', missing and no known rule to make it
```
That's a submodule sync issue, not a real bug in your plugin — checking out a tag doesn't auto-update submodules. Fix:
```bash
git submodule update --init --recursive
```
This is worth knowing as a recurring step: re-run it after *any* checkout/pull, every time.

---

## Step 7: Verify Runtime

### 7.1 Basic launch test

```bash
./build/Debug/QGroundControl
```

Confirm: the app window opens, and your custom UI (checklist panel, toolbar changes, whatever you've added) appears rather than the stock layout. That's your actual integration proof — not a specific log line, since what gets logged depends entirely on what your `PreflightPlugin` constructor chooses to print.

For headless/CI smoke testing instead:
```bash
export QT_QPA_PLATFORM=offscreen
timeout 15 ./build/Debug/QGroundControl
```
Expect it to start and not immediately crash. DBus/video-manager warnings in a headless environment are normal and not a sign of a problem there specifically.

### 7.2 Unit tests

If you've written tests for your check-evaluation logic (recommended — see the earlier phased plan), build with `QGC_UNITTEST_BUILD` and run via QGC's documented unit test process (copy `deploy/qgroundcontrol-start.sh` into the build output directory first, per QGC's own dev docs).

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| `'tags/v5.0.8' is not a commit` | Fork was created with "copy default branch only," which excludes tags | `git remote add upstream ...; git fetch upstream --tags` |
| `ninja: error: ... missing and no known rule to make it` (submodule path) | Submodules not synced after checkout | `git submodule update --init --recursive` |
| `custom/src` files don't appear in `ninja -t targets` | `QGC_CUSTOM_BUILD` not set, or `custom/` isn't at repo root | `grep QGC_CUSTOM_BUILD build/CMakeCache.txt`; reconfigure with `-DQGC_CUSTOM_BUILD=ON` if missing |
| `/usr/lib/x86_64-linux-gnu/libstdc++.so.6: version 'GLIBCXX_3.4.20' not found` at runtime | Older system libstdc++ | `sudo apt-get install libstdc++6`, or update gcc |
| CMake demands a Qt version newer than 6.8.3 | You're likely on `master`'s `CMakeLists.txt`, not the `v5.0.8` tag | `git describe --tags` to confirm which commit you're actually on |
| Binary launches but shows stock QGC UI, no custom plugin behavior | `add_subdirectory(custom)` not reached | Re-check Step 3/Step 5 verification commands |
| A real, reproducible compiler error referencing a specific upstream header | Possible — but confirm it's reproducible on a clean `v5.0.8` + Qt 6.8.3 checkout *without* your plugin first, to isolate whether it's pre-existing or caused by your code | See Step 4 for the supported resolution order |

---

## Reference: Key Files & Their Roles

| File | Role | Critical? |
|---|---|---|
| `custom/CMakeLists.txt` | Build integration — globs your sources, sets include paths | YES |
| `custom/cmake/CustomOverrides.cmake` | App branding override, feature toggles, MAVLink dialect/tag pin | YES |
| `custom/src/PreflightPlugin.cpp/.h` | Plugin entry point — subclasses `QGCCorePlugin` | YES |
| `custom/src/core/PreflightManager.cpp` | Registers your preflight checks | YES |
| `custom/src/core/ArmingGate.cpp` | `MAV_CMD` intercept for arming, if hard-gating (vs. advisory-only) | Conditional — see earlier discussion on gating risk |
| `custom/src/adapters/TelemetryBridge.cpp` | `Vehicle` → QML telemetry bridge | YES |
| `custom/custom.qrc` | Qt resource bindings for your QML/assets | YES |
| `custom/qgroundcontrol.exclusion` | Excludes specific stock files from the resource bundle, only if you're genuinely overriding one | As needed |

---

## Reference: Upstream Files Modified

**None.** Zero upstream files should be modified. This is intentional, not an oversight — it's what keeps a future `git merge`/rebase onto `v5.0.9`, `v5.0.10`, etc. a clean operation instead of a manual re-patch exercise every release.

If this section ever needs an entry, treat that as a flag to revisit Step 4 before proceeding, not as routine.

---

## Appendix: CMake Output Checks

```bash
# Confirm the custom build flag is actually on
grep "QGC_CUSTOM_BUILD" build/CMakeCache.txt

# Confirm your custom sources are in the build graph
ninja -C build -t targets all 2>&1 | grep -c "custom/src"
# Should be > 0

# Confirm which commit/tag you're actually built from (sanity check against drift)
git describe --tags
```

---

*Document v1.1 — corrected for Qt 6.8.3 / QGC v5.0.8. Re-verify version numbers and flags against QGC's official docs (docs.qgroundcontrol.com) before pinning to a newer tag.*
