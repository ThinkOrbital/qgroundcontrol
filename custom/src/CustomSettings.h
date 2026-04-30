#pragma once

#include <QtQmlIntegration/QtQmlIntegration>

#include "SettingsGroup.h"

class CustomSettings : public SettingsGroup
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
public:
    CustomSettings(QObject* parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(separationDistance)
    DEFINE_SETTINGFACT(bearing)
    DEFINE_SETTINGFACT(altitude)
    DEFINE_SETTINGFACT(detOffset)
    DEFINE_SETTINGFACT(emAltOffset)
    DEFINE_SETTINGFACT(flightAlt)
    DEFINE_SETTINGFACT(flightVel)
    DEFINE_SETTINGFACT(goalLat)
    DEFINE_SETTINGFACT(goalLon)
    DEFINE_SETTINGFACT(numImages)
    DEFINE_SETTINGFACT(fileName)
    DEFINE_SETTINGFACT(detectorXrayWindow)

};
