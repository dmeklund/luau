/**
 * libautoupdate 0.1.0
 * - Test Suite -
 *
 * Parser Tester (testparse.c): Test the parsing methods.
 * 
 * (License info goes here)
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <glib.h>

#include "libuau.h"
#include "parseupdates.h"
#include "util.h"

void printUpdate(AUpdate *update);

int
main(int argc, char *argv[]) {
	/* Taken from updates.example */
	char *string = "
UPDATE SOFTWARE
	ID 101
	SHORT Upgrade to myproject version 1.1.1
	LONG
Version 1.1.1 supports CoolStuff extensions and drops legacy support for pre-1.0 versions.
Many bug fixes have also been made, including:
  o Preventing myproject from crashing when user inputs curse words
  o No more gamma radiation problems (hopefully)
!!! NOTE !!!
INSTALLING VERSION 1.1.1 WILL DROP COMPATIBILITY WITH BINARIES COMPILED WITH PRE-1.0 VERSIONS!
If this is a problem, please stick with the 1.0 branch
	ENDLONG
	PACKAGES
		RPM 1002453 95dbc2411471aab385ad4adf18a8b5b7 ftp://ftp.myserver.com/pub/myproject-1.1.1/binaries/myproject-1.1.1.rpm
		DEB 1432534 98336d9f8cb04bc909a9a2a13a9e893c ftp://ftp.myserver.com/pub/myproject-1.1.1/binaries/myproject-1.1.1.deb
		SRC 6323511 717ab9d5dfcb8900ca14182dc0a0dbb2 ftp://ftp.myserver.com/pub/myproject-1.1.1/src/myproject-1.1.1.tar.gz
	ENDPACKAGES
	VALIDFROM 02/05/2003
	VALIDTO 05/15/2003
	SETDATE 05/15/2003
ENDUPDATE

UPDATE MESSAGE
	ID 201
	SHORT Artists needed!
	DATE 04/14/2003
	LONG
myproject is looking for some talented artists to help with the newly planned
action simulation adventure game, 'MyProject: The GAME.'  If you're interested, please
contact jimbo@myserver.com
	ENDLONG
ENDUPDATE

UPDATE LIBUPDATE
	ID 301
	DATE 04/14/2003
	SHORT New server
	LONG
We've got our own ftp server now, so we're trying to get everyone to switch over to
using the myserver.com database.  Please accept this update, since the old ftp server
will be unavailable in a month or so, and trying to autoupdate from it after that will fail.
Thanks. :)
	ENDLONG
	SETSERVER ftp.myserver.com
	SETDIR /pub/myproject/
ENDUPDATE
";

	GPtrArray *results;
	int i;
	
	results = luau_parseUpdateFile(string);
	
	printf("Number of updates: %d (should be 3)\n", results->len);
	for (i = 0; i < results->len; ++i) {
		printf("Update #%d:\n", i+1);
		printUpdate(g_ptr_array_index(results, i));
		printf("\n");
	}
	
	return 0;
}

void
printUpdate(AUpdate *update) {
	printf("  ID: %s\n", update->id);
	printf("  Update type: %d\n", update->type);
	printf("  Date: %s\n", luau_dateString(update->date));
	printf("  Short Description: %s\n", update->shortDesc);
	printf("  Long Description:\n%s\n", update->fullDesc);
	if (update->type == LUAU_SOFTWARE) {
		printf("Software Extras:\n");
		printf("  Available Formats: %d\n", update->availableFormats);
		printf("  Number of packages: %d\n", update->packages->len);
		printf("  Valid From: %s\n", luau_dateString(update->validFrom));
		printf("  Valid To: %s\n", luau_dateString(update->validTo));
	} else if (update->type == LUAU_LIBUPDATE) {
		printf("LibUpdate Extras:\n");
		printf("  New Updates Location: %s\n", update->newURL);
	}
}
