#pragma once
#include <QObject>

class ClipboardHelper : public QObject {
    Q_OBJECT
public:
    explicit ClipboardHelper(QObject *parent = nullptr);

    Q_INVOKABLE void copyToClipboard(const QString &text);
};
