// Copyright 2005-2018 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "mumble_pch.hpp"
#include <iostream>

#include "Database.h"

#include "Global.h"
#include "Message.h"
#include "Net.h"
#include "Version.h"


static void logSQLError(const QSqlQuery &query) {
	const QSqlError error(query.lastQuery());
    //qWarning() << "SQL Query failed" << query.lastQuery();
	qWarning() << error.number() << query.lastError().text();
}

static bool execQueryAndLogFailure(QSqlQuery &query) {
	if(!query.exec()) {
		logSQLError(query);
		return false;
	}
	return true;
}

static bool execQueryAndLogFailure(QSqlQuery &query, const QString &queryString) {
	if(!query.exec(queryString)) {
		logSQLError(query);
		return false;
	}
	return true;
}

QStringList tableNames;


Database::Database(const QString &dbname) {
	db = QSqlDatabase::addDatabase(QLatin1String("QSQLITE"), dbname);
	QSettings qs;
	QStringList datapaths;
    tableNames = QStringList();
	int i;

    datapaths << g.qdBasePath.absolutePath();
#if QT_VERSION >= 0x050000
	datapaths << QStandardPaths::writableLocation(QStandardPaths::DataLocation);
#else
	datapaths << QDesktopServices::storageLocation(QDesktopServices::DataLocation);
#endif
#if defined(Q_OS_UNIX) && ! defined(Q_OS_MAC)
	datapaths << QDir::homePath() + QLatin1String("/.config/Mumble");
#endif
	datapaths << QDir::homePath();
	datapaths << QDir::currentPath();
	datapaths << qApp->applicationDirPath();
	datapaths << qs.value(QLatin1String("InstPath")).toString();
	bool found = false;

	for (i = 0; (i < datapaths.size()) && ! found; i++) {
		if (!datapaths[i].isEmpty()) {
			// Try the legacy path first, and use it if it exists.
			// If it doesn't, use the new, non-hidden version.
			QFile legacyDatabaseFile(datapaths[i] + QLatin1String("/.mumble.sqlite"));
			if (legacyDatabaseFile.exists()) {
				db.setDatabaseName(legacyDatabaseFile.fileName());
				found = db.open();
			}
			if (found) {
				break;
			}
			QFile databaseFile(datapaths[i] + QLatin1String("/mumble.sqlite"));
			if (databaseFile.exists()) {
				db.setDatabaseName(databaseFile.fileName());
				found = db.open();
			}
		}
	}

	if (! found) {
		for (i = 0; (i < datapaths.size()) && ! found; i++) {
			if (!datapaths[i].isEmpty()) {
				QDir::root().mkpath(datapaths[i]);
				QFile f(datapaths[i] + QLatin1String("/mumble.sqlite"));
				db.setDatabaseName(f.fileName());
				found = db.open();
			}
		}
	}

	if (! found) {
		QMessageBox::critical(NULL, QLatin1String("Mumble"), tr("Mumble failed to initialize a database in any\nof the possible locations."), QMessageBox::Ok | QMessageBox::Default, QMessageBox::NoButton);
		qFatal("Database: Failed initialization");
	}

	QFileInfo fi(db.databaseName());

	if (! fi.isWritable()) {
		QMessageBox::critical(NULL, QLatin1String("Mumble"), tr("The database '%1' is read-only. Mumble cannot store server settings (i.e. SSL certificates) until you fix this problem.").arg(Qt::escape(fi.filePath())), QMessageBox::Ok | QMessageBox::Default, QMessageBox::NoButton);
		qWarning("Database: Database is read-only");
	}

	{
		QFile f(db.databaseName());
		f.setPermissions(f.permissions() & ~(QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther));
	}

	QSqlQuery query(db);

	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `servers` (`id` INTEGER PRIMARY KEY AUTOINCREMENT, `name` TEXT, `hostname` TEXT, `port` INTEGER DEFAULT " MUMTEXT(DEFAULT_MUMBLE_PORT) ", `username` TEXT, `password` TEXT)"));
	query.exec(QLatin1String("ALTER TABLE `servers` ADD COLUMN `url` TEXT")); // Upgrade path, failing this query is not noteworthy

	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `comments` (`who` TEXT, `comment` BLOB, `seen` DATE)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE UNIQUE INDEX IF NOT EXISTS `comments_comment` ON `comments`(`who`, `comment`)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE INDEX IF NOT EXISTS `comments_seen` ON `comments`(`seen`)"));

	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `blobs` (`hash` TEXT, `data` BLOB, `seen` DATE)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE UNIQUE INDEX IF NOT EXISTS `blobs_hash` ON `blobs`(`hash`)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE INDEX IF NOT EXISTS `blobs_seen` ON `blobs`(`seen`)"));

	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `tokens` (`id` INTEGER PRIMARY KEY AUTOINCREMENT, `digest` BLOB, `token` TEXT)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE INDEX IF NOT EXISTS `tokens_host_port` ON `tokens`(`digest`)"));

	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `shortcut` (`id` INTEGER PRIMARY KEY AUTOINCREMENT, `digest` BLOB, `shortcut` BLOB, `target` BLOB, `suppress` INTEGER)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE INDEX IF NOT EXISTS `shortcut_host_port` ON `shortcut`(`digest`)"));

	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `udp` (`id` INTEGER PRIMARY KEY AUTOINCREMENT, `digest` BLOB)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE UNIQUE INDEX IF NOT EXISTS `udp_host_port` ON `udp`(`digest`)"));

	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `cert` (`id` INTEGER PRIMARY KEY AUTOINCREMENT, `hostname` TEXT, `port` INTEGER, `digest` TEXT)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE UNIQUE INDEX IF NOT EXISTS `cert_host_port` ON `cert`(`hostname`,`port`)"));

	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `friends` (`id` INTEGER PRIMARY KEY AUTOINCREMENT, `name` TEXT, `hash` TEXT)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE UNIQUE INDEX IF NOT EXISTS `friends_name` ON `friends`(`name`)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE UNIQUE INDEX IF NOT EXISTS `friends_hash` ON `friends`(`hash`)"));

	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `ignored` (`id` INTEGER PRIMARY KEY AUTOINCREMENT, `hash` TEXT)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE UNIQUE INDEX IF NOT EXISTS `ignored_hash` ON `ignored`(`hash`)"));

	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `muted` (`id` INTEGER PRIMARY KEY AUTOINCREMENT, `hash` TEXT)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE UNIQUE INDEX IF NOT EXISTS `muted_hash` ON `muted`(`hash`)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `volume` (`id` INTEGER PRIMARY KEY AUTOINCREMENT, `hash` TEXT, `volume` FLOAT)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE UNIQUE INDEX IF NOT EXISTS `volume_hash` ON `volume`(`hash`)"));

	//Note: A previous snapshot version created a table called 'hidden'
	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `filtered_channels` (`id` INTEGER PRIMARY KEY AUTOINCREMENT, `server_cert_digest` TEXT NOT NULL, `channel_id` INTEGER NOT NULL)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE UNIQUE INDEX IF NOT EXISTS `filtered_channels_entry` ON `filtered_channels`(`server_cert_digest`, `channel_id`)"));

	execQueryAndLogFailure(query, QLatin1String("CREATE TABLE IF NOT EXISTS `pingcache` (`id` INTEGER PRIMARY KEY AUTOINCREMENT, `hostname` TEXT, `port` INTEGER, `ping` INTEGER)"));
	execQueryAndLogFailure(query, QLatin1String("CREATE UNIQUE INDEX IF NOT EXISTS `pingcache_host_port` ON `pingcache`(`hostname`,`port`)"));

	execQueryAndLogFailure(query, QLatin1String("DELETE FROM `comments` WHERE `seen` < datetime('now', '-1 years')"));
	execQueryAndLogFailure(query, QLatin1String("DELETE FROM `blobs` WHERE `seen` < datetime('now', '-1 months')"));

    //INSERTED  NEW TABLE IN SQLITE DATABASE
    //IF MESSAGE TABLE IS NOT IN DATABASE, CREATE!


	execQueryAndLogFailure(query, QLatin1String("VACUUM"));

	execQueryAndLogFailure(query, QLatin1String("PRAGMA synchronous = NORMAL"));
#ifdef Q_OS_WIN
	// Windows can not handle TRUNCATE with multiple connections to the DB. Thus less performant DELETE.
	execQueryAndLogFailure(query, QLatin1String("PRAGMA journal_mode = DELETE"));
#else
	execQueryAndLogFailure(query, QLatin1String("PRAGMA journal_mode = TRUNCATE"));
#endif

	execQueryAndLogFailure(query, QLatin1String("SELECT sqlite_version()"));
	while (query.next())
		qWarning() << "Database SQLite:" << query.value(0).toString();
}

