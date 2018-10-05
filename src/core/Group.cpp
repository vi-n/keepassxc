/*
 *  Copyright (C) 2010 Felix Geyer <debfx@fobos.de>
 *  Copyright (C) 2017 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Group.h"

#include "core/Clock.h"
#include "core/Config.h"
#include "core/DatabaseIcons.h"
#include "core/Global.h"
#include "core/Metadata.h"

#include <QRegularExpression>

const int Group::DefaultIconNumber = 48;
const int Group::RecycleBinIconNumber = 43;
const QString Group::RootAutoTypeSequence = "{USERNAME}{TAB}{PASSWORD}{ENTER}";

Group::CloneFlags Group::DefaultCloneFlags =
    static_cast<Group::CloneFlags>(Group::CloneNewUuid | Group::CloneResetTimeInfo | Group::CloneIncludeEntries);
Entry::CloneFlags Group::DefaultEntryCloneFlags =
    static_cast<Entry::CloneFlags>(Entry::CloneNewUuid | Entry::CloneResetTimeInfo);

Group::Group()
    : m_customData(new CustomData(this))
    , m_updateTimeinfo(true)
{
    m_data.iconNumber = DefaultIconNumber;
    m_data.isExpanded = true;
    m_data.autoTypeEnabled = Inherit;
    m_data.searchingEnabled = Inherit;
    m_data.mergeMode = Default;

    connect(m_customData, SIGNAL(modified()), this, SIGNAL(modified()));
    connect(this, SIGNAL(modified()), SLOT(updateTimeinfo()));
}

Group::~Group()
{
    setUpdateTimeinfo(false);
    // Destroy entries and children manually so DeletedObjects can be added
    // to database.
    const QList<Entry*> entries = m_entries;
    for (Entry* entry : entries) {
        delete entry;
    }

    const QList<Group*> children = m_children;
    for (Group* group : children) {
        delete group;
    }

    if (m_db && m_parent) {
        DeletedObject delGroup;
        delGroup.deletionTime = Clock::currentDateTimeUtc();
        delGroup.uuid = m_uuid;
        m_db->addDeletedObject(delGroup);
    }

    cleanupParent();
}

Group* Group::createRecycleBin()
{
    Group* recycleBin = new Group();
    recycleBin->setUuid(QUuid::createUuid());
    recycleBin->setName(tr("Recycle Bin"));
    recycleBin->setIcon(RecycleBinIconNumber);
    recycleBin->setSearchingEnabled(Group::Disable);
    recycleBin->setAutoTypeEnabled(Group::Disable);
    return recycleBin;
}

template <class P, class V> inline bool Group::set(P& property, const V& value)
{
    if (property != value) {
        property = value;
        emit modified();
        return true;
    } else {
        return false;
    }
}

bool Group::canUpdateTimeinfo() const
{
    return m_updateTimeinfo;
}

void Group::updateTimeinfo()
{
    if (m_updateTimeinfo) {
        m_data.timeInfo.setLastModificationTime(Clock::currentDateTimeUtc());
        m_data.timeInfo.setLastAccessTime(Clock::currentDateTimeUtc());
    }
}

void Group::setUpdateTimeinfo(bool value)
{
    m_updateTimeinfo = value;
}

const QUuid& Group::uuid() const
{
    return m_uuid;
}

const QString Group::uuidToHex() const
{
    return QString::fromLatin1(m_uuid.toRfc4122().toHex());
}

QString Group::name() const
{
    return m_data.name;
}

QString Group::notes() const
{
    return m_data.notes;
}

QImage Group::icon() const
{
    if (m_data.customIcon.isNull()) {
        return databaseIcons()->icon(m_data.iconNumber);
    } else {
        Q_ASSERT(m_db);

        if (m_db) {
            return m_db->metadata()->customIcon(m_data.customIcon);
        } else {
            return QImage();
        }
    }
}

QPixmap Group::iconPixmap() const
{
    if (m_data.customIcon.isNull()) {
        return databaseIcons()->iconPixmap(m_data.iconNumber);
    } else {
        Q_ASSERT(m_db);

        if (m_db) {
            return m_db->metadata()->customIconPixmap(m_data.customIcon);
        } else {
            return QPixmap();
        }
    }
}

QPixmap Group::iconScaledPixmap() const
{
    if (m_data.customIcon.isNull()) {
        // built-in icons are 16x16 so don't need to be scaled
        return databaseIcons()->iconPixmap(m_data.iconNumber);
    } else {
        Q_ASSERT(m_db);

        if (m_db) {
            return m_db->metadata()->customIconScaledPixmap(m_data.customIcon);
        } else {
            return QPixmap();
        }
    }
}

int Group::iconNumber() const
{
    return m_data.iconNumber;
}

const QUuid& Group::iconUuid() const
{
    return m_data.customIcon;
}

const TimeInfo& Group::timeInfo() const
{
    return m_data.timeInfo;
}

bool Group::isExpanded() const
{
    return m_data.isExpanded;
}

QString Group::defaultAutoTypeSequence() const
{
    return m_data.defaultAutoTypeSequence;
}

/**
 * Determine the effective sequence that will be injected
 * This function return an empty string if the current group or any parent has autotype disabled
 */
