#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QMultiHash>
#include <QDirIterator>
#include <QFile>
#include <QSet>
#include <QStringList>
#include <QTest>
#include <QXmlStreamReader>

#include "UnitTest.h"

// Stock QML is compiled into Qt QML modules with RESOURCE_PREFIX /qml, so it
// lands at :/qml/<URI>/<File>.qml.  custom.qrc aliases custom QML onto those
// exact resource paths, which makes every shared basename a *shadow*: the copy
// the QML engine resolves depends purely on which registration won.  A silent
// loss of an override looks identical to "my change had no effect", so every
// alias is checked against the bytes actually served by the binary.
class QmlShadowTest : public UnitTest {
    Q_OBJECT

private:
    struct Entry {
        QString resourcePath;      // :/qml/... path claimed by custom.qrc
        QString declaredSource;    // <file> text, relative to custom/
        QString absoluteSource;    // resolved on-disk path of declaredSource
    };

    // Walk up from the executable looking for the checkout.  Deliberately not a
    // compile definition: adding one to the main target forces a full
    // recompile of every translation unit in the project, which is a far bigger
    // cost than the check itself.  Packaged runs (AppImage) have no source tree
    // alongside them, so callers must skip rather than fail.
    static QString _sourceDir()
    {
        static QString cached;
        static bool resolved = false;
        if (resolved) {
            return cached;
        }
        resolved = true;
        QDir dir(QCoreApplication::applicationDirPath());
        for (int i = 0; i < 8; ++i) {
            if (QFile::exists(dir.filePath(QStringLiteral("custom/custom.qrc")))) {
                cached = dir.path();
                return cached;
            }
            if (!dir.cdUp()) {
                break;
            }
        }
        return cached;
    }

    static QByteArray _sha1(const QByteArray &bytes)
    {
        return QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(12);
    }

    static QByteArray _readFile(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return file.readAll();
    }

    static QList<Entry> _customQrcEntries()
    {
        QList<Entry> entries;
        const QString qrcPath = _sourceDir() + QStringLiteral("/custom/custom.qrc");
        QFile qrc(qrcPath);
        if (!qrc.open(QIODevice::ReadOnly)) {
            return entries;
        }

        const QString customDir = _sourceDir() + QStringLiteral("/custom/");
        QXmlStreamReader xml(&qrc);
        QString prefix;
        // Single flat pass: a qresource sets the current prefix, each file
        // becomes an entry.  Note atEnd() is true until the first readNext().
        xml.readNext();
        while (!xml.atEnd()) {
            if (xml.isStartElement() && xml.name() == QStringLiteral("qresource")) {
                prefix = xml.attributes().value(QStringLiteral("prefix")).toString();
            } else if (xml.isStartElement() && xml.name() == QStringLiteral("file")) {
                const QString alias = xml.attributes().value(QStringLiteral("alias")).toString();
                const QString declared = xml.readElementText().trimmed();
                if (declared.isEmpty()) {
                    continue;
                }
                Entry entry;
                entry.declaredSource = declared;
                entry.absoluteSource = QDir::cleanPath(customDir + declared);
                const QString base = alias.isEmpty() ? QFileInfo(declared).fileName() : alias;
                // Prefixes in custom.qrc are inconsistent about a leading slash
                // ("/qml/..." vs "qml/pages"), and Qt always exposes them under
                // ":/", so normalise both sides.
                QString cleanPrefix = prefix;
                while (cleanPrefix.startsWith(QLatin1Char('/'))) {
                    cleanPrefix.remove(0, 1);
                }
                while (cleanPrefix.endsWith(QLatin1Char('/'))) {
                    cleanPrefix.chop(1);
                }
                entry.resourcePath = QStringLiteral(":/") + cleanPrefix
                                     + (cleanPrefix.isEmpty() ? QString() : QStringLiteral("/")) + base;
                entries.append(entry);
            }
            xml.readNext();
        }
        return entries;
    }

