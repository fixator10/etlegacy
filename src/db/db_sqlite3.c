/*
 * Wolfenstein: Enemy Territory GPL Source Code
 * Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.
 *
 * ET: Legacy
 * Copyright (C) 2012-2018 ET:Legacy team <mail@etlegacy.com>
 *
 * This file is part of ET: Legacy - http://www.etlegacy.com
 *
 * ET: Legacy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ET: Legacy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ET: Legacy. If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, Wolfenstein: Enemy Territory GPL Source Code is also
 * subject to certain additional terms. You should have received a copy
 * of these additional terms immediately following the terms and conditions
 * of the GNU General Public License which accompanied the source code.
 * If not, please request a copy in writing from id Software at the address below.
 *
 * id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
 */
/**
 * @file db_sqlite3.c
 * @brief ET: Legacy SQL interface
 *
 * Tutorial: http://zetcode.com/db/sqlitec/
 */

#include "db_sql.h"

cvar_t *db_mode;
cvar_t *db_uri;

sqlite3  *db;
qboolean isDBActive;


// Important Note
// Always create optional feature tables see f.e. rating tables otherwise we can't ensure db integrity for updates

// version 1
//
// ....
//
//

// version 2
//
// table client_servers
//   profile - profilepath
//   source  - favorite source
//   address - IP + port
//   name    - server name
//   mod     - server mod
//   updated (last visit)
//   created
//
// table etl_whitelist
//   key
//   filename
//   type (map or mod)
//   created
//

const char *sql_Version_Statements[SQL_DBMS_SCHEMA_VERSION] =
{
		//version 1
		"CREATE TABLE IF NOT EXISTS etl_version (Id INT PRIMARY KEY NOT NULL, name TEXT, sql TEXT, created TEXT);"  // both
		"CREATE TABLE IF NOT EXISTS rating_users (guid TEXT PRIMARY KEY NOT NULL, mu REAL, sigma REAL, created TEXT, updated TEXT, UNIQUE (guid));"     // server table
		"CREATE TABLE IF NOT EXISTS rating_match (guid TEXT PRIMARY KEY NOT NULL, mu REAL, sigma REAL, time_axis INT, time_allies INT, UNIQUE (guid));" // server table
		"CREATE TABLE IF NOT EXISTS rating_maps (mapname TEXT PRIMARY KEY NOT NULL, win_axis INT, win_allies INT, UNIQUE (mapname));",                  // server table
		// version 2
		"CREATE TABLE IF NOT EXISTS client_servers (profile TEXT NOT NULL, source INT NOT NULL, address TEXT NOT NULL, name TEXT NOT NULL, mod TEXT NOT NULL, updated DATETIME, created DATETIME);"
		"CREATE INDEX client_servers_profile_idx ON client_servers(profile);"
		"CREATE INDEX client_servers_address_idx ON client_servers(address);" // client table

		"CREATE TABLE IF NOT EXISTS etl_whitelist (key TEXT PRIMARY KEY NOT NULL, filename TEXT NOT NULL, updated DATETIME , created DATETIME NOT NULL);"
		"CREATE INDEX etl_whitelist_idx ON etl_whitelist(filename);" // client table (no use for server atm)
};



/*
  	  	// version 3?!
		// ban/mute table (ensure we can also do IP range ban entries)
		// type = mute/ban
		// af = AddressFamily
		sql = "DROP TABLE IF EXISTS ban;"
		      "CREATE TABLE ban (Id INT PRIMARY KEY NOT NULL, address TEXT, guid TEXT, type INT NOT NULL, reason TEXT, af INT, length TEXT, expires TEXT, created TEXT, updated TEXT);"
		      "CREATE INDEX ban_address_idx ON ban(address);"
		      "CREATE INDEX ban_guid_idx ON ban(guid);";
*/

/**
 * @brief DB_Init
 *
 * @return
 */
