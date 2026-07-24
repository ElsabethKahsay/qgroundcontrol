#pragma once

// ============================================================================
// Hysteresis — Signal processing utility templates for smoothing noisy
// telemetry data in the preflight checklist.
//
// DebounceFilter<T> — Suppresses rapid fluctuations by requiring a value
//   to remain stable (within tolerance) for a configurable period before
//   accepting it.  Used for things like GPS lock status, sensor health
//   flags, and battery percentage readings.
//
// AveragingFilter<T> — Maintains a sliding time window of samples and
//   computes running statistics (average, min, max).  Prunes stale
//   samples on each addSample() call.  Used for smoothing wind speed,
//   voltage readings, and other noisy sensor values.
//
// Both templates are header-only and work with any numeric type.
// ============================================================================

#include <QObject>
#include <QElapsedTimer>
#include <QVector>
#include <QDateTime>
#include <algorithm>
#include <cmath>

/**
 * @brief Debounce filter — accepts a value only after it has been stable
 *        for the configured duration (default 2 seconds).
 *
 * Call update() with the latest sensor reading each time it arrives.
 * Returns true only once the value hasn't changed beyond the tolerance
 * (0.001) for the full stable period.  Resets the timer on any change.
 */
template<typename T>
class DebounceFilter {
public:
    explicit DebounceFilter(int stableTimeMs = 2000)
        : m_stableTimeMs(stableTimeMs) {}

    void setStableTimeMs(int ms) { m_stableTimeMs = ms; }

    // Returns true when the value has been stable for the debounce period
    bool update(T value) {
        if (std::abs(static_cast<double>(value - m_lastValue)) > m_tolerance) {
            m_lastChangeTime.start();
            m_lastValue = value;
            return false;
        }
        m_lastValue = value;
        return m_lastChangeTime.hasExpired(m_stableTimeMs);
    }

    void reset() {
        m_lastChangeTime.invalidate();
        m_lastValue = T{};
    }

    T lastValue() const { return m_lastValue; }

private:
    QElapsedTimer m_lastChangeTime;
    T m_lastValue{};
    int m_stableTimeMs = 2000;
    double m_tolerance = 0.001;
};

/**
 * @brief Sliding-window averaging filter with min/max tracking.
 *
 * Maintains samples within a configurable time window (default 3 seconds).
 * Stale samples are automatically pruned on each addSample() call.
 * Provides average(), minimum(), and maximum() over the active window.
 */
template<typename T>
class AveragingFilter {
public:
    explicit AveragingFilter(int windowMs = 3000)
        : m_windowMs(windowMs) {}

    void setWindowMs(int ms) { m_windowMs = ms; }

    void addSample(T value) {
        m_samples.append({QDateTime::currentDateTime(), value});
        prune();
    }

    T average() const {
        if (m_samples.isEmpty()) return T{};
        double sum = 0;
        for (const auto &s : m_samples)
            sum += static_cast<double>(s.second);
        return static_cast<T>(sum / m_samples.size());
    }

    T minimum() const {
        if (m_samples.isEmpty()) return T{};
        T minVal = m_samples.first().second;
        for (const auto &s : m_samples)
            if (s.second < minVal) minVal = s.second;
        return minVal;
    }

    T maximum() const {
        if (m_samples.isEmpty()) return T{};
        T maxVal = m_samples.first().second;
        for (const auto &s : m_samples)
            if (s.second > maxVal) maxVal = s.second;
        return maxVal;
    }

    int sampleCount() const { return m_samples.size(); }
    bool isReady() const { return !m_samples.isEmpty(); }

    void reset() { m_samples.clear(); }

private:
    /** Remove samples older than the time window to keep memory bounded. */
    void prune() {
        QDateTime cutoff = QDateTime::currentDateTime().addMSecs(-m_windowMs);
        auto it = std::remove_if(m_samples.begin(), m_samples.end(),
            [&cutoff](const auto &s) { return s.first < cutoff; });
        m_samples.erase(it, m_samples.end());
    }

    QVector<QPair<QDateTime, T>> m_samples;
    int m_windowMs = 3000;
};
