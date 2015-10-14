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

#include <stdio.h>
#include <glib.h>

#include "test.h"
#include "util.h"

static int counter = 0;
static int failed = 0;

gboolean
testInt(const char *name, int expected, int result) {
	++counter;
	if (expected == result) {
		/*printf("Test %d: %s: PASS\n", counter, name);*/
		return TRUE;
	} else {
		++failed;
		printf("Test %d: %s: !!! FAILED !!!\n", counter, name);
		printf("         Expected %d, got %d\n", expected, result);
		return FALSE;
	}
}

gboolean
testStr(const char *name, const char *expected, const char *result) {
	++counter;
	if (lutil_streq(expected, result)) {
		/*printf("Test %d: %s: PASS\n", counter, name);*/
		return TRUE;
	} else {
		++failed;
		printf("Test %d: %s: !!! FAILED !!!\n", counter, name);
		printf("         Expected %s, got %s\n", expected, result);
		return FALSE;
	}
}

int
testsFailed(void) {
	return failed;
}

int testsPassed(void) {
	return counter - failed;
}

int
testsTotal(void) {
	return counter;
}