Database::~Database() {
	QSqlQuery query(db);
	execQueryAndLogFailure(query, QLatin1String("PRAGMA journal_mode = DELETE"));
	execQueryAndLogFailure(query, QLatin1String("VACUUM"));
}

QList<FavoriteServer> Database::getFavorites() {
	QSqlQuery query(db);
	QList<FavoriteServer> ql;

	query.prepare(QLatin1String("SELECT `name`, `hostname`, `port`, `username`, `password`, `url` FROM `servers` ORDER BY `name`"));
	execQueryAndLogFailure(query);

	while (query.next()) {
		FavoriteServer fs;
		fs.qsName = query.value(0).toString();
		fs.qsHostname = query.value(1).toString();
		fs.usPort = static_cast<unsigned short>(query.value(2).toUInt());
		fs.qsUsername = query.value(3).toString();
		fs.qsPassword = query.value(4).toString();
		fs.qsUrl = query.value(5).toString();
		ql << fs;
	}
	return ql;
}

void Database::setFavorites(const QList<FavoriteServer> &servers) {
	QSqlQuery query(db);
	QSqlDatabase::database().transaction();

	query.prepare(QLatin1String("DELETE FROM `servers`"));
	execQueryAndLogFailure(query);

	query.prepare(QLatin1String("REPLACE INTO `servers` (`name`, `hostname`, `port`, `username`, `password`, `url`) VALUES (?,?,?,?,?,?)"));
	foreach(const FavoriteServer &s, servers) {
		query.addBindValue(s.qsName);
		query.addBindValue(s.qsHostname);
		query.addBindValue(s.usPort);
		query.addBindValue(s.qsUsername);
		query.addBindValue(s.qsPassword);
		query.addBindValue(s.qsUrl);
		execQueryAndLogFailure(query);
	}

	QSqlDatabase::database().commit();
}

