/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "settings.h"

#ifndef NO_QT_SUPPORT
#include <QDateTime>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#endif

#ifdef MUSE_MODULE_MULTIWINDOWS
#include "multiwindows/resourcelockguard.h"
#endif

#include "muse_framework_config.h"

#include "log.h"

using namespace muse;
using namespace muse::async;

static const std::string SETTINGS_RESOURCE_NAME("SETTINGS");

Settings* Settings::instance()
{
    static Settings s;
    return &s;
}

//! NOTE mscz_test: the Qt-free build does not persist settings.
//!
//! Settings is QSettings underneath -- an INI file in the user's data
//! directory. A renderer only ever reads defaults and whatever was set during
//! the session, and writing to a user's disk is not something it should do,
//! so without Qt the store is simply absent: reads find nothing and fall back
//! to the default, and writes stay in memory.
Settings::Settings()
{
#ifdef NO_QT_SUPPORT
    // Nothing to open.
#else
#ifdef WIN_PORTABLE
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dataPath());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, dataPath());
#endif

#ifndef Q_OS_MAC
    QSettings::setDefaultFormat(QSettings::IniFormat);
#endif

    m_settings = new QSettings();
#endif
}

Settings::~Settings()
{
#ifndef NO_QT_SUPPORT
    delete m_settings;
#endif
}

io::path_t Settings::filePath() const
{
#ifdef NO_QT_SUPPORT
    // No store, so no file behind it.
    return io::path_t();
#else
    return m_settings->fileName();
#endif
}

const Settings::Items& Settings::items() const
{
    return m_isTransactionStarted ? m_localSettings : m_items;
}

/**
 * @brief Settings::reload method needed only for compatibility with the old MU preferences
 */
void Settings::reload()
{
    Items items = readItems();

    for (auto it = items.cbegin(); it != items.cend(); ++it) {
        setSharedValue(it->first, it->second.value);
    }
}

void Settings::load()
{
    m_items = readItems();
}

void Settings::reset(bool keepDefaultSettings, bool notifyAboutChanges, bool notifyOtherInstances)
{
#ifndef NO_QT_SUPPORT
    m_settings->clear();
#endif

    m_isTransactionStarted = false;

    std::vector<Settings::Key> locallyAddedKeys;
    for (auto it = m_localSettings.begin(); it != m_localSettings.end(); ++it) {
        auto item = m_items.find(it->first);
        if (item == m_items.end()) {
            locallyAddedKeys.push_back(it->first);
        } else {
            // UI currently has the values from m_localSettings but we've turned off the transaction.
            item->second.value = it->second.value;
        }
    }

    m_localSettings.clear();

#ifndef NO_QT_SUPPORT
    if (!keepDefaultSettings) {
        QDir(dataPath()).removeRecursively();
        QDir().mkpath(dataPath());
    }
#else
    UNUSED(keepDefaultSettings);
#endif

    if (!notifyAboutChanges) {
        return;
    }

    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if (it->second.value == it->second.defaultValue) {
            continue;
        }

        it->second.value = it->second.defaultValue;

        Channel<Val>& channel = findChannel(it->first);
        channel.send(it->second.value);
    }

    for (auto it = locallyAddedKeys.cbegin(); it != locallyAddedKeys.cend(); ++it) {
        Channel<Val>& channel = findChannel(*it);
        channel.send(Val());
    }

    UNUSED(notifyOtherInstances);
#ifdef MUSE_MODULE_MULTIWINDOWS
    if (notifyOtherInstances && multiwindowsProvider()) {
        multiwindowsProvider()->settingsReset();
    }
#endif
}

#ifndef NO_QT_SUPPORT
static Val compat_QVariantToVal(const QVariant& var)
{
    if (!var.isValid()) {
        return Val();
    }

    switch (var.typeId()) {
    case QMetaType::QByteArray: return Val(var.toByteArray().toStdString());
    case QMetaType::QDateTime: return Val(var.toDateTime().toString(Qt::ISODate));
    case QMetaType::QStringList: {
        QStringList sl = var.toStringList();
        ValList vl;
        for (const QString& s : sl) {
            vl.push_back(Val(s));
        }
        return Val(vl);
    }
    default:
        break;
    }

    return Val::fromQVariant(var);
}
#endif

Settings::Items Settings::readItems() const
{
    Items result;
#ifdef NO_QT_SUPPORT
    // No store to read: every key falls back to its default.
    return result;
#else
#ifdef MUSE_MODULE_MULTIWINDOWS
    muse::mi::ReadResourceLockGuard resource_lock(multiwindowsProvider.get(), SETTINGS_RESOURCE_NAME);
#endif
    for (const QString& key : m_settings->allKeys()) {
        Item item;
        item.key = Key(std::string(), key.toStdString());
        item.value = compat_QVariantToVal(m_settings->value(key));

        result[item.key] = item;
    }

    return result;
#endif
}

const Val& Settings::value(const Key& key) const
{
    return findItem(key).value;
}

const Val& Settings::defaultValue(const Key& key) const
{
    return findItem(key).defaultValue;
}

void Settings::setSharedValue(const Key& key, const Val& value)
{
    setLocalValue(key, value);
#ifdef MUSE_MODULE_MULTIWINDOWS
    if (multiwindowsProvider()) {
        multiwindowsProvider()->settingsSetValue(key.key, value);
    }
#endif
}