int DB_Init()
{
	char *to_ospath;
	int  msec;

	msec = Sys_Milliseconds();

	isDBActive = qfalse;

	db_mode = Cvar_Get("db_mode", "2", CVAR_ARCHIVE | CVAR_LATCH);
	db_uri  = Cvar_Get("db_uri", "etl.db", CVAR_ARCHIVE | CVAR_LATCH); // .db extension is must have!

	if (db_mode->integer < 1 || db_mode->integer > 2)
	{
		Com_Printf("... DBMS is disabled\n");
		return 0; // return 0! - see isDBActive
	}

	Com_Printf("SQLite3 libversion %s - database URI '%s' - %s\n", sqlite3_libversion(), db_uri->string, db_mode->integer == 1 ? "in-memory" : "in file");

	if (!db_uri->string[0])
	{
		Com_Printf("... can't init database - empty URI\n");
		return 1;
	}

	if (!COM_CompareExtension(db_uri->string, ".db"))
	{
		Com_Printf("... can't init database - invalid filename extension\n");
		return 1;
	}

	to_ospath                        = FS_BuildOSPath(Cvar_VariableString("fs_homepath"), db_uri->string, "");
	to_ospath[strlen(to_ospath) - 1] = '\0';

	if (FS_SV_FileExists(db_uri->string))
	{
		int result;

		Com_Printf("... loading existing database '%s'\n", to_ospath);

		if (db_mode->integer == 1)
		{
			// init memory table
			result = sqlite3_open_v2("file::memory:?mode=memory&cache=shared", &db, (SQLITE_OPEN_READWRITE | SQLITE_OPEN_MEMORY | SQLITE_OPEN_SHAREDCACHE), NULL);

			if (result != SQLITE_OK)
			{
				Com_Printf("... failed to open memory database - error: %s\n", sqlite3_errstr(result));
				(void) sqlite3_close(db);
				return 1;
			}

			result = sqlite3_enable_shared_cache(1);

			if (result != SQLITE_OK)
			{
				Com_Printf("... failed to share memory database - error: %s\n", sqlite3_errstr(result));
				(void) sqlite3_close(db);
				return 1;
			}
			else
			{
				Com_Printf("... shared cache enabled\n");
			}

			// load from disk into memory
			result = DB_LoadOrSaveDb(db, to_ospath, 0);

			if (result != SQLITE_OK)
			{
				Com_Printf("... WARNING can't load database file %s\n", db_uri->string);
				return 1;
			}
		}
		else if (db_mode->integer == 2)
		{
			result = sqlite3_open_v2(to_ospath, &db, (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE), NULL);

			if (result != SQLITE_OK)
			{
				Com_Printf("... failed to open file database - error: %s\n", sqlite3_errstr(result));
				(void) sqlite3_close(db);
				return 1;
			}
		}
		else
		{
			Com_Printf("... failed to open database - unknown mode\n");
			return 1;
		}

		Com_Printf("... database file '%s' loaded\n", to_ospath);
	}
	else // create new
	{
		int result;

		Com_Printf("... no database file '%s' found ... creating now\n", to_ospath);
		result = DB_Create();

		if (result != 0)
		{
			(void) sqlite3_close(db);
			Com_Printf("... WARNING can't create database [%i]\n", result);
			return 1;
		}
	}

	isDBActive = qtrue;

	Com_Printf("SQLite3 ETL: DB init #%i%s in [%i] ms - autocommit %i\n", SQL_DBMS_SCHEMA_VERSION, to_ospath, (Sys_Milliseconds() - msec), sqlite3_get_autocommit(db));

	if (!DB_CheckUpdates())
	{
		Com_Printf("SQLite3 update failed.\n");
		//DB_Close(); ?
		//error drop ?
		// db is still active
	}

	// save memory db to disk
	if (db_mode->integer == 1)
	{
		int result;

		result = DB_SaveMemDB();

		if (result != SQLITE_OK)
		{
			Com_Printf("... WARNING can't save memory database file [%i]\n", result);
			return 1;
		}
	}

	return 0;
}

/**
 * @brief creates tables and populates our scheme.
 * @param[in]
 *
 * @return
 *
 * @note We are using the same db for client and server so listen servers are happy
 */
static int DB_CreateOrUpdateSchema(int startSchemaVersion)
{
	int  result, i;
	char *err_msg = 0;
	char *sql;

	for (i = startSchemaVersion; i < SQL_DBMS_SCHEMA_VERSION; i++)
	{
		result = sqlite3_exec(db, sql_Version_Statements[i], 0, 0, &err_msg);

		if (result != SQLITE_OK)
		{
			Com_Printf("SQLite3 failed to create schema version %i: %s\n", i + 1, err_msg);
			sqlite3_free(err_msg);
			return 1;
		}

		if (i + 1 == SQL_DBMS_SCHEMA_VERSION)
		{
			sql = va("INSERT INTO etl_version VALUES (%i, 'ET: L DBMS schema V%i for %s', '%s', CURRENT_TIMESTAMP);", i + 1, i + 1, ET_VERSION, sql_Version_Statements[i]);
		}
		else
		{
			sql = va("INSERT INTO etl_version VALUES (%i, 'ET: L DBMS schema V%i', '%s', CURRENT_TIMESTAMP);", i + 1 , i + 1, sql_Version_Statements[i]);
		}

		result = sqlite3_exec(db, sql, 0, 0, &err_msg);

		if (result != SQLITE_OK)
		{
			Com_Printf("SQLite3 failed to write ETL version %i: %s\n", i + 1, err_msg);
			sqlite3_free(err_msg);
			return 1;
		}

		Com_Printf("... DB schema version: %i created\n", i + 1);
	}

	return 0;
}

