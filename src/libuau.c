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

#ifdef __unix__
#  include <unistd.h>
#elif defined(_MSC_VER)
#  define S_ISREG(mode) (mode == _S_IFREG)
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include <glib.h>

#include "libuau.h"
#include "network.h"
#include "parseupdates.h"
#include "error.h"
#include "util.h"
#include "install.h"
#include "ftp.h"

#ifdef WITH_DMALLOC
#  include <dmalloc.h>
#endif

#ifdef WITH_LEAKBUG
#  include <leakbug.h>
#endif

static int compareAlphaNumeric(const char *v1, const char *v2);

static void categorizeUpdates(GContainer *updates, const AProgInfo *progInfo);
static void categorizeUpdate(AUpdate *update, const AProgInfo *progInfo);
static gboolean isIncompatible(AUpdate *update, const AProgInfo *progInfo);
static gboolean isOld(AUpdate *update, const AProgInfo *progInfo);


gboolean
luau_getProgInfoFromXML_url(AProgInfo *progInfo, const char *url, GError **err) {
	GString *data;
	gboolean result;
	
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
	
	data = lutil_ftp_getURL(url, err);
	if (data == NULL) {
		g_assert(err == NULL || *err != NULL);
		return FALSE;
	}
	
	result = luau_parseXML_progInfo(data, progInfo, err);
	if (result == FALSE) {
		g_assert(err == NULL || *err != NULL);
		return FALSE;
	}
	
	return TRUE;
}

gboolean
luau_getProgInfoFromXML_data(AProgInfo *progInfo, const char *data, GError **err) {
	GString *gdata;
	gboolean result;
	
	gdata = g_string_new(data);
	
	result = luau_parseXML_progInfo(gdata, progInfo, err);
	if (result == FALSE) {
		g_assert(err == NULL || *err != NULL);
		return FALSE;
	}
	
	g_string_free(gdata, TRUE);
	
	return TRUE;
}

/**
 * Retrieve information for update \c updateID for program described by \c progInfo.  Contacts
 * server for program in question to retrieve all updates and then extracts the one matching
 * \c updateID.
 * Use \ref luau_freeUpdateInfo to free the data associated with \c updateInfo
 *
 * @arg updateInfo is an AUpdate struct pointer where the data will be stored
 * @arg updateID is the ID for the update wanted.
 * @arg progInfo describes the program for which we're looking up an update.
 * @return whether the operation was successful
 *
 * @see luau_freeUpdateInfo
 */
gboolean
luau_getUpdateInfo(AUpdate *updateInfo, const char* updateID, const AProgInfo *progInfo, GError **err) {
	GContainer *allUpdates;
	GList *updateList;
	GIterator iter;
	AUpdate *temp;
	gboolean found;
	
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
	
	allUpdates = luau_net_queryServer(progInfo, err);
	if (allUpdates == NULL) {
		g_assert(err == NULL || *err != NULL);
		return FALSE;
	}
	
	g_container_get_iter(&iter, allUpdates);
	found = FALSE;
	while (g_iterator_hasNext(&iter)) {
		temp = g_iterator_next(&iter);
		if (lutil_streq(temp->id, updateID)) {
			luau_copyUpdate(updateInfo, temp);
			found = TRUE;
			break;
		}
	}
	updateList = g_container_free(allUpdates, FALSE);
	luau_freeUpdateList(updateList);
	
	if (found)
		categorizeUpdate(updateInfo, progInfo);
	else
		g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_INVALID_ARG,
	                "No such update ID: %s (%s)", updateID, progInfo->id);
	
	return found;
}

/**
 * Check for updates for the given ID by contacting the server for the program
 * and downloading and parsing the updates file.
 * Returned GPtrArray \b must be free'd (use \ref luau_freeUpdateArray).
 *
 * @arg info describes the program we want to check updates for.
 * @return an array of updates for the program in question (must be free'd).
 *
 * @see luau_freeUpdateArray
 */
GList *
luau_checkForUpdates(const AProgInfo *info, GError **err) {
	GContainer *result;
	GList *ret;
	
	g_return_val_if_fail(err == NULL || *err == NULL, NULL);
	
	DBUGOUT("Checking for updates for %s: %s", info->id, info->url);
	result = luau_net_queryServer(info, err);
	if (result == NULL) {
		g_assert(err == NULL || *err != NULL);
		return NULL;
	}
	
	categorizeUpdates(result, info);
	
	ret = g_container_free(result, FALSE);
	
	return ret;
}

GList *
luau_checkForUpdates_url(const char *url, GError **err) {
	AProgInfo progInfo;
	
	g_return_val_if_fail(err == NULL || *err == NULL, NULL);
	
	progInfo.id = "(unknown)";
	progInfo.url = (char*) url;
	
	return luau_checkForUpdates(&progInfo, err);
}



/**
 * Install an update.
 *
 * @arg info describes the program we're updating
 * @arg newUpdate describes the update we're installing
 * @arg type is the type of package (eg, RPM, DEB, ...) we want to install.
 * @return whether the operation succeeded
 */
 
gboolean
luau_installUpdate(const AProgInfo *info, const AUpdate *newUpdate, const APkgType type, GError **err) {
	char *filename, *actualFilename;
	gboolean result;
	
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
	
	filename = lutil_getTempFilename();
	
	actualFilename = luau_downloadUpdate(info, newUpdate, type, filename, err);
	g_free(filename);
	if (actualFilename == NULL) {
		g_assert(err == NULL || *err != NULL);
		return FALSE;
	}
	
	result = luau_installPackage(actualFilename, type, err);	
	g_free(actualFilename);
	if (result == FALSE) {
		g_assert(err == NULL || *err != NULL);
		return FALSE;
	}
	
	return result;
}

/**
 * Download an update to the specified location.
 * Returned string \b must be examined and \b must be free'd.  Do not assume that the update was downloaded
 * exactly to \c downloadTo.
 *
 * @arg info describes the program we want to download an update for.
 * @arg newUpdate describes the update we want to download.
 * @arg type is which kind of update (eg. RPM, DEB, ...) we want to download
 * @arg downloadTo specifies where we want to download this update to.  If a directory is specified, it will be downloaded
 *         to that directory with the original filename.
 * @return where this update was actually downloaded (should be == downloadTo -unless- downloadTo is a directory) (\b must be free'd),
 *         or NULL if the operation was unsuccessful.
 *
 * @see luau_installPackage
 */
