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

#include <memory>
#include <mutex>

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
    Q_DISABLE_COPY_MOVE(SettingsBackend)
    explicit SettingsBackend(I18nBridge* i18n = nullptr, QObject* parent = nullptr);
    ~SettingsBackend() override;

    [[nodiscard]] bool                           fcitxOffline() const;
    [[nodiscard]] bool                           hasUnsavedChanges() const;
    [[nodiscard]] bool                           hasValidationErrors() const;
    [[nodiscard]] QString                        validationMessage() const;
    [[nodiscard]] static QString                 appVersion();
    [[nodiscard]] QVariantMap                    values() const;
    [[nodiscard]] QVariantList                   modeList() const;
    [[nodiscard]] QVariantList                   appRules() const;
    [[nodiscard]] QVariantList                   macros() const;
    [[nodiscard]] QVariantList                   dictionaryWords() const;
    [[nodiscard]] int                            totalWordCount() const;
    [[nodiscard]] QVariantList                   keymapBindings() const;
    [[nodiscard]] QVariantList                   keymapPresets() const;
    [[nodiscard]] QVariantList                   bambooActions() const;

    Q_INVOKABLE void                             retryConnection();
    Q_INVOKABLE void                             setValue(const QString& key, const QVariant& val);
    Q_INVOKABLE [[nodiscard]] QVariantList       getEnumOptions(const QString& key) const;
    Q_INVOKABLE [[nodiscard]] QVariantList       getAppModeOptions() const;
    Q_INVOKABLE [[nodiscard]] QString            optionTitle(const QString& key) const;
    Q_INVOKABLE [[nodiscard]] QString            optionDescription(const QString& key) const;
    Q_INVOKABLE [[nodiscard]] QVariant           optionDefault(const QString& key) const;
    Q_INVOKABLE bool                             applyAll();
    Q_INVOKABLE void                             cancelAll();

    Q_INVOKABLE [[nodiscard]] bool               isSectionModifiedFromDefault(const QString& sectionId) const;
    Q_INVOKABLE [[nodiscard]] QVariantList       getSectionResetChanges(const QString& sectionId) const;
    Q_INVOKABLE bool                             resetSection(const QString& sectionId);

    Q_INVOKABLE void                             reorderModes(int fromIdx, int toIdx);
    Q_INVOKABLE void                             setModeVisible(const QString& name, bool vis);
    Q_INVOKABLE void                             setModeShortcut(const QString& name, const QString& key);

    Q_INVOKABLE void                             addAppRule(const QString& appId);
    Q_INVOKABLE void                             updateAppRule(const QString& appId, int mode, int commit, int backspace, int post);
    Q_INVOKABLE void                             removeAppRule(const QString& appId);

    Q_INVOKABLE void                             upsertMacro(const QString& oldKey, const QString& newKey, const QString& val);
    Q_INVOKABLE void                             removeMacro(const QString& key);
    Q_INVOKABLE [[nodiscard]] static bool        isValidMacroKey(const QString& key);

    Q_INVOKABLE void                             addWord(const QString& word);
    Q_INVOKABLE void                             removeWord(const QString& word);
    Q_INVOKABLE [[nodiscard]] QVariantList       filterWords(const QString& query, int limit) const;
    Q_INVOKABLE [[nodiscard]] int                countMatchingWords(const QString& query) const;
    Q_INVOKABLE [[nodiscard]] static bool        isValidWord(const QString& word);

    Q_INVOKABLE void                             applyKeymapPreset(const QString& presetName);
    Q_INVOKABLE void                             upsertKeymap(const QString& key, const QString& actionCode);
    Q_INVOKABLE void                             removeKeymap(const QString& key);

    Q_INVOKABLE bool                             exportBackup(const QString& fileUrl, const QVariantMap& components);
    Q_INVOKABLE [[nodiscard]] QVariantMap        inspectBackup(const QString& fileUrl) const;
    Q_INVOKABLE bool                             importBackup(const QString& fileUrl, const QVariantMap& components);
    Q_INVOKABLE void                             exportDebugInfo(const QString& fileUrl);

    Q_INVOKABLE [[nodiscard]] static QString     formatKeyEvent(int qtKey, int qtModifiers, const QString& text, bool allowSingleKey, bool singleKeyOnly = false);
    Q_INVOKABLE [[nodiscard]] static QStringList prettyHotkeyParts(const QString& hotkeyStr);

  signals:
    void stateChanged();
    void toastRequested(const QString& message);
    void debugExportFinished(bool ok, const QString& message);

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

    QString                      translate(const char* text) const;
    [[nodiscard]] QString        translate(const QString& text) const;

    void                         loadAll();
    void                         applyLoadedConfig(const LotusDBusClient::ConfigData& cfg);
    void                         parseMetadata(const QVariantList& rawMetadata);
    void                         populateValuesFromConfig(const QVariantMap& rawValues);
    void                         loadAppRules();
    void                         loadMacros();
    void                         loadDictionary();
    void                         loadKeymaps();

    void                         scanDesktopEntries();
    [[nodiscard]] QString        resolveAppDisplayName(const QString& appId) const;
    [[nodiscard]] QString        resolveAppIconName(const QString& appId) const;

    void                         rebuildCachedDictionaryWords();
    void                         rebuildModeList();
    [[nodiscard]] QStringList    modeNames() const;
    [[nodiscard]] QString        modeTitle(const QString& suffix) const;
    void                         rebuildKeymapCatalog();
    void                         recomputeDerivedState();

    [[nodiscard]] QVariant       defaultTypedValue(const QString& key) const;
    [[nodiscard]] QVariant       normalizeConfigValue(const QString& key, const QVariant& raw) const;
    [[nodiscard]] QVariant       toWireValue(const QString& key, const QVariant& typedVal) const;
    [[nodiscard]] QVariantMap    buildWireConfigPayload(const QVariantMap& baseValues, const QStringList& keysToUpdate) const;

    [[nodiscard]] QString        formatDisplayValue(const QString& key, const QVariant& val) const;
    [[nodiscard]] QString        formatShortcutDisplay(const QString& shortcut) const;
    [[nodiscard]] QString        sectionDisplayTitle(const QString& sectionId) const;
    [[nodiscard]] QStringList    sectionConfigKeys(const QString& sectionId) const;

    [[nodiscard]] bool           isConfigKeyModifiedFromDefault(const QString& key) const;
    [[nodiscard]] bool           isModeOrderModifiedFromDefault() const;
    [[nodiscard]] bool           isModeMenuModifiedFromDefault() const;
    [[nodiscard]] bool           isDictionaryModifiedFromDefault() const;
    [[nodiscard]] bool           isDictionaryModifiedFromInitial() const;

    I18nBridge*                  i18n_ = nullptr;
    LotusDBusClient              dbusClient_;

    bool                         fcitxOffline_        = true;
    bool                         hasUnsavedChanges_   = false;
    bool                         hasValidationErrors_ = false;
    bool                         dictLoadFailed_      = false;
    bool                         dictDirtyInitial_    = false;
    bool                         dictDirtyDefault_    = false;
    bool                         dictHasInvalidWord_  = false;
    QString                      validationMessage_;

    QHash<QString, MetadataItem> metadata_;

    QVariantMap                  values_;
    QVariantMap                  initialValues_;

    QVariantList                 modeList_;
    QVariantList                 appRules_;
    QVariantList                 initialAppRules_;
    QVariantList                 macros_;
    QVariantList                 initialMacros_;

    QStringList                  dictionaryWords_;
    QStringList                  initialDictionaryWords_;
    QStringList                  globalDictionaryWords_;
    QVariantList                 cachedDictionaryWords_;

    QVariantList                 keymapBindings_;
    QVariantList                 initialKeymapBindings_;

    QVariantList                 keymapPresets_;
    QVariantList                 bambooActions_;

    struct AliveGuard {
        std::mutex mutex;
        bool       alive = true;
    };
    std::shared_ptr<AliveGuard>     aliveGuard_ = std::make_shared<AliveGuard>();

    QHash<QString, QString>         desktopIconCache_;
    QHash<QString, DesktopAppEntry> desktopApps_;
};

#endif // LOTUS_SETTINGS_BACKEND_H
