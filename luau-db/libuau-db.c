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

#include <string.h>

#include <glib.h>

#include "libuau.h"
#include "libuau-db.h"
#include "database.h"
#include "util.h"
#include "error.h"


static gboolean isHidden(AUpdate *update, const AProgInfo *progInfo);

gboolean
luau_db_openThreadedEnvironment(void) {
	gboolean result;
	
	result = luau_db_createEnvironment();
	/*if (result) {
		luau_db_closeEnvironment();
		result = luau_db_createEnvironment();
	}*/
	
	return result;
}

gboolean
luau_db_closeThreadedEnvironment(void) {
	luau_db_closeEnvironment();
	return TRUE;
}

gboolean
luau_db_getUpdateInfo(AUpdate *update, const char* updateID, const AProgInfo *progInfo, GError **err) {
	gboolean result;
	
	result = luau_getUpdateInfo(update, updateID, progInfo, err);
	if (result == TRUE)
		luau_db_categorizeUpdate(update, progInfo);
	
	return result;
}

GList *
luau_db_checkForUpdates(const AProgInfo *info, GError **err) {
	GList *results;
	
	results = luau_checkForUpdates(info, err);
	if (results != NULL)
		luau_db_categorizeUpdateList(results, info);
	
	return results;
}


/**
 * Retrieve program information for \c progID from the luau database and store it in \c info.
 * Use \ref luau_freeProgInfo to free associated data.
 *
 * @arg info is a pointer to an AProgInfo struct to store data in.
 * @arg progID is the identifier for the program to look up.
 * @return whether the operation was successful
 *
 * @see luau_freeProgInfo
 */
gboolean
luau_db_getProgInfo(AProgInfo *info, const char* progID, GError **err) {
	GContainer *cont;
	char *date, *keywords, *interface;
	int *exists;
	
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
	
	exists = luau_db_queryDatabase("program_info", "all", progID);
	if (exists != NULL) {
		g_free(exists);
		
		memset(info, 0, sizeof(AProgInfo));
		
		date = luau_db_queryDatabase("program_info", "date", progID);
		keywords = luau_db_queryDatabase("program_info", "keywords", progID);
		interface = luau_db_queryDatabase("program_info", "interface", progID);
		
		info->id = g_strdup(progID);
		info->shortname = luau_db_queryDatabase("program_info", "shortname", progID);
		info->fullname = luau_db_queryDatabase("program_info", "fullname", progID);
		info->desc = luau_db_queryDatabase("program_info", "desc", progID);
		info->version = luau_db_queryDatabase("program_info", "version", progID);
		info->displayVersion = luau_db_queryDatabase("program_info", "display_version", progID);
		info->url = luau_db_queryDatabase("program_info", "url", progID);
		if (date != NULL) {
			info->date = g_malloc(sizeof(ADate));
			luau_parseDate(info->date, date);
		} else {
			info->date = NULL;
		}
		
		luau_parseInterface(&(info->interface), interface);
		
		if (keywords != NULL && keywords[0] != '\0') {
			cont = lutil_gsplit(", ", keywords);
			info->keywords = cont->data;
			g_container_free(cont, FALSE);
		} else
			info->keywords = g_ptr_array_new();
		
		nnull_g_free(date);
		nnull_g_free(keywords);
		nnull_g_free(interface);
		
		return TRUE;
	} else {
		g_set_error(err, LUAU_DB_ERROR, LUAU_DB_ERROR_INVALID_ARG, "No such program in database: %s", progID);
		return FALSE;
	}
}

/**
 * Return an array of all registered programs by looking them up in the luau database.
 * Returned array \b must be free'd \em along with all the keys (i.e., the strings stored in the array itself).
 *
 * @return a GPtrArray of strings containing every registered program (\b must be free'd).
 */
GPtrArray *
luau_db_getAllPrograms(void) {
	return luau_db_getAllDBKeys("program_info", "all");
}

/**
 * "Hide" an update.  The point of this operation is not to bother the user with update
 * which he has already installed or simply doesn't care about.  Updates that are hidden
 * will be marked with a "_hidden" keyword - both after calling this function and any time
 * they're retrieved in the future (unless they are "unhidden" - see \ref luau_unhideUpdate).
 *
 * @arg prog describes the program for which we're hiding an update
 * @arg update describes the update we're hiding.
 * @return whether the operation was successful (\b Note: Also returns true if the update provided was already hidden in the first place)
 *
 * @see luau_unhideUpdate
 */
