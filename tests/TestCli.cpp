/*
 *  Copyright (C) 2018 KeePassXC Team <team@keepassxc.org>
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

#include "TestCli.h"
#include "core/Config.h"
#include "core/Bootstrap.h"
#include "core/Tools.h"
#include "crypto/Crypto.h"
#include "config-keepassx-tests.h"

#include "cli/Utils.h"
#include "cli/Add.h"

#include <QFile>

#include <cstdio>

QTEST_MAIN(TestCli)

void TestCli::initTestCase()
{
    QVERIFY(Crypto::init());

    Config::createTempFileInstance();
    Bootstrap::bootstrapApplication();

    // Load the NewDatabase.kdbx file into temporary storage
    QFile sourceDbFile(QString(KEEPASSX_TEST_DATA_DIR).append("/NewDatabase.kdbx"));
    QVERIFY(sourceDbFile.open(QIODevice::ReadOnly));
    QVERIFY(Tools::readAllFromDevice(&sourceDbFile, m_dbData));
    sourceDbFile.close();
}

void TestCli::init()
{
    m_dbFile.reset(new QTemporaryFile());
    m_dbFile->open();
    m_dbFile->write(m_dbData);
    m_dbFile->flush();

    m_stdoutFile.reset(new QTemporaryFile());
    m_stdoutFile->open();
    m_stdoutHandle = fdopen(m_stdoutFile->handle(), "r+");
    Command::setOutputDescriptor(m_stdoutHandle);
}

void TestCli::cleanup()
{
    m_dbFile.reset();
    m_stdoutFile.reset();
    m_stdoutHandle = stdout;
}

void TestCli::cleanupTestCase()
{
}

void TestCli::testAdd()
{
    Add addCmd;
    Utils::setNextPassword("a");
    addCmd.execute({"addx", "-u", "newuser", "--url", "https://example.com/", "-g", "-l", "20", m_dbFile->fileName(), "/newuser-entry"});

    Utils::setNextPassword("a");
    QScopedPointer<Database> db(Database::unlockFromStdin(m_dbFile->fileName(), "", m_stdoutHandle));
    auto* entry = db->rootGroup()->findEntryByPath("/newuser-entry");
    QVERIFY(entry);
    QCOMPARE(entry->username(), "newuser");
    QCOMPARE(entry->url(), "https://example.com/");
    QCOMPARE(entry->password().size(), 20);

    Utils::setNextPassword("a");
    Utils::setNextPassword("newpassword");
    addCmd.execute({"addx", "-u", "newuser2", "--url", "https://example.net/", "-g", "-l", "20", "-p", m_dbFile->fileName(), "/newuser-entry2"});

    Utils::setNextPassword("a");
    db.reset(Database::unlockFromStdin(m_dbFile->fileName(), "", m_stdoutHandle));
    entry = db->rootGroup()->findEntryByPath("/newuser-entry2");
    QVERIFY(entry);
    QCOMPARE(entry->username(), "newuser2");
    QCOMPARE(entry->url(), "https://example.net/");
    QCOMPARE(entry->password(), QString("newpassword"));
}

void TestCli::testClip()
{
}
