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

#include "libuau.h"
#include "test.h"
#include "util.h"
#include "gcontainer.h"

#ifdef WITH_LEAKBUG
#  include <leakbug.h>
#endif

#ifdef WITH_DMALLOC
#  include <dmalloc.h>
#endif

static gboolean testVersionCompare(void);
static gboolean testDateCompare(void);
static gboolean testInterfaceCompat(void);
static gboolean testToString(void);
static gboolean testFromString(void);
static gboolean testGContainer(void);

static ADate* setDate(ADate *date, int month, int day, int year);
static AInterface* setInterf(AInterface *interf, int major, int minor);

int
main(int argc, char *argv[]) {
	gboolean result = TRUE;
	
	printf("Luau %d.%d.%d Testing Suite\n\n", LUAU_VERSION_MAJOR, LUAU_VERSION_MINOR, LUAU_VERSION_PATCH);
	
	result = testVersionCompare();
	result = testDateCompare()     && result;
	result = testInterfaceCompat() && result;
	result = testToString()        && result;
	result = testFromString()      && result;
	result = testGContainer()      && result;
	
	if (result == TRUE)
		printf("All tests in all categories were successful.\n\n");
	else
		printf("Some tests FAILED.\n\n");

#ifdef WITH_LEAKBUG
	lbDumpLeaks();
#endif
	
	return (result != TRUE);
}

static gboolean
testVersionCompare(void) {
	gboolean result;
	
	printf("Version Comparison Tests\n");
	printf("------------------------\n");
	
	result = testInt( "Version #1",   0, luau_versioncmp("0.1.5b",   "0.1.5b")   );
	result = testInt( "Version #2",  -1, luau_versioncmp("1",        "1.2")      ) && result;
	result = testInt( "Version #3",   1, luau_versioncmp("1.2",      "1")        ) && result;
	result = testInt( "Version #4",   1, luau_versioncmp("1.3",      "1.2")      ) && result;
	result = testInt( "Version #5",   0, luau_versioncmp("1.2",      "1.2")      ) && result;
	result = testInt( "Version #6",   0, luau_versioncmp("1.2b",     "1.2b")     ) && result;
	result = testInt( "Version #7",  -1, luau_versioncmp("2.5-pre3", "2.5")      ) && result;
	result = testInt( "Version #8",   1, luau_versioncmp("2.5-pre3", "2.5-pre2") ) && result;
	result = testInt( "Version #9",   1, luau_versioncmp("2-RC10f",  "2-rc2d")   ) && result;
	result = testInt( "Version #10", -1, luau_versioncmp("3.1-RC3",  "3.1-rc12") ) && result;
	result = testInt( "Version #11",  1, luau_versioncmp("1.3",      "0.1.5")    ) && result;
	result = testInt( "Version #12", -1, luau_versioncmp("1.99.6",   "2")        ) && result;
	result = testInt( "Version #13",  0, luau_versioncmp("1.6.x",    "1.6.7")    ) && result;
	result = testInt( "Version #14",  0, luau_versioncmp("1.6.x",    "1.6")      ) && result;
	result = testInt( "Version #15",  1, luau_versioncmp("1.6.x",    "1.5.7")    ) && result;
	result = testInt( "Version #16",  0, luau_versioncmp("1.x",      "1.5.7")    ) && result;
	result = testInt( "Version #17",  0, luau_versioncmp("1.2.5",    "1.x")      ) && result;
	result = testInt( "Version #18",  1, luau_versioncmp("1.2.5b",   "1.2.3")    ) && result;
	result = testInt( "Version #19", -1, luau_versioncmp("2.0",      "2.2b")     ) && result;
	result = testInt( "Version #20",  1, luau_versioncmp("2.0",      "2.0.0b")   ) && result;
	result = testInt( "Version #20", -1, luau_versioncmp("2.0",      "2.0.4b")   ) && result;
	
	if (result)
		printf("All tests passed.\n\n");
	else
		printf("Some tests failed.\n\n");
	
	return result;
}

