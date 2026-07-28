// ClipboardHelper.cpp -- Implementation of the QML clipboard wrapper.

#include "ClipboardHelper.h"

#include <QClipboard>
#include <QGuiApplication>

ClipboardHelper::ClipboardHelper(QObject *parent)
    : QObject(parent) {}

/// Delegates to the global QGuiApplication clipboard singleton.
void ClipboardHelper::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}