gboolean
luau_db_hideUpdate(const AProgInfo *prog, const AUpdate *update) {
	DBUGOUT("Hiding update %s for program %s", update->id, prog->id);
	
	if (luau_db_setValueInt("updates_hidden", prog->id, update->id, 1)) {
		luau_setKeyword(update->keywords, "_hidden");
		return TRUE;
	} else {
		ERROR("Couldn't hide update %s.%s", prog->id, update->id);
		return FALSE;
	}
}

/**
 * "Unhide" an update that has already been hidden. (see \ref luau_hideUpdate for "hiding" details)
 *
 * @arg prog describes the program for which we're unhiding an update.
 * @arg update describes the update we're unhiding.
 * @return whether the operation was successful (\b Note: Also returns true even if the update provided was already unhidden in the first place)
 *
 * @see luau_hideUpdate
 */
gboolean
luau_db_unhideUpdate(const AProgInfo *prog, const AUpdate *update) {
	DBUGOUT("Unhiding update %s for program %s", update->id, prog->id);
	
	if (luau_db_setValueInt("updates_hidden", prog->id, update->id, 0)) {
		luau_unsetKeyword(update->keywords, "_hidden");
		return TRUE;
	} else {
		ERROR("Couldn't unhide update %s.%s", prog->id, update->id);
		return FALSE;
	}
}

/**
 * Specifies whether the luau databases should be kept open until being explicitly closed.
 * Default is off (<code>luau_keepDatabasesOpen(FALSE)</code>).  Only suggested to use if you're
 * planning on doing many database operations.
 * <b>VERY IMPORTANT:</b> You \b MUST call luau_closeAllDatabases \em before exiting your application
 * if you set this true.  Otherwise, you will almost certainly <b>lose data</b> that was supposed to be
 * written to the database!
 *
 * @arg yesOrNo specifies whether to keep all databases open (TRUE => yes, FALSE => no).
 *
 * @see luau_closeAllDatabases
 */
void
luau_db_keepDatabasesOpen(gboolean yesOrNo) {
	DBUGOUT("Setting keep-databases-open to: %s", (yesOrNo ? "YES" : "NO"));
	luau_db_keepOpen(yesOrNo);
}

/**
 * Closes all open databases.  Only useful when luau_keepDatabasesOpen has been set to TRUE (in
 * which case it's <b>VERY IMPORTANT</b>: see luau_keepDatabasesOpen for more information).
 *
 * @see luau_keepDatabasesOpen
 */
void
luau_db_closeAllDatabases(void) {
	DBUGOUT("Closing all open databases.");
	luau_db_closeAll();
}

/**
 * Register a new application with the luau database. This adds the specified program
 * with the required information to the luau database so that it can be automatically checked for updates. 
 * While luau won't complain if you specify NULL values for any of these, it's highly suggested
 * you supply information for all of them.
 *
 * You may also use this function to redefine new parameters for an already registered application.  Simply
 * specify the program ID and whichever data you need to change.  If you enter NULL for any value, it will
 * simply be left to its previous value (won't be changed).
 *
 * @arg progInfo describes the new program (see \ref AProgInfo)
 * @return whether the operation was successful
 */
