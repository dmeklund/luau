/*
 * luau (Lib Update/Auto-Update): Simple Update Library
 * Copyright (C) 2003  David Eklund
 *
 * - This library is free software; you can redistribute it and/or             -
 * - modify it under the terms of the GNU Lesser General Public                -
 * - License as published by the Free Software Foundation; either              -
 * - version 2.1 of the License, or (at your option) any later version.        -
 * -                                                                           -
 * - This library is distributed in the hope that it will be useful,           -
 * - but WITHOUT ANY WARRANTY; without even the implied warranty of            -
 * - MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU         -
 * - Lesser General Public License for more details.                           -
 * -                                                                           -
 * - You should have received a copy of the GNU Lesser General Public          -
 * - License along with this library; if not, write to the Free Software       -
 * - Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA -
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <db.h>
#include <glib.h>

#include "database.h"
#include "util.h"
#include "error.h"

#ifdef WITH_DMALLOC
#  include <dmalloc.h>
#endif

#if DB_VERSION_MAJOR == 3
#  define DB_OPEN(ptr, file, db, type, flags, mode) ptr->open(ptr, dbFile, subcategory, type, flags, mode);
   static void dbError_wrapper(const char *errpfx, char *msg);
#elif DB_VERSION_MAJOR >= 4
#  define DB_OPEN(ptr, file, db, type, flags, mode) ptr->open(ptr, NULL, dbFile, subcategory, type, flags, mode);
   static void dbError_wrapper(const DB_ENV *dbenv, const char *errpfx, const char *msg);
#else
#  error Unsupported version of libdb installed - please retrieve the latest from www.sleepycat.com
#endif

typedef struct {
	char *id;
	DB *ptr;
} ADatabaseHandle;

typedef enum { LDB_ENV_CREATE, LDB_ENV_OPEN, LDB_ENV_CLOSE,
               LDB_CREATE, LDB_OPEN, LDB_GET, LDB_PUT, LDB_DEL, LDB_REMOVE, LDB_CLOSE,
               LDB_CURSOR, LDBC_GET } LDBOperation;

G_LOCK_DEFINE_STATIC (database_array);

static gboolean keepOpen = FALSE;
static GPtrArray *dbPs = NULL;
static DB_ENV *dbEnv = NULL;

static void* queryDatabase(const char* category, const char* subcategory, const char* key, gboolean local);
static gboolean setValue(const char* category, const char* subcategory, const char* key, const void* value, size_t size, gboolean local);

static DB * getDatabaseP(const char* category, const char* subcategory, gboolean writeable, gboolean local);
static DB * openDatabase(const char* category, const char* subcategory, gboolean needWrite, gboolean local);
static DB * openDatabaseDir(const char* category, const char* subcategory, gboolean needWrite, const char *dir);

static u_int32_t getDBFlags(LDBOperation oper);

gboolean
luau_db_createEnvironment(void) {
	int ret;
	
	if (dbEnv != NULL) {
		ERROR("Database environment already created (and not closed)");
		return FALSE;
	}
	
	ret = db_env_create(&dbEnv, getDBFlags(LDB_ENV_CREATE));
	if (ret != 0) {
		ERROR("Couldn't create database environment: %s", db_strerror(ret));
		return FALSE;
	}
	
	ret = dbEnv->open(dbEnv, TEMP_DIR, DB_CREATE | getDBFlags(LDB_ENV_OPEN), 0);
	if (ret != 0) {
		ERROR("Couldn't open database environment: %s", db_strerror(ret));
		dbEnv->close(dbEnv, getDBFlags(LDB_ENV_CLOSE));
		dbEnv = NULL;
		return FALSE;
	}
	
	dbEnv->set_errcall(dbEnv, dbError_wrapper);
	
	return TRUE;
}

void
luau_db_closeEnvironment(void) {
	int ret;
	
	if (dbEnv == NULL) {
		ERROR("Database environment never opened");
		return;
	}
	
	ret = dbEnv->close(dbEnv, getDBFlags(LDB_ENV_CLOSE));
	if (ret != 0)
		ERROR("Couldn't close database environment: %s", db_strerror(ret));
	
	dbEnv = NULL;
}


/**
 * Query for the value associated with the given key.  Returns NULL if the query failed, or if
 * there's no such key in the database.
 *
 * @arg <i>category</i> is the main category where the key is stored (actually the database filename)
 * @arg <i>subcategory</i> is the sub category where the key is stored (actually the database name)
 * @arg <i>key</i> is the name of the key we want to find the data for (we only support strings as keys)
 * @return the associated data (\b must be <tt>free</tt>'d), or NULL otherwise
 */
