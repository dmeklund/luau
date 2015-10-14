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

#ifdef __unix__
#  include <unistd.h>
#endif

#include "libuau.h"
#include "network.h"
#include "ftp.h"
#include "util.h"
#include "error.h"
#include "parseupdates.h"
#include "md5.h"

#ifdef WITH_DMALLOC
#  include <dmalloc.h>
#endif

/**
 * Downloads the update file from the luau server for the given program and parses
 * it to read in the updates listed.  Note that it returns an array of <b>all</b>
 * the updates listed, including ones that have already been installed.
 *
 * @arg <i>info</i> is a struct describing the program updates are wanted for.
 * @return a GPtrArray of updates
 */
GContainer *
luau_net_queryServer(const AProgInfo *info, GError **err) {
	GContainer *updates;
	GString *contents, *temp;
	int len;
	
	g_return_val_if_fail(err == NULL || *err == NULL, NULL);
	
	if (info->url == NULL) {
		g_set_error(err, LUAU_NET_ERROR, LUAU_NET_ERROR_INVALID_ARG, "Can't check for updates: no URL specified");
		return NULL;
	}
	contents = lutil_ftp_getURL(info->url, err);
	if (contents == NULL) {
		g_assert(err == NULL || *err != NULL);
		return NULL;
	}
	
	len = strlen(info->url);
	if (len > 3 && lutil_streq(info->url+len-3, ".gz")) {
		DBUGOUT("Updates file is compressed: uncompressing");
		temp = lutil_uncompress(contents, err);
		g_string_free(contents, TRUE);
		if (temp == NULL) {
			g_assert(err == NULL || *err != NULL);
			return NULL;
		}
		contents = temp;
	}
	
	updates = luau_parseXML_updates(contents->str, err);
	g_string_free(contents, TRUE);
	
	if (updates == NULL) {
		g_assert(err == NULL || *err != NULL);
		return NULL;
	}
	
	return updates;
}

/**
 * Download the specified update to <tt>downloadTo</tt>, or do a temporary location if <tt>downloadTo == NULL</tt>
 * @warning Even if downloadTo is specified, this function returns a newly allocated string that must be <tt>free</tt>'d!
 * 
 * @arg <i>info</i> describes the program we're downloading updates for.
 * @arg <i>update</i> describes the update we're downloading.
 * @arg <i>pkgType</i> is the package type we want (RPM, .deb, ...)
 * @arg <i>downloadTo</i> is where we'd like to download the file, or NULL if a temporary location is desired.
 * @return whether the download was successful
 */
gboolean
luau_net_downloadUpdate(const AProgInfo* info, const AUpdate *update, APkgType pkgType, const char* downloadTo, GError **err) {
	char md5[33], *loc;
	APackage *package;
	gboolean loopAgain, result;
	int choice;
	
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
	
	md5[0] = '\0';
	
	package = luau_getUpdatePackage(update, pkgType, err);
	
	if (package == NULL) {
		g_assert(err == NULL || *err != NULL);
		return FALSE;
	} else if (package->mirrors == NULL || package->mirrors->len == 0) {
		g_set_error(err, LUAU_NET_ERROR, LUAU_NET_ERROR_INVALID_ARG, "Couldn't find filename of specified type for this update");
		return FALSE;
	}
	
	loc = luau_getPackageURL(package, err);
	if (loc == NULL) {
		g_assert(err == NULL || *err != NULL);
		return FALSE;
	}
	
	result = lutil_ftp_downloadToFile(loc, downloadTo, err);
	if (result == FALSE) {
		g_assert(err == NULL || *err != NULL);
		return FALSE;
	}
	
	if (package->md5sum[0] != '\0') {
		lutil_md5_file(downloadTo, md5, err);
		if (md5[0] == '\0') {
			g_assert(err == NULL || *err != NULL);
			return FALSE;
		}
		
		result = FALSE;
		if (! lutil_streq(md5, package->md5sum)) {
			while (1) {
				loopAgain = FALSE;
				choice = lutil_error_prompt("MD5 Sum Mismatch", "The computed md5 sum of the downloaded file does not match the expected value", 3, 2, "Re-Download", "Use Anyway", "Cancel");
				switch (choice) {
					case 0: /* Re-Download */
						unlink(downloadTo);
						result = luau_net_downloadUpdate(info, update, pkgType, downloadTo, err);
						if (result == FALSE) {
							g_assert(err == NULL || *err != NULL);
							return FALSE;
						}
						break;
					case 1: /* Use Anyway */
						result = TRUE;
						break;
					case 2: /* Cancel */
						g_set_error(err, LUAU_NET_ERROR, LUAU_NET_ERROR_ABORTED, "MD5 sum mismatch (found %s, expected %s): aborted", md5, package->md5sum);
						result = FALSE;
						unlink(downloadTo);
						break;
					default:
						loopAgain = TRUE;
				}
				
				if (!loopAgain)
					break;
			}
		} else {
			/* MD5 matched expected */
			result = TRUE;
		}
	} else {
		/* MD5 not checked because none given */
		result = TRUE;
	}
	
	return result;
}