char *
luau_downloadUpdate(const AProgInfo *info, const AUpdate *newUpdate, const APkgType type, const char* downloadTo, GError **err) {
	char *actualFilename = NULL, *temp;
	APackage *pkg;
	struct stat fileinfo;
	int ret;
	gboolean result = FALSE;
	
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

	if (downloadTo != NULL && downloadTo[0] != '\0') {
		while (result == FALSE) {
			ret = stat(downloadTo, &fileinfo);
			if (ret == -1) {
				if (errno != ENOENT) {
					g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_PERMS, "Can't write to %s: %s", downloadTo, strerror(errno));
					return NULL;
				} else {
					result = TRUE;
				}
			} else {
				if (S_ISREG(fileinfo.st_mode)) {
					ret = lutil_error_prompt("File Exists", "The specified download location already exists.  What would you like to do?", 2, 0, "Overwrite", "Cancel");
					if (ret == 0) {
						DBUGOUT("File '%s' exists: overwriting", downloadTo);
						result = TRUE;
					} else {
						g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_ABORTED, "File '%s' exists: not overwriting", downloadTo);
						return NULL;
					}
				} else if (S_ISDIR(fileinfo.st_mode)) {
					DBUGOUT("Is a directory: writing into that directory");
					pkg = luau_getUpdatePackage(newUpdate, type, err);
					if (pkg == NULL) {
						g_assert(err == NULL || *err != NULL);
						return NULL;
					}
					temp = luau_getPackageURL(pkg, err);
					if (temp == NULL) {
						g_assert(err == NULL || *err != NULL);
						return NULL;
					}
					
					DBUGOUT("Preparing to download: %s", temp);
					temp = g_path_get_basename(temp);
					actualFilename = lutil_vstrcreate(downloadTo, "/", temp, NULL);
					g_free(temp);
					downloadTo = actualFilename;
					/* Loop back and check if this file now exists */
				} else {
					DBUGOUT("FIFO, device, symlink, or socket given: treating as regular file");
					result = TRUE;
				}
			}
		}
		
		if (actualFilename == NULL)
			actualFilename = g_strdup(downloadTo);
	} else {
		g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_INVALID_ARG, "No filename given");
		return NULL;
	}
	
	result = luau_net_downloadUpdate(info, newUpdate, type, actualFilename, err);
	
	if (result == FALSE) {
		g_assert(err == NULL || *err != NULL);
		g_free(actualFilename);
		return NULL;
	}
	
	return actualFilename;
}

/**
 * Installs a package that has already been downloaded.
 *
 * @arg filename is the location where the update package can be found.
 * @arg type is the type of update (eg. RPM, DEB, ...) we're installing.
 * @return whether the operation was successful.
 */
gboolean
luau_installPackage(const char *filename, const APkgType type, GError **err) {
	gboolean result = FALSE;
	
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
	
	switch (type) {
		case LUAU_RPM:
			result = luau_install_rpm(filename, err);
			break;
		case LUAU_DEB:
			result = luau_install_deb(filename, err);
			break;
		case LUAU_SRC:
			result = luau_install_src(filename, err);
			break;
		case LUAU_EXEC:
			result = luau_install_exec(filename, err);
			break;
		case LUAU_AUTOPKG:
			result = luau_install_autopkg(filename, err);
			break;
		default:
			g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_INVALID_ARG, "Invalid packager type qualifier: %d", type);
			result = FALSE;
	}
	
	return result;
}

/**
 * Set the function to display luau errors with.  See the definition of AErrorFunc for
 * specific function parameters.
 *
 * @arg errorFunc is the new error output function.
 *
 * @see AErrorFunc
 */
/*void
luau_registerErrorFunc(AErrorFunc errorFunc) {
	lutil_error_setErrorFunc(errorFunc); // in error.[ch]
}*/

/**
 * Set the function to prompt the user for input with.  See the definition of APromptFunc
 * for specific function parameters.  Note that the default behavior (if you don't define
 * a new one) is simply not to prompt the user and select the default choice.  This is probably
 * not what you want!
 *
 * @arg promptFunc is the new prompting function to use
 *
 * @see APromptFunc
 */
void
luau_registerPromptFunc(APromptFunc promptFunc) {
	lutil_error_setPromptFunc(promptFunc);
}

/**
 * Set the function to display download progress with.  See the definition of AProgressFunc
 * for specific function parameters (and maybe the libcurl documentation too while you're at
 * it - specifically the CURLOPT_PROGRESSFUNC option).  Default behavior is not to display
 * anything.
 *
 * @arg callback is the new progress callback to use.
 *
 * @see AProgressCallback
 */
void
luau_registerProgressCallback(AProgressCallback callback) {
	lutil_ftp_setCallbackFunc(callback);
}

/**
 * Reset the error facilities to their default behavior (that is, simply displaying errors to the
 * command line).
 *
 * @see luau_registerErrorFunc
 */
/*void
luau_resetErrorFunc(void) {
	lutil_error_resetErrorFunc();
}*/

/**
 * Reset the prompting facilities to their default behavior (that is, don't ask but just select
 * the default option).
 *
 * @see luau_registerPromptFunc
 */
void
luau_resetPromptFunc(void) {
	lutil_error_resetPromptFunc();
}

/**
 * Reset the progress callback (which doesn't display anything).
 *
 * @see luau_registerProgressCallback
 */
void
luau_resetProgressCallback(void) {
	lutil_ftp_resetCallbackFunc();
}



/*
 *  *** UTILITY FUNCTIONS ***
 */

/**
 * Find and return an APackage struct of the type \c pkgType from an AUpdate.
 * If one does not exist, return NULL.
 *
 * @arg update is the update to look in.
 * @arg pkgType is the package type to find
 * @return the corresponding APackage struct ptr, or NULL otherwise.
 */
APackage *
luau_getUpdatePackage(const AUpdate *update, APkgType pkgType, GError **err) {
	GPtrArray *updateArray = update->packages;
	APackage *temp, *result = NULL;
	unsigned int i;
	
	g_return_val_if_fail(err == NULL || *err == NULL, NULL);
	
	if (pkgType == LUAU_EMPTY)
	{
		if (updateArray->len == 1)
			return g_ptr_array_index(updateArray, 0);
		else
		{
			g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_INVALID_ARG, "Must specify package type");
			return NULL;
		}
	}
	
	for (i = 0; i < updateArray->len; ++i) {
		temp = g_ptr_array_index(updateArray, i);
		if (temp->type == pkgType) {
			result = temp;
			break;
		}
	}
	
	if (result == NULL) {
		g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_INVALID_ARG, "No package of type %s in selected update!", luau_packageTypeString(pkgType));
		return NULL;
	} else {
		return result;
	}
}

/**
 * Convert an ADate into a string ("YYYY-MM-DD").  Returns "(null)" if
 * \c date == NULL.  Returned string \b must be free'd.
 *
 * @arg date is the date to convert into a string
 * @return a string representing the date - \b must be free'd.
 */
