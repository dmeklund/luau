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

#include <glib.h>

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifdef __unix__
#  include <sys/wait.h>
#elif defined(_MSC_VER)
#  define WEXITSTATUS(n) n
#endif

#include "util.h"
#include "error.h"
#include "libuau.h"

#include "install.h"
#ifdef WITH_DMALLOC
#  include <dmalloc.h>
#endif

gboolean
luau_install_rpm(const char *filename, GError **err) {
	int ret;
	char *execString;
	gboolean result;
	
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

	execString = lutil_mprintf("rpm -U --nodeps %s", filename);
	
	ret = system(execString);
	
	if (ret == -1) {
		g_set_error(err, LUAU_INSTALL_ERROR, LUAU_INSTALL_ERROR_FAILED, "Couldn't execute command: %s", execString);
		result = FALSE;
	} else if (WEXITSTATUS(ret) == 127) {
		g_set_error(err, LUAU_INSTALL_ERROR, LUAU_INSTALL_ERROR_FAILED, "Couldn't install rpm: 'rpm' command not found by shell");
		result = FALSE;
	} else if (WEXITSTATUS(ret) != 0) {
		g_set_error(err, LUAU_INSTALL_ERROR, LUAU_INSTALL_ERROR_FAILED, "RPM Command failed: %s", execString);
		result = FALSE;
	} else {
		result = TRUE;
	}
	
	g_free(execString);
		
	return result;
}

gboolean luau_install_deb(const char *filename, GError **err) {
	int ret;
	char *execString;
	gboolean result;
	
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
	
	execString = lutil_mprintf("dpkg -i %s", filename);
	
	ret = system(execString);
	
	if (ret == -1) {
		g_set_error(err, LUAU_INSTALL_ERROR, LUAU_INSTALL_ERROR_FAILED, "Couldn't execute command: %s", execString);
		result = FALSE;
	} else if (WEXITSTATUS(ret) == 127) {
		g_set_error(err, LUAU_INSTALL_ERROR, LUAU_INSTALL_ERROR_FAILED, "Couldn't install deb package: 'dpkg' command not found by shell");
		result = FALSE;
	} else if (WEXITSTATUS(ret) != 0) {
		g_set_error(err, LUAU_INSTALL_ERROR, LUAU_INSTALL_ERROR_FAILED, "dpkg Command failed: %s", execString);
		result = FALSE;
	} else {
		result = TRUE;
	}
	
	g_free(execString);
	
	return result;
}

gboolean luau_install_src(const char *filename, GError **err) {
	g_set_error(err, LUAU_INSTALL_ERROR, LUAU_INSTALL_ERROR_INVALID_ARG, "Source packages cannot be installed.  You must download and install them by hand");
	return FALSE;
}

gboolean luau_install_exec(const char *filename, GError **err) {
	int ret;
	gboolean result;

	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
	
	ret = system(filename);
	
	if (ret == -1) {
		g_set_error(err, LUAU_INSTALL_ERROR, LUAU_INSTALL_ERROR_FAILED, "Couldn't execute command: %s", filename);
		result = FALSE;
	} else if (WEXITSTATUS(ret) == 127) {
		g_set_error(err, LUAU_INSTALL_ERROR, LUAU_INSTALL_ERROR_INVALID_ARG, "Execution failed: could not find specified file");
		result = FALSE;
	} else if (WEXITSTATUS(ret) != 0) {
		g_set_error(err, LUAU_INSTALL_ERROR, LUAU_INSTALL_ERROR_FAILED, "Execution failed.  See STDOUT for details");
		result = FALSE;
	} else {
		result = TRUE;
	}
	
	return result;
}

gboolean luau_install_autopkg(const char *filename, GError **err) {
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

	return luau_install_exec(filename, err);
}
