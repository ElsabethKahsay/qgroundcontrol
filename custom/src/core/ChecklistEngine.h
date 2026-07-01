// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: src/ChecklistEngine.h
// Description: Auto-evaluation engine that binds TelemetryBridge properties
//              to ChecklistItemModel items. When telemetry changes, auto items
//              are re-evaluated against their requiredValue ± tolerance.
//              Manual items require operator confirmation via confirmItem().

#pragma once