char *
luau_dateString(const ADate *date) {
	/* 2003-04-28 */
	char* string;
	if (date == NULL)
		string = g_strdup("(null)");
	else
		string = lutil_mprintf("%.4d-%.2d-%.2d", date->year, date->month, date->day);
	
	return string;
}

/**
 * Convert a string of format "YYYY-MM-DD" into an ADate.  If <code>string == NULL</code>
 * or <code>string</code> is the empty string, all the fields of the resultant ADate will
 * be set to 0.
 *
 * @arg date is the ADate to write the parsed information into
 * @arg string is the string to convert to an ADate
 * @return whether the operation was successful
 */
gboolean
luau_parseDate(ADate *date, const char *string) {
	char *ptr1, *ptr2, *dup;
	gboolean result;
	
	/* Clear date field */
	date->year = 0;
	date->month = 0;
	date->day = 0;
	
	if (string == NULL) {
		DBUGOUT("NULL pointer passed to luau_parseDate");
		return TRUE;
	} else if (string[0] == '\0') {
		DBUGOUT("Empty string passed to luau_parseDate");
		return TRUE;
	}
	
	/* Make a duplicate which we can edit as we please */
	dup = g_strdup(string);
	
	/* Convert all dashes into '\0' and then read in the three
	   strings created into the year, month, and date fields
	   respectively.  We only do some very basic sanity checks
	   here - more might be advisable (eg checking to make sure
	   that there aren't any non-numeric characters, aside from
	   the dashes). */
	   
	result = TRUE;
	do {
		
		ptr1 = dup;
		ptr2 = strchr(ptr1, '-');
		if (ptr2 == NULL) {
			ERROR("Invalid date: no '-' separator found");
			result = FALSE;
			break;
		}
		
		*ptr2 = '\0';
		date->year = atoi(ptr1);
		
		ptr1 = ptr2+1;
		ptr2 = strchr(ptr1, '-');
		if (ptr2 == NULL) {
			ERROR("Invalid date: not enough fields (2 found, 3 needed)");
			result = FALSE;
			break;
		}
		
		*ptr2 = '\0';
		date->month = atoi(ptr1);
		if (date->month < 1 || date->month > 12)  {
			ERROR("Invalid date: given month (%d) is out of valid range (1-12)", date->month);
			result = FALSE;
			/* we still continue (instead of breaking out here) just because we can, and so
			   the user can still read in the day argument even if the month is invalid */
		}
		
		ptr1 = ptr2+1;
		date->day = atoi(ptr1);
		if (date->day < 1 || date->day > 31) {
			ERROR("Invalid date: given day (%d) is out of valid range (1-31)", date->day);
			result = FALSE;
			/* only a basic check: April 31st would still be accepted even though it isn't
			   a valid date. */
		}
		
		if (strchr(ptr1, '-') != NULL) {
			ERROR("Invalid date: too many fields");
			result = FALSE;
			break;
		}
	} while (0);
	
	g_free(dup);
	
	return result;
}

/**
 * Compare two dates and return an integer accordingly (in style of \c strcmp).
 *
 * @arg d1 is the first date
 * @arg d2 is the date to compare \c d1 with
 * @return 
 *   - -1 if d1 is earlier than d2,
 *   -  0 if d1 is the same as d2, or
 *   - +1 if d1 is later than d2.
 */
int
luau_datecmp(ADate *d1, ADate *d2) {
	int result;
	
	if (d1 == NULL || d2 == NULL)
		return 0;
	else if ((result = lutil_intcmp(d1->year, d2->year)) != 0)
		return result;
	else if ((result = lutil_intcmp(d1->month, d2->month)) != 0)
		return result;
	else
		return lutil_intcmp(d1->day, d2->day);
}


/*
 * compareVersions <REQUIRED> <CURRENT>
 * REQUIRED: Required version.
 * CURRENT: Current version.
 * Returns: 0 if passed, 1 if failed.
 *
 * This function compares 2 strings - infinite level of decimal groups.
 * REQUIRED string for required version; CURRENT string for current version.
 * Returns 1 if REQUIRED is > CURRENT, else 0. [ 0 - PASS, 1 - FAIL ]
 *
 * Parameter string format: "x.y.z", where y and z are optional. Wildcards can
 * only be used for an entire decimal group like "1.6.x" or "2.x" NOT "2.5x" .
 * Function looks ahead in the decimal groups with alphabetic and numeric
 * identifers to match full numbers. For instance REQUIRED:2-RC10f and
 * CURRENT:2-rc2d, it ends up comparing 10 to 2 and returns 1 [ FAIL ]
 * instead of 1 to 2 returning 0 [ PASS ].
 *
 * Example:
 *                     Required    Current          Return Value
 *    compareVersions  "1"         "1.2"       --->  0 [ PASS ]
 *    compareVersions  "1.2"       "1"         --->  1 [ FAIL ]
 *    compareVersions  "1.1"       "1.2"       --->  0 [ PASS ]
 *    compareVersions  "1.3"       "1.2"       --->  1 [ FAIL ]
 *    compareVersions  "1.2"       "1.2"       --->  0 [ PASS ]
 *    compareVersions  "1.2b"      "1.2b"      --->  0 [ PASS ]
 *    compareVersions  "2.5-pre3"  "2.5"       --->  0 [ PASS ]
 *    compareVersions  "2.5-pre3"  "2.5-pre2"  --->  1 [ FAIL ]
 *    compareVersions  "2-RC10f"   "2-rc2d"    --->  1 [ FAIL ]
 *    compareVersions  "3.1-RC3"   "3.1-rc12"  --->  0 [ PASS ]
 *    compareVersions  "1.3"       "0.1.5"     --->  1 [ FAIL ]
 *    compareVersions  "1.99.6"    "2"         --->  0 [ PASS ]
 *    compareVersions  "1.6.x"     "1.6.7"     --->  0 [ PASS ]
 *    compareVersions  "1.6.x"     "1.6"       --->  0 [ PASS ]
 *    compareVersions  "1.6.x"     "1.5.7"     --->  1 [ FAIL ]
 *    compareVersions  "1.x"       "1.5.7"     --->  0 [ PASS ]
 */
