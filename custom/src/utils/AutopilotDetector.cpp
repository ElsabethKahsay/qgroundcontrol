// AutopilotDetector — USB autopilot hot-plug detection with priority ordering

#include "AutopilotDetector.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>