static gboolean
testDateCompare(void) {
	ADate date1, date2;
	gboolean result;
	
	printf("Date Comparison Tests\n");
	printf("---------------------\n");
	
	result = testInt( "Date #1",  0, luau_datecmp( setDate(&date1, 12, 22, 1998), setDate(&date2, 12, 22, 1998) ) );
	result = testInt( "Date #2",  1, luau_datecmp( setDate(&date1, 12, 22, 1999), setDate(&date2, 12, 22, 1998) ) ) && result;
	result = testInt( "Date #3", -1, luau_datecmp( setDate(&date1, 12, 22, 1997), setDate(&date2, 12, 22, 1998) ) ) && result;
	result = testInt( "Date #4", -1, luau_datecmp( setDate(&date1, 12, 22, 1997), setDate(&date2, 12, 25, 1997) ) ) && result;
	result = testInt( "Date #4",  1, luau_datecmp( setDate(&date1, 12, 25, 1997), setDate(&date2, 12, 10, 1997) ) ) && result;
	result = testInt( "Date #4",  1, luau_datecmp( setDate(&date1, 12, 22, 1997), setDate(&date2, 11, 22, 1997) ) ) && result;
	result = testInt( "Date #4", -1, luau_datecmp( setDate(&date1, 5,  22, 1997), setDate(&date2,  7, 22, 1997) ) ) && result;
	
	if (result)
		printf("All tests passed.\n\n");
	else
		printf("Some tests failed.\n\n");
	
	return result;
}

static gboolean
testInterfaceCompat(void) {
	AInterface int1, int2;
	gboolean result;
	
	printf("Interface Satisfaction Tests\n");
	printf("----------------------------\n");
	
	result = testBool( "Interface #1",  TRUE, luau_satisfiesInterface( setInterf(&int1, 2, 0), setInterf(&int2, 2, 0) ) );
	result = testBool( "Interface #2",  TRUE, luau_satisfiesInterface( setInterf(&int1, 2, 2), setInterf(&int2, 2, 0) ) ) && result;
	result = testBool( "Interface #3", FALSE, luau_satisfiesInterface( setInterf(&int1, 3, 2), setInterf(&int2, 3, 5) ) ) && result;
	result = testBool( "Interface #4", FALSE, luau_satisfiesInterface( setInterf(&int1, 2, 5), setInterf(&int2, 3, 2) ) ) && result;
	
	if (result)
		printf("All tests passed.\n\n");
	else
		printf("Some tests failed.\n\n");
	
	return result;
}

static gboolean
testToString(void) {
	AInterface interf;
	ADate date;
	gboolean result;
	char *str;
	
	printf("To String Conversion Tests\n");
	printf("--------------------------\n");
	
	setDate(&date, 2, 25, 2002);
	str = luau_dateString(&date);
	result = testStr( "Date Conversion", "02/25/2002", str);
	g_free(str);
	
	setInterf(&interf, 4, 25);
	str = luau_interfaceString(&interf);
	result = testStr( "Interface Conversion #1", "4.25", str);
	g_free(str);
	
	setInterf(&interf, -1, -1);
	str = luau_interfaceString(&interf);
	result = testStr( "Interface Conversion #2", "", str);
	g_free(str);
	
	str = luau_multPackageTypeString(LUAU_RPM | LUAU_AUTOPKG);
	result = testStr( "Package Type #1", "RPM AUTOPKG", str);
	g_free(str);
	
	str = luau_multPackageTypeString(0);
	result = testStr( "Package Type #2", "(none)", str);
	g_free(str);
	
	result = testStr( "Package Type #3", "UNKNOWN", luau_packageTypeString(LUAU_RPM | LUAU_AUTOPKG) );
	
	if (result)
		printf("All tests passed.\n\n");
	else
		printf("Some tests failed.\n\n");
	
	return result;
}