/* FIXME - don't reference indices of the GContainer explicitly!  use iterator! */
int
luau_versioncmp(const char *required, const char *current) {
	GContainer *reqElements, *curElements;
	char *req, *cur;
	int result, x, y;
	unsigned int i, len, max;
	
	result = 0;
	
	req = g_strdup(required);
	cur = g_strdup(current);
	
	lutil_strToLower(req);
	lutil_strToLower(cur);
	
	/* replace all '-' '_' '/' '+' with '.' to index for arrays */
	len = strlen(req);
	for (i = 0; i < len; ++i) {
		if (req[i] == '-' || req[i] == '_' || req[i] == '/' || req[i] == '+' || req[i] == '\\')
				req[i] = '.';
	}
	
	len = strlen(cur);
	for (i = 0; i < len; ++i) {
		if (cur[i] == '-' || cur[i] == '_' || cur[i] == '/' || cur[i] == '+' || cur[i] == '\\')
				cur[i] = '.';
	}
	
	/* create arrays from parameters */
	reqElements = lutil_gsplit(".", req);
	curElements = lutil_gsplit(".", cur);
	
	g_free(req);
	g_free(cur);
	
	/* obtain maximum count of elements from either array */
	if (reqElements->len > curElements->len)
		max = reqElements->len;
	else
		max = curElements->len;

	for (i = 0; i < max; ++i) {
		/* process each respective decimal group ...
		 * if alphanumeric group then extend the compare to compareAlphaNumeric, otherwise do integer comparisons */
		
		req = (i < reqElements->len) ? g_container_index(reqElements, i) : NULL;
		cur = (i < curElements->len) ? g_container_index(curElements, i) : NULL;
		
		/* special case: if wildcard in either decimal group then they are "equal" */
		if ((req != NULL && lutil_streq(req, "x")) || (cur != NULL && lutil_streq(cur, "x"))) {
			result = 0;
			break;
		}
		
		if (lutil_containsAlpha(cur) || lutil_containsAlpha(req)) {
			result = compareAlphaNumeric(req, cur);
			
			if (result != 0)
				break;
		} else if (req == NULL) {
			result = -1;
			break;
		} else if (cur == NULL) {
			result = 1;
			break;
		} else {
			x = atoi(req);
			y = atoi(cur);
			
			if (x != y) {
				result = (x < y) ? -1 : 1;
				break;
			}
		}
	}
	
	g_container_destroy(reqElements);
	g_container_destroy(curElements);
	
	return result;
}

/**
 * Check to see if \c query contains type \c type.
 * 
 * @arg query is the APkgType to check
 * @arg type is the type to see if \c query contains.
 * @return TRUE if it does, FALSE otherwise
 */
gboolean
luau_isOfType(APkgType query, APkgType type) {
  return ( (query & type) == 0 ? FALSE : TRUE);
}

/**
 * Convert an APkgType of multiple package types into a string.
 * Resultant string \b must be free'd.
 *
 * @arg types is the APkgType representing multiple types to convert
 * @return a space-separated string of the types it contains
 *
 * @see luau_packageTypeString
 */
char *
luau_multPackageTypeString(APkgType types) {
	char *str = lutil_createString(25);
	int n;
	
	str[0] = '\0';
	if (luau_isOfType(types, LUAU_RPM))
		strcat(str, "RPM ");
	if (luau_isOfType(types, LUAU_DEB))
		strcat(str, "DEB ");
	if (luau_isOfType(types, LUAU_SRC))
		strcat(str, "SRC ");
	if (luau_isOfType(types, LUAU_EXEC))
		strcat(str, "EXEC ");
	if (luau_isOfType(types, LUAU_AUTOPKG))
		strcat(str, "AUTOPKG ");
	
	if ((n = strlen(str)) != 0) {
		str[n-1] = '\0'; /* Cut off trailing white space */
	} else if (types == LUAU_EMPTY) {
		strcpy(str, "(none)");
	} else {
		ERROR("Unrecognized package type: %d", types);
		strcpy(str, "UNKNOWN");
	}
	
	return str;
}

/**
 * Convert an APkgType of one package type into a string.  Resultant string
 * <b>should not</b> be free'd (constant string).
 *
 * @arg type is an APkgType of only one type
 * @return a constant string representing it
 *
 * @see luau_multPackageTypeString
 */
const char *
luau_packageTypeString(APkgType type) {
	const char *str = NULL;
	
	if (type == LUAU_RPM)
		str = "RPM";
	else if (type == LUAU_DEB)
		str = "DEB";
	else if (type == LUAU_SRC)
		str = "SRC";
	else if (type == LUAU_EXEC)
		str = "EXEC";
	else if (type == LUAU_AUTOPKG)
		str = "AUTOPKG";
	else if (type == LUAU_EMPTY)
		str = "(none)";
	else
		str = "UNKNOWN";
	
	return str;
}

/**
 * Take a string representing a package type ("RPM", "DEB", etc.) and convert
 * it into an APkgType.
 *
 * @arg typeString is the string to convert
 * @return the corresponding APkgType, or LUAU_UNKNOWN if unrecognized
 *
 * @see luau_packageTypeList
 */
APkgType
luau_parsePkgType(const char* typeString) {
	APkgType type = LUAU_UNKNOWN;
	if      (lutil_strcaseeq(typeString, "RPM")) type = LUAU_RPM;
	else if (lutil_strcaseeq(typeString, "DEB")) type = LUAU_DEB;
	else if (lutil_strcaseeq(typeString, "SRC")) type = LUAU_SRC;
	else if (lutil_strcaseeq(typeString, "EXEC")) type = LUAU_EXEC;
	else if (lutil_strcaseeq(typeString, "autopackage")) type = LUAU_AUTOPKG;
	else if (lutil_strcaseeq(typeString, "AUTOPKG")) type = LUAU_AUTOPKG;
	return type;
}

char *
luau_getPackageURL(APackage *pkgInfo, GError **err) {
	GPtrArray *mirrors = pkgInfo->mirrors;
	char *loc = NULL;
	int percentage, random;
	unsigned int i;
	
	g_return_val_if_fail(err == NULL || *err == NULL, NULL);

#ifdef DEBUG
	for (i = 0; i < mirrors->len; i += 2)
		DBUGOUT("URL: %s; weight: %d\n", (char*)g_ptr_array_index(mirrors, i+1), GPOINTER_TO_INT (g_ptr_array_index(mirrors, i)));
#endif /* DEBUG */
	
	if (mirrors == NULL || mirrors->len == 0) {
		g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_INVALID_ARG, "Cannot retrieve package URL: none available in supplied argument");
		return NULL;
	}
	else if (mirrors->len == 2)
		return g_ptr_array_index(mirrors, 1);
	
	random = rand() % 100; /* integer between 0 and 100 */
	
	for (i = 0; i < mirrors->len; i+=2) {
		percentage = GPOINTER_TO_INT( g_ptr_array_index(mirrors, i) );
		if (random <= percentage) {
			loc = g_ptr_array_index(mirrors, i+1);
			break;
		} else {
			random -= percentage;
		}
	}
	
	if (loc == NULL && mirrors->len >= 2)
		loc = g_ptr_array_index(mirrors, 1);
	
	return loc;
}