void *
luau_db_queryDatabase(const char* category, const char* subcategory, const char* key) {
	void *data;
	
	data = queryDatabase(category, subcategory, key, TRUE); /* local db */
	if (data == NULL)
		data = queryDatabase(category, subcategory, key, FALSE); /* global db */
	
#ifdef DEBUG
	if (data == NULL)
		DBUGOUT("Result: NULL");
	else if (lutil_streq(subcategory, "all") || lutil_streq(category, "updates_hidden"))
		DBUGOUT("Result: %d", *((int*)data));
	else
		DBUGOUT("Result: %s", (char*)data);
#endif /* DEBUG */
	
	return data;
}

/**
 * Find if a key exists in the specified database.  Note that this will also return FALSE
 * if the database doesn't exist, or there is an error in opening it.
 *
 * @arg <i>category</i> is the main category where the key is stored (actually the database filename)
 * @arg <i>subcategory</i> is the sub category where the key is stored (actually the database name)
 * @arg <i>key</i> is the name of the key we want to see exists
 * @return TRUE if the key exists, FALSE otherwise
 */
gboolean
luau_db_keyExists(const char* category, const char* subcategory, const char* key) {
	void *data;
	gboolean result;
	
	data = luau_db_queryDatabase(category, subcategory, key);
	if (data == NULL)
		result = FALSE;
	else {
		g_free(data);
		result = TRUE;
	}
	
	return result;
}

gboolean
luau_db_create(const char* category, const char* subcategory) {
	DB *dbp;
	
	dbp = getDatabaseP(category, subcategory, TRUE, FALSE);
	if (dbp == NULL)
		dbp = getDatabaseP(category, subcategory, TRUE, TRUE);
	
	if (dbp == NULL)
		return FALSE;
	
	else {
		if (!keepOpen)
			dbp->close(dbp, getDBFlags(LDB_CLOSE));
		
		return TRUE;
	}
}


/**
 * Get all the keys in the specified database using libdb's 'cursor' interface.  Returns
 * NULL on error.
 *
 * @arg <i>category</i> is the main category where the key is stored (actually the database filename)
 * @arg <i>subcategory</i> is the sub category where the key is stored (actually the database name)
 * @return a GPtrArray of all the keys in specified database (must be <tt>free</tt>'d), or NULL on failure.
 */
GPtrArray *
luau_db_getAllDBKeys(const char* category, const char* subcategory) {
	int ret;
	DB *dbp;
	DBC *dbcp;
	DBT key, data;
	GPtrArray *keys = g_ptr_array_new();
	gboolean local;
	
	local = FALSE;
	while (1) {
		if ((dbp = getDatabaseP(category, subcategory, FALSE, local)) == NULL) {
			DBUGOUT("Couldn't open database %s.%s", category, subcategory);
		} else {
			if ((ret = dbp->cursor(dbp, NULL, &dbcp, getDBFlags(LDB_CURSOR))) != 0) {
				ERROR("Couldn't get cursor: %s", db_strerror(ret));
			} else {
				memset(&key, 0, sizeof(key));
				memset(&data, 0, sizeof(data));
				
				while ((ret = dbcp->c_get(dbcp, &key, &data, DB_NEXT | getDBFlags(LDBC_GET))) == 0)
					 g_ptr_array_add(keys, g_strdup((char*)key.data));
				
				if (ret != DB_NOTFOUND)
					ERROR("DBcursor->c_get error: %s", db_strerror(ret));
				
				dbcp->c_close(dbcp);
			}
			if (!keepOpen)
				dbp->close(dbp, getDBFlags(LDB_CLOSE));
		}
		
		if (local == FALSE)
			local = TRUE;
		else
			break;
	}
	
	return keys;
}

/**
 * Set the value of <tt>key</tt> to <tt>value</tt> in the <tt>category.subcategory</tt> database.
 * Overwrites previous value if one exists, or creates a new pair otherwise.  <tt>size</tt> must contain
 * the size (in bytes) of <tt>value</tt>.
 *
 * @arg <i>category</i> is the main category where the key is stored (actually the database filename)
 * @arg <i>subcategory</i> is the sub category where the key is stored (actually the database name)
 * @arg <i>key</i> is the name of the key we want to set/change
 * @arg <i>value</i> is the new value
 * @arg <i>size</i> is the size of the new value (in bytes)
 *
 * @see luau_db_setValueString
 * @see luau_db_setValueInt
 */
