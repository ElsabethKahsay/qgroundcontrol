#include "ClipboardHelper.h"

#include <QClipboard>
#include <QGuiApplication>

ClipboardHelper::ClipboardHelper(QObject *parent)
    : QObject(parent) {}

void ClipboardHelper::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}