gboolean
luau_db_registerNewApp(const AProgInfo *progInfo, GError **err) {
	char *dateStr = NULL, *keywordStr = NULL, *interfaceStr = NULL, *str;
	const char *shortname, *fullname, *displayVersion;
	gboolean result = TRUE;
	
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
	
	if (progInfo->id == NULL || progInfo->id[0] == '\0') {
		g_set_error(err, LUAU_DB_ERROR, LUAU_DB_ERROR_INVALID_ARG, 
		            "Can't register application: invalid program ID: must have non-zero length");
		return FALSE;
	}
	
	if (progInfo->date != NULL) {
		dateStr = luau_dateString(progInfo->date);
		if (dateStr == NULL) {
			g_set_error(err, LUAU_DB_ERROR, LUAU_DB_ERROR_INVALID_ARG,
			            "Date string invalid: must be in form 'YYYY-MM-DD'");
			return FALSE;
		}
	}
	
	if (progInfo->keywords != NULL)
		keywordStr = luau_keywordsString(progInfo->keywords);

	interfaceStr = luau_interfaceString(&(progInfo->interface));
	
	if ((progInfo->shortname == NULL) && (!luau_db_keyExists("program_info", "shortname", progInfo->id))) 
		shortname = progInfo->id;
	else
		shortname = progInfo->shortname;
	
	if ((progInfo->fullname == NULL) && (!luau_db_keyExists("program_info", "fullname", progInfo->id)))
		fullname = shortname;
	else
		fullname = progInfo->fullname;
	
	if ((progInfo->displayVersion == NULL) && (!luau_db_keyExists("program_info", "display_version", progInfo->id)))
		displayVersion = progInfo->version;
	else
		displayVersion = progInfo->displayVersion;
	
#ifdef DEBUG
	str = luau_progInfoString(progInfo);
	DBUGOUT("Registering new application:");
	DBUGOUT("  %s", str);
	g_free(str);
#endif /* DEBUG */
	
	if (!luau_db_keyExists("program_info", "all", progInfo->id))
		result = luau_db_setValueInt("program_info", "all", progInfo->id, 1);
	
	if (shortname != NULL && result)
		result = luau_db_setValueString("program_info", "shortname", progInfo->id, shortname);
	if (fullname != NULL && result)
		result = luau_db_setValueString("program_info", "fullname",  progInfo->id, fullname);
	if (displayVersion != NULL && result)
		result = luau_db_setValueString("program_info", "display_versoin",  displayVersion, fullname);
	if (progInfo->desc != NULL && result)
		result = luau_db_setValueString("program_info", "desc",      progInfo->id, progInfo->desc);
	if (progInfo->version != NULL && result) 
		result = luau_db_setValueString("program_info", "version",   progInfo->id, progInfo->version);
	if (interfaceStr != NULL && result)
		result = luau_db_setValueString("program_info", "interface", progInfo->id, interfaceStr);
	if (dateStr != NULL && result) 
		result = luau_db_setValueString("program_info", "date",      progInfo->id, dateStr);
	if (progInfo->url != NULL && result)
		result = luau_db_setValueString("program_info", "url",       progInfo->id, progInfo->url);
	if (keywordStr != NULL && result)
		result = luau_db_setValueString("program_info", "keywords",  progInfo->id, keywordStr);
	
	if (result)
		DBUGOUT("Success.");
	else
		DBUGOUT("Failed.");
	
	nnull_g_free(dateStr);
	nnull_g_free(keywordStr);
	nnull_g_free(interfaceStr);

	return result;
}

/**
 * Removes an application from the Luau database.  Useful when you don't care about
 * or are uninstalling a certain application that has registered itself with the Luau
 * database.
 *
 * @arg info describes the program to remove from the database
 * @return whether the operation was successful
 */
gboolean
luau_db_deleteApp(const AProgInfo *info) {
	gboolean result = TRUE;
	const char *progID = (const char*) info->id;
	
	result = luau_db_deleteKey("program_info", "all",       progID) && result;
	result = luau_db_deleteKey("program_info", "shortname", progID) && result;
	result = luau_db_deleteKey("program_info", "fullname",  progID) && result;
	result = luau_db_deleteKey("program_info", "desc",      progID) && result;
	result = luau_db_deleteKey("program_info", "version",   progID) && result;
	result = luau_db_deleteKey("program_info", "display_version", progID) && result;
	result = luau_db_deleteKey("program_info", "interface", progID) && result;
	result = luau_db_deleteKey("program_info", "date",      progID) && result;
	result = luau_db_deleteKey("program_info", "url",       progID) && result;
	result = luau_db_deleteKey("program_info", "keywords",  progID) && result;
	
	/* result = luau_db_clear("updates_hidden", progID) && result; */
	
	return result;
}

void
luau_db_categorizeUpdateList(GList *updates, const AProgInfo *progInfo) {
	GList *curr;
	
	if (updates == NULL) {
		DBUGOUT("NULL pointer passed to luau_db_categorizeUpdateArray");
		return;
	}
	
	for (curr = updates; curr != NULL; curr = curr->next)
		luau_db_categorizeUpdate(curr->data, progInfo);
}

void
luau_db_categorizeUpdate(AUpdate *update, const AProgInfo *progInfo) {
	if (isHidden(update, progInfo))
		luau_setKeyword(update->keywords, "_hidden");
}

/* Non-Interface Methods */

/**
 * Check if an update has been hidden.
 *
 * @arg update is the update to check
 * @arg progInfo is the update to check against
 */
static gboolean
isHidden(AUpdate *update, const AProgInfo *progInfo) {
	int *temp;
	gboolean result;
	
	temp = luau_db_queryDatabase("updates_hidden", progInfo->id, update->id);
	result = (temp != NULL && *temp != 0);
	g_free(temp);
	
	return result;
}