QString Group::effectiveAutoTypeSequence() const
{
    QString sequence;

    const Group* group = this;
    do {
        if (group->autoTypeEnabled() == Group::Disable) {
            return QString();
        }

        sequence = group->defaultAutoTypeSequence();
        group = group->parentGroup();
    } while (group && sequence.isEmpty());

    if (sequence.isEmpty()) {
        sequence = RootAutoTypeSequence;
    }

    return sequence;
}

Group::TriState Group::autoTypeEnabled() const
{
    return m_data.autoTypeEnabled;
}

Group::TriState Group::searchingEnabled() const
{
    return m_data.searchingEnabled;
}

Group::MergeMode Group::mergeMode() const
{
    if (m_data.mergeMode == Group::MergeMode::Default) {
        if (m_parent) {
            return m_parent->mergeMode();
        }
        return Group::MergeMode::KeepNewer; // fallback
    }
    return m_data.mergeMode;
}

Entry* Group::lastTopVisibleEntry() const
{
    return m_lastTopVisibleEntry;
}

bool Group::isExpired() const
{
    return m_data.timeInfo.expires() && m_data.timeInfo.expiryTime() < Clock::currentDateTimeUtc();
}

CustomData* Group::customData()
{
    return m_customData;
}

const CustomData* Group::customData() const
{
    return m_customData;
}

bool Group::equals(const Group* other, CompareItemOptions options) const
{
    if (!other) {
        return false;
    }
    if (m_uuid != other->m_uuid) {
        return false;
    }
    if (!m_data.equals(other->m_data, options)) {
        return false;
    }
    if (m_customData != other->m_customData) {
        return false;
    }
    if (m_children.count() != other->m_children.count()) {
        return false;
    }
    if (m_entries.count() != other->m_entries.count()) {
        return false;
    }
    for (int i = 0; i < m_children.count(); ++i) {
        if (m_children[i]->uuid() != other->m_children[i]->uuid()) {
            return false;
        }
    }
    for (int i = 0; i < m_entries.count(); ++i) {
        if (m_entries[i]->uuid() != other->m_entries[i]->uuid()) {
            return false;
        }
    }
    return true;
}

void Group::setUuid(const QUuid& uuid)
{
    set(m_uuid, uuid);
}

void Group::setName(const QString& name)
{
    if (set(m_data.name, name)) {
        emit dataChanged(this);
    }
}

void Group::setNotes(const QString& notes)
{
    set(m_data.notes, notes);
}

void Group::setIcon(int iconNumber)
{
    Q_ASSERT(iconNumber >= 0);

    if (m_data.iconNumber != iconNumber || !m_data.customIcon.isNull()) {
        m_data.iconNumber = iconNumber;
        m_data.customIcon = QUuid();
        emit modified();
        emit dataChanged(this);
    }
}

void Group::setIcon(const QUuid& uuid)
{
    Q_ASSERT(!uuid.isNull());

    if (m_data.customIcon != uuid) {
        m_data.customIcon = uuid;
        m_data.iconNumber = 0;
        emit modified();
        emit dataChanged(this);
    }
}

void Group::setTimeInfo(const TimeInfo& timeInfo)
{
    m_data.timeInfo = timeInfo;
}

void Group::setExpanded(bool expanded)
{
    if (m_data.isExpanded != expanded) {
        m_data.isExpanded = expanded;
        if (config()->get("IgnoreGroupExpansion").toBool()) {
            updateTimeinfo();
            return;
        }
        emit modified();
    }
}

void Group::setDefaultAutoTypeSequence(const QString& sequence)
{
    set(m_data.defaultAutoTypeSequence, sequence);
}

void Group::setAutoTypeEnabled(TriState enable)
{
    set(m_data.autoTypeEnabled, enable);
}