/**
 * @brief DB_CallbackVersion
 */
int DB_CallbackVersion(void *NotUsed, int argc, char **argv,
                    char **azColName)
{
    NotUsed = 0;

    for (int i = 0; i < argc; i++)
    {

        Com_Printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }

    return 0;
}

/**
 * @brief DB_CheckUpdates
 *
 * @return
 */
int DB_CheckUpdates()
{
	int version = 0;
	char         *sql;
	int          result;
	sqlite3_stmt *res;

	// read version from version table
	sql    = va("SELECT id from etl_version ORDER BY Id DESC LIMIT 1;");
	result = sqlite3_prepare_v2(db, sql, -1, &res, 0);

	if (result == SQLITE_OK)
	{
		result = sqlite3_step(res);

		if (result == SQLITE_ROW)
		{
			version = sqlite3_column_int(res, 0);
		}
	}

	if (version == SQL_DBMS_SCHEMA_VERSION) // we are done
	{
		Com_Printf("SQLite3 ETL: DB schema version #%i is up to date!\n", version);
		sqlite3_finalize(res);
		return 1;
	}
	else if (version < SQL_DBMS_SCHEMA_VERSION)
	{
		Com_Printf("SQLite3 ETL: Old DB schema #%i detected - performing update ...\n", version);

		// FIXME: do a copy before ...

		if (!DB_CreateOrUpdateSchema(version))
		{
			return 0;
		}
		
		Com_Printf("SQLite3 ETL: Old DB schema has been updated to version #%i...\n", SQL_DBMS_SCHEMA_VERSION);
		
		return 1;
	}

	if (version == 0)
	{
		Com_Printf("Warning: DB update can't find a valid schema! Game and database are not in sync!\n");
	}
	else 	// downgrade case ... we can't ensure a working system
	{
		Com_Printf("Warning: DB update has detected an unknown schema #%i! Game and database are not in sync!\n", version);
	}

	sqlite3_finalize(res);

	return 1;
}

/**
 * @brief DB_Create
 *
 * @return result code
 */
int DB_Create()
{
	int result;
	int msec;

	msec = Sys_Milliseconds();

	if (db_mode->integer == 1)
	{
		result = sqlite3_open_v2("file::memory:?cache=shared", &db, (SQLITE_OPEN_READWRITE | SQLITE_OPEN_MEMORY), NULL); // we may use SQLITE_OPEN_SHAREDCACHE see URI

		if (result != SQLITE_OK)
		{
			Com_Printf("... failed to create memory database - error: %s\n", sqlite3_errstr(result));
			return 1;
		}

		result = sqlite3_enable_shared_cache(1);

		if (result != SQLITE_OK)
		{
			Com_Printf("... failed to share memory database - error: %s\n", sqlite3_errstr(result));
			return 1;
		}
		else
		{
			Com_Printf("... shared cache enabled\n");
		}
	}
	else if (db_mode->integer == 2)
	{
		char *to_ospath;

		if (!db_uri->string[0])
		{
			Com_Printf("... can't create database - empty URI\n");
			return 1;
		}

		if (!COM_CompareExtension(db_uri->string, ".db"))
		{
			Com_Printf("... DB_Create failed - invalid filename extension\n");
			return 1;
		}

		// Make sure that we actually have the homepath available so we dont try to create a database file into a nonexisting path
		to_ospath = FS_BuildOSPath(Cvar_VariableString("fs_homepath"), "", "");
		if (FS_CreatePath(to_ospath))
		{
			Com_Printf("... DB_Create failed - can't create path\n");
			return 1;
		}
		
		to_ospath = FS_BuildOSPath(Cvar_VariableString("fs_homepath"), db_uri->string, "");

		to_ospath[strlen(to_ospath) - 1] = '\0';

		result = sqlite3_open_v2(to_ospath, &db, (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE), NULL);

		if (result != SQLITE_OK)
		{
			Com_Printf("... DB_Create failed - error %s\n", sqlite3_errstr(result));
			return 1;
		}
	}
	else
	{
		Com_Printf("... DB_Create failed - unknown mode\n");
		return 1;
	}

	result = DB_CreateOrUpdateSchema(0);

	if (result != 0)
	{
		Com_Printf("... DB_Create failed - can't create database schema\n");
		return 1;
	}

	Com_Printf("... database '%s' created in [%i] ms\n", db_uri->string, (Sys_Milliseconds() - msec));
	return 0;
}