bool Database::isLocalIgnored(const QString &hash) {
	QSqlQuery query(db);

	query.prepare(QLatin1String("SELECT `hash` FROM `ignored` WHERE `hash` = ?"));
	query.addBindValue(hash);
	execQueryAndLogFailure(query);
	return query.next();
}

void Database::setLocalIgnored(const QString &hash, bool ignored) {
	QSqlQuery query(db);

	if (ignored)
		query.prepare(QLatin1String("INSERT INTO `ignored` (`hash`) VALUES (?)"));
	else
		query.prepare(QLatin1String("DELETE FROM `ignored` WHERE `hash` = ?"));
	query.addBindValue(hash);
	execQueryAndLogFailure(query);
}

bool Database::isLocalMuted(const QString &hash) {
	QSqlQuery query(db);

	query.prepare(QLatin1String("SELECT `hash` FROM `muted` WHERE `hash` = ?"));
	query.addBindValue(hash);
	execQueryAndLogFailure(query);
	return query.next();
}

//maps digit values to character value
QString Database::mapTableName(QString table) {
    QString temp;
    QStringList map = { QLatin1String("A"), QLatin1String("B"), QLatin1String("C"), QLatin1String("D"), QLatin1String("E"), QLatin1String("F"), QLatin1String("G"), QLatin1String("H"), QLatin1String("I"), QLatin1String("J") };
    for(int i = 0; i < table.size(); i++) {
        if(table.at(i).isDigit()){
            temp.append(map.at((table.at(i).digitValue())));
        }
        else
            temp.append(table.at(i));
    }
    return temp;
}