void Group::setSearchingEnabled(TriState enable)
{
    set(m_data.searchingEnabled, enable);
}

void Group::setLastTopVisibleEntry(Entry* entry)
{
    set(m_lastTopVisibleEntry, entry);
}

void Group::setExpires(bool value)
{
    if (m_data.timeInfo.expires() != value) {
        m_data.timeInfo.setExpires(value);
        emit modified();
    }
}

void Group::setExpiryTime(const QDateTime& dateTime)
{
    if (m_data.timeInfo.expiryTime() != dateTime) {
        m_data.timeInfo.setExpiryTime(dateTime);
        emit modified();
    }
}

void Group::setMergeMode(MergeMode newMode)
{
    set(m_data.mergeMode, newMode);
}

Group* Group::parentGroup()
{
    return m_parent;
}

const Group* Group::parentGroup() const
{
    return m_parent;
}

void Group::setParent(Group* parent, int index)
{
    Q_ASSERT(parent);
    Q_ASSERT(index >= -1 && index <= parent->children().size());
    // setting a new parent for root groups is not allowed
    Q_ASSERT(!m_db || (m_db->rootGroup() != this));

    bool moveWithinDatabase = (m_db && m_db == parent->m_db);

    if (index == -1) {
        index = parent->children().size();

        if (parentGroup() == parent) {
            index--;
        }
    }

    if (m_parent == parent && parent->children().indexOf(this) == index) {
        return;
    }

    if (!moveWithinDatabase) {
        cleanupParent();
        m_parent = parent;
        if (m_db) {
            recCreateDelObjects();

            // copy custom icon to the new database
            if (!iconUuid().isNull() && parent->m_db && m_db->metadata()->containsCustomIcon(iconUuid())
                && !parent->m_db->metadata()->containsCustomIcon(iconUuid())) {
                parent->m_db->metadata()->addCustomIcon(iconUuid(), icon());
            }
        }
        if (m_db != parent->m_db) {
            recSetDatabase(parent->m_db);
        }
        QObject::setParent(parent);
        emit aboutToAdd(this, index);
        Q_ASSERT(index <= parent->m_children.size());
        parent->m_children.insert(index, this);
    } else {
        emit aboutToMove(this, parent, index);
        m_parent->m_children.removeAll(this);
        m_parent = parent;
        QObject::setParent(parent);
        Q_ASSERT(index <= parent->m_children.size());
        parent->m_children.insert(index, this);
    }

    if (m_updateTimeinfo) {
        m_data.timeInfo.setLocationChanged(Clock::currentDateTimeUtc());
    }

    emit modified();

    if (!moveWithinDatabase) {
        emit added();
    } else {
        emit moved();
    }
}

void Group::setParent(Database* db)
{
    Q_ASSERT(db);
    Q_ASSERT(db->rootGroup() == this);

    cleanupParent();

    m_parent = nullptr;
    recSetDatabase(db);

    QObject::setParent(db);
}

QStringList Group::hierarchy() const
{
    QStringList hierarchy;
    const Group* group = this;
    const Group* parent = m_parent;
    hierarchy.prepend(group->name());

    while (parent) {
        group = group->parentGroup();
        parent = group->parentGroup();

        hierarchy.prepend(group->name());
    }

    return hierarchy;
}

Database* Group::database()
{
    return m_db;
}

const Database* Group::database() const
{
    return m_db;
}

QList<Group*> Group::children()
{
    return m_children;
}

const QList<Group*>& Group::children() const
{
    return m_children;
}

QList<Entry*> Group::entries()
{
    return m_entries;
}

const QList<Entry*>& Group::entries() const
{
    return m_entries;
}

QList<Entry*> Group::entriesRecursive(bool includeHistoryItems) const
{
    QList<Entry*> entryList;

    entryList.append(m_entries);

    if (includeHistoryItems) {
        for (Entry* entry : m_entries) {
            entryList.append(entry->historyItems());
        }
    }

    for (Group* group : m_children) {
        entryList.append(group->entriesRecursive(includeHistoryItems));
    }

    return entryList;
}

Entry* Group::findEntry(QString entryId)
{
    Q_ASSERT(!entryId.isNull());

    Entry* entry;
    QUuid entryUuid = QUuid::fromRfc4122(QByteArray::fromHex(entryId.toLatin1()));
    if (!entryUuid.isNull()) {
        entry = findEntryByUuid(entryUuid);
        if (entry) {
            return entry;
        }
    }

    entry = findEntryByPath(entryId);
    if (entry) {
        return entry;
    }

    for (Entry* entry : entriesRecursive(false)) {
        if (entry->title() == entryId) {
            return entry;
        }
    }

    return nullptr;
}