gboolean
luau_db_setValue(const char* category, const char* subcategory, const char* key, const void* value, size_t size) {
	gboolean result = TRUE;
	
	if (value == NULL)
		return TRUE;
	
	result = setValue(category, subcategory, key, value, size, FALSE);
	
	if (result == FALSE)
		result = setValue(category, subcategory, key, value, size, TRUE);
	
	return result;
}

/**
 * Front-end to luau_db_setValue for setting string values in the database.  Otherwise works the
 * same as luau_db_setValue.
 *
 * @arg <i>category</i> is the main category where the key is stored (actually the database filename)
 * @arg <i>subcategory</i> is the sub category where the key is stored (actually the database name)
 * @arg <i>key</i> is the name of the key we want to set/change
 * @arg <i>value</i> is the new value
 *
 * @see luau_db_setValue
 */
gboolean
luau_db_setValueString(const char* category, const char* subcategory, const char* key, const char* string) {
	DBUGOUT("Setting \"%s\" to \"%s\" in %s.%s", key, string, category, subcategory);
	if (string == NULL)
		return luau_db_setValue(category, subcategory, key, (const void*) NULL, 1);
	else
		return luau_db_setValue(category, subcategory, key, (const void*) string, sizeof(char) * (strlen(string) + 1));
}

/**
 * Front-end to luau_db_setValue for setting string values in the database.  Otherwise works the same
 * as luau_db_setValue.
 *
 * @arg <i>category</i> is the main category where the key is stored (actually the database filename)
 * @arg <i>subcategory</i> is the sub category where the key is stored (actually the database name)
 * @arg <i>key</i> is the name of the key we want to set/change
 * @arg <i>value</i> is the new value
 *
 * @see luau_db_setValue
 */
gboolean
luau_db_setValueInt(const char* category, const char* subcategory, const char* key, int value) {
	DBUGOUT("Setting \"%s\" to %d in %s.%s", key, value, category, subcategory);
	return luau_db_setValue(category, subcategory, key, (const void*) &value, sizeof(int));
}

gboolean
luau_db_deleteKey(const char* category, const char* subcategory, const char* key) {
	gboolean local, result;
	DB *dbp;
	DBT dkey;
	int ret;
	
	memset(&dkey, 0, sizeof(dkey));
	dkey.data = (void*) key;
	dkey.size = sizeof(char) * (strlen(key)+1);
	
	result = TRUE;
	local = FALSE;
	while (1) {
		dbp = getDatabaseP(category, subcategory, TRUE, local);
		ret = dbp->del(dbp, NULL, &dkey, getDBFlags(LDB_DEL));
		result = result && (ret == 0 || ret == DB_NOTFOUND);
		
		if (local == FALSE)
			local = TRUE;
		else
			break;
	}
	
	if (!result)
		ERROR("Couldn't delete key: %s", db_strerror(ret));
	
	if (!keepOpen)
		dbp->close(dbp, getDBFlags(LDB_CLOSE));
	
	return result;
}

gboolean
luau_db_clear(const char* category, const char* subcategory) {
	DB *dbp;
	int ret = 0;
	char *filename;
	
	filename = lutil_vstrcreate(DB_GLOBAL_DIR "/", category);
	
	dbp = getDatabaseP(category, subcategory, TRUE, FALSE);
	if (dbp != NULL) {
		ret = dbp->remove(dbp, filename, subcategory, 0);
		if (ret != 0)
			ERROR("Couldn't truncate database: %s", db_strerror(ret));
		
		if (!keepOpen)
			dbp->close(dbp, getDBFlags(LDB_CLOSE));
	}
	g_free(filename);
	
	filename = lutil_vstrcreate(DB_LOCAL_DIR "/", category);
	
	dbp = getDatabaseP(category, subcategory, TRUE, TRUE);
	if (dbp != NULL) {
		ret = dbp->remove(dbp, filename, subcategory, getDBFlags(LDB_REMOVE));
		if (ret != 0)
			ERROR("Couldn't truncate database: %s", db_strerror(ret));
		
		if (!keepOpen)
			dbp->close(dbp, getDBFlags(LDB_CLOSE));
	}
	
	g_free(filename);
	
	return (ret == 0);
}

void
luau_db_keepOpen(gboolean yesOrNo) {
	keepOpen = yesOrNo;
}

void
luau_db_closeAll(void) {
	unsigned int i;
	ADatabaseHandle *curr;
	
	if (dbPs == NULL)
		return;
	
	for (i = 0; i < dbPs->len; ++i) {
		curr = g_ptr_array_index(dbPs, i);
		if (curr != NULL) {
			DBUGOUT("Closing database with handle %s", curr->id);
			curr->ptr->close(curr->ptr, getDBFlags(LDB_CLOSE));
			g_free(curr->id);
			g_free(curr);
		}
	}
	
	g_ptr_array_free(dbPs, TRUE);
	
	dbPs = NULL;
}