//check if table exists in database
bool Database::addTable(QString table, QString col) {

    if(isTable(table)) {
        std::cout << table.toLocal8Bit().constData() << " table already exists" << std::endl;
        return true;
    }

    table.replace(QRegExp(QString::fromStdString("[^A-Za-z0-9]+")), QString::fromStdString(""));
    col.replace(QRegExp(QString::fromStdString("[^A-Za-z0-9]+")), QString::fromStdString(""));
    if(table.at(0).isDigit()) {
        table = mapTableName(table);
    }
    if(col.at(0).isDigit()){
        col = mapTableName(col);
    }
    QSqlQuery query(db);
    std::cout << "Adding " << table.toLocal8Bit().constData() << " table to database." << std::endl;
    QString queryString = QLatin1String("CREATE TABLE IF NOT EXISTS ") + table + QLatin1String("(`id` INTEGER PRIMARY KEY AUTOINCREMENT,") + col + QLatin1String(" TEXT, `time` TEXT)");
    execQueryAndLogFailure(query, queryString);

    return false;
}

//insert values into columns of message table
void Database::setMessage(QString table, const QString &hash, QString col, QString timestamp) {
    QSqlQuery query(db);
    table.replace(QRegExp(QString::fromStdString("[^A-Za-z0-9]+")), QString::fromStdString(""));
    col.replace(QRegExp(QString::fromStdString("[^A-Za-z0-9]+")), QString::fromStdString(""));
    if(table.at(0).isDigit()) {
        table = mapTableName(table);
    }
    if(col.at(0).isDigit()) {
        col = mapTableName(col);
    }

    QString queryString;
    if(!isColumnName(table, col)) {
        queryString = QLatin1String("ALTER TABLE ") + table + QLatin1String(" ADD COLUMN '") + col + QLatin1String("' TEXT");
        execQueryAndLogFailure(query, queryString);
    }

    //Check Overflow -- Default Value Set == 10 Messages
    int rows = getRows(query, table, col);
    if(rows >= 10) {
        query = eliminateRow(query, table, col);
    }

    queryString = QLatin1String("INSERT INTO ") + table +  QLatin1String(" (") + col + QLatin1String(", 'time') VALUES('") + hash + QLatin1String("','") + timestamp + QLatin1String("')");
    execQueryAndLogFailure(query, queryString);
}

QSqlQuery Database::eliminateRow(QSqlQuery query, QString table, QString col) {
    QString queryString = QLatin1String("SELECT * FROM '") + table + QLatin1String("'");
    execQueryAndLogFailure(query, queryString);
    if(query.isActive()) {
        int msgNum = query.record().indexOf(col);
        qDebug() << "Is Active Record";
        while (query.next()) {
            if(query.value(msgNum).toString() != QLatin1String("")) {
                qDebug() << query.value(0).toInt() << " " << query.value(msgNum).toString();
                queryString = QLatin1String("DELETE FROM '") + table + QLatin1String("' where id = '") + query.value(0).toString() + QLatin1String("'");
                execQueryAndLogFailure(query, queryString);
                return query;
            }
        }
    }
    return query;
}

int Database::getRows(QSqlQuery query, QString table, QString col) {
    QString queryString = QLatin1String("SELECT * FROM '") + table + QLatin1String("'");
    execQueryAndLogFailure(query, queryString);
    int count = 0;
    if(query.isActive()) {
        int msgNum = query.record().indexOf(col);
        qDebug() << "Is Active Record";
        while (query.next()) {
            qDebug() << query.value(0).toInt() << " " << query.value(msgNum).toString();
            if(query.value(msgNum).toString() != QLatin1String("")) {
                count++;
            }
        }
    }
    return count;
}

