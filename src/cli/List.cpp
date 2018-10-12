/*
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

#include <cstdlib>
#include <stdio.h>

#include "List.h"

#include <QCommandLineParser>
#include <QTextStream>

#include "core/Database.h"
#include "core/Entry.h"
#include "core/Group.h"

List::List()
{
    name = QString("ls");
    description = QObject::tr("List database entries.");
}

List::~List()
{
}

int List::execute(const QStringList& arguments)
{
    QTextStream out(stdout);

    QCommandLineParser parser;
    parser.setApplicationDescription(this->description);
    parser.addPositionalArgument("database", QObject::tr("Path of the database."));
    parser.addPositionalArgument("group", QObject::tr("Path of the group to list. Default is /"), QString("[group]"));
    QCommandLineOption keyFile(QStringList() << "k" << "key-file",
                               QObject::tr("Key file of the database."),
                               QObject::tr("path"));
    parser.addOption(keyFile);

    QCommandLineOption recursiveOption(QStringList() << "R" << "recursive",
                                       QObject::tr("Recursive mode, list elements recursively"));
    parser.addOption(recursiveOption);

    QCommandLineOption flattenOption(QStringList() << "f" << "flatten",
                                       QObject::tr("Instead of indenting subelements of a group, prepend the path"));
    parser.addOption(flattenOption);
    parser.process(arguments);

    const QStringList args = parser.positionalArguments();
    if (args.size() != 1 && args.size() != 2) {
        out << parser.helpText().replace("keepassxc-cli", "keepassxc-cli ls");
        return EXIT_FAILURE;
    }

    bool recursive = parser.isSet(recursiveOption);
    bool flatten = parser.isSet(flattenOption);

    Database* db = Database::unlockFromStdin(args.at(0), parser.value(keyFile));
    if (db == nullptr) {
        return EXIT_FAILURE;
    }

    if (args.size() == 2) {
        return this->listGroup(db, recursive, flatten, args.at(1));
    }
    return this->listGroup(db, recursive, flatten);
}

int List::listGroup(Database* database, bool recursive, bool flatten, QString groupPath)
{
    QTextStream outputTextStream(stdout, QIODevice::WriteOnly);
    if (groupPath.isEmpty()) {
        outputTextStream << database->rootGroup()->print(recursive, flatten);
        outputTextStream.flush();
        return EXIT_SUCCESS;
    }

    Group* group = database->rootGroup()->findGroupByPath(groupPath);
    if (group == nullptr) {
        qCritical("Cannot find group %s.", qPrintable(groupPath));
        return EXIT_FAILURE;
    }

    outputTextStream << group->print(recursive, flatten);
    outputTextStream.flush();
    return EXIT_SUCCESS;
}
