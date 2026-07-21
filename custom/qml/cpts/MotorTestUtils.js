.pragma library

function channelValue(index) {
    var raw = TelemetryProvider.servoOutputsString
    if (!raw || raw.length === 0) return "\u2014"
    var parts = raw.trim().split(/\s+/)
    if (parts.length > index) return parts[index]
    return "\u2014"
}