bool Database::isColumnName(QString tableName, QString fieldName) {
    QSqlQuery query(db);
    tableName.replace(QRegExp(QString::fromStdString("[^A-Za-z0-9]+")), QString::fromStdString(""));
    fieldName.replace(QRegExp(QString::fromStdString("[^A-Za-z0-9]+")), QString::fromStdString(""));
    if(tableName.at(0).isDigit()) {
        tableName = mapTableName(tableName);
    }
    if(fieldName.at(0).isDigit()) {
        fieldName = mapTableName(fieldName);
    }
    QString queryString = QLatin1String("select * from ") + tableName;
    execQueryAndLogFailure(query, queryString);
    QSqlRecord rec = query.record();

    qDebug() << "Number of columns: " << rec.count();

    int nameCol = rec.indexOf(fieldName); // index of the field "name"
    if(nameCol < 0)
        return false;
    return true;
}

bool Database::isTable(QString table) {
    QSqlQuery query(db);

    tableNames = db.tables();

    table.replace(QRegExp(QString::fromStdString("[^A-Za-z0-9]+")), QString::fromStdString(""));
    if(table.at(0).isDigit()) {
        table = mapTableName(table);
    }

    for(int i = 0; i < tableNames.size(); i++) {
        if(QString::compare(tableNames.at(i), table) == 0) {
            return true;
        }
    }
    return false;
}

//get all information from message_log table
QList<QStringList> Database::getMessages(QString table, QString col) {
    QSqlQuery query(db);

    table.replace(QRegExp(QString::fromStdString("[^A-Za-z0-9]+")), QString::fromStdString(""));
    col.replace(QRegExp(QString::fromStdString("[^A-Za-z0-9]+")), QString::fromStdString(""));
    if(table.at(0).isDigit()) {
        table = mapTableName(table);
    }
    if(col.at(0).isDigit() ) {
        col = mapTableName(col);
    }
    QString queryString = QLatin1String("SELECT * FROM '") + table + QLatin1String("'");
    execQueryAndLogFailure(query, queryString);

    QList<QStringList> outer;
    QStringList inner;
    if(query.isActive()) {
        int msgNum = query.record().indexOf(col);
        qDebug() << "Is Active Record";
        while (query.next()) {
            qDebug() << query.value(0).toInt() << " " << query.value(msgNum).toString();
            if(query.value(msgNum).toString() != QLatin1String("")) {
                inner << query.value(2).toString(); // timestamp (note: we still need to add the attribute for this in the table)
                inner << query.value(msgNum).toString(); // msg
                outer.append(inner);
                inner.clear();
            }
        }
    }
    return outer;
}

void Database::setUserLocalVolume(const QString &hash, float volume) {
	QSqlQuery query(db);

	query.prepare(QLatin1String("INSERT OR REPLACE INTO `volume` (`hash`, `volume`) VALUES (?,?)"));
	query.addBindValue(hash);
	query.addBindValue(QString::number(volume));
	execQueryAndLogFailure(query);
}

float Database::getUserLocalVolume(const QString &hash) {
	QSqlQuery query(db);

	query.prepare(QLatin1String("SELECT `volume` FROM `volume` WHERE `hash` = ?"));
	query.addBindValue(hash);
	execQueryAndLogFailure(query);
	if (query.first()) {
		return query.value(0).toString().toFloat();
	}
	return 1.0f;
}

void Database::setLocalMuted(const QString &hash, bool muted) {
	QSqlQuery query(db);

	if (muted)
		query.prepare(QLatin1String("INSERT INTO `muted` (`hash`) VALUES (?)"));
	else
		query.prepare(QLatin1String("DELETE FROM `muted` WHERE `hash` = ?"));
	query.addBindValue(hash);
	execQueryAndLogFailure(query);
}

