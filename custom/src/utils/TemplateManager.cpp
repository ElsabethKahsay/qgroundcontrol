// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: src/TemplateManager.cpp
// Description: Implementation of TemplateManager.

#include "TemplateManager.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "DatabaseManager.h"

/**
 * @brief Get the singleton instance of TemplateManager
 * @return Reference to the singleton instance
 * 
 * Uses Meyers' singleton pattern for thread-safe lazy initialization.
 */
TemplateManager &TemplateManager::instance()
{
    static TemplateManager instance;
    return instance;
}

/**
 * @brief Constructor for TemplateManager
 * @param parent Parent QObject
 */
TemplateManager::TemplateManager(QObject *parent) : QObject(parent)
{
}

/**
 * @brief Destructor for TemplateManager
 */
TemplateManager::~TemplateManager()
{
}

/**
 * @brief Save a checklist template to the database
 * @param templateId Unique template identifier
 * @param vehicleType Vehicle type (Quad, FixedWing, VTOL)
 * @param templateName Human-readable template name
 * @param items List of checklist items as QVariantList
 * @return true if save successful, false on error
 * 
 * Converts the QVariantList to JSON and stores it in the database
 * via DatabaseManager.
 */
bool TemplateManager::saveTemplate(const QString &templateId, const QString &vehicleType,
                                   const QString &templateName, const QVariantList &items)
{
    // Convert QVariantList to QJsonArray
    QJsonArray jsonArray;
    for (const QVariant &itemVar : items) {
        QVariantMap itemMap = itemVar.toMap();
        QJsonObject jsonObj;
        for (auto it = itemMap.begin(); it != itemMap.end(); ++it) {
            jsonObj[it.key()] = QJsonValue::fromVariant(it.value());
        }
        jsonArray.append(jsonObj);
    }
    
    // Convert to JSON string
    QJsonDocument doc(jsonArray);
    QString jsonString(doc.toJson(QJsonDocument::Compact));
    
    // Save via DatabaseManager
    return DatabaseManager::instance().saveTemplate(templateId, vehicleType, templateName, jsonString);
}

/**
 * @brief Load a checklist template from the database
 * @param templateId Unique template identifier
 * @return List of checklist items as QVariantList, or empty list if not found
 * 
 * Loads JSON from the database via DatabaseManager and converts
 * it to QVariantList for QML consumption.
 */
QVariantList TemplateManager::loadTemplate(const QString &templateId)
{
    QVariantList result;
    QString jsonString = DatabaseManager::instance().loadTemplate(templateId);
    if (jsonString.isEmpty()) return result;
    
    // Parse JSON
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8());
    if (!doc.isArray()) return result;
    
    // Convert QJsonArray to QVariantList
    QJsonArray jsonArray = doc.array();
    for (int i = 0; i < jsonArray.size(); ++i) {
        QJsonObject jsonObj = jsonArray[i].toObject();
        QVariantMap itemMap;
        for (const QString &key : jsonObj.keys()) {
            itemMap[key] = jsonObj[key].toVariant();
        }
        result.append(itemMap);
    }
    
    return result;
}
