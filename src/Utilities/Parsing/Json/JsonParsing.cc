#include "JsonParsing.h"

#include <limits>

#include <QtCore/QApplicationStatic>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonParseError>
#include <QtCore/QObject>
#include <QtCore/QTranslator>

#include "QGCCompression.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(JsonParsingLog, "Utilities.Parsing.Json")

namespace JsonParsing {

bool isJsonFile(const QByteArray& bytes, QJsonDocument& jsonDoc, QString& errorString)
{
    QJsonParseError parseError;
    jsonDoc = QGCCompression::parseCompressedJson(bytes, &parseError);

    if (parseError.error == QJsonParseError::NoError) {
        return true;
    }

    if (QGCCompression::looksLikeCompressedData(bytes)) {
        // parseError.offset refers to the decompressed text, so a slice of `bytes` would be garbage.
        qCDebug(JsonParsingLog) << "Json read error (compressed input) offset" << parseError.offset;
    } else {
        const int startPos = qMax(0, parseError.offset - 100);
        const int length = qMin(bytes.length() - startPos, 200);
        qCDebug(JsonParsingLog) << "Json read error" << bytes.mid(startPos, length).constData();
    }
    errorString = QStringLiteral("%1 (offset %2)").arg(parseError.errorString()).arg(parseError.offset);

    return false;
}

bool isJsonFile(const QString& fileName, QJsonDocument& jsonDoc, QString& errorString)
{
    const QByteArray jsonBytes = QGCCompression::readFile(fileName, &errorString);
    if (jsonBytes.isEmpty() && !errorString.isEmpty()) {
        jsonDoc = {};
        return false;
    }

    return isJsonFile(jsonBytes, jsonDoc, errorString);
}

double possibleNaNJsonValue(const QJsonValue& value)
{
    if (value.type() == QJsonValue::Null) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return value.toDouble();
}

}  // namespace JsonParsing

// ---------------------------------------------------------------------------
// QGC json file header / translation utilities
// ---------------------------------------------------------------------------

namespace {

constexpr const char *_translateKeysKey = "translateKeys";
constexpr const char *_arrayIDKeysKey = "_arrayIDKeys";
constexpr const char *_jsonGroundStationKey = "groundStation";
constexpr const char *_jsonGroundStationValue = "QGroundControl";

Q_APPLICATION_STATIC(QTranslator, s_jsonTranslator);

QJsonObject translateObject(QJsonObject &jsonObject, const QString &translateContext, const QStringList &translateKeys);

QString translateString(const QString& text, const QString& translateContext)
{
    QString source = text;
    QString disambiguation;
    const QString prefix = QStringLiteral("#loc.disambiguation#");
    if (source.startsWith(prefix)) {
        source.remove(0, prefix.size());
        const qsizetype commentEndIndex = source.indexOf(u'#');
        if (commentEndIndex != -1) {
            disambiguation = source.left(commentEndIndex);
            source.remove(0, commentEndIndex + 1);
        }
    }
    const QString translated = JsonParsing::translator()->translate(
        translateContext.toUtf8().constData(), source.toUtf8().constData(), disambiguation.toUtf8().constData());
    return translated.isNull() ? source : translated;
}

QJsonArray translateArray(QJsonArray& jsonArray, const QString& translateContext, const QStringList& translateKeys,
                          bool translateStrings)
{
    for (qsizetype i = 0; i < jsonArray.count(); i++) {
        if (jsonArray[i].isObject()) {
            QJsonObject childJsonObject = jsonArray[i].toObject();
            jsonArray[i] = translateObject(childJsonObject, translateContext, translateKeys);
        } else if (jsonArray[i].isArray()) {
            QJsonArray childJsonArray = jsonArray[i].toArray();
            jsonArray[i] = translateArray(childJsonArray, translateContext, translateKeys, translateStrings);
        } else if (translateStrings && jsonArray[i].isString()) {
            jsonArray[i] = translateString(jsonArray[i].toString(), translateContext);
        }
    }
    return jsonArray;
}

QJsonObject translateObject(QJsonObject &jsonObject, const QString &translateContext, const QStringList &translateKeys)
{
    for (const QString &key : jsonObject.keys()) {
        if (jsonObject[key].isString()) {
            if (translateKeys.contains(key)) {
                jsonObject[key] = translateString(jsonObject[key].toString(), translateContext);
            }
        } else if (jsonObject[key].isArray()) {
            QJsonArray childJsonArray = jsonObject[key].toArray();
            jsonObject[key] =
                translateArray(childJsonArray, translateContext, translateKeys, translateKeys.contains(key));
        } else if (jsonObject[key].isObject()) {
            QJsonObject childJsonObject = jsonObject[key].toObject();
            jsonObject[key] = translateObject(childJsonObject, translateContext, translateKeys);
        }
    }
    return jsonObject;
}

/// Resolves the translate-keys list for `jsonObject`. If the JSON omits `translateKeys`
/// and the caller supplied non-empty defaults, those defaults are written in.
/// Same logic for `_arrayIDKeys` and `defaultArrayIDKeys`.
QStringList resolveTranslateKeys(QJsonObject &jsonObject,
                                 const QStringList &defaultTranslateKeys,
                                 const QStringList &defaultArrayIDKeys)
{
    QString translateKeys;
    if (jsonObject.contains(_translateKeysKey)) {
        translateKeys = jsonObject[_translateKeysKey].toString();
    } else if (!defaultTranslateKeys.isEmpty()) {
        translateKeys = defaultTranslateKeys.join(",");
        jsonObject[_translateKeysKey] = translateKeys;
    }

    if (!jsonObject.contains(_arrayIDKeysKey) && !defaultArrayIDKeys.isEmpty()) {
        jsonObject[_arrayIDKeysKey] = defaultArrayIDKeys.join(",");
    }

    if (translateKeys.isEmpty()) {
        return {};
    }
    return translateKeys.split(",");
}

}  // namespace

