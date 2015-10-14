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
#include <getopt.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>

#ifdef __unix__
#  include <unistd.h>
#elif defined(_MSC_VER)
#  include <direct.h>
#endif

#include <glib.h>

#ifndef PATH_MAX
#  define PATH_MAX MAXPATHLEN
#endif

#include "libuau.h"
#include "libuau-db.h"
#include "database.h"
#include "util.h"

#ifdef WITH_DMALLOC
#  include <dmalloc.h>
#endif


static void printUsage(void);
static struct option* getLongOptions();
static char* createFileURL(const char *loc);

int
main(int argc, char *argv[]) {
	AProgInfo info;
	GContainer *keywords = NULL;
	GError *err = NULL;
	struct option *longOptions = getLongOptions();
	gboolean remove = FALSE, result;
	char *url = NULL, *version = NULL, *shortname = NULL, *fullname = NULL, *desc = NULL, *program = NULL;
	char *xmlFile = NULL, *xmlURL = NULL;
	ADate *date = NULL;
	AInterface interface = {-1, -1};
	int c, ret = 0;
	
	if (argc == 1) {
		printUsage();
		g_free(longOptions);
		exit(0);
	}
	
	while ((c = getopt_long(argc, argv, ":u:d:k:v:i:s:n:f:e:l:hr", longOptions, NULL)) != -1) {
		switch (c) {
			case 'u':
				url = g_strdup(optarg);
				break;
			case 'd':
				date = g_malloc(sizeof(ADate));
				luau_parseDate(date, optarg);
				break;
			case 'k':
				keywords = lutil_gsplit(",", optarg);
				break;
			case 'i':
				luau_parseInterface(&interface, optarg);
				break;
			case 'v':
				version = g_strdup(optarg);
				break;
			case 'n':
				shortname = g_strdup(optarg);
				break;
			case 'f':
				fullname = g_strdup(optarg);
				break;
			case 's':
				desc = g_strdup(optarg);
				break;
			case 'e':
				xmlFile = g_strdup(optarg);
				break;
			case 'l':
				xmlURL = g_strdup(optarg);
			case 'h':
				printUsage();
				g_free(longOptions);
				exit(0);
			case '?':
				printf("Invalid option: -%c\n", optopt);
				printUsage();
				g_free(longOptions);
				exit(1);
			case 'r':
				remove = TRUE;
				break;
			case ':':
				printf("Argument needed for -%c\n", optopt);
				printUsage();
				g_free(longOptions);
				exit(1);
			default:
				printf("Unknown error: got character code 0%o\n", c);
				printUsage();
				g_free(longOptions);
				exit(1);
		}
	}
	
	g_free(longOptions);
	
	if (argc == optind+1 || xmlURL != NULL || xmlFile != NULL) {
		if (argc == optind+1)
			program = argv[optind];
		
		if (remove) {
			result = luau_db_getProgInfo(&info, program, &err);
			if (result == TRUE) {
				luau_db_deleteApp(&info);
				luau_freeProgInfo(&info);
			} else {
				fprintf(stderr, "ERROR: couldn't retrieve program info for %s: %s\n", program, err->message);
				g_error_free(err);
				ret = 1;
			}
		} else {
			memset(&info, 0, sizeof(AProgInfo));
			
			if (xmlFile != NULL) {
				xmlURL = createFileURL(xmlFile);
				g_free(xmlFile);
			}
			if (xmlURL != NULL) {
				luau_getProgInfoFromXML_url(&info, xmlURL, &err);
				g_free(xmlURL);
				if (err != NULL) {
					fprintf(stderr, "ERROR: Couldn't retrieve information from given URL (%s): %s", xmlURL, err->message);
					g_error_free(err);
					exit(EXIT_FAILURE);
				}
			}
			
			/* Override values read in from file/URL if specified on command line */
			
			if (program != NULL) info.id = program;
			if (fullname != NULL) info.fullname = fullname;
			if (shortname != NULL) info.shortname = shortname;
			if (desc != NULL) info.desc = desc;
			if (version != NULL) info.version = version;
			if (date != NULL) info.date = date;
			if (url != NULL) info.url = url;
			if (keywords != NULL) info.keywords = keywords->data;
			if (interface.minor != -1 && interface.major != -1)
				luau_copyInterface(&(info.interface), &interface);
			
			printf("Registering %s ... \n", info.id);
			result = luau_db_registerNewApp(&info, &err);
			if (result == FALSE) {
				g_assert(err != NULL);
				fprintf(stderr, "ERROR: Couldn't register application %s: %s", program, err->message);
				g_error_free(err);
				err = NULL;
				ret = 1;
			} else {
				luau_db_create("updates_hidden", program);
				printf("Done.\n");
			}
		}
	} else {
		if (argc == optind)
			printf("ERROR: No program name specified\n");
		else /* argc > optind + 1 */
			printf("ERROR: Too many arguments\n");
		
		printUsage();
		ret = 1;
	}
	
	if (keywords != NULL)
		g_container_destroy(keywords);
	
	g_free(url);
	g_free(date);
	g_free(version);
	g_free(shortname);
	g_free(fullname);
	g_free(desc);
	
#ifdef WITH_LEAKBUG
	lbDumpLeaks();
#endif

	return ret;
}