/**
 * Take an APkgType of multiple types and convert it into a list of
 * single APkgType's.
 *
 * @arg types is an APkgType of multiple types
 * @return a GSList of single APkgType's
 */
GSList *
luau_packageTypeList(APkgType types) {
	GSList *list = NULL;
	
	if (luau_isOfType(types, LUAU_RPM))
		list = g_slist_append(list, GINT_TO_POINTER (LUAU_RPM));
	if (luau_isOfType(types, LUAU_DEB))
		list = g_slist_append(list, GINT_TO_POINTER (LUAU_DEB));
	if (luau_isOfType(types, LUAU_SRC))
		list = g_slist_append(list, GINT_TO_POINTER (LUAU_SRC));
	if (luau_isOfType(types, LUAU_EXEC))
		list = g_slist_append(list, GINT_TO_POINTER (LUAU_EXEC));
	if (luau_isOfType(types, LUAU_AUTOPKG))
		list = g_slist_append(list, GINT_TO_POINTER (LUAU_AUTOPKG));
	
	return list;
}

/**
 * Convert an AUpdateType into a constant string.  Resultant string
 * <b>should not</b> be free'd.
 *
 * @arg type is an AUpdateType to convert
 * @return a constant string representing it
 */
const char *
luau_updateTypeString(AUpdateType type) {
	switch (type) {
		case LUAU_SOFTWARE: return "Software";
		case LUAU_MESSAGE: return "Message";
		case LUAU_LIBUPDATE: return "Luau Config";
		default: return "UNKNOWN";
	}
}

/**
 * Convert an AProgInfo struct into a string.  Resultant string \b must be free'd.
 *
 * @arg info is a AProgInfo struct to convert
 * @return a new allocated string representing it (\b must be free'd).
 */
char *
luau_progInfoString(const AProgInfo *info) {
	char *str, *keywordsStr, *dateStr;
	
	keywordsStr = luau_keywordsString(info->keywords);
	dateStr = luau_dateString(info->date);
	str = lutil_mprintf("{ ID => %s,\n  Name => %s,\n  Desc => %s,\n  URL => %s,\n  Version => %s,\n Date => %s,\n Keywords => %s\n}",
	                     info->id, info->fullname, info->desc, info->url, info->version, dateStr, keywordsStr);
	
	g_free(keywordsStr);
	g_free(dateStr);
	
	return str;
}

/**
 * Convert an array of keywords into a string, separated by commas.
 * Resultant string \b must be free'd.
 *
 * @arg keywords are the keywords to convert
 * @return a string representing them (\b must be free'd)
 */
char *
luau_keywordsString(const GPtrArray *keywords) {
	GContainer *keyCont;
	char *str;
	
	if (keywords == NULL)
		str = g_strdup("");
	else {
		keyCont = g_container_new_from_data(GCONT_PTR_ARRAY, (void*)keywords);
		str = lutil_strjoin(", ", keyCont);
		g_container_free(keyCont, FALSE);
	}
	
	return str;
}

/**
 * Add a new keyword to a keyword array.
 *
 * @arg keywords is the keyword array to add on to
 * @arg newKeyword is the new keyword to add
 */
void
luau_setKeyword(GPtrArray *keywords, const char *newKeyword) {
	if (keywords != NULL && newKeyword != NULL)
		g_ptr_array_add(keywords, g_strdup(newKeyword));
}

/**
 * Remove a keyword from a keyword array.  Return if it was there in
 * the first place.
 *
 * @arg keywords is the keyword array to edit
 * @arg oldKeyword is the keyword to remove
 * @return whether the keyword existed in the array at all
 */
gboolean
luau_unsetKeyword(GPtrArray *keywords, const char *oldKeyword) {
	unsigned int i;
	gboolean found = FALSE;
	char *curr;
	
	for (i = 0; i < keywords->len; ++i) {
		curr = g_ptr_array_index(keywords, i);
		if (lutil_streq(curr, oldKeyword)) {
			found = TRUE;
			g_ptr_array_remove_index_fast(keywords, i);
			break;
		}
	}
	
	return found;
}

/**
 * See if a keyword exists in a keyword array.
 *
 * @arg keywords is the array to check
 * @arg needle is the keyword to look for
 * @return if it exists in the array
 */
gboolean
luau_checkKeyword(const GPtrArray *keywords, const char *needle) {
	GContainer *cont;
	gboolean result;
	
	if (keywords != NULL && needle != NULL) {
		cont = g_container_new_from_data(GCONT_PTR_ARRAY, (void*)keywords);
		result = lutil_findString(cont, needle);
		g_container_free(cont, FALSE);
	}
	else
		result = FALSE;
	
	return result;
}

/**
 * Check if an update has been marked as "incompatible"
 *
 * @arg update is the update to check
 * @return whether it has been marked as incompatible
 */
gboolean
luau_isIncompatible(AUpdate *update) {
	if (update != NULL && update->keywords != NULL)
		return luau_checkKeyword(update->keywords, "_incompatible");
	else
		return FALSE;
}

/**
 * Check if an update has been marked as "hidden"
 *
 * @arg update is the update to check
 * @return whether it has been marked as hidden
 */
gboolean
luau_isHidden(AUpdate *update) {
	if (update != NULL && update->keywords != NULL)
		return luau_checkKeyword(update->keywords, "_hidden");
	else
		return FALSE;
}

gboolean
luau_isOld(AUpdate *update) {
	if (update != NULL && update->keywords != NULL)
		return luau_checkKeyword(update->keywords, "_old");
	else
		return FALSE;
}

gboolean
luau_isVisible(AUpdate *update) {
	return !(luau_isOld(update) || luau_isHidden(update) || luau_isIncompatible(update));
}

/**
 * Check if a given program/update interface satisfies the one needed.
 * Conditions for being satisfactory:
 * <ol>
 *  <li>wanted->major == needed->major</li>
 *  <li>wanted->minor >= needed->minor</li>
 * </ol>
 *
 * @arg wanted is the AInterface we want to check
 * @arg needed is the AInterface to test against (the "needed" interface)
 * @return whether \c wanted satisfies \c needed
 */
gboolean
luau_satisfiesInterface(const AInterface *wanted, const AInterface *needed) {
	return ((wanted->major == needed->major) && (wanted->minor >= needed->minor));
}