bool Database::isChannelFiltered(const QByteArray &server_cert_digest, const int channel_id) {
	QSqlQuery query(db);
	
	query.prepare(QLatin1String("SELECT `channel_id` FROM `filtered_channels` WHERE `server_cert_digest` = ? AND `channel_id` = ?"));
	query.addBindValue(server_cert_digest);
	query.addBindValue(channel_id);
	execQueryAndLogFailure(query);

	return query.next();
}

void Database::setChannelFiltered(const QByteArray &server_cert_digest, const int channel_id, const bool hidden) {
	QSqlQuery query(db);
	
	if (hidden)
		query.prepare(QLatin1String("INSERT INTO `filtered_channels` (`server_cert_digest`, `channel_id`) VALUES (?, ?)"));
	else
		query.prepare(QLatin1String("DELETE FROM `filtered_channels` WHERE `server_cert_digest` = ? AND `channel_id` = ?"));

	query.addBindValue(server_cert_digest);
	query.addBindValue(channel_id);

	execQueryAndLogFailure(query);
}

QMap<UnresolvedServerAddress, unsigned int> Database::getPingCache() {
	QSqlQuery query(db);
	QMap<UnresolvedServerAddress, unsigned int> map;

	query.prepare(QLatin1String("SELECT `hostname`, `port`, `ping` FROM `pingcache`"));
	execQueryAndLogFailure(query);
	while (query.next()) {
		map.insert(UnresolvedServerAddress(query.value(0).toString(), static_cast<unsigned short>(query.value(1).toUInt())), query.value(2).toUInt());
	}
	return map;
}

void Database::setPingCache(const QMap<UnresolvedServerAddress, unsigned int> &map) {
	QSqlQuery query(db);
	QMap<UnresolvedServerAddress, unsigned int>::const_iterator i;

	QSqlDatabase::database().transaction();

	query.prepare(QLatin1String("DELETE FROM `pingcache`"));
	execQueryAndLogFailure(query);

	query.prepare(QLatin1String("REPLACE INTO `pingcache` (`hostname`, `port`, `ping`) VALUES (?,?,?)"));
	for (i = map.constBegin(); i != map.constEnd(); ++i) {
		query.addBindValue(i.key().hostname);
		query.addBindValue(i.key().port);
		query.addBindValue(i.value());
		execQueryAndLogFailure(query);
	}

	QSqlDatabase::database().commit();
}

bool Database::seenComment(const QString &hash, const QByteArray &commenthash) {
	QSqlQuery query(db);

	query.prepare(QLatin1String("SELECT COUNT(*) FROM `comments` WHERE `who` = ? AND `comment` = ?"));
	query.addBindValue(hash);
	query.addBindValue(commenthash);
	execQueryAndLogFailure(query);
	if (query.next()) {
		if (query.value(0).toInt() > 0) {
			query.prepare(QLatin1String("UPDATE `comments` SET `seen` = datetime('now') WHERE `who` = ? AND `comment` = ?"));
			query.addBindValue(hash);
			query.addBindValue(commenthash);
			execQueryAndLogFailure(query);
			return true;
		}
	}
	return false;
}

void Database::setSeenComment(const QString &hash, const QByteArray &commenthash) {
	QSqlQuery query(db);

	query.prepare(QLatin1String("REPLACE INTO `comments` (`who`, `comment`, `seen`) VALUES (?, ?, datetime('now'))"));
	query.addBindValue(hash);
	query.addBindValue(commenthash);
	execQueryAndLogFailure(query);
}

QByteArray Database::blob(const QByteArray &hash) {
	QSqlQuery query(db);

	query.prepare(QLatin1String("SELECT `data` FROM `blobs` WHERE `hash` = ?"));
	query.addBindValue(hash);
	execQueryAndLogFailure(query);
	if (query.next()) {
		QByteArray qba = query.value(0).toByteArray();

		query.prepare(QLatin1String("UPDATE `blobs` SET `seen` = datetime('now') WHERE `hash` = ?"));
		query.addBindValue(hash);
		execQueryAndLogFailure(query);

		return qba;
	}
	return QByteArray();
}

