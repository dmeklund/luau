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

/** @file database.h
 * \brief Handle locally-stored database operations
 *
 * Methods to access Berkeley DB databases using the Berkeley DB (-ldb) library.
 */

 
#ifndef DATABASE_H
#define DATABASE_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/// Where to store all the databases
#define DB_GLOBAL_DIR "/var/lib/luau"
#define DB_LOCAL_DIR ".luau"

#include <glib.h>

gboolean luau_db_createEnvironment(void);
void luau_db_closeEnvironment(void);

/// Database query
void* luau_db_queryDatabase(const char* category, const char* subcategory, const char* key);
/// Check for key existence
gboolean luau_db_keyExists(const char* category, const char* subcategory, const char* key);
/// Get an array of all keys in the database
GPtrArray* luau_db_getAllDBKeys(const char* category, const char* subcategory);

gboolean luau_db_create(const char* category, const char* subcategory);

/// Set a key/value pair in the database
gboolean luau_db_setValue(const char* category, const char* subcategory, const char* key, const void* value, size_t size);
/// Set a key/value pair where the value is a string
gboolean luau_db_setValueString(const char* category, const char* subcategory, const char* key, const char* string);
/// Set a key/value pair where the value is an integer
gboolean luau_db_setValueInt(const char* category, const char* subcategory, const char* key, int value);

/// Delete a key/value pair in the specified database
gboolean luau_db_deleteKey(const char* category, const char* subcategory, const char* key);
/// Clear a database of all keys
gboolean luau_db_clear(const char* category, const char* subcategory);

/// Keep all opened databases open to speed up multiple database operations
void luau_db_keepOpen(gboolean yesOrNo);
/// Close all open databases
void luau_db_closeAll(void);

#endif