static gboolean
testFromString(void) {
	AInterface interf;
	ADate date1, date2;
	gboolean result, ret;
	
	printf("From String Conversion Tests\n");
	printf("----------------------------\n");
	
	ret = luau_parseDate(&date1, "02/25/2003");
	result = testBool( "Date Parse #1", TRUE, ret );
	result = testInt ( "Date Parse #2", 0, luau_datecmp(&date1, setDate(&date2, 2, 25, 2003)) ) && result;
	
	/*ret = luau_parseDate(&date1, "12/23/2003/12")   && result;
	result = testBool( "Date Parse #3", FALSE, ret) && result;*/
	
	/*ret = luau_parseDate(&date1, "12-23-2003")      && result;
	result = testBool( "Date Parse #4", FALSE, ret) && result;*/
	
	ret = luau_parseInterface(&interf, "3.2");
	result = testBool( "Interface Parse #1", TRUE, ret ) && result;
	result = testBool( "Interface Parse #2", TRUE, (interf.major == 3 && interf.minor == 2) ) && result;
	
	ret = luau_parseInterface(&interf, "");
	result = testBool( "Interface Parse #3", TRUE, ret ) && result;
	result = testBool( "Interface Parse #4", TRUE, (interf.major == -1 && interf.minor == -1) ) && result;
	
	if (result)
		printf("All tests passed.\n\n");
	else
		printf("Some tests failed.\n\n");
	
	return result;
}

