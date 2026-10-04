#!/usr/bin/env python3
"""pre-commit hook: run Qt's qmllint over changed QML files.

This is the gate that would have caught the ``_canTest`` property typo, a QML
call into a C++ method that is not Q_INVOKABLE, and a stray syntax error left in
custom/qml/pages/GimbalTest.qml -- all of which reached the working tree because
no PR touching custom/** ran any check.

qmllint needs the same import search path the application uses, otherwise every
file trips "Type is not installed" style noise.
"""
import os
import shutil
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Directories that must be importable for qmllint to resolve QGroundControl.*
# singletons/properties.  Kept in sync with the modules registered by CMake.
IMPORT_DIRS = [
    "custom/qml",
    "custom/qml/cpts",
    "src/UI",
    "src/UI/toolbar",
    "src/FlightDisplay",
    "src/FlightMap",
    "src/FlightMap/MapItems",
    "src/QmlControls",
    "src/AnalyzeView",
    "src/QmlControls/ScreenTools",
    "src/FlightDisplay/MapItems",
]


def find_qmllint():
    """Locate qmllint: explicit override, then PATH, then common Qt layouts."""
    override = os.environ.get("QMLLINT")
    if override:
        return override
    found = shutil.which("qmllint") or shutil.which("qmllint6")
    if found:
        return found
    for root in ("/usr/lib/qt6/bin", "/usr/bin", os.path.expanduser("~/Qt")):
        candidate = os.path.join(root, "bin", "qmllint")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
        if os.path.isfile(candidate):
            return candidate
    for base in sorted(os.path.expanduser("~/Qt").glob("*/gcc_64/bin/qmllint"), reverse=True):
        return str(base)
    return None


def main(argv):
    files = [a for a in argv[1:] if a.endswith(".qml") and os.path.isfile(a)]
    if not files:
        return 0

    # Never gate on files that a custom build intentionally shadows into a
    # different directory layout; they are linted from their real location.
    qmllint = find_qmllint()
    if not qmllint:
        print("qmllint not found; set QMLLINT=/path/to/qmllint to enable this hook")
        return 0

    imports = [os.path.join(REPO_ROOT, d) for d in IMPORT_DIRS]
    imports = [d for d in imports if os.path.isdir(d)]

    failures = []
    for path in files:
        cmd = [qmllint]
        for inc in imports:
            cmd += ["-I", inc]
        cmd.append(path)
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            failures.append((path, (proc.stdout + proc.stderr).strip()))

    for path, output in failures:
        print(f"qmllint failed for {path}:\n{output}\n", file=sys.stderr)

    if failures:
        print(f"{len(failures)} QML file(s) failed qmllint.", file=sys.stderr)
        return 1

    print(f"qmllint: {len(files)} file(s) clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
