#pragma once

// ============================================================================
// ClipboardHelper -- Thin QML-accessible wrapper around QClipboard.
// Allows QML code to copy text to the system clipboard without
// needing to import QtQuick or access C++ clipboard APIs directly.
// ============================================================================

#include <QObject>

/// Exposes a single Q_INVOKABLE method so QML views can copy strings
/// to the platform clipboard (Ctrl+C / Cmd+C target).
class ClipboardHelper : public QObject {
    Q_OBJECT
public:
    explicit ClipboardHelper(QObject *parent = nullptr);

    /// Copy arbitrary text to the system clipboard, overwriting any previous content.
    Q_INVOKABLE void copyToClipboard(const QString &text);
};
