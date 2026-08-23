# UAV Preflight QGC — Lead Developer Overview

> Written for a .NET/C# developer. Maps C++/Qt/QML concepts to familiar C#/ASP.NET/WPF patterns.

---

## Table of Contents

1. [Project Structure](#1-project-structure-solution-layout)
2. [Application Startup](#2-application-startup-programcs--preflightplugincs)
3. [Complete Telemetry Pipeline](#3-complete-telemetry-pipeline-mavlink-wire--qml-screen)
4. [Database Layer](#4-database-layer-entity-framework-core--databasemanagercs)
5. [UI Framework](#5-ui-framework-razor-pages--wpf--qml)
6. [Weather API Integration](#6-weather-api-integration-httpclient--weatherprovidercs)
7. [Vehicle Detection & Registry](#7-vehicle-detection--registry)
8. [Checklist & Rule Engine (Deep Dive)](#8-checklist-rule-engine-deep-dive)
9. [Arming Gate (Deep Dive)](#9-arming-gate-auth-guard-deep-dive)
10. [Motor/Servo Hardware Test (Deep Dive)](#10-motor-servo-hardware-test-deep-dive)
11. [VehicleProfileManager](#11-vehicleprofilemanager)
12. [Key Architecture Patterns](#12-key-architecture-patterns)
13. [Build System](#13-build-system-msbuild--cmake)
14. [Feature Inventory](#14-current-feature-inventory)
15. [Key Gotchas](#15-key-gotchas-things-that-differ-from-net)

---

## 1. Project Structure (Solution Layout)

```
/ ── Root (QGroundControl upstream — treat as "third-party SDK")
└── custom/ ── Our code (like your app layer)
    ├── src/              → C++ (like your C# class libraries / backend)
    │   ├── PreflightPlugin.cpp  → App entry point, DI, service registration
    │   ├── adapters/            → Adapter layer between QGC SDK and our app
    │   ├── core/                → Business logic / domain services
    │   ├── controllers/         → Hardware interaction controllers
    │   ├── detection/           → Vehicle detection service
    │   └── utils/               → Utility services (DB, export, weather, templates)
    ├── qml/                    → UI layer (like your Razor Pages / WPF XAML)
    │   ├── pages/              → Full-page views
    │   ├── cpts/               → Reusable custom controls (user controls)
    │   ├── singletons/         → Theme & configuration
    │   └── analyze/            → Analyze tool views
    └── custom.qrc              → Resource manifest (like .resx)
```

| This project | C# / .NET Equivalent |
| --- | --- |
| `custom/` | Your app project |
| `QGroundControl/` | NuGet SDK package (reference only) |
| `PreflightPlugin.cpp` | `Startup.cs` / `Program.cs` |
| `VehicleProfileManager` | Scoped service `IVehicleProfileService` |
| `DatabaseManager` | `DbContext` with Dapper-style SQL |
| `.qml` files | `.cshtml` Razor Pages + `.xaml` hybrid |
| `Colors.qml`, `Config.qml` | CSS variables + `appsettings.json` |
| `custom.qrc` | `.resx` embedded resource manifest |

---

## 2. Application Startup (Program.cs → PreflightPlugin.cpp)

**File:** `custom/src/PreflightPlugin.cpp`

```csharp
// PreflightPlugin.cs — Implements IQGCCorePlugin (like IHostedService / IStartupFilter)
public class PreflightPlugin : QGCCorePlugin
{
    public override void Initialize()
    {
        // 1. Load custom font (like registering a font in Blazor CSS)
        LoadFont("Abel-Regular.ttf");

        // 2. Create singleton services
        _weatherProvider = new WeatherProvider();
        _telemetryBridge = new TelemetryBridge(this);
        _vehicleProfileManager = new VehicleProfileManager(this);
        _preflightManager = new PreflightManager(this);
        _checklistModel = new PreflightChecklistModel(this);
        _databaseManager.Initialize();  // Open SQLite connection

        // 3. Register services into QML (like @inject in Razor Pages)
        RegisterQmlSingleton("WeatherProvider", _weatherProvider);
        RegisterQmlSingleton("VehicleProfileManager", _vehicleProfileManager);
        RegisterQmlSingleton("Database", _databaseManager);
    }
}
```

**QML gets C# objects via** `setContextProperty()`:

```csharp
// Like: services.AddSingleton<IVehicleProfileService>();
// Then in QML: VehicleProfileManager.propertyName
qmlEngine.RootContext.SetContextProperty("VehicleProfileManager", _vehicleProfileManager);
```

---

## 3. Complete Telemetry Pipeline (MAVLink Wire → QML Screen)

This is the most important data flow in the system. It's like a **SignalR hub** receiving telemetry and broadcasting typed properties to the UI.

### 3.1 Architecture Overview

```
MAVLink (UDP/Serial)
  ↓  (50 Hz message stream)
Vehicle (QGC SDK)
  ↓  (C# events: mavlinkMessageReceived, Fact::valueChanged)
TelemetryBridge (our adapter)
  ↓  (Q_PROPERTY with NOTIFY signals)
QML Bindings (Text { text: TelemetryProvider.roll.toFixed(1) })
  ↓
Screen
```

### 3.2 TelemetryBridge — The Hub Client (754 lines)

**File:** `custom/src/adapters/TelemetryBridge.cpp`

```csharp
// TelemetryBridge.cs — Like a SignalR hub client that:
//   1. Subscribes to raw MAVLink messages
//   2. Decodes each message type into typed C# properties
//   3. Emits PropertyChanged only when value actually changes (epsilon filter)
//   4. Also subscribes to QGC's Fact system for pre-decoded values

public class TelemetryBridge : QObject, INotifyPropertyChanged
{
    // ——— Input Sources ———

    // Source 1: Raw MAVLink message stream (like hub.On("message"))
    vehicle.MavlinkMessageReceived += OnMavlinkMessage;

    // Source 2: QGC Fact system (vehicle already decoded these from HEARTBEAT etc.)
    vehicle.AltitudeRelative.ValueChanged += OnAltChanged;
    vehicle.Heading.ValueChanged += OnHeadingChanged;
    vehicle.GroundSpeed.ValueChanged += OnGroundSpeedChanged;

    // Source 3: Battery FactGroup (QGC models battery as a set of Facts)
    foreach (var battery in vehicle.Batteries) {
        battery.Voltage.ValueChanged += v => BatteryVoltage = v;
        battery.Current.ValueChanged += v => BatteryCurrent = v;
        battery.Percent.ValueChanged += v => BatteryPercent = v;
    }

    // ——— 50+ Q_PROPERTIES that QML can bind to ———
    public double Roll { get; private set; }       // → QML: TelemetryProvider.roll
    public double Pitch { get; private set; }
    public double Yaw { get; private set; }
    public double BatteryVoltage { get; private set; }
    public double GpsLatitude { get; private set; }
    public double GpsLongitude { get; private set; }
    public int GpsFixType { get; private set; }
    public int GpsSatellites { get; private set; }
    public bool IsConnected { get; private set; }
    public string ConnectionStatus { get; private set; }
    // ... ~45 more properties
}
```

### 3.3 Raw MAVLink Message Handling

The `OnMavlinkMessage` method is a **giant switch statement** (lines 373-692) handling 20+ MAVLink message IDs:

```csharp
private void OnMavlinkMessage(int msgId, byte[] payload)
{
    switch (msgId) {
        case 24:  // GPS_RAW_INT
            _gpsLatitude = DecodeLat(payload) / 1e7;  // MAVLink stores lat * 1e7
            _gpsLongitude = DecodeLon(payload) / 1e7;
            _gpsFixType = payload[6];                  // 0=NoGPS, 3=3D, 6=RTK
            EmitIfChanged(nameof(GpsLatitude));         // Only emit if actually changed
            EmitIfChanged(nameof(GpsLongitude));
            break;

        case 30:  // ATTITUDE
            _roll = DecodeFloat(payload, 0);            // radians from MAVLink
            _pitch = DecodeFloat(payload, 4);
            _yaw = DecodeFloat(payload, 8);
            EmitIfChanged(nameof(Roll));
            EmitIfChanged(nameof(Pitch));
            EmitIfChanged(nameof(Yaw));
            break;

        case 1:   // SYS_STATUS
            _sensorHealth = CheckBit(payload[4], 0);    // Bitmask decode
            _ahrsHealth = CheckBit(payload[4], 2);
            _sysVoltageBattery = DecodeUShort(payload, 6) / 1000.0;
            EmitIfChanged(nameof(SensorHealth));
            break;

        case 339: // ESTIMATOR_STATUS (PX4)
        case 193: // EKF_STATUS_REPORT (ArduPilot)
            DecodeEstimatorFlags(payload);              // Complex bitfield decode
            break;

        case 37:  // SERVO_OUTPUT_RAW
            for (int i = 0; i < 16; i++)
                _motorOutputs[i] = payload[6 + i*2];    // Raw PWM values
            EmitIfChanged(nameof(MotorOutputs));
            break;

        case 1100: // ESC_TELEMETRY_1_TO_4
        case 1101: // ESC_TELEMETRY_5_TO_8
        case 1102: // ESC_TELEMETRY_9_TO_12
            DecodeEscTelemetry(payload);
            break;
    }
}
```

### 3.4 Epsilon Filtering (Performance Optimization)

To prevent **infinite QML re-renders** from noisy sensor data, every property setter has an epsilon check:

```csharp
// In the Roll setter:
if (Math.Abs(newValue - _roll) < 0.001) return;  // Ignore micro-changes
_roll = newValue;
EmitPropertyChanged(nameof(Roll));  // → QML Text re-evaluates

// For heading (degrees):
if (Math.Abs(newValue - _heading) < 0.5) return;  // 0.5° deadband

// For GPS:
if (Math.Abs(newLat - _gpsLatitude) < 1e-7) return;  // ~1cm

// For voltage:
if (Math.Abs(newV - _sysVoltageBattery) < 0.01) return;  // 10mV
```

### 3.5 Deferred Parameter Loading (200ms Timer)

There is a **vestigial 200ms timer** that checks if vehicle parameters are ready:

```csharp
_updateTimer = new Timer(200);  // 5 Hz
_updateTimer.Elapsed += () => {
    if (_vehicle.ParametersReady && !_parametersLoaded) {
        LoadParameters();
        _parametersLoaded = true;
    }
};
_updateTimer.Start();
```

After parameters load once, this timer becomes a **no-op**. The real data flow is entirely event-driven.

### 3.6 QML Binding — How the Screen Updates

**File:** `custom/qml/cpts/FlyViewTelemetryStrip.qml`

```qml
// Like Blazor @bind / WPF {Binding}
// When TelemetryBridge.roll changes → C# emits RollChanged → QML re-evaluates this:
readonly property real _roll: activeVehicle ? activeVehicle.roll.rawValue : 0
readonly property real _pitch: activeVehicle ? activeVehicle.pitch.rawValue : 0
readonly property real _hdg: activeVehicle ? activeVehicle.heading.rawValue : 0
readonly property real _altRel: activeVehicle ? activeVehicle.altitudeRelative.rawValue : 0
readonly property real _gndSpd: activeVehicle ? activeVehicle.groundSpeed.rawValue : 0
readonly property real _distHome: activeVehicle ? activeVehicle.distanceToHome.rawValue : 0
readonly property real _flightDist: activeVehicle ? activeVehicle.flightDistance.rawValue : 0

// Haversine distance to plan waypoint (custom addition):
readonly property real _planDist: _haversine(
    _currLat, _currLon,
    VehicleProfileManager.planLatitude,
    VehicleProfileManager.planLongitude)

// Display helper: returns "--" for invalid values instead of "NaN"
function _formatVal(val, decimals) {
    if (!activeVehicle || val === undefined || val === null || isNaN(val)) return "--"
    return val.toFixed(decimals)
}
```

**The `TelColumn` Repeater** (lines 334-388) renders each telemetry row:

```
  GPS:    3D Fix           (label: value, colored by status)
  Sats:   18 sat           (label: number)
  Alt:    123.4 m          (label: formatted)
  Hdg:    ↑ 45°            (label: arrow + degrees)
  Dist:   567 m            (label: distance to home)
  Flight: 1234 m           (label: flight distance)
  Plan:   890 m            (label: distance to plan waypoint)
```

### 3.7 Complete Data Path Summary

```
[UDP/Serial] → MAVLink packet arrives
  ↓
QGC MAVLink stack parses into mavlink_message_t
  ↓
Vehicle.mavlinkMessageReceived(message) — C# event
  ↓ [OR]
Vehicle battery/gps/altitude Fact::valueChanged — C# event
  ↓
TelemetryBridge._handleMavlinkMessage() or lambda callback
  ↓  epsilon filter (ignore noise)
  ↓  store value
  ↓  emit PropertyChanged (e.g. RollChanged)
  ↓
QML binding engine detects RollChanged
  ↓  re-evaluates Text { text: TelemetryProvider.roll.toFixed(1) }
  ↓
Qt Quick scene graph renders text on screen
  ↓
[Total latency: ~5-20ms from MAVLink arrival to screen update]
```

---

## 4. Database Layer (Entity Framework Core → DatabaseManager.cs)

### 4.1 Singleton Connection

```csharp
// DatabaseManager.cs — Singleton like DbContext with Dapper
public class DatabaseManager
{
    private static readonly Lazy<DatabaseManager> _instance = new(() => new());
    public static DatabaseManager Instance => _instance.Value;

    private SQLiteConnection _db;

    public void Initialize(string path = null)
    {
        path ??= Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "UAVSystems/UAVPreFlight/uav_preflight_data.db");

        _db = new SQLiteConnection($"Data Source={path}");
        _db.Open();
        CreateTables();      // EnsureCreated equivalent
        MigrateSchema();     // EF Core migrations equivalent
    }
}
```

### 4.2 Schema Migrations (v1–v5)

```csharp
// Like EF Core Migration classes
private void MigrateSchema()
{
    int current = GetSchemaVersion();  // SELECT version FROM schema_version
    if (current < 1) { /* CREATE TABLE flight_sessions */ }
    if (current < 2) { /* ALTER TABLE checklist_templates ... */ }
    if (current < 3) { /* CREATE TABLE vehicles */ }
    if (current < 4) { /* ALTER TABLE vehicles ADD fingerprint, compid, ... */ }
    if (current < 5) { /* ALTER TABLE flight_sessions ADD plan_lat, plan_lon */ }
    SetSchemaVersion(5);
}
```

### 4.3 Entity Models

```csharp
[Table("vehicles")]
public class VehicleEntity
{
    [Key, AutoIncrement] public int Id { get; set; }
    public long DeviceUid { get; set; }
    public string Fingerprint { get; set; }  // SHA256(UID + autopilotType + boardVersion)
    public string FriendlyName { get; set; }
    public string AutopilotType { get; set; }
    public string AirframeType { get; set; }
    public string FirmwareVersion { get; set; }
    public string BoardVersion { get; set; }
    public int TotalFlightCount { get; set; }
    public double TotalFlightHours { get; set; }
    public DateTime LastSeen { get; set; }
}

[Table("flight_sessions")]
public class FlightSessionEntity
{
    [Key, AutoIncrement] public int Id { get; set; }
    public string DeviceUid { get; set; }
    public double PayloadWeightKg { get; set; }    // v3 migration
    public string LocationName { get; set; }        // v3 migration
    public double PlanLatitude { get; set; }        // v5 migration
    public double PlanLongitude { get; set; }       // v5 migration
    public DateTime StartedAt { get; set; }
    public double DurationSeconds { get; set; }
}

[Table("checklist_templates")]
public class ChecklistTemplateEntity
{
    [Key] public string TemplateId { get; set; }
    public string VehicleType { get; set; }
    public string TemplateName { get; set; }
    public string ItemsJson { get; set; }            // JSON array (v2 migration)
    public bool IsDefault { get; set; }
    public DateTime CreatedAt { get; set; }
}

[Table("schema_version")]
public class SchemaVersion
{
    [Key] public int Version { get; set; }
    public DateTime AppliedAt { get; set; }
}
```

### 4.4 Key Operations

```csharp
public class DatabaseManager
{
    // CRUD for vehicles
    public void RegisterNewVehicle(string fingerprint, int sysId, int compId,
        string autopilotType, string airframeType, string firmwareVersion,
        ulong uid, string boardVersion, string name)
    {
        Execute(@"INSERT OR REPLACE INTO vehicles
            (device_uid, friendly_name, autopilot_type, airframe_type,
             fingerprint, compid, firmware_version, board_version)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            uid, name, autopilotType, airframeType,
            fingerprint, compId, firmwareVersion, boardVersion);
    }

    public string GetAllVehiclesJson()
    {
        // Returns JSON array for QML: [{deviceUid, friendlyName, autopilotType, ...}]
        return ExecuteQueryAsJson(
            "SELECT * FROM vehicles ORDER BY last_seen DESC");
    }

    public int StartFlightSession(string deviceUid, string batterySerial,
        double payloadWeightKg)
    {
        Execute(@"INSERT INTO flight_sessions
            (device_uid, battery_serial, payload_weight_kg, started_at)
            VALUES (?, ?, ?, datetime('now'))",
            deviceUid, batterySerial, payloadWeightKg);
        return LastInsertRowId();
    }

    public void UpdateFlightSessionPlanLocation(int sessionId,
        double lat, double lon)
    {
        Execute(@"UPDATE flight_sessions
            SET plan_lat = ?, plan_lon = ? WHERE id = ?",
            lat, lon, sessionId);
    }
}
```

---

## 5. UI Framework (Razor Pages + WPF → QML)

QML is a declarative UI language — think **Razor Pages** (declarative HTML-like syntax + data binding) mixed with **WPF XAML** (property binding, triggers, animations).

### 5.1 Page Structure

```qml
// PreFlightChecklist.qml — Like a .cshtml Razor Page
Page {                          // @page "/checklist"
    id: root                    // Page identifier (like @code { ... })

    // Page-level properties (like [Parameter] / @bind)
    readonly property bool isSingle: width < 1000

    // Local font size overrides for this page
    readonly property int _fsBody: Config.fontSizeBody + 3
    readonly property int _fsSmall: Config.fontSizeSmall + 3

    // Layout hierarchy (like HTML divs + CSS flexbox)
    ColumnLayout {               // display: flex; flex-direction: column
        Rectangle {              // <div style="background: ...">
            RowLayout {          // display: flex; flex-direction: row
                TelRow { label: "Power"       // Custom component
                    TelVal { text: TelemetryProvider.batteryVoltage.toFixed(2) + " V" }
                }
            }
        }
    }

    // Event handlers (like @onclick)
    Button {
        onClicked: VehicleProfileManager.SetPayloadWeightKg(parseFloat(text))
    }

    // Property change watchers (like OnParametersSetAsync)
    Connections {
        target: VehicleProfileManager
        function onPayloadWeightChanged() { /* re-run validation */ }
    }
}
```

### 5.2 Custom Components (User Controls)

```qml
// CustomButton.qml — Like a partial view / Blazor component
component CustomButton : Button {
    [Parameter] public Color BaseColor { get; set; } = Colors.Primary;
    [Parameter] public bool Rounded { get; set; } = false;

    // Render body (like Razor @body / RenderFragment)
    override void BuildRenderTree()
    {
        <Text text="@Text" bold letterSpacing="0.5" color="white" />
        <Background>
            <Rectangle radius="@(Rounded ? Height/2 : 6)"
                       color="@(Enabled ? BaseColor : Gray)"
                       shadow="true" />
        </Background>
    }
}
```

### 5.3 Theme System (CSS Variables + appsettings.json)

```qml
// Colors.qml — Like :root CSS custom properties
// Usage in any page: Rectangle { color: Colors.background }
Colors.background   → "#0f1117"    // Dark slate
Colors.surface      → "#1a1d27"    // Card background
Colors.primary      → "#f59e0b"    // Amber accent
Colors.secondary    → "#f97316"    // Coral accent
Colors.textPrimary  → "#e8eaf0"    // Light text
Colors.textSecondary→ "#9ca3af"    // Muted text
Colors.border       → "#2d3144"    // Subtle border
Colors.success      → "#34d399"    // Green
Colors.error        → "#fb7185"    // Pink-red
Colors.warning      → "#fbbf24"    // Amber

// Config.qml — Like appsettings.json sections
Config.fontSizeH1    → 16
Config.fontSizeBody  → 11
Config.spacingMedium → 10
Config.radiusMedium  → 6
Config.fontFamily    → "Abel"
```

---

## 6. Weather API Integration (HttpClient → WeatherProvider.cs)

```csharp
// WeatherProvider.cs — Singleton, like a typed HttpClient
public class WeatherProvider : QObject, INotifyPropertyChanged
{
    private HttpClient _client = new();
    private MemoryCache _cache = new(50 * 1024 * 1024);  // 50 MB
    private DateTime _metarTimestamp;

    // Properties QML binds to:
    public double Temperature { get; private set; }    // °C
    public double Humidity { get; private set; }       // %
    public int WeatherCode { get; private set; }       // WMO code (0=clear, 3=overcast, etc.)
    public double WindSpeed { get; private set; }      // m/s
    public double WindDirection { get; private set; }  // degrees
    public double VisibilityKm { get; private set; }
    public double WindGust { get; private set; }
    public bool Loading { get; private set; }
    public string MetarString { get; private set; }
    public string TafString { get; private set; }

    // Hardcoded values set at build time:
    //   Transfer timeout: 4 seconds (was 15)
    //   Cache staleness:  60 minutes (was 15)
    //   Cache size:       50 MB (was 5)
    //   ICAO lookup:      2.5s timeout + 3s guard (was 5s / 6s)

    public async Task FetchWeather(double lat, double lon)
    {
        _loading = true;
        EmitPropertyChanged(nameof(Loading));

        var url = $"https://api.open-meteo.com/v1/forecast" +
                  $"?latitude={lat}&longitude={lon}" +
                  $"&current=temperature_2m,relative_humidity_2m," +
                  $"weather_code,wind_speed_10m,wind_direction_10m,visibility";

        var request = new HttpRequestMessage(HttpMethod.Get, url);
        request.Properties["CacheMode"] = CacheMode.PreferExisting;
        request.Timeout = TimeSpan.FromSeconds(4);

        var response = await _client.SendAsync(request);
        var json = await response.Content.ReadAsJsonAsync();

        _temperature = json["current"]["temperature_2m"];
        _humidity = json["current"]["relative_humidity_2m"];
        _weatherCode = json["current"]["weather_code"];
        _metarTimestamp = DateTime.UtcNow;

        _loading = false;
        EmitPropertyChanged(nameof(Temperature));
        EmitPropertyChanged(nameof(Humidity));
        EmitPropertyChanged(nameof(WeatherDescription));  // computed
    }

    public string WeatherDescription => _weatherCode switch {
        0 => "Clear sky",
        1..2 => "Partly cloudy",
        3 => "Overcast",
        45..48 => "Foggy",
        51..55 => "Drizzle",
        61..65 => "Rain",
        71..77 => "Snow",
        80..82 => "Rain showers",
        95..99 => "Thunderstorm",
        _ => "Unknown"
    };
}
```

**Auto-fetch on GPS change (debounced):**

```qml
// PreFlightChecklist.qml — Like a ReactiveCommand
property real _lastFetchLat: 0
property real _lastFetchLon: 0
property real _lastFetchTime: 0

Connections {
    target: TelemetryProvider
    function onGpsLatitudeChanged() {
        var lat = TelemetryProvider.gpsLatitude
        var lon = TelemetryProvider.gpsLongitude
        if (lat === 0 && lon === 0) return

        var now = Date.now()
        if (now - _lastFetchTime < 30000) return           // Max 30s between fetches
        if (Math.abs(lat - _lastFetchLat) < 0.005 &&
            Math.abs(lon - _lastFetchLon) < 0.005) return  // Or ~500m

        _lastFetchLat = lat; _lastFetchLon = lon; _lastFetchTime = now
        WeatherProvider.fetchWeather(lat, lon)
    }
}
```

---

## 7. Vehicle Detection & Registry

```csharp
// VehicleRegistry.cs — Change tracker that fires on vehicle connect/disconnect
public class VehicleRegistry : QObject
{
    private string _currentFingerprint;

    // Called when any MAVLink vehicle is detected on any link
    public void OnVehicleAdded(Vehicle vehicle)
    {
        // Extract identity from MAVLink HEARTBEAT + AUTOPILOT_VERSION
        var uid = vehicle.VehicleUID;              // From MAVLink UTM_GLOBAL_POSITION or via MAV_CMD
        var firmwareType = vehicle.FirmwareType;    // 1=PX4, 2=ArduPilot
        var boardId = vehicle.FirmwareBoardProductId;
        var version = $"{vehicle.FirmwareMajorVersion}.{vehicle.FirmwareMinorVersion}.{vehicle.FirmwarePatchVersion}";

        // Generate unique fingerprint (SHA256 of composite key)
        var fingerprint = GenerateFingerprint(uid, firmwareType, boardId);
        // Like: SHA256("1234567890" + "1" + "45") → "a1b2c3d4..."

        // Check if this vehicle was seen before
        var existing = _db.LookupByFingerprint(fingerprint);
        if (existing != null) {
            _db.UpdateLastSeen(fingerprint);       // Touch timestamp only
            Emit("KnownVehicleConnected", existing.FriendlyName);
        } else {
            _db.RegisterNewVehicle(fingerprint,     // Full insert
                vehicle.Id, vehicle.DefaultComponentId,
                GetTypeString(firmwareType),        // "PX4" or "ArduPilot"
                GetVehicleTypeString(vehicle.VehicleType),
                version, uid, boardId,
                $"UAV-{vehicle.Id}-{fingerprint[..8]}");
            Emit("NewVehicleRegistered");
        }
    }

    // SHA256(uid.ToString() + autopilotType.ToString() + boardVersion)
    // This uniquely identifies a specific physical autopilot board
    private static string GenerateFingerprint(ulong uid, int autopilotType, string boardVersion)
    {
        var data = Encoding.UTF8.GetBytes($"{uid}{autopilotType}{boardVersion}");
        return Convert.ToHexString(SHA256.HashData(data)).ToLower();
    }
}
```

---

## 8. Checklist & Rule Engine (Deep Dive)

This is the core validation pipeline — think **FluentValidation** + a **reactive property binding engine**.

### 8.1 Data Model

```csharp
// Like a FluentValidation rule
public class ChecklistItemData
{
    public string Id;             // "battery.voltage"
    public string Label;          // "Battery Voltage"
    public string BindProperty;   // "batteryVoltage" — maps to TelemetryBridge Q_PROPERTY name
    public double RequiredValue;  // 12.6 — expected nominal
    public double Tolerance;      // 0.5 — ± tolerance
    public string Unit;           // "V"
    public bool IsManual;         // true = operator must confirm manually
    public int Status;            // 0=Pending, 1=Passed, 2=Failed
    public string Message;        // "12.45 V (ok: 12.10–13.10)"
}
```

### 8.2 Dynamic Signal Binding (The Magic)

The `ChecklistEngine` uses **C# reflection via QMetaObject** to dynamically connect TelemetryBridge's property-changed signals to the evaluation engine:

```csharp
// ChecklistEngine.cs — Reactive binding engine
public class ChecklistEngine : QObject
{
    private TelemetryBridge _bridge;
    private Dictionary<string, List<int>> _bindings;
    // e.g. { "batteryVoltage": [0, 3, 7], "gpsFixType": [1, 5] }

    // Called when model is loaded or TelemetryBridge is set
    private void RebuildBindings()
    {
        _bindings.Clear();
        for (int row = 0; row < _model.Count; row++) {
            var item = _model[row];
            var prop = item.BindProperty;  // e.g. "batteryVoltage"

            // Build reverse index: property → list of rows
            if (!_bindings.ContainsKey(prop))
                _bindings[prop] = new List<int>();
            _bindings[prop].Add(row);
        }

        // Connect each unique property's NOTIFY signal to our handler
        // Using reflection instead of explicit connect() calls
        foreach (var prop in _bindings.Keys) {
            var signalName = prop + "Changed";  // e.g. "batteryVoltageChanged"
            var signal = _bridge.GetType().GetEvent(signalName);
            // Dynamically subscribe — like _bridge.batteryVoltageChanged += OnPropertyChanged;
            SubscribeDynamic(signal, OnPropertyChanged);
        }
    }

    // When ANY TelemetryBridge property changes:
    private void OnPropertyChanged(object sender, string propertyName)
    {
        // Look up which checklist items depend on this property
        if (!_bindings.TryGetValue(propertyName, out var rows))
            return;

        foreach (int row in rows)
            EvaluateItem(row);  // Re-evaluate only affected items

        EmitPropertyChanged(nameof(AllPassed));
        EmitPropertyChanged(nameof(PassedItems));
    }

    // Evaluate a single checklist item
    private void EvaluateItem(int row)
    {
        var item = _model[row];
        if (item.IsManual) return;  // Skip — waiting for user confirmation

        // Read current value from TelemetryBridge via reflection
        // Like: (double)_bridge.GetType().GetProperty(item.BindProperty).GetValue(_bridge)
        double value = ReadBridgeProperty(item.BindProperty);

        if (double.IsNaN(value)) {
            SetItemStatus(row, 0, "Waiting for telemetry");
            return;
        }

        // Range check: requiredValue ± tolerance
        double lower = item.RequiredValue - item.Tolerance;
        double upper = item.RequiredValue + item.Tolerance;

        if (value >= lower && value <= upper) {
            SetItemStatus(row, 1, $"{value:F2} {item.Unit} (ok)");
        } else {
            SetItemStatus(row, 2, $"{value:F2} {item.Unit} (expected {lower:F2}–{upper:F2})");
        }
    }
}
```

### 8.3 Full Evaluation Flow

```
MAVLink message arrives
  ↓
TelemetryBridge decodes, epsilon-filters, emits "batteryVoltageChanged"
  ↓
ChecklistEngine.OnPropertyChanged(null, "batteryVoltage")
  ↓  _bindings["batteryVoltage"] = [0, 3, 7]
  ↓
EvaluateItem(0): compare batteryVoltage (12.45) against [12.1, 13.1]
  ↓  PASSED → setItemStatus(0, 1, "12.45 V (ok: 12.10–13.10)")
  ↓
EvaluateItem(3): compare batteryVoltage against a different rule
  ↓
EvaluateItem(7): ...
  ↓
Emit AllPassed, PassedItems, FailedItems
  ↓
QML Repeater re-evaluates — green checkmarks update on screen
  ↓
ArmingGate's 500ms timer picks up AllPassed = true
  ↓  Opens gate, allows arming
```

---

## 9. Arming Gate (Auth Guard — Deep Dive)

```csharp
// ArmingGate.cs — Like an AuthorizationHandler that intercepts arm commands
public class ArmingGate : QObject
{
    // ——— Modes ———
    public enum GateMode { Passive = 0, Active = 1, Hybrid = 2 }

    // Passive:  log only, never block
    // Active:   block arm if any mandatory check fails
    // Hybrid:   block on mandatory, allow manual-only via ACK override

    // ——— State ———
    private GateMode _mode = GateMode.Active;
    private bool _overrideActive = false;  // Emergency bypass
    private bool _ackReceived = false;     // Pilot acknowledged warnings
    private string _denialReason = "";
    private DateTime _lastTelemetryTick;

    // ——— 500ms Evaluation Timer ———
    private Timer _gateTimer = new(500);  // Runs updateArmingState()

    // ——— Decision Logic ———

    // Called every 500ms by timer
    private void UpdateArmingState()
    {
        if (_overrideActive) { SetGateOpen("Override active"); return; }
        if (_mode == Passive) { SetGateOpen("Passive mode"); return; }

        // Check telemetry freshness
        if ((DateTime.UtcNow - _lastTelemetryTick).TotalSeconds > 10) {
            SetGateClosed("Telemetry stale — no update in 10s");
            return;
        }

        // Check all mandatory checks from PreflightManager
        if (_manager.AllMandatoryPassed()) {
            SetGateOpen("All checks passed");
        } else {
            SetGateClosed($"{_manager.FailedMandatoryCount} blocking check(s) failed");
        }
    }

    // ——— MAVLink Command Interception ———
    // This is the KEY method — it runs BEFORE the arm command reaches the vehicle

    public bool InterceptCommandLong(int command, double[] parameters)
    {
        // Only intercept MAV_CMD_COMPONENT_ARM_DISARM (ID=400)
        if (command != 400) return true;     // Passthrough
        if (parameters[0] != 1.0) return true;  // Disarm, not arm

        if (_overrideActive) return true;    // Emergency — let through
        if (_ackReceived) {                  // One-time bypass
            _ackReceived = false;
            return true;
        }

        // Active/Hybrid mode: block if checks failing
        if (_mode != Passive && !_manager.AllMandatoryPassed()) {
            Emit("ArmingDenied", _denialReason);
            return false;  // ← BLOCKED: command never reaches vehicle
        }

        return true;  // All good, let the arm command through
    }

    // ——— Override Mechanisms ———

    public void ForceArm()
    {
        // Operator overrides: opens gate for 10 seconds
        OverrideGate("Operator force arm", timeoutSec: 10);
        // Emits armingOverrideActivated
        // All MAVLink commands pass through during override
    }

    public void AcknowledgeOverride(string pilotName, string reason)
    {
        // Pilot acknowledged warnings
        _ackReceived = true;
        // Next arm attempt will be allowed once
    }

    // ——— Conditions Summary ———

    // ALLOW arming when:
    //   • Passive mode  OR
    //   • Override active (10s timeout)  OR
    //   • ALLOW_WITH_ACK acknowledged (one-time)  OR
    //   • All mandatory checks passed

    // BLOCK arming when:
    //   • Active/Hybrid mode AND mandatory check(s) failing  OR
    //   • Telemetry stale (>10s since last update)
}
```

### 9.1 Arming Gate State Machine

```
                  ┌─────────────────────────────┐
                  │      Gate: CLOSED           │
                  │  Arm commands intercepted   │
                  └──────────┬──────────────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
        All checks     Override       ACK received
        passed         activated      (one-time)
              │              │              │
              ▼              ▼              ▼
        ┌──────────┐  ┌──────────┐  ┌──────────┐
        │ Gate:    │  │ Gate:    │  │ Gate:    │
        │ OPEN     │  │ OPEN     │  │ OPEN     │
        │ (normal) │  │ (10s)    │  │ (1x)     │
        └──────────┘  └──────────┘  └──────────┘
```

---

## 10. Motor/Servo Hardware Test (Deep Dive)

This is a **sequential state machine** that sends MAVLink commands and verifies feedback — like an integration test that runs on real hardware.

### 10.1 State Machine

```
IDLE → TESTING (step 0 → step 1 → ... → step N) → COMPLETE (passed/failed)
```

### 10.2 Step-by-Step Flow

```
[User clicks "Run Motor Test"]
  ↓
HardwareTestController.RunMotorTest()
  ↓  Clear previous results
  ↓  Build 4 TestSteps (Motor 1..4, 35% throttle, 3s each)
  ↓
StartTest()
  ↓  Reset feedback arrays, set running=true, currentStep=0
  ↓
AdvanceStep()
  ↓
  ├─ [currentStep == 0] → skip feedback check (no previous step)
  ├─ [currentStep > 0]  → verify feedback from PREVIOUS step
  │     ↓
  │     Motor feedback: check _feedbackPwm[motor-1] is between 800–2200 µs
  │     Servo feedback: check |actualPwm - targetPwm| <= 50 µs
  │
  ├─ Send current step command via MAVLink:
  │     Motor: MAV_CMD_DO_MOTOR_TEST (ID=209)
  │       param1 = motorInstance (1–4)
  │       param2 = throttleType (0=PWM, 1=percent)
  │       param3 = throttleValue (35)
  │       param4 = durationSec (3)
  │     Servo: MAV_CMD_DO_SET_SERVO (ID=188)
  │       param1 = servoInstance
  │       param2 = targetPwm
  │
  ├─ Start one-shot timer: durationMs + settleMs (3000 + 1000 = 4s)
  ├─ Increment currentStep
  └─ Return
        ↓
      [4 seconds later — timer fires]
        ↓
      AdvanceStep() called again
        ↓  [currentStep == 1] → verify Motor 1 feedback
        ↓  Send Motor 2 command
        ↓  Start 4s timer
        ↓  ...repeat for all 4 motors...
        ↓
      [currentStep >= totalSteps] → FinishTest()
        ↓  Set running=false, progress=1.0
        ↓  Emit sequenceCompleted
        ↓  UI shows pass/fail status
```

### 10.3 MAVLink Feedback Path

```csharp
// During the test, autopilot publishes SERVO_OUTPUT_RAW messages at ~10 Hz
// These contain actual PWM values for all 16 servo channels

private void OnMavlinkMessage(int msgId, byte[] payload)
{
    if (msgId != 37) return;  // SERVO_OUTPUT_RAW

    var msg = DecodeServoOutputRaw(payload);
    // msg.servo1_raw through msg.servo16_raw (µs PWM values)

    for (int i = 0; i < 16; i++)
        _feedbackPwm[i] = GetServoValue(msg, i);
}

// Verification (called when advancing to next step):
private bool VerifyMotorFeedback(int motorInstance)
{
    int pwm = _feedbackPwm[motorInstance - 1];
    bool ok = pwm >= 800 && pwm <= 2200;  // Reasonable PWM range
    if (!ok) {
        _allPassed = false;
        _lastError = $"Motor {motorInstance}: no PWM feedback (got {pwm})";
    }
    return ok;
}

private bool VerifyServoFeedback(int instance, int expectedPwm)
{
    int actual = _feedbackPwm[instance - 1];
    bool ok = Math.Abs(actual - expectedPwm) <= 50;  // 50 µs tolerance
    if (!ok) {
        _allPassed = false;
        _lastError = $"Servo {instance}: expected {expectedPwm}, got {actual}";
    }
    return ok;
}
```

### 10.4 UI Binding

```qml
// MotorTestCard.qml — Shows live PWM values during test
// TelemetryBridge.servoOutputsString updates at ~10 Hz from SERVO_OUTPUT_RAW

RowLayout {
    Repeater {
        model: 16  // Channels 1–16
        delegate: Text {
            text: TelemetryBridge.servoOutputsString  // "1500 1500 1500 ..."
            // Updates in real-time as autopilot responds
        }
    }
}

ProgressBar {
    value: HardwareTestController.stepProgress  // 0.0 → 1.0
    visible: HardwareTestController.isRunning
}

Text {
    text: HardwareTestController.status         // "Running motor 2/4..."
    color: HardwareTestController.allPassed ? "green" : "red"
}
```

---

## 11. VehicleProfileManager

```csharp
// VehicleProfileManager.cs — Like a form service that persists vehicle-specific data
public class VehicleProfileManager : QObject, INotifyPropertyChanged
{
    private int _flightSessionId = -1;
    private double _payloadWeightKg = 0;
    private string _locationName = "";
    private double _planLat = 0;
    private double _planLon = 0;

    // ——— Q_PROPERTIES (QML binds to these) ———

    public double CurrentPayloadWeightKg
    {
        get => _payloadWeightKg;
        set {
            if (Math.Abs(value - _payloadWeightKg) < 0.01) return;
            _payloadWeightKg = Math.Max(0, value);
            if (_flightSessionId > 0)
                _db.UpdateFlightSessionPayload(_flightSessionId, _payloadWeightKg);
            EmitPropertyChanged();
        }
    }

    public string CurrentLocationName
    {
        get => _locationName;
        set {
            if (value == _locationName) return;
            _locationName = value;
            if (_flightSessionId > 0)
                _db.UpdateFlightSessionLocation(_flightSessionId, _locationName);
            EmitPropertyChanged();
        }
    }

    public double PlanLatitude
    {
        get => _planLat;
        set {
            if (Math.Abs(value - _planLat) < 1e-8) return;
            _planLat = value;
            if (_flightSessionId > 0)
                _db.UpdateFlightSessionPlanLocation(_flightSessionId, _planLat, _planLon);
            EmitPropertyChanged();
        }
    }

    public double PlanLongitude
    {
        get => _planLon;
        set {
            if (Math.Abs(value - _planLon) < 1e-8) return;
            _planLon = value;
            if (_flightSessionId > 0)
                _db.UpdateFlightSessionPlanLocation(_flightSessionId, _planLat, _planLon);
            EmitPropertyChanged();
        }
    }

    // ——— Flight Session Lifecycle ———

    public void OnArm() {
        _flightSessionId = _db.StartFlightSession(
            _currentDeviceUid, _batterySerial, _payloadWeightKg);
        // Session is now active — all property changes auto-persist to DB
    }

    public void OnDisarm() {
        if (_flightSessionId > 0) {
            _db.EndFlightSession(_flightSessionId, ElapsedSeconds);
            _db.IncrementBatteryCycle(_flightSessionId);
            _flightSessionId = -1;
        }
    }
}
```

**Key pattern:** Properties auto-persist to SQLite when a flight session is active. When the vehicle is disarmed, the session is finalized with duration.

---

## 12. Key Architecture Patterns

| Design Pattern | C# Equivalent | Our Implementation |
| --- | --- | --- |
| **Singleton** | `Lazy<T>` | `DatabaseManager::instance()` static local |
| **Observer** | `INotifyPropertyChanged` / events | Qt signals → QML bindings |
| **Strategy** | Interface + DI | `ICheck` → `BatteryVoltageCheck`, `CompassOrientationCheck` |
| **Adapter** | Adapter pattern | `TelemetryBridge` translates QGC Vehicle → our properties |
| **Facade** | Facade service | `PreflightPlugin` surfaces entire subsystem to QML |
| **MVC/MVVM** | Model-View-ViewModel | C++ Model → QML View with property binding |
| **Service Locator** | DI container | `setContextProperty()` — injects C# objects into QML |
| **Migration** | EF Core migrations | `migrateSchema()` with version tracking table |
| **State Machine** | `Enum` + switch | ArmingGate states, motor test step machine |
| **Debounce/Throttle** | Reactive Extensions | GPS weather fetch (30s min interval, 1km min delta) |
| **Reflection Binding** | `PropertyChanged` + `INotifyPropertyChanged` | ChecklistEngine's `_rebuildBindings()` via QMetaObject |
| **Interceptor** | `IAuthorizationHandler` / middleware | ArmingGate intercepts MAV_CMD_COMPONENT_ARM_DISARM |
| **Repository** | Repository pattern | `DatabaseManager` CRUD methods |
| **Proxy** | `Lazy<T>` or `RealProxy` | `TelemetryBridge` proxies `Vehicle` for QML-safe access |

---

## 13. Build System (MSBuild → CMake)

```bash
# 1. Configure (like dotnet restore)
cmake -S . -B build

# 2. Build (like dotnet build)
cmake --build build -j$(nproc)

# 3. Known link.txt issue:
#    CMake generates a linker command line that exceeds shell limit (41647 chars).
#    Workaround:
bash build/CMakeFiles/PreflightQGroundControl.dir/link.txt   # Manual link
cmake --build build                                            # Let cmake sync
```

---

## 14. Current Feature Inventory

| Feature | C# Analogy | Key Files | Status |
| --- | --- | --- | --- |
| MAVLink telemetry pipeline | SignalR Hub + typed events | `TelemetryBridge.cs` | ✅ |
| Vehicle detection + registration | Entity Framework change tracker | `VehicleRegistry.cs`, `DatabaseManager.cs` | ✅ |
| Preflight checklist + rule engine | FluentValidation + reactive bindings | `ChecklistEngine.cs`, `PreflightManager.cs` | ✅ |
| Arm/Disarm gate | Authorization handler (interceptor) | `ArmingGate.cs` | ✅ |
| Battery health analytics | Time-series threshold analysis | `BatteryHealthCheck.cs` | ✅ |
| Mission energy estimation | Calculator service | `MissionEnergyCheck.cs`, `PowerModel.cs` | ✅ |
| Weather API (Open-Meteo, METAR, TAF) | Typed HttpClient + MemoryCache | `WeatherProvider.cs` | ✅ (4s timeout, 50MB cache) |
| Export (CSV, HTML, PDF) | Report generator | `ExportHelper.cs` | ✅ |
| Hardware test (motor, servo) | Integration test controller | `HardwareTestController.cs` | ✅ |
| DB schema w/ migrations | EF Core migrations | `DatabaseManager.cs` (v1–v5) | ✅ |
| Vehicle profile (payload, plan coords) | Form data + auto-persist | `VehicleProfileManager.cs` | ✅ |
| Dark theme + Abel font | CSS variables + custom font | `Colors.qml`, `Config.qml` | ✅ |
| Flight distance on telemetry | View extension | `FlyViewTelemetryStrip.qml` | ✅ |
| Plan coord inputs + haversine | Form + calculator | `VehicleManagement.qml` | ✅ |

---

## 15. Key Gotchas (Things That Differ From .NET)

| .NET Expectation | Reality in this project |
| --- | --- |
| `async/await` HTTP calls | Qt signals/slots + event loop (callback-based) |
| Entity Framework LINQ | Raw SQL (SQLite) + `QJsonDocument` |
| DI container (`IServiceCollection`) | Manual `setContextProperty()` injection |
| `[PropertyChanged]` via Fody/ReactiveUI | Manual `emit propertyNameChanged()` everywhere |
| MSBuild `.csproj` / NuGet | CMake + `custom.qrc` + git submodules |
| `HttpClient` | `QNetworkAccessManager` (no `async`/`await`) |
| `IHostedService` / `BackgroundService` | `QTimer` + signal/slot callbacks |
| `System.Text.Json` / `Newtonsoft.Json` | `QJsonDocument` / `QJsonObject` / `QJsonArray` |
| `ILogger<T>` / `Serilog` | `qCDebug(category)` via `QLoggingCategory` |
| `Task.Run()` / `ThreadPool` | `QThread` + `QObject::moveToThread()` (manual) |
| `INotifyPropertyChanged` | Qt `signals:` + `Q_PROPERTY(NOTIFY ...)` |
| `[CallerMemberName]` in property setters | Every setter explicitly names the signal: `emit myPropertyChanged()` |
| `ConcurrentDictionary` for thread safety | `QMutex` + `QMutexLocker` (manual locking) |
| `ExpandoObject` / `dynamic` | `QVariantMap` / `QJsonObject` |
| `System.Timers.Timer` | `QTimer` (single-shot or interval) |
| `foreach` / LINQ | `for` loops + `QList::iterator` |
| `StringBuilder` | `QStringBuilder` / `QStringLiteral` |
| `nameof(Property)` | `QStringLiteral("propertyName")` — string literals |
| `Debug.Assert()` | `Q_ASSERT()` |
| `Stopwatch` | `QElapsedTimer` |
| `Environment.GetFolderPath` | `QStandardPaths::writableLocation()` |
| `SHA256` / `System.Security.Cryptography` | `QCryptographicHash::Sha256` |
| SQLite via `Microsoft.Data.Sqlite` / Dapper | Raw `QSqlQuery` with `?` parameter placeholders |