gboolean
luau_satisfiesQuant(const AQuantifier *needed, const AProgInfo *installed) {
	gboolean result;
	int compare;
	
	/* Note that if one of the quantifiers is invalid (anywhere you see an "ERROR" call here), we
	   return TRUE - in essense, we ignore that the quantifier even exists by saying that any
	   program version satisfies it */
	
	if (needed->dtype == LUAU_QUANT_DATA_INTERFACE) {
		if (needed->qtype == LUAU_QUANT_FOR)
			result = luau_satisfiesInterface(&(installed->interface), (AInterface*) needed->data);
		else {
			ERROR("Interface quantifiers can only be of type 'for', not of 'from' or 'to'");
			result = TRUE;
		}
	} else if (needed->dtype == LUAU_QUANT_DATA_KEYWORD) {
		if (needed->qtype == LUAU_QUANT_FOR)
			result = luau_checkKeyword(installed->keywords, (char*)needed->data);
		else {
			ERROR("Keyword quantifiers can only be of type 'for', not of 'from' or 'to'");
			result = TRUE;
		}
	} else if (needed->dtype == LUAU_QUANT_DATA_DATE || needed->dtype == LUAU_QUANT_DATA_VERSION) {
		if (needed->dtype == LUAU_QUANT_DATA_DATE)
			compare = luau_datecmp(installed->date, (ADate*) needed->data);
		else /* needed->dtype == LUAU_QUANT_DATA_VERSION */
			compare = luau_versioncmp(installed->version, (const char *) needed->data);
		
		if (needed->qtype == LUAU_QUANT_FOR)
			result = (compare == 0);
		else if (needed->qtype == LUAU_QUANT_FROM)
			result = (compare != -1); /* Inclusive */
		else if (needed->qtype == LUAU_QUANT_TO)
			result = (compare == -1); /* Exclusive */
		else {
			ERROR("Internal Error: Unrecognized quantifier type (%d)", needed->qtype);
			result = TRUE;
		}
	} else {
		ERROR("Internal Error: Unrecognized quantifier data type (%d)", needed->dtype);
		result = TRUE;
	}
	
	return result;
}

AQuantDataType
luau_parseQuantDataType(const char *str) {
	AQuantDataType type;
	
	if (lutil_strcaseeq(str, "version"))
		type = LUAU_QUANT_DATA_VERSION;
	else if (lutil_strcaseeq(str, "interface"))
		type = LUAU_QUANT_DATA_INTERFACE;
	else if (lutil_strcaseeq(str, "date"))
		type = LUAU_QUANT_DATA_DATE;
	else if (lutil_strcaseeq(str, "keyword"))
		type = LUAU_QUANT_DATA_KEYWORD;
	else
		type = LUAU_QUANT_DATA_INVALID;
	
	return type;
}

/**
 * Take a string ("x.y", where x is the major and y the minor interface version)and convert it 
 * into an AInterface, writing the data into \c interface.
 * If <code>intStr == NULL</code> or there is an error in reading the string, then both \c major
 * and \c minor will be set to -1.
 *
 * @arg interface is the AInterface struct to write data into
 * @arg intStr is the string to parse (of form "x.y")
 * @return whether the operation was successful
 */
gboolean
luau_parseInterface(AInterface *interface, const char *intStr) {
	gboolean result = FALSE;
	int ret = 0;
	
	interface->major = -1;
	interface->minor = -1;

	if (intStr != NULL) {
		if (lutil_isCompletelyBlank(intStr))
			result = TRUE;
		else {
			ret = sscanf(intStr, "%d.%d", &(interface->major), &(interface->minor));
			result = (ret == 2);
		}
	}
	
	return result;
}

/**
 * Take an AInterface struct and convert it into a string of form "x.y", where x is the major and y
 * the minor interface version.  If the major and minor values are both -1, then NULL will be returned.
 * The returned string is allocated dynamically and \b must be free'd.
 *
 * @arg interface is the AInterface struct to convert into a string
 * @return a newly allocated string representing the AInterface (\b must be free'd).
 */
char *
luau_interfaceString(const AInterface *interface) {
	char *ret;
	
	if (interface->major == -1 && interface->minor == -1)
		ret = g_strdup("");
	else
		ret = lutil_mprintf("%d.%d", interface->major, interface->minor);
	
	return ret;
}


/* Structure copying */

/**
 * Copy an AUpdate \c src to \c dest.  If src or dest is NULL, the operation will be aborted.
 *
 * @arg dest is the AUpdate struct to copy \em into.
 * @arg src is the AUpdate struct to copy \em from.
 */
void
luau_copyUpdate(AUpdate *dest, const AUpdate *src) {
	APackage *destPkg, *srcPkg;
	unsigned int i;
	
	if (dest == NULL || src == NULL) {
		DBUGOUT("Null pointer passed to luau_copyUpdate");
	} else {
		/* Note that g_strdup(NULL) == NULL, as desired for this code block */
		dest->id = g_strdup(src->id);
		dest->shortDesc = g_strdup(src->shortDesc);
		dest->fullDesc = g_strdup(src->fullDesc);
		dest->newVersion = g_strdup(src->newVersion);
		dest->newURL = g_strdup(src->newURL);
		
		dest->type = src->type;
		dest->availableFormats = src->availableFormats;
		luau_copyInterface(&(dest->interface), &(src->interface));
		
		if (src->keywords != NULL) {
			GContainer *destCont, *srcCont;

			dest->keywords = g_ptr_array_new();
			destCont = g_container_new_from_data(GCONT_PTR_ARRAY, dest->keywords);
			srcCont = g_container_new_from_data(GCONT_PTR_ARRAY, src->keywords);
			g_container_copy(destCont, srcCont);
			g_container_free(srcCont, FALSE);
			g_container_free(destCont, FALSE);
		} else {
			dest->keywords = NULL;
		}
		if (src->packages != NULL) {
			dest->packages = g_ptr_array_new();
			for (i = 0; i < src->packages->len; ++i) {
				srcPkg = g_ptr_array_index(src->packages, i);
				destPkg = g_malloc(sizeof(APackage));
				
				luau_copyPackage(destPkg, srcPkg);
				g_ptr_array_add(dest->packages, destPkg);
			}
		} else {
			dest->packages = NULL;
		}
		
		if (src->date != NULL) {
			dest->date = g_malloc(sizeof(ADate));
			luau_copyDate(dest->date, src->date);
		} else {
			dest->date = NULL;
		}
		
		if (src->quantifiers != NULL) {
			dest->quantifiers = g_ptr_array_new();
			luau_copyQuants(dest->quantifiers, src->quantifiers);
		} else {
			dest->quantifiers = NULL;
		}
		
		/*if (src->newDate != NULL) {
			dest->newDate = g_malloc(sizeof(ADate));
			luau_copyDate(dest->newDate, src->newDate);
		} else {
			dest->newDate = NULL;
		}*/
	}
}