    static QMultiHash<QString, QString> _stockQmlByBasename()
    {
        QMultiHash<QString, QString> stock;
        QDirIterator it(_sourceDir() + QStringLiteral("/src"), QStringList{QStringLiteral("*.qml")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            stock.insert(QFileInfo(path).fileName(), path);
        }
        return stock;
    }

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testCustomQrcAliasesResolveToDeclaredSource();
    void testShadowedOverridesCarryCustomContent();
    void testNoUndeclaredQmlShadow();
};

// Core invariant: whatever custom.qrc declares at a resource path is what the
// binary actually serves there.  Catches a stock registration winning the path.
void QmlShadowTest::testCustomQrcAliasesResolveToDeclaredSource()
{
    if (_sourceDir().isEmpty()) {
        QSKIP("no source tree next to the executable (packaged run); shadow audit skipped");
    }
    const QList<Entry> entries = _customQrcEntries();
    QVERIFY2(!entries.isEmpty(), "failed to parse custom/custom.qrc");

    QStringList missingResource;
    QStringList missingSource;
    QStringList mismatched;

    for (const Entry &entry : entries) {
        const QByteArray served = _readFile(entry.resourcePath);
        if (served.isEmpty()) {
            missingResource.append(entry.resourcePath);
            continue;
        }
        const QByteArray declared = _readFile(entry.absoluteSource);
        if (declared.isEmpty()) {
            missingSource.append(entry.absoluteSource);
            continue;
        }
        if (served != declared) {
            mismatched.append(QStringLiteral("%1: resource=%2 declared=%3")
                                  .arg(entry.resourcePath, _sha1(served), _sha1(declared)));
        }
    }

    QVERIFY2(missingResource.isEmpty(),
             qPrintable(QStringLiteral("custom.qrc aliases absent from binary: %1")
                            .arg(missingResource.join(QStringLiteral(", ")))));
    QVERIFY2(missingSource.isEmpty(),
             qPrintable(QStringLiteral("declared sources missing on disk: %1")
                            .arg(missingSource.join(QStringLiteral(", ")))));
    QVERIFY2(mismatched.isEmpty(),
             qPrintable(QStringLiteral("resource path serves content other than the declared custom "
                                       "source (shadow lost): %1")
                            .arg(mismatched.join(QStringLiteral("; ")))));
}

// A shadow is only dangerous while the two copies differ.  Confirm every
// basename shared with the stock tree is genuinely overridden, not a stale copy.
void QmlShadowTest::testShadowedOverridesCarryCustomContent()
{
    if (_sourceDir().isEmpty()) {
        QSKIP("no source tree next to the executable (packaged run); shadow audit skipped");
    }
    const QList<Entry> entries = _customQrcEntries();
    const QMultiHash<QString, QString> stock = _stockQmlByBasename();

    int shadows = 0;
    QStringList identicalCopies;

    for (const Entry &entry : entries) {
        const QString base = QFileInfo(entry.resourcePath).fileName();
        if (!stock.contains(base)) {
            continue;
        }
        ++shadows;
        const QByteArray served = _readFile(entry.resourcePath);
        const QStringList stockPaths = stock.values(base);
        bool differsFromAnyStock = false;
        for (const QString &stockPath : stockPaths) {
            if (_readFile(stockPath) != served) {
                differsFromAnyStock = true;
            }
        }
        if (!differsFromAnyStock) {
            // Entries that point straight at a stock file are deliberate mirrors.
            if (!entry.absoluteSource.startsWith(_sourceDir() + QStringLiteral("/src/"))) {
                identicalCopies.append(QStringLiteral("%1 == stock %2")
                                           .arg(entry.resourcePath, stockPaths.join(QStringLiteral(","))));
            }
        }
    }

    QVERIFY2(shadows >= 18, qPrintable(QStringLiteral("expected >=18 shadowed custom.qrc entries, found %1")
                                          .arg(shadows)));
    QVERIFY2(identicalCopies.isEmpty(),
             qPrintable(QStringLiteral("custom override is byte-identical to stock (no-op shadow): %1")
                            .arg(identicalCopies.join(QStringLiteral("; ")))));
}

// The failure that bit this project twice: a custom QML file sharing a stock
// basename but never aliased in custom.qrc, so the stock copy loads and every
// edit to the custom file is silently dead.
void QmlShadowTest::testNoUndeclaredQmlShadow()
{
    if (_sourceDir().isEmpty()) {
        QSKIP("no source tree next to the executable (packaged run); shadow audit skipped");
    }
    const QList<Entry> entries = _customQrcEntries();
    QSet<QString> declaredSources;
    for (const Entry &entry : entries) {
        declaredSources.insert(entry.absoluteSource);
    }

    const QMultiHash<QString, QString> stock = _stockQmlByBasename();
    QDirIterator it(_sourceDir() + QStringLiteral("/custom/qml"), QStringList{QStringLiteral("*.qml")}, QDir::Files,
                    QDirIterator::Subdirectories);
    QStringList undeclared;
    int found = 0;

    while (it.hasNext()) {
        const QString path = QDir::cleanPath(it.next());
        const QString base = QFileInfo(path).fileName();
        if (!stock.contains(base)) {
            continue;
        }
        ++found;
        if (!declaredSources.contains(path)) {
            undeclared.append(QStringLiteral("%1 shadows %2 but is not declared in custom.qrc")
                                  .arg(path, stock.values(base).join(QStringLiteral(","))));
        }
    }

    // 12 distinct custom/qml files share a basename with a stock QML file.  The
    // shadowed-entry count is higher (18) because custom.qrc registers some of
    // them twice and separately mirrors five stock AnalyzeView pages, whose
    // source lives in src/ rather than custom/qml.
    QVERIFY2(found >= 12, qPrintable(QStringLiteral("expected >=12 shadowing custom QML files, found %1")
                                         .arg(found)));
    QVERIFY2(undeclared.isEmpty(),
             qPrintable(QStringLiteral("undeclared QML shadows (custom copy never loads): %1")
                            .arg(undeclared.join(QStringLiteral("; ")))));
}

UT_REGISTER_TEST(QmlShadowTest)

#include "QmlShadowTest.moc"
