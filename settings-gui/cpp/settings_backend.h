/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LOTUS_SETTINGS_BACKEND_H
#define LOTUS_SETTINGS_BACKEND_H

#include "dbus_client.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class I18nBridge;

/**
 * @brief Central staged state manager and QML backend for Lotus settings.
 */
class SettingsBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool fcitxOffline READ fcitxOffline NOTIFY stateChanged)
    Q_PROPERTY(bool hasUnsavedChanges READ hasUnsavedChanges NOTIFY stateChanged)
    Q_PROPERTY(bool hasValidationErrors READ hasValidationErrors NOTIFY stateChanged)
    Q_PROPERTY(QString validationMessage READ validationMessage NOTIFY stateChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QVariantMap values READ values NOTIFY stateChanged)
    Q_PROPERTY(QVariantList modeList READ modeList NOTIFY stateChanged)
    Q_PROPERTY(QVariantList appRules READ appRules NOTIFY stateChanged)
    Q_PROPERTY(QVariantList macros READ macros NOTIFY stateChanged)
    Q_PROPERTY(QVariantList dictionaryWords READ dictionaryWords NOTIFY stateChanged)
    Q_PROPERTY(int totalWordCount READ totalWordCount NOTIFY stateChanged)
    Q_PROPERTY(QVariantList keymapBindings READ keymapBindings NOTIFY stateChanged)
    Q_PROPERTY(QVariantList keymapPresets READ keymapPresets NOTIFY stateChanged)
    Q_PROPERTY(QVariantList bambooActions READ bambooActions NOTIFY stateChanged)

  public:
    explicit SettingsBackend(I18nBridge* i18n = nullptr, QObject* parent = nullptr);

    bool                     fcitxOffline() const;
    bool                     hasUnsavedChanges() const;
    bool                     hasValidationErrors() const;
    QString                  validationMessage() const;
    QString                  appVersion() const;
    QVariantMap              values() const;
    QVariantList             modeList() const;
    QVariantList             appRules() const;
    QVariantList             macros() const;
    QVariantList             dictionaryWords() const;
    int                      totalWordCount() const;
    QVariantList             keymapBindings() const;
    QVariantList             keymapPresets() const;
    QVariantList             bambooActions() const;

    Q_INVOKABLE void         retryConnection();
    Q_INVOKABLE void         setValue(const QString& key, const QVariant& val);
    Q_INVOKABLE QVariantList getEnumOptions(const QString& key) const;
    Q_INVOKABLE bool         applyAll();
    Q_INVOKABLE void         cancelAll();

    Q_INVOKABLE bool         isSectionModifiedFromDefault(const QString& sectionId) const;
    Q_INVOKABLE QVariantList getSectionResetChanges(const QString& sectionId) const;
    Q_INVOKABLE bool         resetSection(const QString& sectionId);

    Q_INVOKABLE void         reorderModes(int fromIdx, int toIdx);
    Q_INVOKABLE void         setModeVisible(const QString& name, bool vis);
    Q_INVOKABLE void         setModeShortcut(const QString& name, const QString& key);

    Q_INVOKABLE void         addAppRule(const QString& appId);
    Q_INVOKABLE void         updateAppRule(const QString& appId, int mode, int commit, int backspace, int post);
    Q_INVOKABLE void         removeAppRule(const QString& appId);

    Q_INVOKABLE void         upsertMacro(const QString& oldKey, const QString& newKey, const QString& val);
    Q_INVOKABLE void         removeMacro(const QString& key);
    Q_INVOKABLE bool         isValidMacroKey(const QString& key) const;

    Q_INVOKABLE void         addWord(const QString& word);
    Q_INVOKABLE void         removeWord(const QString& word);
    Q_INVOKABLE QVariantList filterWords(const QString& query, int limit) const;
    Q_INVOKABLE int          countMatchingWords(const QString& query) const;
    Q_INVOKABLE bool         isValidWord(const QString& word) const;

    Q_INVOKABLE void         applyKeymapPreset(const QString& presetName);
    Q_INVOKABLE void         upsertKeymap(const QString& key, const QString& actionCode);
    Q_INVOKABLE void         removeKeymap(const QString& key);

    Q_INVOKABLE bool         exportBackup(const QString& fileUrl, const QVariantMap& components);
    Q_INVOKABLE QVariantMap  inspectBackup(const QString& fileUrl) const;
    Q_INVOKABLE bool         importBackup(const QString& fileUrl, const QVariantMap& components);
    Q_INVOKABLE void         exportDebugInfo(const QString& fileUrl);

    Q_INVOKABLE QString      formatKeyEvent(int qtKey, int qtModifiers, const QString& text, bool allowSingleKey, bool singleKeyOnly = false) const;
    Q_INVOKABLE QStringList  prettyHotkeyParts(const QString& hotkeyStr) const;

  signals:
    void stateChanged();
    void toastRequested(const QString& message);

  private:
    struct MetadataItem {
        QString     type;
        QString     title;
        QVariant    defaultValue;
        QVariantMap extra;
    };

    struct DesktopAppEntry {
        QString name;
        QString icon;
    };

    QString                         translate(const char* text) const;
    QString                         translate(const QString& text) const;

    void                            loadAll();
    void                            parseMetadata(const QVariantList& rawMetadata);
    void                            populateValuesFromConfig(const QVariantMap& rawValues);
    void                            loadAppRules();
    void                            loadMacros();
    void                            loadDictionary();
    void                            loadKeymaps();

    void                            scanDesktopEntries();
    QString                         resolveAppDisplayName(const QString& appId) const;
    QString                         resolveAppIconName(const QString& appId) const;

    void                            rebuildCachedDictionaryWords();
    void                            rebuildModeList();
    void                            recomputeDerivedState();

    QVariant                        defaultTypedValue(const QString& key) const;
    QVariant                        normalizeConfigValue(const QString& key, const QVariant& raw) const;
    QVariant                        toWireValue(const QString& key, const QVariant& typedVal) const;
    QVariantMap                     buildWireConfigPayload(const QVariantMap& baseValues, const QStringList& keysToUpdate) const;

    QString                         formatDisplayValue(const QString& key, const QVariant& val) const;
    QString                         formatShortcutDisplay(const QString& shortcut) const;
    QString                         sectionDisplayTitle(const QString& sectionId) const;
    QStringList                     sectionConfigKeys(const QString& sectionId) const;

    bool                            isConfigKeyModifiedFromDefault(const QString& key) const;
    bool                            isModeOrderModifiedFromDefault() const;
    bool                            isModeMenuModifiedFromDefault() const;
    bool                            isDictionaryModifiedFromDefault() const;
    bool                            isDictionaryModifiedFromInitial() const;

    I18nBridge*                     i18n_ = nullptr;
    LotusDBusClient                 dbusClient_;

    bool                            fcitxOffline_        = true;
    bool                            hasUnsavedChanges_   = false;
    bool                            hasValidationErrors_ = false;
    bool                            dictLoadFailed_      = false;
    bool                            dictDirtyInitial_    = false;
    bool                            dictDirtyDefault_    = false;
    bool                            dictHasInvalidWord_  = false;
    QString                         validationMessage_;

    QHash<QString, MetadataItem>    metadata_;

    QVariantMap                     values_;
    QVariantMap                     initialValues_;

    QVariantList                    modeList_;
    QVariantList                    appRules_;
    QVariantList                    initialAppRules_;
    QVariantList                    macros_;
    QVariantList                    initialMacros_;

    QStringList                     dictionaryWords_;
    QStringList                     initialDictionaryWords_;
    QStringList                     globalDictionaryWords_;
    QVariantList                    cachedDictionaryWords_;

    QVariantList                    keymapBindings_;
    QVariantList                    initialKeymapBindings_;

    QVariantList                    keymapPresets_;
    QVariantList                    bambooActions_;

    QHash<QString, QString>         desktopIconCache_;
    QHash<QString, DesktopAppEntry> desktopApps_;
};

#endif // LOTUS_SETTINGS_BACKEND_H