/**
 * @brief saves memory db to disk
 *
 * @return result code
 */
int DB_SaveMemDB()
{
	if (db_mode->integer == 1)
	{
		int  result, msec;
		char *to_ospath;

		if (!db_uri->string[0])
		{
			Com_Printf("... can't save database - empty URI\n");
			return 1;
		}

		if (!COM_CompareExtension(db_uri->string, ".db"))
		{
			Com_Printf("... can't save database - invalid filename extension\n");
			return 1;
		}

		to_ospath                        = FS_BuildOSPath(Cvar_VariableString("fs_homepath"), db_uri->string, "");
		to_ospath[strlen(to_ospath) - 1] = '\0';

		msec = Sys_Milliseconds();

		result = DB_LoadOrSaveDb(db, to_ospath, 1);

		if (result != SQLITE_OK)
		{
			Com_Printf("... WARNING can't save memory database file [%i]\n", result);
			return 1;
		}
		Com_Printf("SQLite3 in-memory tables saved to disk @[%s] in [%i] ms\n", to_ospath, (Sys_Milliseconds() - msec));
	}
	else
	{
		Com_Printf("DB_SaveMemDB called for unknown database mode\n");
	}
	return 0;
}

/**
 * @brief Closes the database properly.
 *
 * @return
 */
int DB_Close()
{
	int result;

	if (!isDBActive)
	{
		Com_Printf("SQLite3 can't close database - not active.\n");
		return 1;
	}

	// save memory db to disk
	if (db_mode->integer == 1)
	{
		result = DB_SaveMemDB();

		if (result != SQLITE_OK)
		{
			Com_Printf("... WARNING can't save memory database file [%i]\n", result);
			// let's close the db ...
		}
	}

	result     = sqlite3_close(db);
	isDBActive = qfalse;

	if (result != SQLITE_OK)
	{
		Com_Printf("SQLite3 failed to close database.\n");
		return 1;
	}

	Com_Printf("SQLite3 database closed.\n");
	return 0;
}

/**
 * @brief Perform an online backup of our database db to the database file named
 * by zFilename. Used to store our memory table in between map changes to disk
 *
 * This function copies 5 database pages from pDb to
 * zFilename, then unlocks pDb and sleeps for 250 ms, then repeats the
 * process until the entire database is backed up.
 *
 * The third argument passed to this function must be a pointer to a progress
 * function. After each set of 5 pages is backed up, the progress function
 * is invoked with two integer parameters: the number of pages left to
 * copy, and the total number of pages in the source file. This information
 * may be used, for example, to update a GUI progress bar.
 *
 * While this function is running, another thread may use the database pDb, or
 * another process may access the underlying database file via a separate
 * connection.
 *
 * If the backup process is successfully completed, SQLITE_OK is returned.
 * Otherwise, if an error occurs, an SQLite error code is returned.
 *
 * @param[in] zFilename
 * @param xProgress Progress function to invoke
 *
 * @return sqlite result code
 *
 */
int DB_BackupDB(const char *zFilename, void (*xProgress)(int, int))
{
	int            rc;        // Function return code
	sqlite3        *pFile;    // Database connection opened on zFilename
	sqlite3_backup *pBackup;  // Backup handle used to copy data

	// Open the database file identified by zFilename.
	rc = sqlite3_open(zFilename, &pFile);
	// FIXME
	//rc = sqlite3_open_v2(zFilename, &pFile, (SQLITE_OPEN_READWRITE | "SQLITE_OPEN_CREATE"), NULL);

	if (rc == SQLITE_OK)
	{
		// Open the sqlite3_backup object used to accomplish the transfer
		pBackup = sqlite3_backup_init(pFile, "main", db, "main");
		if (pBackup)
		{
			// Each iteration of this loop copies 5 database pages from database
			// pDb to the backup database. If the return value of backup_step()
			// indicates that there are still further pages to copy, sleep for
			// 250 ms before repeating.
			do
			{
				rc = sqlite3_backup_step(pBackup, 5);
				xProgress(sqlite3_backup_remaining(pBackup), sqlite3_backup_pagecount(pBackup));
				if (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
				{
					sqlite3_sleep(250);
				}
			}
			while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);

			// Release resources allocated by backup_init().
			(void) sqlite3_backup_finish(pBackup);
		}
		rc = sqlite3_errcode(pFile);
	}

	// Close the database connection opened on database file zFilename and return the result of this function.
	(void) sqlite3_close(pFile);
	return rc;
}

