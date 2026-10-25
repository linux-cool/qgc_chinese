#include "AppSettingsTest.h"

#include <memory>

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QScopeGuard>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrlQuery>
#include <QtGui/QRawFont>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>

#include "AppSettings.h"
#include "FirmwarePluginManager.h"
#include "JsonParsing.h"
#include "PX4FirmwarePlugin.h"
#include "ParameterEditorController.h"
#include "QGCApplication.h"
#include "QGCMAVLink.h"
#include "QGCMapUrlEngine.h"
#include "SettingsManager.h"
#include "UnitsSettings.h"

UT_REGISTER_TEST(AppSettingsTest, TestLabel::Unit)

void AppSettingsTest::_preferredFirmwareClassEnumFiltered()
{
    _verifyFirmwareClassEnumFiltered(SettingsManager::instance()->appSettings()->preferredFirmwareClass());
}

void AppSettingsTest::_offlineEditingFirmwareClassEnumFiltered()
{
    _verifyFirmwareClassEnumFiltered(SettingsManager::instance()->appSettings()->offlineEditingFirmwareClass());
}

void AppSettingsTest::_chineseDefaultAndDisplayText()
{
    Fact* language = SettingsManager::instance()->appSettings()->qLocaleLanguage();
    QCOMPARE(language->rawDefaultValue().toInt(), static_cast<int>(QLocale::Chinese));
    expectAppMessage(QRegularExpression(QStringLiteral("Restart application for changes to take effect")));
    language->setRawValue(static_cast<int>(qgcApp()->getCurrentLanguage().language()));
    verifyExpectedLogMessage();
    const QVariant previousLanguage = language->rawValue();
    const auto restoreLanguage =
        qScopeGuard([language, previousLanguage]() { language->setRawValue(previousLanguage); });
    language->setRawValue(static_cast<int>(QLocale::Chinese));
    QCOMPARE(qgcApp()->getCurrentLanguage().name(), QStringLiteral("zh_CN"));
    QCOMPARE(QCoreApplication::translate("ParameterEditor", "Search"), QStringLiteral("搜索"));

    QObject owner;
    const auto settings = FactMetaData::createMapFromJsonFile(QStringLiteral(":/json/App.SettingsGroup.json"), &owner);
    QVERIFY(settings.contains(QStringLiteral("qLocaleLanguage")));
    QCOMPARE(settings.value(QStringLiteral("qLocaleLanguage"))->label(), QStringLiteral("语言"));

    UnitsSettings units(&owner);
    Fact* const speedUnits = units.speedUnits();
    QCOMPARE(speedUnits->enumStrings(),
             QStringList({QStringLiteral("英尺/秒"), QStringLiteral("米/秒"), QStringLiteral("英里/小时"),
                          QStringLiteral("千米/小时"), QStringLiteral("节")}));
    QCOMPARE(speedUnits->enumValues().at(UnitsSettings::SpeedUnitsMetersPerSecond).toUInt(),
             static_cast<uint32_t>(UnitsSettings::SpeedUnitsMetersPerSecond));

    int version = 0;
    QString error;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString fileName = directory.filePath(QStringLiteral("GNSSReceiver.SettingsUI.json"));
    QFile file(fileName);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(R"({"fileType":"SettingsUI","version":1,"keywords":["base station"]})") > 0);
    file.close();
    const auto page = JsonParsing::openInternalQGCJsonFile(fileName, QStringLiteral("SettingsUI"), 1, 1, version, error,
                                                           {QStringLiteral("keywords")});
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const auto keywords = page.value(QStringLiteral("keywords")).toArray();
    QVERIFY(keywords.contains(QStringLiteral("基站")));

    Fact fact(1, QStringLiteral("MPC_XY_CRUISE"), FactMetaData::valueTypeUint32);
    auto* metaData = new FactMetaData(FactMetaData::valueTypeUint32, &fact);
    metaData->setShortDescription(QStringLiteral("Cruise speed"));
    metaData->setRawUnits(QStringLiteral("m/s"));
    metaData->setEnumInfo({QStringLiteral("Disabled"), QStringLiteral("Enabled")}, {0, 1});
    fact.setMetaData(metaData);
    QCOMPARE(fact.shortDescription(), QStringLiteral("巡航速度"));
    QCOMPARE(fact.enumStrings(), QStringList({QStringLiteral("禁用"), QStringLiteral("启用")}));
    fact.setEnumStringValue(QStringLiteral("启用"));
    QCOMPARE(fact.rawValue().toUInt(), 1U);
    QCOMPARE(fact.name(), QStringLiteral("MPC_XY_CRUISE"));
    QCOMPARE(fact.rawUnits(), QStringLiteral("m/s"));

    ParameterEditorCategory category(nullptr);
    category.name = QStringLiteral("Advanced");
    QCOMPARE(category.displayName(), QStringLiteral("高级"));
    QCOMPARE(category.name, QStringLiteral("Advanced"));

    // AVAILABLE_MODES names are display labels; mode commands must retain their numeric IDs.
    PX4FirmwarePlugin firmware;
    FlightModeList receivedModes{{QStringLiteral("Manual"), 1234U, true},
                                 {QStringLiteral("Experimental Mode Alpha"), 1235U, true},
                                 {QStringLiteral("手动"), 1236U, true}};
    firmware.updateAvailableFlightModes(receivedModes);
    QCOMPARE(receivedModes.first().mode_name, QStringLiteral("Manual"));
    QCOMPARE(firmware.flightMode(MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, 1234U), QStringLiteral("手动"));
    QCOMPARE(firmware.flightMode(MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, 1235U), QStringLiteral("Experimental Mode Alpha"));
    uint8_t baseMode = 0;
    uint32_t customMode = 0;
    QVERIFY(firmware.setFlightMode(QStringLiteral("手动"), &baseMode, &customMode));
    QCOMPARE(baseMode, static_cast<uint8_t>(MAV_MODE_FLAG_CUSTOM_MODE_ENABLED));
    QCOMPARE(customMode, 1234U);
    QCOMPARE(firmware.flightMode(MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, 1236U), QStringLiteral("手动 (1236)"));
    QVERIFY(firmware.setFlightMode(QStringLiteral("手动 (1236)"), &baseMode, &customMode));
    QCOMPARE(customMode, 1236U);

    // Providers are registered before application startup; requests must use the selected locale.
    if (UrlFactory::getProviderTypes().contains(QStringLiteral("Google Street Map"))) {
        const QUrl googleUrl = UrlFactory::getTileURL(QStringLiteral("Google Street Map"), 1, 1, 2);
        QVERIFY2(googleUrl.toString().contains(QStringLiteral("&hl=zh-CN&")), qPrintable(googleUrl.toString()));
    }
    const QUrl bingUrl = UrlFactory::getTileURL(QStringLiteral("Bing Road"), 1, 1, 2);
    QCOMPARE(QUrlQuery(bingUrl).queryItemValue(QStringLiteral("mkt")), QStringLiteral("zh-CN"));

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QGroundControl.Controls
        LabelledComboBox {
            model: ["Street Map", "Hybrid"]
            currentIndex: 1
            translationContext: "MapType"
        }
    )",
                      QUrl());
    std::unique_ptr<QObject> comboBox(component.create());
    QVERIFY2(comboBox, qPrintable(component.errorString()));
    QCOMPARE(comboBox->property("currentText").toString(), QStringLiteral("Hybrid"));
    QObject* const innerComboBox = comboBox->property("comboBox").value<QObject*>();
    QVERIFY(innerComboBox);
    QObject* const content = innerComboBox->property("contentItem").value<QObject*>();
    QVERIFY(content);
    QCOMPARE(content->property("text").toString(), QStringLiteral("混合影像"));
    QString rawText;
    QVERIFY(QMetaObject::invokeMethod(innerComboBox, "textAt", Q_RETURN_ARG(QString, rawText), Q_ARG(int, 0)));
    QCOMPARE(rawText, QStringLiteral("Street Map"));

    QQmlComponent telemetryDialog(
        &engine, QUrl(QStringLiteral("qrc:/qml/QGroundControl/Controls/InstrumentValueEditDialog.qml")));
    QVERIFY2(telemetryDialog.isReady(), qPrintable(telemetryDialog.errorString()));

    QRawFont font(QStringLiteral(":/fonts/NotoSansCJKsc-Regular.otf"), 14);
    QVERIFY(font.isValid());
    const QString probe = QStringLiteral("简体中文设置参数配置解锁上锁返航横滚俯仰偏航");
    for (const QChar character : probe) {
        QVERIFY2(font.supportsCharacter(character), qPrintable(QString(character)));
    }
}

void AppSettingsTest::_verifyFirmwareClassEnumFiltered(Fact *fact)
{
    const QList<QGCMAVLink::FirmwareClass_t> supportedClasses = FirmwarePluginManager::instance()->supportedFirmwareClasses();

    const QVariantList enumValues = fact->enumValues();
    QCOMPARE(fact->enumStrings().count(), enumValues.count());
    QVERIFY(!enumValues.isEmpty());

    for (const QVariant &enumValue : enumValues) {
        const auto firmwareClass = static_cast<QGCMAVLink::FirmwareClass_t>(enumValue.toUInt());
        QVERIFY2(supportedClasses.contains(firmwareClass),
                 qPrintable(QStringLiteral("%1 enum offers unsupported firmware class %2").arg(fact->name()).arg(enumValue.toUInt())));
    }

    QVERIFY2(supportedClasses.contains(static_cast<QGCMAVLink::FirmwareClass_t>(fact->rawValue().toUInt())),
             qPrintable(QStringLiteral("%1 value is an unsupported firmware class %2").arg(fact->name()).arg(fact->rawValue().toUInt())));
}