Entry* Group::findEntryByUuid(const QUuid& uuid) const
{
    Q_ASSERT(!uuid.isNull());
    for (Entry* entry : entriesRecursive(false)) {
        if (entry->uuid() == uuid) {
            return entry;
        }
    }

    return nullptr;
}

Entry* Group::findEntryByPath(QString entryPath, QString basePath)
{

    Q_ASSERT(!entryPath.isNull());

    for (Entry* entry : asConst(m_entries)) {
        QString currentEntryPath = basePath + entry->title();
        if (entryPath == currentEntryPath || entryPath == QString("/" + currentEntryPath)) {
            return entry;
        }
    }

    for (Group* group : asConst(m_children)) {
        Entry* entry = group->findEntryByPath(entryPath, basePath + group->name() + QString("/"));
        if (entry != nullptr) {
            return entry;
        }
    }

    return nullptr;
}

Group* Group::findGroupByPath(QString groupPath)
{
    Q_ASSERT(!groupPath.isNull());

    // normalize the groupPath by adding missing front and rear slashes. once.
    QString normalizedGroupPath;

    if (groupPath == "") {
        normalizedGroupPath = QString("/"); // root group
    } else {
        normalizedGroupPath = ((groupPath.startsWith("/"))? "" : "/")
            + groupPath
            + ((groupPath.endsWith("/") )? "" : "/");
    }
    return findGroupByPathRecursion(normalizedGroupPath, "/");
}

Group* Group::findGroupByPathRecursion(QString groupPath, QString basePath)
{
    // paths must be normalized
    Q_ASSERT(groupPath.startsWith("/") && groupPath.endsWith("/"));
    Q_ASSERT(basePath.startsWith("/") && basePath.endsWith("/"));

    if (groupPath == basePath) {
        return this;
    }

    for (Group* innerGroup : children()) {
        QString innerBasePath = basePath + innerGroup->name() + "/";
        Group* group = innerGroup->findGroupByPathRecursion(groupPath, innerBasePath);
        if (group != nullptr) {
            return group;
        }
    }

    return nullptr;
}

QString Group::print(bool recursive, int depth)
{

    QString response;
    QString indentation = QString("  ").repeated(depth);

    if (entries().isEmpty() && children().isEmpty()) {
        response += indentation + tr("[empty]", "group has no children") + "\n";
        return response;
    }

    for (Entry* entry : entries()) {
        response += indentation + entry->title() + "\n";
    }

    for (Group* innerGroup : children()) {
        response += indentation + innerGroup->name() + "/\n";
        if (recursive) {
            response += innerGroup->print(recursive, depth + 1);
        }
    }

    return response;
}

QList<const Group*> Group::groupsRecursive(bool includeSelf) const
{
    QList<const Group*> groupList;
    if (includeSelf) {
        groupList.append(this);
    }

    for (const Group* group : m_children) {
        groupList.append(group->groupsRecursive(true));
    }

    return groupList;
}

QList<Group*> Group::groupsRecursive(bool includeSelf)
{
    QList<Group*> groupList;
    if (includeSelf) {
        groupList.append(this);
    }

    for (Group* group : asConst(m_children)) {
        groupList.append(group->groupsRecursive(true));
    }

    return groupList;
}

QSet<QUuid> Group::customIconsRecursive() const
{
    QSet<QUuid> result;

    if (!iconUuid().isNull()) {
        result.insert(iconUuid());
    }

    const QList<Entry*> entryList = entriesRecursive(true);
    for (Entry* entry : entryList) {
        if (!entry->iconUuid().isNull()) {
            result.insert(entry->iconUuid());
        }
    }

    for (Group* group : m_children) {
        result.unite(group->customIconsRecursive());
    }

    return result;
}

Group* Group::findGroupByUuid(const QUuid& uuid)
{
    Q_ASSERT(!uuid.isNull());
    for (Group* group : groupsRecursive(true)) {
        if (group->uuid() == uuid) {
            return group;
        }
    }

    return nullptr;
}

Group* Group::findChildByName(const QString& name)
{
    for (Group* group : asConst(m_children)) {
        if (group->name() == name) {
            return group;
        }
    }

    return nullptr;
}