static char *
createFileURL(const char *loc) {
	char *cwd = NULL, *url = NULL, *result = NULL;
	int len;
	
	if (loc[0] != '/') {
		/* Relative file location */		
#ifdef __USE_GNU
		cwd = get_current_dir_name(); /* GNU extension */
#else /* ! __USE_GNU */
		len = PATH_MAX;
		cwd = g_malloc(len);
		result = getcwd(cwd, len);
		while (result == NULL && errno == ERANGE) {
			len *= 2;
			cwd = g_realloc(cwd, len);
			result = getcwd(cwd, len);
		}
		if (result == NULL) { /* errno != ERANGE */
			perror("Couldn't retrieve current working directory");
			exit(1);
		}
#endif
			
		url = lutil_vstrcreate("file://", cwd, "/", loc, NULL);
		g_free(cwd);
	} else {
		url = lutil_vstrcreate("file://", loc, NULL);
	}
	
	return url;
}

static void
printUsage(void) {
	printf("Usage: luau-register [OPTION] ... program_id\n");
	printf("\n");
	printf("Register a program with the specified details in the Luau database.\n");
	printf("\n");
	printf("Options:\n");
	printf("  -r, --remove                     remove specified program from the database\n\n");
	
	printf("  -u, --url=LOCATION               location of Luau software repository file\n");
	printf("  -d, --date=\"MM/DD/YYYY\"          release date of this program version\n");
	printf("  -k, --keywords=\"KEY1,KEY2,...\"   keywords for this program revision\n");
	printf("  -v, --version=VERSION            installed version of this program\n");
	printf("  -i, --interface=VERSION          interface version for this program\n");
	printf("  -n, --shortname=NAME             short \"UNIX name\" of program\n");
	printf("  -f, --fullname=NAME              full/display name of program\n");
	printf("  -s, --desc=DESC                  one-line description of program\n\n");
	
	printf("  -l, --from-url=URL               read program information from specified URL\n");
	printf("  -e, --from-file=FILE             read program information from local file\n\n");
	
	printf("  -h, --help                       display this help message\n");
	printf("\n");
	lutil_printIndented(2, 80, "Note that both the server and the software repository URL must be set for Luau to work for any given program.  If no short name is specified, the program_id is used.  Please see the Luau whitepaper for more information.");
	printf("\n");
}

static struct option *
getLongOptions() {
	struct option *options = (struct option *) calloc(13, sizeof(struct option));
	
	options[0].name = "remove";
	options[0].has_arg = 0;
	options[0].flag = NULL;
	options[0].val = 'r';
	
	options[1].name = "url";
	options[1].has_arg = 1;
	options[1].flag = NULL;
	options[1].val = 'u';
	
	options[2].name = "date";
	options[2].has_arg = 1;
	options[2].flag = NULL;
	options[2].val = 'd';
	
	options[3].name = "keywords";
	options[3].has_arg = 1;
	options[3].flag = NULL;
	options[3].val = 'k';
	
	options[4].name = "version";
	options[4].has_arg = 1;
	options[4].flag = NULL;
	options[4].val = 'v';
	
	options[5].name = "interface";
	options[5].has_arg = 1;
	options[5].flag = NULL;
	options[5].val = 'i';
	
	options[6].name = "shortnam";
	options[6].has_arg = 1;
	options[6].flag = NULL;
	options[6].val = 'n';
	
	options[7].name = "fullname";
	options[7].has_arg = 1;
	options[7].flag = NULL;
	options[7].val = 'f';
	
	options[8].name = "desc";
	options[8].has_arg = 1;
	options[8].flag = NULL;
	options[8].val = 's';
	
	options[9].name = "help";
	options[9].has_arg = 0;
	options[9].flag = NULL;
	options[9].val = 'h';
	
	options[10].name = "from-url";
	options[10].has_arg = 1;
	options[10].flag = NULL;
	options[10].val = 'l';
	
	options[11].name = "from-file";
	options[11].has_arg = 1;
	options[11].flag = NULL;
	options[11].val = 'e';
	
	memset(&options[12], 0, sizeof(struct option));
	
	return options;
}
	
