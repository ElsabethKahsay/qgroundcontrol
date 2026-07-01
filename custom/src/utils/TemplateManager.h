// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: src/TemplateManager.h
// Description: Manages saving and loading of templates via DatabaseManager.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class TemplateManager : public QObject {
    Q_OBJECT
public:
    /** @brief Get the singleton instance of TemplateManager */
    static TemplateManager &instance();

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
    Q_INVOKABLE bool saveTemplate(const QString &templateId, const QString &vehicleType,
                                  const QString &templateName, const QVariantList &items);
    
    /**
     * @brief Load a checklist template from the database
     * @param templateId Unique template identifier
     * @return List of checklist items as QVariantList, or empty list if not found
     * 
     * Loads JSON from the database via DatabaseManager and converts
     * it to QVariantList for QML consumption.
     */
    Q_INVOKABLE QVariantList loadTemplate(const QString &templateId);

private:
    explicit TemplateManager(QObject *parent = nullptr);
    ~TemplateManager() override;
};
