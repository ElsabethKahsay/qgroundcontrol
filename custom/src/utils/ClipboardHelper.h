#pragma once

// ============================================================================
// ClipboardHelper — Thin QML-accessible wrapper around QClipboard.
// Allows QML code to copy text to the system clipboard without
// needing to import QtQuick or access C++ clipboard APIs directly.
// ============================================================================

#include <QObject>

class ClipboardHelper : public QObject {
    Q_OBJECT
public:
    explicit ClipboardHelper(QObject *parent = nullptr);

    Q_INVOKABLE void copyToClipboard(const QString &text);
};