/* Non-Interface Methods */

static void *
queryDatabase(const char* category, const char* subcategory, const char* key, gboolean local) {
	DB *dbp;
	DBT dkey, data;
	int ret;
	
	DBUGOUT("Querying %s:%s for key %s (local == %d)", category, subcategory, key, local);
	
	dbp = getDatabaseP(category, subcategory, FALSE, local);
	if (dbp == NULL)
		return NULL;
	
	memset(&dkey, 0, sizeof(dkey));
	memset(&data, 0, sizeof(data));

	dkey.data = (void*) key;
	dkey.size = sizeof(char) * (strlen(key) + 1);
	data.flags = DB_DBT_MALLOC;
	
	ret = dbp->get(dbp, NULL, &dkey, &data, getDBFlags(LDB_GET));
	
	if (!keepOpen)
		dbp->close(dbp, getDBFlags(LDB_CLOSE));
	
	if (ret != 0) {
		if (ret != DB_NOTFOUND)
			ERROR("Couldn't query database: %s", db_strerror(ret));
		else
			DBUGOUT("Key not found in this database");

		return NULL;
	} else {
		DBUGOUT("Successful");
		LB_REGISTER(data.data, data.size);
		return data.data;
	}
}

static gboolean
setValue(const char* category, const char* subcategory, const char* key, const void* value, size_t size, gboolean local) {
	DB *dbp;
	DBT dkey, data;
	int ret;
	gboolean result = TRUE;
	
	DBUGOUT("Attempting to set key %s in %s:%s (local == %d)", key, category, subcategory, local);
	
	dbp = getDatabaseP(category, subcategory, TRUE, local);
	if (dbp == NULL) {
		DBUGOUT("Couldn't get database pointer");
		return FALSE;
	}
	
	memset(&dkey, 0, sizeof(dkey));
	memset(&data, 0, sizeof(data));
	dkey.data = (void*) key;
	dkey.size = strlen(key)+1;
	data.data = (void*) value;
	data.size = size;
	
	ret = dbp->put(dbp, NULL, &dkey, &data, getDBFlags(LDB_PUT));
	if (ret != 0) {
		ERROR("Couldn't create key value pair (%s): %s", key, db_strerror(ret));
		result = FALSE;
	}
	
	if (!keepOpen)
		dbp->close(dbp, getDBFlags(LDB_CLOSE));
	
	DBUGOUT("Successful");
	
	return result;
}

static DB *
getDatabaseP(const char* category, const char* subcategory, gboolean writeable, gboolean local) {
	unsigned int i;
	DB *ptr = NULL;
	ADatabaseHandle *curr;
	char *id = NULL, *readonly_id;
	
	if (!keepOpen)
		return openDatabase(category, subcategory, writeable, local);
	
	if (subcategory == NULL) {
		if (local) {
			id = lutil_vstrcreate("*local*", id);
		} else {
			id = g_strdup(category);
		}
	} else {
		if (local) {
			id = lutil_vstrcreate("*local*", category, "*", subcategory, NULL);
		} else {
			id = lutil_vstrcreate("*", category, "*", subcategory, NULL);
		}
	}
	/* printf("ID ============= %s\n", id); */
	
	/* Databases opened read-only are identified with a _ro suffix */
	readonly_id = lutil_vstrcreate(id, "*ro", NULL);
	
	G_LOCK (database_array);
	
	if (dbPs == NULL)
		dbPs = g_ptr_array_new();
	
	G_UNLOCK (database_array);
	
	for (i = 0; i < dbPs->len; ++i) {
		curr = (ADatabaseHandle*) g_ptr_array_index(dbPs, i);

		if (lutil_streq(id, curr->id)) {
			/* regardless of whether we only needed write acess, we've found a read-write enabled
			 * open handle, which will work regardless */
			ptr = curr->ptr;
			break;
		} else if (lutil_streq(readonly_id, curr->id)) {
			/* found an open read-only pointer */
			if (writeable) {
				/* this won't work - we need write access - so we close this open handle and (later)
				 * open a new one */
				curr->ptr->close(curr->ptr, 0);
				g_free(curr->id);
				g_free(curr);
				
				G_LOCK (database_array);
				
				/* messes up the array order, but doesn't matter since we break from this for loop next */
				g_ptr_array_remove_index_fast(dbPs, i);
				
				G_UNLOCK (database_array);
				
				ptr = NULL;
			} else {
				/* only need read-only, so this is fine */
				ptr = curr->ptr;
			}
			
			break;
		}
	}
	
	if (ptr == NULL) {
		/* no appropriate handle found - open a new one and store it */
		
		ptr = openDatabase(category, subcategory, writeable, local);
		if (ptr != NULL) {
			curr = (ADatabaseHandle*) g_malloc(sizeof(ADatabaseHandle));
			curr->ptr = ptr;
			
			if (writeable) {
				DBUGOUT("Opening Database: handle name %s", id);
				curr->id = id;
				g_free(readonly_id);
			} else {
				/* if we're opening read-only, we store the read-only id (with the _ro tacked on) so we know */
				DBUGOUT("Opening Database: handle name %s", id);
				curr->id = readonly_id;
				g_free(id);
			}
			
			G_LOCK (database_array);
			g_ptr_array_add(dbPs, curr);
			G_UNLOCK (database_array);
		} else {
			g_free(id);
			g_free(readonly_id);
		}
	} else {
		g_free(id);
		g_free(readonly_id);
	}
	
	return ptr;
}