static gboolean
testGContainer(void) {
	GContainer *container;
	GIterator iter;
	gboolean result;
	
	printf("GContainer Tests\n");
	printf("----------------\n");
	
	container = g_container_new(GCONT_PTR_ARRAY);
	g_container_add(container, "string1");
	result = testStr( "Pointer Array Add", "string1", ((GPtrArray*)container->data)->pdata[0] );
	
	g_container_add(container, "string2");
	result = testStr( "Pointer Array Index", "string2", g_container_index(container, 1) ) && result;
	
	g_container_get_iter(&iter, container);
	result = testBool( "Pointer Array Iterator #1",  TRUE,      g_iterator_hasNext(&iter) ) && result;
	result = testBool( "Pointer Array Iterator #2",  FALSE,     g_iterator_hasPrev(&iter) ) && result;
	result = testStr ( "Pointer Array Iterator #3",  "string1", g_iterator_next(&iter)    ) && result;
	result = testBool( "Pointer Array Iterator #4",  TRUE,      g_iterator_hasNext(&iter) ) && result;
	result = testStr ( "Pointer Array Iterator #5",  "string2", g_iterator_next(&iter)    ) && result;
	result = testBool( "Pointer Array Iterator #6",  FALSE,     g_iterator_hasNext(&iter) ) && result;
	result = testStr ( "Pointer Array Iterator #7",  "string2", g_iterator_prev(&iter)    ) && result;
	result = testBool( "Pointer Array Iterator #8",  TRUE,      g_iterator_hasNext(&iter) ) && result;
	result = testStr ( "Pointer Array Iterator #9",  "string1", g_iterator_prev(&iter)    ) && result;
	result = testBool( "Pointer Array Iterator #10", FALSE,     g_iterator_hasPrev(&iter) ) && result;
	result = testStr ( "Pointer Array Iterator #11", "string1", g_iterator_next(&iter)    ) && result;
	
	g_container_get_iter_last(&iter, container);
	result = testBool( "Pointer Array Reverse Iter #1", FALSE,     g_iterator_hasNext(&iter) ) && result;
	result = testBool( "Pointer Array Reverse Iter #2", TRUE,      g_iterator_hasPrev(&iter) ) && result;
	result = testStr ( "Pointer Array Reverse Iter #3", "string2", g_iterator_prev(&iter)    ) && result;
	result = testStr ( "Pointer Array Reverse Iter #4", "string1", g_iterator_prev(&iter)    ) && result;
	result = testBool( "Pointer Array Reverse Iter #5", TRUE,      g_iterator_hasNext(&iter) ) && result;
	result = testBool( "Pointer Array Reverse Iter #6", FALSE,     g_iterator_hasPrev(&iter) ) && result;
	
	g_container_convert(GCONT_LIST, container);
	result = testStr( "->List Convert #1", "string1", g_container_index(container, 0) ) && result;
	result = testStr( "->List Convert #2", "string2", g_container_index(container, 1) ) && result;
	
	g_container_get_iter(&iter, container);	
	result = testBool( "List Iterator #1",  TRUE,      g_iterator_hasNext(&iter) ) && result;
	result = testBool( "List Iterator #2",  FALSE,     g_iterator_hasPrev(&iter) ) && result;
	result = testStr ( "List Iterator #3",  "string1", g_iterator_next(&iter)    ) && result;
	result = testBool( "List Iterator #4",  TRUE,      g_iterator_hasNext(&iter) ) && result;
	result = testStr ( "List Iterator #5",  "string2", g_iterator_next(&iter)    ) && result;
	result = testBool( "List Iterator #6",  FALSE,     g_iterator_hasNext(&iter) ) && result;
	result = testStr ( "List Iterator #7",  "string2", g_iterator_prev(&iter)    ) && result;
	result = testBool( "List Iterator #8",  TRUE,      g_iterator_hasNext(&iter) ) && result;
	result = testStr ( "List Iterator #9",  "string1", g_iterator_prev(&iter)    ) && result;
	result = testBool( "List Iterator #10", FALSE,     g_iterator_hasPrev(&iter) ) && result;
	result = testStr ( "List Iterator #11", "string1", g_iterator_next(&iter)    ) && result;
	
	g_container_get_iter_last(&iter, container);
	result = testBool( "List Reverse Iter #1", FALSE,     g_iterator_hasNext(&iter) ) && result;
	result = testBool( "List Reverse Iter #2", TRUE,      g_iterator_hasPrev(&iter) ) && result;
	result = testStr ( "List Reverse Iter #3", "string2", g_iterator_prev(&iter)    ) && result;
	result = testStr ( "List Reverse Iter #4", "string1", g_iterator_prev(&iter)    ) && result;
	result = testBool( "List Reverse Iter #5", TRUE,      g_iterator_hasNext(&iter) ) && result;
	result = testBool( "List Reverse Iter #6", FALSE,     g_iterator_hasPrev(&iter) ) && result;
	
	g_container_convert(GCONT_SLIST, container);
	result = testStr( "->SList Convert #1", "string1", g_container_index(container, 0) ) && result;
	result = testStr( "->SList Convert #2", "string2", g_container_index(container, 1) ) && result;
	
	g_container_get_iter(&iter, container);	
	result = testBool( "List Iterator #1",  TRUE,      g_iterator_hasNext(&iter) ) && result;
	result = testStr ( "List Iterator #3",  "string1", g_iterator_next(&iter)    ) && result;
	result = testBool( "List Iterator #4",  TRUE,      g_iterator_hasNext(&iter) ) && result;
	result = testStr ( "List Iterator #5",  "string2", g_iterator_next(&iter)    ) && result;
	result = testBool( "List Iterator #6",  FALSE,     g_iterator_hasNext(&iter) ) && result;
	
	g_container_free(container, TRUE);
	
	if (result)
		printf("All tests passed.\n\n");
	else
		printf("Some tests failed.\n\n");
	
	return result;
}

static ADate *
setDate(ADate *date, int month, int day, int year) {
	date->day = day;
	date->month = month;
	date->year = year;
	return date;
}

static AInterface *
setInterf(AInterface *interf, int major, int minor) {
	interf->major = major;
	interf->minor = minor;
	return interf;
}
