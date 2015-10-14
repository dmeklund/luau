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


/** @file libuau.h
 * \brief Interface to the libuau library
 *
 * This is the one and only header file you'll need to use and understand in order
 * to interface with libuau from a third party program.  It provides all the 
 * necessary functionality for using libuau and is the only installed header file.
 */

#ifndef LIBUAU_DB_H
#define LIBUAU_DB_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <libuau.h>

#ifdef __cplusplus
extern "C" {
#endif

LUAU_DLL_EXPORT gboolean luau_db_openThreadedEnvironment(void);
LUAU_DLL_EXPORT gboolean luau_db_closeThreadedEnvironment(void);

/// Retrieve update info (type, description, etc.) corresponding to the given ID (and program)
LUAU_DLL_EXPORT gboolean luau_db_getUpdateInfo(AUpdate *update, const char* updateID, const AProgInfo *progInfo, GError **err);
/// Retrieve any new updates for the specified program
LUAU_DLL_EXPORT GList* luau_db_checkForUpdates(const AProgInfo *info, GError **err);

/// Retrieve program info (version, updates url, etc.) from the luau database given the ID
LUAU_DLL_EXPORT gboolean luau_db_getProgInfo(AProgInfo *progInfo, const char* progID, GError **err);
/// Retrieve a list of all registered programs
LUAU_DLL_EXPORT GPtrArray* luau_db_getAllPrograms(void);

/// Mark an update as being "hidden" (ie, updates the user has already seen but does not want to install)
LUAU_DLL_EXPORT gboolean luau_db_hideUpdate(const AProgInfo *prog, const AUpdate *update);
/// Unmark an update as being "hidden"
LUAU_DLL_EXPORT gboolean luau_db_unhideUpdate(const AProgInfo *prog, const AUpdate *update);

/// Register a new application (or library, or anything else) with luau
LUAU_DLL_EXPORT gboolean luau_db_registerNewApp(const AProgInfo *progInfo, GError **err);
/// Remove information for the given app from the database
LUAU_DLL_EXPORT gboolean luau_db_deleteApp(const AProgInfo *info);

/// Tell luau whether to close databases after each database operation (FALSE) or keep them open (TRUE)
LUAU_DLL_EXPORT void luau_db_keepDatabasesOpen(gboolean yesOrNo);
/// Close all open databases (<b>only necessary when keepDatabasesOpen is TRUE</b>)
LUAU_DLL_EXPORT void luau_db_closeAllDatabases(void);

LUAU_DLL_EXPORT void luau_db_categorizeUpdateList(GList *updates, const AProgInfo *progInfo);
LUAU_DLL_EXPORT void luau_db_categorizeUpdate(AUpdate *update, const AProgInfo *progInfo);

#ifdef __cplusplus
}
#endif

#endif /* !LIBUAU_DB_H */