/**
 * @brief This function is used to load the contents of a database file on disk
 * into the "main" database of open database connection pInMemory, or
 * to save the current contents of the database opened by pInMemory into
 * a database file on disk. pInMemory is probably an in-memory database,
 * but this function will also work fine if it is not.
 *
 * Parameter zFilename points to a nul-terminated string containing the
 * name of the database file on disk to load from or save to. If parameter
 * isSave is non-zero, then the contents of the file zFilename are
 * overwritten with the contents of the database opened by pInMemory. If
 * parameter isSave is zero, then the contents of the database opened by
 * pInMemory are replaced by data loaded from the file zFilename.
 *
 * If the operation is successful, SQLITE_OK is returned. Otherwise, if
 * an error occurs, an SQLite error code is returned.
 *
 * @param[in] pInMemory
 * @param[in] zFilename
 * @param[in] isSave
 *
 * @return result code
 */
int DB_LoadOrSaveDb(sqlite3 *pInMemory, const char *zFilename, int isSave)
{
	int            rc;      // Function return code
	sqlite3        *pFile;  // Database connection opened on zFilename
	sqlite3_backup *pBackup; // Backup object used to copy data
	sqlite3        *pTo;    // Database to copy to (pFile or pInMemory)
	sqlite3        *pFrom;  // Database to copy from (pFile or pInMemory)

	// Open the database file identified by zFilename. Exit early if this fails for any reason.
	rc = sqlite3_open(zFilename, &pFile);
	// FIXME
	//rc = sqlite3_open_v2(zFilename, &pFile, (SQLITE_OPEN_READWRITE | "SQLITE_OPEN_CREATE"), NULL);

	if (rc == SQLITE_OK)
	{
		// If this is a 'load' operation (isSave==0), then data is copied
		// from the database file just opened to database pInMemory.
		// Otherwise, if this is a 'save' operation (isSave==1), then data
		// is copied from pInMemory to pFile.  Set the variables pFrom and
		// pTo accordingly.
		pFrom = (isSave ? pInMemory : pFile);
		pTo   = (isSave ? pFile     : pInMemory);

		// Set up the backup procedure to copy from the "main" database of
		// connection pFile to the main database of connection pInMemory.
		// If something goes wrong, pBackup will be set to NULL and an error
		// code and  message left in connection pTo.
		//
		// If the backup object is successfully created, call backup_step()
		// to copy data from pFile to pInMemory. Then call backup_finish()
		// to release resources associated with the pBackup object.  If an
		// error occurred, then  an error code and message will be left in
		// connection pTo. If no error occurred, then the error code belonging
		// to pTo is set to SQLITE_OK.
		pBackup = sqlite3_backup_init(pTo, "main", pFrom, "main");
		if (pBackup)
		{
			(void) sqlite3_backup_step(pBackup, -1);
			(void) sqlite3_backup_finish(pBackup);
		}
		rc = sqlite3_errcode(pTo);
	}

	// Close the database connection opened on database file zFilename and return the result of this function.
	(void) sqlite3_close(pFile);
	return rc;
}

/**
 * @brief Simple callback function which can be used to show results printed on console
 *
 * @param NotUsed
 * @param[in] argc
 * @param[in] argv
 * @param azColName
 *
 * @return 0
 */
int DB_Callback(void *NotUsed, int argc, char **argv, char **azColName)
{
	int i;
	// NotUsed = 0;

	//for (i = 0; i < argc; i++)
	//{
	//	Com_Printf("%s ", azColName[i]);
	//}
	//Com_Printf("\n");

	Com_Printf("^2|");
	for (i = 0; i < argc; i++)
	{
		Com_Printf("^7%s^2|^7", argv[i] && argv[i][0] ? argv[i] : "NULL");
	}
	Com_Printf("\n");

	return 0;
}


/**
 * @brief Get the last inserted ROWID
 *
 * @see "SELECT last_insert_rowid()"
 *
 * @return If database is available, last insert row id. Otherwise -1
 */
int DB_LastInsertRowId()
{
	if (db)
	{
		return sqlite3_last_insert_rowid(db);
	}

	return -1;
}