namespace JsonParsing {

QTranslator *translator()
{
    return s_jsonTranslator();
}

void saveQGCJsonFileHeader(QJsonObject &jsonObject, const QString &fileType, int version)
{
    jsonObject[_jsonGroundStationKey] = _jsonGroundStationValue;
    jsonObject[jsonFileTypeKey] = fileType;
    jsonObject[jsonVersionKey] = version;
}

bool validateInternalQGCJsonFile(const QJsonObject &jsonObject, const QString &expectedFileType,
                                 int minSupportedVersion, int maxSupportedVersion, int &version,
                                 QString &errorString)
{
    static const QList<KeyValidateInfo> requiredKeys = {
        {jsonFileTypeKey, QJsonValue::String, true},
        {jsonVersionKey, QJsonValue::Double, true},
    };

    if (!validateKeys(jsonObject, requiredKeys, errorString)) {
        return false;
    }

    const QString fileTypeValue = jsonObject[jsonFileTypeKey].toString();
    if (fileTypeValue != expectedFileType) {
        errorString = QObject::tr("Incorrect file type key expected:%1 actual:%2").arg(expectedFileType, fileTypeValue);
        return false;
    }

    version = jsonObject[jsonVersionKey].toInt();
    if (version < minSupportedVersion) {
        errorString = QObject::tr("File version %1 is no longer supported").arg(version);
        return false;
    }

    if (version > maxSupportedVersion) {
        errorString = QObject::tr("File version %1 is newer than current supported version %2")
                          .arg(version)
                          .arg(maxSupportedVersion);
        return false;
    }

    return true;
}

bool validateExternalQGCJsonFile(const QJsonObject &jsonObject, const QString &expectedFileType,
                                 int minSupportedVersion, int maxSupportedVersion, int &version,
                                 QString &errorString)
{
    static const QList<KeyValidateInfo> requiredKeys = {
        {_jsonGroundStationKey, QJsonValue::String, true},
    };

    if (!validateKeys(jsonObject, requiredKeys, errorString)) {
        return false;
    }

    return validateInternalQGCJsonFile(jsonObject, expectedFileType, minSupportedVersion, maxSupportedVersion, version,
                                       errorString);
}

QJsonObject openInternalQGCJsonFile(const QString &jsonFilename, const QString &expectedFileType,
                                    int minSupportedVersion, int maxSupportedVersion, int &version,
                                    QString &errorString,
                                    const QStringList &defaultTranslateKeys,
                                    const QStringList &defaultArrayIDKeys)
{
    QJsonDocument doc;
    if (!isJsonFile(jsonFilename, doc, errorString)) {
        errorString = QObject::tr("Unable to parse json file: %1 error: %2").arg(jsonFilename, errorString);
        return {};
    }

    if (!doc.isObject()) {
        errorString = QObject::tr("Root of json file is not object: %1").arg(jsonFilename);
        return {};
    }

    QJsonObject jsonObject = doc.object();
    const bool success = validateInternalQGCJsonFile(jsonObject, expectedFileType, minSupportedVersion,
                                                     maxSupportedVersion, version, errorString);
    if (!success) {
        errorString = QObject::tr("Json file: '%1'. %2").arg(jsonFilename, errorString);
        return {};
    }

    const QStringList translateKeys = resolveTranslateKeys(jsonObject, defaultTranslateKeys, defaultArrayIDKeys);
    const QString context = QFileInfo(jsonFilename).fileName();
    return translateObject(jsonObject, context, translateKeys);
}

}  // namespace JsonParsing