void Settings::setLocalValue(const Key& key, const Val& value)
{
    Item& item = findItem(key);

    if (!item.isNull() && item.value == value) {
        return;
    }

    if (!m_isTransactionStarted) {
        writeValue(key, value);
    }

    if (item.isNull()) {
        insertNewItem(key, value);
    } else {
        item.value = value;
    }

    auto it = m_channels.find(key);
    if (it != m_channels.end()) {
        async::Channel<Val> channel = it->second;
        channel.send(value);
    }
}

void Settings::writeValue(const Key& key, const Val& value)
{
#ifdef NO_QT_SUPPORT
    // Values live in m_items for the session; nothing goes to disk.
    UNUSED(key);
    UNUSED(value);
    return;
#else
#ifdef MUSE_MODULE_MULTIWINDOWS
    muse::mi::WriteResourceLockGuard resource_lock(multiwindowsProvider.get(), SETTINGS_RESOURCE_NAME);
#endif
    // TODO: implement writing/reading first part of key (module name)
    m_settings->setValue(QString::fromStdString(key.key), value.toQVariant());
#endif
}

#ifndef NO_QT_SUPPORT
QString Settings::dataPath() const
{
#ifdef WIN_PORTABLE
    return QDir::cleanPath(QString("%1/../../../Data/settings").arg(QCoreApplication::applicationDirPath()));
#else
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
#endif
}
#endif

void Settings::setDefaultValue(const Key& key, const Val& value)
{
    Item& item = findItem(key);

    if (item.isNull()) {
        m_items[key] = Item{ key, value, value, "", false, Val(), Val() };
    } else {
        item.defaultValue = value;
        item.value.setType(value.type());
    }
}

void Settings::setDescription(const Key& key, const std::string& value)
{
    Item& item = findItem(key);
    if (item.isNull()) {
        return;
    }

    item.description = value;
}

void Settings::setCanBeManuallyEdited(const Settings::Key& key, bool canBeManuallyEdited, const Val& minValue, const Val& maxValue)
{
    Item& item = findItem(key);

    if (item.isNull()) {
        m_items[key] = Item{ key, Val(), Val(), "", canBeManuallyEdited, minValue, maxValue };
    } else {
        item.canBeManuallyEdited = canBeManuallyEdited;
        item.minValue = minValue;
        item.maxValue = maxValue;
    }
}

void Settings::insertNewItem(const Settings::Key& key, const Val& value)
{
    Item item = Item{ key, value, value, "", false, Val(), Val() };
    if (m_isTransactionStarted) {
        m_localSettings[key] = item;
    } else {
        m_items[key] = item;
    }
}

void Settings::beginTransaction(bool notifyToOtherInstances)
{
    if (m_isTransactionStarted) {
        LOGW() << "Transaction is already started";
        return;
    }

    m_localSettings = m_items;
    m_isTransactionStarted = true;

    UNUSED(notifyToOtherInstances)
#ifdef MUSE_MODULE_MULTIWINDOWS
    if (notifyToOtherInstances && multiwindowsProvider()) {
        multiwindowsProvider()->settingsBeginTransaction();
    }
#endif
}

void Settings::commitTransaction(bool notifyToOtherInstances)
{
    m_isTransactionStarted = false;

    for (auto it = m_localSettings.begin(); it != m_localSettings.end(); ++it) {
        Item& item = findItem(it->first);
        if (item.value == it->second.value) {
            continue;
        }

        if (item.isNull()) {
            insertNewItem(it->first, it->second.value);
        } else {
            item.value = it->second.value;
        }

        writeValue(it->first, it->second.value);
    }

    m_localSettings.clear();

    UNUSED(notifyToOtherInstances)
#ifdef MUSE_MODULE_MULTIWINDOWS
    if (notifyToOtherInstances && multiwindowsProvider()) {
        multiwindowsProvider()->settingsCommitTransaction();
    }
#endif
}

void Settings::rollbackTransaction(bool notifyToOtherInstances)
{
    m_isTransactionStarted = false;

    for (auto it = m_localSettings.begin(); it != m_localSettings.end(); ++it) {
        Item item = findItem(it->first);
        if (item.value == it->second.value) {
            continue;
        }

        Channel<Val>& channel = findChannel(it->first);
        channel.send(item.value);
    }

    m_localSettings.clear();

    UNUSED(notifyToOtherInstances)
#ifdef MUSE_MODULE_MULTIWINDOWS
    if (notifyToOtherInstances && multiwindowsProvider()) {
        multiwindowsProvider()->settingsRollbackTransaction();
    }
#endif
}

Settings::Item& Settings::findItem(const Key& key) const
{
    Items& items = m_isTransactionStarted ? m_localSettings : m_items;

    auto it = items.find(key);

    if (it == items.end()) {
        static Item null;
        return null;
    }

    return it->second;
}

async::Channel<Val>& Settings::findChannel(const Settings::Key& key) const
{
    auto it = m_channels.find(key);

    if (it == m_channels.end()) {
        static async::Channel<Val> null;
        return null;
    }

    return it->second;
}

async::Channel<Val> Settings::valueChanged(const Key& key) const
{
    return m_channels[key];
}

Settings::Key::Key(std::string moduleName, std::string key)
    : moduleName(std::move(moduleName)), key(std::move(key))
{
}

bool Settings::Key::operator==(const Key& k) const
{
    return key == k.key;
}

bool Settings::Key::operator<(const Key& k) const
{
    return key < k.key;
}

bool Settings::Key::isNull() const
{
    return key.empty();
}