Group* Group::clone(Entry::CloneFlags entryFlags, Group::CloneFlags groupFlags) const
{
    Group* clonedGroup = new Group();

    clonedGroup->setUpdateTimeinfo(false);

    if (groupFlags & Group::CloneNewUuid) {
        clonedGroup->setUuid(QUuid::createUuid());
    } else {
        clonedGroup->setUuid(this->uuid());
    }

    clonedGroup->m_data = m_data;
    clonedGroup->m_customData->copyDataFrom(m_customData);

    if (groupFlags & Group::CloneIncludeEntries) {
        const QList<Entry*> entryList = entries();
        for (Entry* entry : entryList) {
            Entry* clonedEntry = entry->clone(entryFlags);
            clonedEntry->setGroup(clonedGroup);
        }

        const QList<Group*> childrenGroups = children();
        for (Group* groupChild : childrenGroups) {
            Group* clonedGroupChild = groupChild->clone(entryFlags, groupFlags);
            clonedGroupChild->setParent(clonedGroup);
        }
    }

    clonedGroup->setUpdateTimeinfo(true);
    if (groupFlags & Group::CloneResetTimeInfo) {

        QDateTime now = Clock::currentDateTimeUtc();
        clonedGroup->m_data.timeInfo.setCreationTime(now);
        clonedGroup->m_data.timeInfo.setLastModificationTime(now);
        clonedGroup->m_data.timeInfo.setLastAccessTime(now);
        clonedGroup->m_data.timeInfo.setLocationChanged(now);
    }

    return clonedGroup;
}

void Group::copyDataFrom(const Group* other)
{
    m_data = other->m_data;
    m_customData->copyDataFrom(other->m_customData);
    m_lastTopVisibleEntry = other->m_lastTopVisibleEntry;
}

void Group::addEntry(Entry* entry)
{
    Q_ASSERT(entry);
    Q_ASSERT(!m_entries.contains(entry));

    emit entryAboutToAdd(entry);

    m_entries << entry;
    connect(entry, SIGNAL(dataChanged(Entry*)), SIGNAL(entryDataChanged(Entry*)));
    if (m_db) {
        connect(entry, SIGNAL(modified()), m_db, SIGNAL(modifiedImmediate()));
    }

    emit modified();
    emit entryAdded(entry);
}

void Group::removeEntry(Entry* entry)
{
    Q_ASSERT_X(m_entries.contains(entry),
               Q_FUNC_INFO,
               QString("Group %1 does not contain %2").arg(this->name()).arg(entry->title()).toLatin1());

    emit entryAboutToRemove(entry);

    entry->disconnect(this);
    if (m_db) {
        entry->disconnect(m_db);
    }
    m_entries.removeAll(entry);
    emit modified();
    emit entryRemoved(entry);
}

void Group::recSetDatabase(Database* db)
{
    if (m_db) {
        disconnect(SIGNAL(dataChanged(Group*)), m_db);
        disconnect(SIGNAL(aboutToRemove(Group*)), m_db);
        disconnect(SIGNAL(removed()), m_db);
        disconnect(SIGNAL(aboutToAdd(Group*, int)), m_db);
        disconnect(SIGNAL(added()), m_db);
        disconnect(SIGNAL(aboutToMove(Group*, Group*, int)), m_db);
        disconnect(SIGNAL(moved()), m_db);
        disconnect(SIGNAL(modified()), m_db);
    }

    for (Entry* entry : asConst(m_entries)) {
        if (m_db) {
            entry->disconnect(m_db);
        }
        if (db) {
            connect(entry, SIGNAL(modified()), db, SIGNAL(modifiedImmediate()));
        }
    }

    if (db) {
        connect(this, SIGNAL(dataChanged(Group*)), db, SIGNAL(groupDataChanged(Group*)));
        connect(this, SIGNAL(aboutToRemove(Group*)), db, SIGNAL(groupAboutToRemove(Group*)));
        connect(this, SIGNAL(removed()), db, SIGNAL(groupRemoved()));
        connect(this, SIGNAL(aboutToAdd(Group*, int)), db, SIGNAL(groupAboutToAdd(Group*, int)));
        connect(this, SIGNAL(added()), db, SIGNAL(groupAdded()));
        connect(this, SIGNAL(aboutToMove(Group*, Group*, int)), db, SIGNAL(groupAboutToMove(Group*, Group*, int)));
        connect(this, SIGNAL(moved()), db, SIGNAL(groupMoved()));
        connect(this, SIGNAL(modified()), db, SIGNAL(modifiedImmediate()));
    }

    m_db = db;

    for (Group* group : asConst(m_children)) {
        group->recSetDatabase(db);
    }
}