void
luau_copyPackage(APackage *dest, const APackage *src) {
	unsigned int i;
	int curr;

	if (dest == NULL || src == NULL) {
		DBUGOUT("Null pointer passed to luau_copyPackage");
	} else {
		dest->type = src->type;
		dest->size = src->size;
		strncpy(dest->md5sum, src->md5sum, 33);
		if (src->mirrors != NULL) {
			dest->mirrors = g_ptr_array_new();
			for (i = 0; i < src->mirrors->len; i++) {
				if (i%2) /* odd entries => strings */
					g_ptr_array_add(dest->mirrors, g_strdup(g_ptr_array_index(src->mirrors, i)));
				else { /* even entries => integers */
					curr = GPOINTER_TO_INT (g_ptr_array_index(src->mirrors, i));
					g_ptr_array_add(dest->mirrors, GINT_TO_POINTER (curr));
				}
			}
		}
	}
	
#ifdef DEBUG
	for (i = 0; i < dest->mirrors->len; i += 2)
		DBUGOUT("URL: %s; weight: %d\n", (char*)g_ptr_array_index(dest->mirrors, i+1), GPOINTER_TO_INT (g_ptr_array_index(dest->mirrors, i)));
#endif /* DEBUG */

}

/**
 * Copy an AProgInfo \c src to \c dest.  If src or dest is NULL, the operation will be aborted.
 *
 * @arg dest is the AProgInfo struct to copy \em into.
 * @arg src is the AProgInfo struct to copy \em from.
 */
void
luau_copyProgInfo(AProgInfo *dest, const AProgInfo *src) {
	if (dest == NULL || src == NULL) { 
		DBUGOUT("Null pointer passed to luau_copyProgInfo");
	} else {
		dest->id = g_strdup(src->id);
		dest->shortname = g_strdup(src->shortname);
		dest->fullname = g_strdup(src->fullname);
		dest->desc = g_strdup(src->desc);
		dest->url = g_strdup(src->url);
		dest->version = g_strdup(src->version);
		dest->pkgVersion = g_strdup(src->pkgVersion);
		
		luau_copyInterface(&(dest->interface), &(src->interface));
		
		if (src->date != NULL) {
			dest->date = g_malloc(sizeof(ADate));
			luau_copyDate(dest->date, src->date);
		} else {
			dest->date = NULL;
		}
		if (src->keywords != NULL) {
			GContainer *srcCont, *destCont;
			dest->keywords = g_ptr_array_new();
			srcCont = g_container_new_from_data(GCONT_PTR_ARRAY, src->keywords);
			destCont = g_container_new_from_data(GCONT_PTR_ARRAY, dest->keywords);
			g_container_copy(destCont, srcCont);
			g_container_free(srcCont, FALSE);
			g_container_free(destCont, FALSE);
		} else {
			dest->keywords = NULL;
		}
	}
}

/**
 * Copy an AInterface \c src to \c dest.  If src or dest is NULL, the operation will be aborted.
 *
 * @arg dest is the AInterface struct to copy \em into.
 * @arg src is the AInterface struct to copy \em from.
 */
void
luau_copyInterface(AInterface *dest, const AInterface *src) {
	if (dest == NULL || src == NULL) {
		DBUGOUT("Null pointer passed to luau_copyInterface");
	} else {
		dest->major = src->major;
		dest->minor = src->minor;
	}
}

/**
 * Copy an ADate \c src to \c dest.  If src or dest is NULL, the operation will be aborted.
 *
 * @arg dest is the ADate struct to copy \em into.
 * @arg src is the ADate struct to copy \em from.
 */
void
luau_copyDate(ADate *dest, const ADate *src) {
	if (dest == NULL || src == NULL) {
		DBUGOUT("Null pointer passed to luau_copyDate");
	} else {
		dest->year  = src->year;
		dest->month = src->month;
		dest->day   = src->day;
	}
}

void
luau_copyQuants(GPtrArray *dest, const GPtrArray *src) {
	AQuantifier *quant;
	unsigned int i;
	
	for (i = 0; i < src->len; ++i) {
		quant = g_malloc(sizeof(AQuantifier));
		luau_copyQuant(quant, g_ptr_array_index(src, i));
		g_ptr_array_add(dest, quant);
	}
}

void
luau_copyQuant(AQuantifier *dest, const AQuantifier *src) {
	dest->qtype = src->qtype;
	dest->dtype = src->dtype;
	
	switch (dest->dtype) {
		case LUAU_QUANT_DATA_DATE:
			dest->data = g_malloc(sizeof(ADate));
			luau_copyDate(dest->data, src->data);
			break;

		case LUAU_QUANT_DATA_VERSION:
		case LUAU_QUANT_DATA_INTERFACE:
		case LUAU_QUANT_DATA_KEYWORD:
			dest->data = g_strdup(src->data);
			break;
		
		default:
			ERROR("Invalid Quantifier Data type - can't copy");
	}
}

/* Memory management */

/**
 * Free an update list, as returned by \ref luau_checkForUpdates
 *
 * @arg updates is the updates array to free
 */                                                   
void
luau_freeUpdateList(GList *updates) {
	GList *curr;
	
	if (updates != NULL) {
		for (curr = updates; curr != NULL; curr = curr->next) {
			luau_freeUpdateInfo(curr->data);
			g_free(curr->data);
		}
		
		g_list_free(updates);
	}
}

/**
 * Free an AProgInfo struct.
 * 
 * @arg ptr is the struct to free
 */
void
luau_freeProgInfo(AProgInfo *ptr) {
	if (ptr != NULL) {
		unsigned int i;

		nnull_g_free(ptr->id);
		nnull_g_free(ptr->shortname);
		nnull_g_free(ptr->fullname);
		nnull_g_free(ptr->desc);
		nnull_g_free(ptr->url);
		nnull_g_free(ptr->version);
		nnull_g_free(ptr->displayVersion);
		nnull_g_free(ptr->date);
		nnull_g_free(ptr->pkgVersion);
		if (ptr->keywords != NULL) {
			for (i = 0; i < ptr->keywords->len; ++i)
				g_free(g_ptr_array_index(ptr->keywords, i));
			g_ptr_array_free(ptr->keywords, TRUE);
		}
	} else {
		ERROR("Attempt to free NULL pointer");
	}
}

/**
 * Free an AUpdate struct
 *
 * @arg ptr is the struct to free
 */
