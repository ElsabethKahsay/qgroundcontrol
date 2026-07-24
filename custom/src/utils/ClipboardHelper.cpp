#include "ClipboardHelper.h"

#include <QClipboard>
#include <QGuiApplication>

ClipboardHelper::ClipboardHelper(QObject *parent)
    : QObject(parent) {}

/** @brief Copy text to the system clipboard via QGuiApplication. */
void ClipboardHelper::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}