/**
 * \brief Open a database
 *
 * Open and return a DB pointer for the given database.  Returns NULL on error.
 * *
 * @arg <i>category</i> is the main category where the key is stored (actually the database filename)
 * @arg <i>subcategory</i> is the sub category where the key is stored (actually the database name)
 * @arg <i>needWrite</i> specifies whether we need write permissions
 * @return a DB pointer for the database (must be closed when finished), or NULL if operation fails
 */
static DB *
openDatabase(const char* category, const char* subcategory, gboolean needWrite, gboolean local) {
	DB *ptr;
	char *dir;
	
	if (local) {
		dir = lutil_vstrcreate(getenv("HOME"), "/" DB_LOCAL_DIR, NULL);
		ptr = openDatabaseDir(category, subcategory, needWrite, dir);
		g_free(dir);
	} else {
		ptr = openDatabaseDir(category, subcategory, needWrite, DB_GLOBAL_DIR);
	}
	
	return ptr;
}

static DB *
openDatabaseDir(const char* category, const char* subcategory, gboolean needWrite, const char *dir) {
	int ret, flags;
	char *dbFile;
	DB *ptr;
	
	dbFile = lutil_vstrcreate(dir, "/", category, NULL);
	flags = (needWrite ? DB_CREATE : DB_RDONLY);
	
	DBUGOUT("Opening database file %s:%s", dbFile, subcategory);
	
	ret = db_create(&ptr, dbEnv, getDBFlags(LDB_CREATE));
	if (ret != 0) {
		ERROR("Couldn't create db pointer: %s", db_strerror(ret));
		return NULL;
	}
	
	ret = DB_OPEN(ptr, dbFile, subcategory, DB_BTREE, flags | getDBFlags(LDB_OPEN), 0644);
	
	if (ret == ENOENT && needWrite == TRUE && !lutil_fileExists(dir)) {
		lutil_mkdir(dir, 0755);
		ret = DB_OPEN(ptr, dbFile, subcategory, DB_BTREE, flags | getDBFlags(LDB_OPEN), 0644);
	}
	
	g_free(dbFile);
	
	if (ret != 0) {
		DBUGOUT("Couldn't open %s:%s database: %s", category, subcategory, db_strerror(ret));
		ptr->close(ptr, getDBFlags(LDB_CLOSE));
		return NULL;
	}
	
	ptr->set_errcall(ptr, dbError_wrapper);
	
	return ptr;
}

static u_int32_t
getDBFlags(LDBOperation oper) {
	u_int32_t flags = 0;
	
	switch (oper) {
		case LDB_OPEN:
			if (dbEnv != NULL)
				flags = flags | DB_THREAD;
			break;
		case LDB_ENV_OPEN:
			flags = flags | DB_INIT_MPOOL;
			if (dbEnv != NULL)
				flags = flags | DB_INIT_LOCK | DB_THREAD;
			break;
		default:
			break;
	}

	return flags;
}

#if DB_VERSION_MAJOR == 3

static void
dbError_wrapper(const char *errpfx, char *msg) {
	ERROR("libdb: %s: %s", errpfx, msg);
}

#elif DB_VERSION_MAJOR >= 4

static void
dbError_wrapper(const DB_ENV *dbenv, const char *errpfx, const char *msg) {
	ERROR("libdb: %s: %s", errpfx, msg);
}

#endif