void
luau_freeUpdateInfo(AUpdate *ptr) {
	APackage *pkg;
	AQuantifier *quant;
	unsigned int i;
	
	if (ptr != NULL) {
		nnull_g_free(ptr->id);
		nnull_g_free(ptr->date);
		nnull_g_free(ptr->shortDesc);
		nnull_g_free(ptr->fullDesc);
		nnull_g_free(ptr->newVersion);
		nnull_g_free(ptr->newURL);
		
		if (ptr->keywords != NULL) {
			for (i = 0; i < ptr->keywords->len; ++i)
				g_free(g_ptr_array_index(ptr->keywords, i));
			g_ptr_array_free(ptr->keywords, TRUE);
		}
		if (ptr->packages != NULL) {
			for (i = 0; i < ptr->packages->len; ++i) {
				pkg = g_ptr_array_index(ptr->packages, i);
				luau_freePkgInfo(pkg);
				g_free(pkg);
			}
			g_ptr_array_free(ptr->packages, TRUE);
		}
		if (ptr->quantifiers != NULL) {
			for (i = 0; i < ptr->quantifiers->len; ++i) {
				quant = g_ptr_array_index(ptr->quantifiers, i);
				g_free(quant->data);
				g_free(quant);
			}
			g_ptr_array_free(ptr->quantifiers, TRUE);
		}
	} else {
		DBUGOUT("Attempt to free NULL pointer");
	}
}

/**
 * Free an APackage struct.
 *
 * @arg ptr is the struct to free.
 */
void
luau_freePkgInfo(APackage *ptr) {
	unsigned int i;
	
	if (ptr != NULL) {
		if (ptr->mirrors != NULL) {
			for (i = 1; i < ptr->mirrors->len; i+=2)
				g_free(g_ptr_array_index(ptr->mirrors, i));
			g_ptr_array_free(ptr->mirrors, TRUE);
		}
	} else {
		DBUGOUT("Attempt to free NULL pointer");
	}
}


/* Non-Interface Methods */

/* compareAlphaNumeric <VERSION1> <VERSION2>
 * Returns: 1 or 0.
 *
 * Compare two strings and return 1 if VERSION1 > VERSION2, otherwise 0.
 * Otherwise, gathers digits forward to compare full numbers.
 */

static int
compareAlphaNumeric(const char *v1, const char *v2) {
	int i, len1, len2, limit, result = 0;
	
	if (v1 == NULL)
		v1 = "";
	if (v2 == NULL)
		v2 = "";
	
	/* get length of the longest string to index with over as $limit */
	len1 = strlen(v1);
	len2 = strlen(v2);
	if (len1 > len2)
		limit = len1;
	else
		limit = len2;
	
	for (i = 0; i < limit; ++i) {
		char char1, char2;
		int val1, val2;
		
		/* compare character by character indexing up the string */
		if (i < len1)
			char1 = v1[i];
		else
			char1 = '\0';
		
		if (i < len2)
			char2 = v2[i];
		else
			char2 = '\0';
		
		/* special case: point release is higher than alphabetic release
		 * example: REQUIRED 2.5-pre3 < CURRENT 2.5 or REQUIRED alpha-char < CURRENT no char */
		if (! isdigit(char1) && char2 == '\0')
			return -1;

		/* look forward to find next index digit to complete the full number */
		if (isdigit(v1[i]))
			val1 = atoi(v1 + i);
		else if (char1 == '\0')
			val1 = 0;
		else
			val1 = -1;
		
		if (isdigit(v2[i]))
			val2 = atoi(v2 + i);
		else if (char2 == '\0')
			val2 = 0;
		else
			val2 = -1;

		/* if comparing numbers do an integer compare - otherwise do a string compare */
		if (val1 != -1 || val2 != -1) {
			if (val1 > val2) {
				result = 1;
				break;
			} else if (val1 < val2) {
				result = -1;
				break;
			} else if (val1 >= 10) {
				/* Skip over digits of number we just checked */
				i += (int)log10(val1);
			}
		} else if (char1 < char2) {
			result = -1;
			break;
		} else if (char1 > char2) {
			result = 1;
			break;
		}
	}
	
	return result;
}


/**
 * Take an updates array and apply the appropriate internal keywords ("_hidden" and/or "_incompatible")
 * to them.
 *
 * @arg updates is the updates array to categorize
 * @arg progInfo describes the program the updates are for
 */
static void
categorizeUpdates(GContainer *updates, const AProgInfo *progInfo) {
	GIterator iter;
	AUpdate *curr;
	
	if (updates == NULL) {
		ERROR("NULL pointer passed to categorizeUpdates");
		return;
	}
	
	g_container_get_iter(&iter, updates);
	while (g_iterator_hasNext(&iter)) {
		curr = g_iterator_next(&iter);
		categorizeUpdate(curr, progInfo);
	}
}

static void
categorizeUpdate(AUpdate *update, const AProgInfo *progInfo) {
	if (isIncompatible(update, progInfo))
		luau_setKeyword(update->keywords, "_incompatible");
	if (isOld(update, progInfo))
		luau_setKeyword(update->keywords, "_old");
}

/**
 * Check if an update for the specified program is incompatible with the
 * program in question.
 *
 * @arg update is the update to check
 * @arg progInfo is the program version to check against
 */
static gboolean
isIncompatible(AUpdate *update, const AProgInfo *progInfo) {
	AQuantifier *curr;
	gboolean compatible = TRUE;
	unsigned int i;
	
	if (update->quantifiers == NULL)
		compatible = TRUE;
	else {
		for (i = 0; i < update->quantifiers->len; ++i) {
			curr = g_ptr_array_index(update->quantifiers, i);
			
			if (! luau_satisfiesQuant(curr, progInfo)) {
				compatible = FALSE;
				break;
			}
		}
	}
	
	return (!compatible);
}

static gboolean
isOld(AUpdate *update, const AProgInfo *progInfo) {
	gboolean result;
	int ret;
	
	if (update->newVersion == NULL || progInfo->version == NULL)
	{
		result = FALSE;
	}
	else
	{
		ret = luau_versioncmp(update->newVersion, progInfo->version);
		if (ret == 0 && progInfo->pkgVersion != NULL)
		{
			char *newPkgVersion = luau_getMostRecentPkgVersion(update->packages);
			ret = luau_versioncmp(newPkgVersion, progInfo->pkgVersion);
		}
		result = (ret != 1);
	}
	
	return result;
}

char *
luau_getMostRecentPkgVersion(GPtrArray *packages)
{
	APackage *pkg;
	char *mostRecent;
	int i;
	mostRecent = "0";
	for (i = 0; i < packages->len; ++i)
	{
		pkg = g_ptr_array_index(packages, i);
		if (luau_versioncmp(pkg->version, mostRecent) > 0)
		{
			mostRecent = pkg->version;
		}
	}
	return mostRecent;
}