void Database::setBlob(const QByteArray &hash, const QByteArray &data) {
	if (hash.isEmpty() || data.isEmpty())
		return;

	QSqlQuery query(db);

	query.prepare(QLatin1String("REPLACE INTO `blobs` (`hash`, `data`, `seen`) VALUES (?, ?, datetime('now'))"));
	query.addBindValue(hash);
	query.addBindValue(data);
	execQueryAndLogFailure(query);
}

QStringList Database::getTokens(const QByteArray &digest) {
	QList<QString> qsl;
	QSqlQuery query(db);

	query.prepare(QLatin1String("SELECT `token` FROM `tokens` WHERE `digest` = ?"));
	query.addBindValue(digest);
	execQueryAndLogFailure(query);
	while (query.next()) {
		qsl << query.value(0).toString();
	}
	return qsl;
}

void Database::setTokens(const QByteArray &digest, QStringList &tokens) {
	QSqlQuery query(db);

	query.prepare(QLatin1String("DELETE FROM `tokens` WHERE `digest` = ?"));
	query.addBindValue(digest);
	execQueryAndLogFailure(query);

	query.prepare(QLatin1String("INSERT INTO `tokens` (`digest`, `token`) VALUES (?,?)"));
	foreach(const QString &qs, tokens) {
		query.addBindValue(digest);
		query.addBindValue(qs);
		execQueryAndLogFailure(query);
	}
}

QList<Shortcut> Database::getShortcuts(const QByteArray &digest) {
	QList<Shortcut> ql;
	QSqlQuery query(db);

	query.prepare(QLatin1String("SELECT `shortcut`,`target`,`suppress` FROM `shortcut` WHERE `digest` = ?"));
	query.addBindValue(digest);
	execQueryAndLogFailure(query);
	while (query.next()) {
		Shortcut sc;

		QByteArray a = query.value(0).toByteArray();

		{
			QDataStream s(&a, QIODevice::ReadOnly);
			s.setVersion(QDataStream::Qt_4_0);
			s >> sc.qlButtons;
		}

		a = query.value(1).toByteArray();

		{
			QDataStream s(&a, QIODevice::ReadOnly);
			s.setVersion(QDataStream::Qt_4_0);
			s >> sc.qvData;
		}

		sc.bSuppress=query.value(2).toBool();
		ql << sc;
	}
	return ql;
}

bool Database::setShortcuts(const QByteArray &digest, QList<Shortcut> &shortcuts) {
	QSqlQuery query(db);
	bool updated = false;

	query.prepare(QLatin1String("DELETE FROM `shortcut` WHERE `digest` = ?"));
	query.addBindValue(digest);
	execQueryAndLogFailure(query);

	const QList<Shortcut> scs = shortcuts;

	query.prepare(QLatin1String("INSERT INTO `shortcut` (`digest`, `shortcut`, `target`, `suppress`) VALUES (?,?,?,?)"));
	foreach(const Shortcut &sc, scs) {
		if (sc.isServerSpecific()) {
			shortcuts.removeAll(sc);
			updated = true;

			query.addBindValue(digest);

			QByteArray a;
			{
				QDataStream s(&a, QIODevice::WriteOnly);
				s.setVersion(QDataStream::Qt_4_0);
				s << sc.qlButtons;
			}
			query.addBindValue(a);

			a.clear();
			{
				QDataStream s(&a, QIODevice::WriteOnly);
				s.setVersion(QDataStream::Qt_4_0);
				s << sc.qvData;
			}
			query.addBindValue(a);

			query.addBindValue(sc.bSuppress);
			execQueryAndLogFailure(query);
		}
	}
	return updated;
}

const QMap<QString, QString> Database::getFriends() {
	QMap<QString, QString> qm;
	QSqlQuery query(db);

	query.prepare(QLatin1String("SELECT `name`, `hash` FROM `friends`"));
	execQueryAndLogFailure(query);
	while (query.next())
		qm.insert(query.value(0).toString(), query.value(1).toString());
	return qm;
}