void Group::cleanupParent()
{
    if (m_parent) {
        emit aboutToRemove(this);
        m_parent->m_children.removeAll(this);
        emit modified();
        emit removed();
    }
}

void Group::recCreateDelObjects()
{
    if (m_db) {
        for (Entry* entry : asConst(m_entries)) {
            m_db->addDeletedObject(entry->uuid());
        }

        for (Group* group : asConst(m_children)) {
            group->recCreateDelObjects();
        }
        m_db->addDeletedObject(m_uuid);
    }
}

bool Group::resolveSearchingEnabled() const
{
    switch (m_data.searchingEnabled) {
    case Inherit:
        if (!m_parent) {
            return true;
        } else {
            return m_parent->resolveSearchingEnabled();
        }
    case Enable:
        return true;
    case Disable:
        return false;
    default:
        Q_ASSERT(false);
        return false;
    }
}

bool Group::resolveAutoTypeEnabled() const
{
    switch (m_data.autoTypeEnabled) {
    case Inherit:
        if (!m_parent) {
            return true;
        } else {
            return m_parent->resolveAutoTypeEnabled();
        }
    case Enable:
        return true;
    case Disable:
        return false;
    default:
        Q_ASSERT(false);
        return false;
    }
}

QStringList Group::locate(QString locateTerm, QString currentPath)
{
    Q_ASSERT(!locateTerm.isNull());
    QStringList response;

    for (Entry* entry : asConst(m_entries)) {
        QString entryPath = currentPath + entry->title();
        if (entryPath.toLower().contains(locateTerm.toLower())) {
            response << entryPath;
        }
    }

    for (Group* group : asConst(m_children)) {
        for (QString path : group->locate(locateTerm, currentPath + group->name() + QString("/"))) {
            response << path;
        }
    }

    return response;
}

Entry* Group::addEntryWithPath(QString entryPath)
{
    Q_ASSERT(!entryPath.isNull());
    if (this->findEntryByPath(entryPath)) {
        return nullptr;
    }

    /* This looks ridiculous because of the C++ compiler escaping backslashes 
     * in a string. What this says: 
     * Match /      \\/ (we need to escape the slash, and to enter a backslash
     *                   we need double backslashes)
     * But not      (?<! )
     *  match \/    \\\\\\/ we need to escape the backslash, so it's \\\\ for 
     *                      the single backslash and \\/ for the forward slash 
     */
    QRegularExpression re("\\/(?<!\\\\\\/)");

    QStringList groups = entryPath.split(re);
    QString entryTitle = groups.takeLast().replace("\\/", "/");
    QString groupPath = groups.join("/");
    if (groupPath.isNull()) {
        groupPath = QString("");
    }

    Q_ASSERT(!groupPath.isNull());
    Group* group = this->findGroupByPath(groupPath);
    if (!group) {
        return nullptr;
    }

    Entry* entry = new Entry();
    entry->setTitle(entryTitle);
    entry->setUuid(QUuid::createUuid());
    entry->setGroup(group);

    return entry;
}

bool Group::GroupData::operator==(const Group::GroupData& other) const
{
    return equals(other, CompareItemDefault);
}

bool Group::GroupData::operator!=(const Group::GroupData& other) const
{
    return !(*this == other);
}

bool Group::GroupData::equals(const Group::GroupData& other, CompareItemOptions options) const
{
    if (::compare(name, other.name, options) != 0) {
        return false;
    }
    if (::compare(notes, other.notes, options) != 0) {
        return false;
    }
    if (::compare(iconNumber, other.iconNumber) != 0) {
        return false;
    }
    if (::compare(customIcon, other.customIcon) != 0) {
        return false;
    }
    if (timeInfo.equals(other.timeInfo, options) != 0) {
        return false;
    }
    // TODO HNH: Some properties are configurable - should they be ignored?
    if (::compare(isExpanded, other.isExpanded, options) != 0) {
        return false;
    }
    if (::compare(defaultAutoTypeSequence, other.defaultAutoTypeSequence, options) != 0) {
        return false;
    }
    if (::compare(autoTypeEnabled, other.autoTypeEnabled, options) != 0) {
        return false;
    }
    if (::compare(searchingEnabled, other.searchingEnabled, options) != 0) {
        return false;
    }
    if (::compare(mergeMode, other.mergeMode, options) != 0) {
        return false;
    }
    return true;
}