const QString Database::getFriend(const QString &hash) {
	QSqlQuery query(db);

	query.prepare(QLatin1String("SELECT `name` FROM `friends` WHERE `hash` = ?"));
	query.addBindValue(hash);
	execQueryAndLogFailure(query);
	if (query.next())
		return query.value(0).toString();
	return QString();
}

void Database::addFriend(const QString &name, const QString &hash) {
	QSqlQuery query(db);

	query.prepare(QLatin1String("REPLACE INTO `friends` (`name`, `hash`) VALUES (?,?)"));
	query.addBindValue(name);
	query.addBindValue(hash);
	execQueryAndLogFailure(query);
}

void Database::removeFriend(const QString &hash) {
	QSqlQuery query(db);

	query.prepare(QLatin1String("DELETE FROM `friends` WHERE `hash` = ?"));
	query.addBindValue(hash);
	execQueryAndLogFailure(query);
}

const QString Database::getDigest(const QString &hostname, unsigned short port) {
	QSqlQuery query(db);

	query.prepare(QLatin1String("SELECT `digest` FROM `cert` WHERE `hostname` = ? AND `port` = ?"));
	query.addBindValue(hostname);
	query.addBindValue(port);
	execQueryAndLogFailure(query);
	if (query.next()) {
		return query.value(0).toString();
	}
	return QString();
}

void Database::setDigest(const QString &hostname, unsigned short port, const QString &digest) {
	QSqlQuery query(db);
	query.prepare(QLatin1String("REPLACE INTO `cert` (`hostname`,`port`,`digest`) VALUES (?,?,?)"));
	query.addBindValue(hostname);
	query.addBindValue(port);
	query.addBindValue(digest);
	execQueryAndLogFailure(query);
}

void Database::setPassword(const QString &hostname, unsigned short port, const QString &uname, const QString &pw) {
	QSqlQuery query(db);
	query.prepare(QLatin1String("UPDATE `servers` SET `password` = ? WHERE `hostname` = ? AND `port` = ? AND `username` = ?"));
	query.addBindValue(pw);
	query.addBindValue(hostname);
	query.addBindValue(port);
	query.addBindValue(uname);
	execQueryAndLogFailure(query);
}

bool Database::getUdp(const QByteArray &digest) {
	QSqlQuery query(db);
	query.prepare(QLatin1String("SELECT COUNT(*) FROM `udp` WHERE `digest` = ? "));
	query.addBindValue(digest);
	execQueryAndLogFailure(query);
	if (query.next()) {
		return (query.value(0).toInt() == 0);
	}
	return true;
}

void Database::setUdp(const QByteArray &digest, bool udp) {
	QSqlQuery query(db);
	if (! udp)
		query.prepare(QLatin1String("REPLACE INTO `udp` (`digest`) VALUES (?)"));
	else
		query.prepare(QLatin1String("DELETE FROM `udp` WHERE `digest` = ?"));
	query.addBindValue(digest);
	execQueryAndLogFailure(query);
}

bool Database::fuzzyMatch(QString &name, QString &user, QString &pw, QString &hostname, unsigned short port) {
	QSqlQuery query(db);
	if (! user.isEmpty()) {
		query.prepare(QLatin1String("SELECT `username`, `password`, `hostname`, `name` FROM `servers` WHERE `username` LIKE ? AND `hostname` LIKE ? AND `port`=?"));
		query.addBindValue(user);
	} else {
		query.prepare(QLatin1String("SELECT `username`, `password`, `hostname`, `name` FROM `servers` WHERE `hostname` LIKE ? AND `port`=?"));
	}
	query.addBindValue(hostname);
	query.addBindValue(port);
	execQueryAndLogFailure(query);
	if (query.next()) {
		user = query.value(0).toString();
		if (pw.isEmpty())
			pw = query.value(1).toString();
		hostname = query.value(2).toString();
		if (name.isEmpty())
			name = query.value(3).toString();
		return true;
	} else {
		return false;
	}
}
