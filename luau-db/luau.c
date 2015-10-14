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

/** @file luau.c
 * \brief Automatically update registered programs from the command line
 *
 * Command line interface to use all the basic features of the luau library.
 * Supports downloading, installing, and viewing updates, and listing and registering
 * programs.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

#ifndef _MSC_VER
#  include <readline/readline.h>
#  include <readline/history.h>
#endif /* !_MSC_VER */

#ifdef __unix__
#  include <unistd.h>
#endif

#include <glib.h>

#include "libuau.h"
#include "libuau-db.h"
#include "util.h"
#include "error.h"
#include "parse.h"

#ifdef WITH_DMALLOC
#  include <dmalloc.h>
#endif


static int verbosity = 1;
static gboolean outputToString = FALSE;
static GString *outputString = NULL;

static void MSG(int level, const char *template, ...) __attribute__ ((format (printf, 2, 3)));

static void printUsage(void);
static struct option* getLongOptions(void);

static gboolean downloadUpdate(const char* ID, const char* filename, const char* program, APkgType type);
static gboolean installUpdate(const char* ID, const char* program, APkgType type);
static int getUpdates(const char* program, APkgType type);
static void runInteractive(void);
static void printInteractiveHelp(void);
static int list(const char* program);

static gint compareUpdates(gconstpointer p1, gconstpointer p2); /*, gpointer data);*/
static int progressCallback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow);
static int prompt(const char * title, const char* msg, int nTotal, int nDefault, const char *choice1, va_list args);
static void send_email(const char *to, const char *from, const char *subject, const char *message);

int
main(int argc, char *argv[]) {
	int mode = 0, c, n = -1, ret = 0;
	char *file = NULL, *program = NULL, *typeName = NULL, *arg = NULL, *email = NULL;
	APkgType type = LUAU_EMPTY;
	struct option* longOptions = getLongOptions();
	
	srand(time(NULL));
	
	while ((c = getopt_long(argc, argv, ":d:e:gilhm:o:p:t:vq", longOptions, NULL)) != -1) {
		switch (c) {
			case 'd':
			case 'e':
				arg = g_strdup(optarg);
			case 'g':
			case 'i':
			case 'l':
				if (mode == 0) {
					mode = c;
				} else {
					ERROR("Can only do one operation at a time");
					printUsage();
					exit(1);
				}
				break;
				
			case 'm':
				email = g_strdup(optarg);
				outputToString = TRUE;
				outputString = g_string_new("");
				break;
			case 'o':
				file = g_strdup(optarg);
				break;
			case 'p':
				program = g_strdup(optarg);
				break;
			case 't':
				typeName = g_strdup(optarg);
				break;
			case 'h':
				printUsage();
				exit(0);
			case 'q':
				--verbosity;
				break;
			case 'v':
				++verbosity;
				break;
			case '?':
				ERROR("Invalid option: -%c", optopt);
				printUsage();
				exit(1);
			case ':':
				ERROR("Argument needed for -%c", optopt);
				printUsage();
				exit(1);
			default:
				ERROR("Unknown error: got character code 0%o", c);
				printUsage();
				exit(1);
		}
	}
	
	g_free(longOptions);
	
	if (verbosity > 0 && email == NULL) {
		luau_registerProgressCallback(progressCallback);
		luau_registerPromptFunc(prompt);
	}
	
	if (mode == 0) {
		mode = 'i';
	} else if (mode == 'd' || mode == 'e') {
		if (program == NULL) {
			ERROR("Must specify a program name to download or install updates.");
			printUsage();
			exit(1);
		}
		if (typeName == NULL) {
			ERROR("Must specify a package type to download or install updates.");
			printUsage();
			exit(1);
		}
	}
	
	if (typeName != NULL) {
		if ((type = luau_parsePkgType(typeName)) == LUAU_UNKNOWN)
			ERROR("Unknown package type: %s", typeName);
	}
	
	switch (mode) {
		case 'd':
			downloadUpdate(arg, file, program, type);
			break;
		case 'e':
			if (file != NULL) 
				MSG(1, "WARNING: filename specified, but ignored for install mode\n");
			installUpdate(arg, program, type);
			break;
		case 'g':
			if (file != NULL)
				MSG(1, "WARNING: filename specified, but ignored for getupdate mode\n");
			n = getUpdates(program, type);
			break;
		case 'i':
			runInteractive(); /* file, program, type); */
			break;
		case 'l':
			if (file != NULL)
				MSG(1, "WARNING: filename specified, but ignored for list mode\n");
			if (type != LUAU_EMPTY)
				MSG(1, "WARNING: package type specified, but ignored for list mode\n");
			ret = list(program);
			break;
		default:
			ERROR("Unrecognized mode: %c", mode);
			exit(1);
	}
	
	if (email != NULL && outputString != NULL && outputString->len > 0) {
		GString *temp;
		char subject[255];
		
		if (mode == 'g') {
			temp = g_string_new("");
			g_string_printf(temp, "There are %d new updates available.\n\n", n);
			g_string_prepend(outputString, temp->str);
			g_string_free(temp, TRUE);
			
			if (program == NULL)
				snprintf(subject, 255, "Luau: %d New Updates Available", n);
			else
				snprintf(subject, 255, "Luau: %d New Updates Available For '%s'", n, program);
		} else {
			snprintf(subject, 255, "Luau: Report");
		}
		
		send_email(email, "luau@localhost", subject, outputString->str);
	}
		
	g_free(file);
	g_free(email);
	g_free(program);
	g_free(typeName);
	g_free(arg);
	
#ifdef WITH_LEAKBUG
	lbDumpLeaks();
#endif

	return ret;
}

static gboolean
downloadUpdate(const char* ID, const char* filename, const char* program, APkgType type) {
	AProgInfo progInfo;
	AUpdate update;
	gboolean result;
	GError *err = NULL;
	const char *downloadTo;
	char *downloadLoc;
	
	result = luau_db_getProgInfo(&progInfo, program, &err);
	if (result == FALSE) {
		g_assert(err != NULL);
		ERROR("Couldn't retrieve program info for %s: %s", program, err->message);
		g_error_free(err);
		return FALSE;
	}
	
	result = luau_db_getUpdateInfo(&update, ID, &progInfo, &err);
	if (result == FALSE) {
		g_assert(err != NULL);
		ERROR("Couldn't retrieve update information for update %s for program %s: %s",
		      ID, program, err->message);
		luau_freeProgInfo(&progInfo);
		g_error_free(err);
		return FALSE;
	}
	
	result = TRUE;
	if (update.type == LUAU_SOFTWARE) {
		if (filename != NULL && filename[0] != '\0')
			downloadTo = filename;
		else
			downloadTo = ".";
		
		DBUGOUT("Downloading update '%s' for '%s' to '%s'", ID, program, downloadTo);
		downloadLoc = luau_downloadUpdate(&progInfo, &update, type, downloadTo, &err);
		
		if (downloadLoc == NULL) {
			g_assert(err != NULL);
			ERROR("Couldn't download update '%s' for '%s': %s", ID, program, err->message);
			g_error_free(err);
			err = NULL;
			result = FALSE;
		} else {
			MSG(1, "Downloaded update '%s' to %s\n", ID, downloadLoc);
			g_free(downloadLoc);
			result = TRUE;
		}
	} else if (update.type == LUAU_MESSAGE) {
		MSG(1, "ID: %s\n", ID);
		MSG(1, "Brief: %s\n", update.shortDesc);
		MSG(1, "Full:\n");
		lutil_printIndented(2, 80, update.fullDesc);
	} else if (update.type == LUAU_LIBUPDATE) {
		MSG(1, "ID: %s\n", ID);
		MSG(1, "Brief: %s\n", update.shortDesc);
		MSG(1, "Full:\n");
		if (verbosity >= 1)
			lutil_printIndented(2, 80, update.fullDesc);
		MSG(0, "New URL: %s\n", update.newURL);
	}
	
	luau_freeProgInfo(&progInfo);
	luau_freeUpdateInfo(&update);
	
	return result;
}

static gboolean
installUpdate(const char* ID, const char* program, APkgType type) {
	AProgInfo progInfo;
	AUpdate updateInfo;
	GError *err = NULL;
	gboolean result;
	
	result = luau_db_getProgInfo(&progInfo, program, &err);
	if (result == FALSE) {
		g_assert(err != NULL);
		ERROR("Couldn't retrieve program information for %s: %s", program, err->message);
		g_error_free(err);
		return FALSE;
	}
	
	result = luau_db_getUpdateInfo(&updateInfo, ID, &progInfo, &err);
	if (result == FALSE) {
		g_assert(err != NULL);
		ERROR("Couldn't retrieve information for update %s for program %s: %s", ID, program, err->message);
		g_error_free(err);
		return FALSE;
	}
	
	if (updateInfo.type == LUAU_SOFTWARE) {
		result = luau_installUpdate(&progInfo, &updateInfo, type, &err);
		if (result)
			MSG(1, "Done.\n");
		else {
			g_assert(err != NULL);
			ERROR("Couldn't install update %s for program %s: %s", ID, program, err->message);
			g_error_free(err);
			err = NULL;
		}
	} else {
		MSG(1, "Installing new software repository location...\n");
		MSG(1, updateInfo.newURL);
		progInfo.url = updateInfo.newURL;
		result = luau_db_registerNewApp(&progInfo, &err);
		if (result)
			MSG(1, "Done.\n");
		else {
			g_assert(err != NULL);
			ERROR("Couldn't register new URL: %s", err->message);
			g_error_free(err);
			err = NULL;
		}
	}
	
	luau_freeProgInfo(&progInfo);
	luau_freeUpdateInfo(&updateInfo);
	
	return result;
}

static int
getUpdates(const char* program, APkgType type) {
	AProgInfo info;
	GContainer *allUpdates;
	GIterator iter;
	GList *updates, *curr, *temp;
	GPtrArray *progs;
	GError *err = NULL;
	const char* updateType;
	char *packageType, *desc;
	AUpdate *update;
	gboolean result;
	unsigned int i, n, nUpdates = 0, max_len = 4;
	
	allUpdates = g_container_new(GCONT_LIST);
	
	if (program == NULL) {
		progs = luau_db_getAllPrograms();
		if (progs == NULL) {
			ERROR("Couldn't retrieve list of registered programs: exiting");
			exit(1);
		}
		for (i = 0; i < progs->len; ++i) {
			program = g_ptr_array_index(progs, i);
			result = luau_db_getProgInfo(&info, program, &err);
			if (result == FALSE) {
				g_assert(err != NULL);
				ERROR("Couldn't retrieve program information for %s: %s", program, err->message);
				g_error_free(err);
				g_container_destroy_type(GCONT_PTR_ARRAY, progs);
				return 1;
			}
			updates = luau_db_checkForUpdates(&info, &err);
			if (err != NULL) {
				g_assert(updates == NULL);
				ERROR("Couldn't retrieve updates for %s: %s", program, err->message);
				g_error_free(err);
				g_container_destroy_type(GCONT_PTR_ARRAY, progs);
				luau_freeProgInfo(&info);
				return 1;
			}
			
			g_free((char*)program);
			progs->pdata[i] = g_strdup(info.fullname);
			
			for (curr = updates; curr != NULL; curr = curr->next) {
UPDATE_LOOP1:
				update = curr->data;
				g_assert(update != NULL);
				
				if (luau_isVisible(update)) {
					if ((n = strlen(update->id)) > max_len)
						max_len = n;
					
					++nUpdates;
				} else {
					temp = curr;
					curr = curr->next;
					updates = g_list_delete_link(updates, temp);
					
					luau_freeUpdateInfo(update);
					g_free(update);
					
					if (curr == NULL) 
						break;
					else
						goto UPDATE_LOOP1; /* Restart loop while skipping 'for' loop condition */
				}
			}
			for (curr = updates; curr != NULL; curr = curr->next) {
				update = curr->data;
				g_assert(update != NULL);
			}
			g_container_add(allUpdates, updates);
			luau_freeProgInfo(&info);
		}
		program = NULL;
	} else {
		result = luau_db_getProgInfo(&info, program, &err);
		if (result == FALSE) {
			g_assert(err != NULL);
			ERROR("Couldn't retrieve program information for %s: %s", program, err->message);
			g_error_free(err);
			return 1;
		}
		updates = luau_db_checkForUpdates(&info, &err);
		if (err != NULL) {
			g_assert(updates == NULL);
			ERROR("Couldn't retrieve updates for %s", program);
			g_error_free(err);
			return 1;
		}
		g_assert(updates != NULL);
		
		for (curr = updates; curr != NULL; curr = curr->next) {
UPDATE_LOOP2:
			update = curr->data;
			g_assert(update != NULL);
			if (luau_isVisible(update)) {
				if ((n = strlen(update->id)) > max_len)
					max_len = n;
				
				++nUpdates;
			} else {
				temp = curr;
				curr = curr->next;
				updates = g_list_delete_link(updates, temp);
				if (curr == NULL)
					break;
				else
					goto UPDATE_LOOP2; /* restart loop without evaluating for loop condition, since we delete 'curr' */
			}
		}

		nUpdates = g_list_length(updates);
		g_container_add(allUpdates, updates);
		
		progs = g_ptr_array_new();
		g_ptr_array_add(progs, g_strdup(info.fullname));
		luau_freeProgInfo(&info);
	}
	
	if (nUpdates == 0) {
		if (!outputToString)
			MSG(1, "No new updates.\n");
	} else {
		MSG(2, "%d Updates Available:\n", nUpdates);
		MSG(1, "%-*s Type      Formats            Description\n", max_len, "Name");
		MSG(1, "--------------------------------------------------------------------------------\n");
			
		g_container_get_iter(&iter, allUpdates);
		while (g_iterator_hasNext(&iter)) {
			updates = g_iterator_next(&iter);
			
			if (g_list_length(updates) == 0)
				continue;
			
			/*g_ptr_array_sort_with_data(updates, compareUpdates, (gpointer) "date");*/
			/*qsort(updates->pdata, updates->len, sizeof(AUpdate*), compareUpdates);*/

			if (program == NULL || verbosity >= 2) {
				char *name;
				
				if (iter.index-1 != 0) /* Not on first loop */
					MSG(0, "\n");
				
				name = g_ptr_array_index(progs, iter.index-1);
				g_assert(name != NULL);
				MSG(0, "%s\n", name);
				n = strlen(name);
				for (i = 0; i < n; ++i)
					MSG(0, "=");
				MSG(0, "\n");
			}
			
			for (curr = updates; curr != NULL; curr = curr->next) {
				update = curr->data;
				g_assert(update != NULL);

				updateType = luau_updateTypeString(update->type);
				if (update->type == LUAU_SOFTWARE)
					packageType = luau_multPackageTypeString(update->availableFormats);
				else 
					packageType = "";
				
				desc = (update->shortDesc == NULL ? "(none)" : update->shortDesc);
		
				MSG(0, "%-*s %-9s %-18s %s\n", max_len, update->id, updateType, packageType, update->shortDesc);
				
				if (update->type == LUAU_SOFTWARE)
					g_free(packageType);
			}
			luau_freeUpdateList(updates);
		}
		MSG(0, "\n");
	}
	
	g_container_destroy_type(GCONT_PTR_ARRAY, progs);
	g_container_free(allUpdates, TRUE);
	
	return 0;
}

static void
runInteractive(void) {
	LUAU_parsedLine *line;
	char *input, *programID, *updateID, *filename;
	APkgType type;
	
	static const char *symbols[ ] = {"help",    "h",
	                                 "?",       "h",
	                                 "get",     "g",
									 "install", "i",
									 "list",    "l",
									 "updates", "u",
									 "quit",    "q",
	                                  NULL };

	MSG(1, "luau %d.%d.%d - Automatically update supported software.\n", LUAU_VERSION_MAJOR, LUAU_VERSION_MINOR, LUAU_VERSION_PATCH);
	MSG(1, "Copyright (c) 2003, David Eklund\n");
	MSG(1, "All rights reserved.\n");
	MSG(1, "\n");
	MSG(1, "Type 'help' for help.\n");
	
	while ((input = readline("> ")) != NULL) {
		if (input[0] == '\0')
		{
			g_free(input);
			continue;
		}
		add_history(input);
		
		line = lutil_parse_parseLine(input);
		g_free(input);
		
		switch (lutil_parse_parseSymbolArray(line->keyword, symbols)) {
			case 'h':
				printInteractiveHelp();
				break;
				
			case 'g':
				if (line->args->len < 2)
					ERROR("Not enough arguments given");
				else if (line->args->len > 4)
					ERROR("Too many arguments given");
				else {
					programID = g_ptr_array_index(line->args, 0);
					updateID  = g_ptr_array_index(line->args, 1);
					if (line->args->len > 2)
						type  = luau_parsePkgType(g_ptr_array_index(line->args, 2));
					else
						type = LUAU_EMPTY;
					
					if (line->args->len > 3)
						filename = g_ptr_array_index(line->args, 3);
					else
						filename = NULL;
					
					downloadUpdate(updateID, filename, programID, type);
				}
				break;
			
			case 'i':
				if (line->args->len < 3)
					ERROR("Not enough arguments given");
				else if (line->args->len > 3)
					ERROR("Too many arguments given");
				else {
					programID = g_ptr_array_index(line->args, 0);
					updateID  = g_ptr_array_index(line->args, 1);
					type      = luau_parsePkgType(g_ptr_array_index(line->args, 2));
					
					installUpdate(updateID, programID, type);
				}
				break;
			
			case 'l':
				if (line->args->len > 1)
					ERROR("Not enough arguments given");
				else {
					if (line->args->len == 1)
						programID = g_ptr_array_index(line->args, 0);
					else
						programID = NULL;
					
					list(programID);
				}
				break;
			
			case 'u':
				/*if (line->args->len < 1)
					ERROR("Not enough arguments given");*/
				if (line->args->len > 1)
					ERROR("Too many arguments given");
				else {
					if (line->args->len == 1)
						programID = g_ptr_array_index(line->args, 0);
					else
						programID = NULL;
					
					getUpdates(programID, LUAU_EMPTY);
				}
				break;
			
			case 'q':
				lutil_parse_freeParsedLine(line);
				goto END;
			
			default:
				ERROR("Parse error: unrecognized command '%s'", line->keyword);
		}
		
		lutil_parse_freeParsedLine(line);
	}

END:
	MSG(0, "\n");
}

static void
printInteractiveHelp(void) {
	MSG(0, "List of commands:\n\n");
	
	MSG(0, "get <PROGRAM-ID> <UPDATE-ID> [<PACKAGE-TYPE>] [<FILENAME>]\n");
	MSG(0, " - Retrieve a update of type PACKAGE-TYPE and download it to FILENAME\n");
	MSG(0, "   (if specified), or the current directory (if not).\n");
	MSG(0, "install <PROGRAM-ID> <UPDATE-ID> [<PACKAGE-TYPE>]\n");
	MSG(0, " - Retrieve and install an update of type PACKAGE-TYPE.\n");
	MSG(0, "list [<PROGRAM-ID>]\n");
	MSG(0, " - If PROGRAM-ID is specified, show whether it is registered.  Otherwise,\n");
	MSG(0, "   list all registered programs.\n");
	MSG(0, "updates [<PROGRAM-ID>]\n");
	MSG(0, " - Retrieve and list all updates available for PROGRAM-ID\n");
	MSG(0, "quit\n");
	MSG(0, " - Quit luau and return to command prompt\n\n");
	
	MSG(0, "Note that for PACKAGE-TYPE, you must enter one of the following:\n");
	MSG(0, "  RPM DEB SRC EXEC\n");
}

static int
list(const char* program) {
	unsigned int i;
	int ret = 0;
	char *progName, *url, *desc, *nameAndVersion;
	gboolean found = FALSE, result;
	GPtrArray *allPrograms;
	GError *err = NULL;
	AProgInfo info;
		
	allPrograms = luau_db_getAllPrograms();
	if (program != NULL) {
		MSG(2, "Checking if '%s' is registered... ", program);
		for (i = 0; i < allPrograms->len; ++i) {
			if (lutil_streq(program, g_ptr_array_index(allPrograms, i))) {
				found = TRUE;
				break;
			}
		}
		
		if (found) {
			MSG(2, "Yes.\n");
			MSG(1, "%s\n", program);
			ret = 0;
		} else {
			MSG(2, "No\n");
			ret = 1;
		}
	} else {
		MSG(1, "--------------------------------------------------------------------------------\n");
		MSG(1, " * Name            Description\n");
		MSG(1, "   URL\n");
		MSG(1, "--------------------------------------------------------------------------------\n");
		for (i = 0; i < allPrograms->len; ++i) {
			progName = g_ptr_array_index(allPrograms, i);
			if (verbosity > 0) {
				result = luau_db_getProgInfo(&info, progName, &err);
				
				if (result == FALSE)
					FATAL_ERROR("INTERNAL ERROR: program registered, but couldn't retrieve information: %s", err->message);
				
				url = (info.url == NULL ? "(none)" : info.url);
				desc = (info.desc == NULL ? "(none)" : info.desc);
				
				if (info.version == NULL)
					nameAndVersion = g_strdup(info.shortname);
				else
					nameAndVersion = lutil_vstrcreate(info.shortname, " ", info.version, NULL);
				
				MSG(0, " * %-15s %s\n", nameAndVersion, desc);
				MSG(0, "   %s\n\n", url);
				
				luau_freeProgInfo(&info);
				g_free(nameAndVersion);
			} else {
				MSG(0, "%s\n", (char*) g_ptr_array_index(allPrograms, i));
			}
		}
		ret = 0;
	}
	
	for (i = 0; i < allPrograms->len; ++i)
		g_free(g_ptr_array_index(allPrograms, i));
	g_ptr_array_free(allPrograms, TRUE);
	
	return ret;
}

static void
printUsage() {
	MSG(0, "Usage: luau [OPTIONS]...\n");
	MSG(0, "\n");
	MSG(0, "Options:\n");
	MSG(0, "  Modes: (only one may be specified)\n");
	MSG(0, "  -d, --download=ID     download update with given ID for specified program\n");
	MSG(0, "  -e, --install=ID      install update with given ID for specified program\n");
	MSG(0, "  -g, --getupdates      retrieve a list of new updates\n");
	MSG(0, "  -i, --interactive     run in interactive mode [default]\n");
	MSG(0, "  -l, --list            list all registered programs\n");
	MSG(0, "  -h, --help            display this message\n");
	MSG(0, "\n");
	MSG(0, "  Extra Information:\n");
	MSG(0, "  -m, --email=ADDRESS   email the results to ADDRESS, if any results at all\n");
	MSG(0, "  -o, --output=PATH     when downloading an update, specify where to download\n");
	MSG(0, "  -p, --program=NAME    specify a program\n");
	MSG(0, "  -t, --type=TYPE       specify the type of update to download/install\n");
	MSG(0, "  -q, --quiet           suppress all unnecessary output\n");
	MSG(0, "  -v, --verbose         display more informational output\n");
	MSG(0, "\n");
	MSG(0, "To download or install updates, both the program name and the type to download\n");
	MSG(0, "MUST be specified.\n");
	MSG(0, "\n");
}


static struct option *
getLongOptions() {
	struct option *options = (struct option *) g_malloc(15 * sizeof(struct option));
	
	options[0].name = "download";
	options[0].has_arg = 1;
	options[0].flag = NULL;
	options[0].val = 'd';
	
	options[1].name = "install";
	options[1].has_arg = 1;
	options[1].flag = NULL;
	options[1].val = 'e';
	
	options[2].name = "getupdates";
	options[2].has_arg = 0;
	options[2].flag = NULL;
	options[2].val = 'g';

	options[3].name = "interactive";
	options[3].has_arg = 0;
	options[3].flag = NULL;
	options[3].val = 'i';
	
	options[4].name = "list";
	options[4].has_arg = 0;
	options[4].flag = NULL;
	options[4].val = 'l';
	
	options[5].name = "help";
	options[5].has_arg = 0;
	options[5].flag = NULL;
	options[5].val = 'h';
	
	options[6].name = "program";
	options[6].has_arg = 1;
	options[6].flag = NULL;
	options[6].val = 'p';
	
	options[10].name = "output";
	options[10].has_arg = 1;
	options[10].flag = NULL;
	options[10].val = 'o';
	
	options[11].name = "email";
	options[11].has_arg = 1;
	options[11].flag = NULL;
	options[11].val = 'm';
		
	options[7].name = "type";
	options[7].has_arg = 0;
	options[7].flag = NULL;
	options[7].val = 't';
	
	options[8].name = "quiet";
	options[8].has_arg = 0;
	options[8].flag = NULL;
	options[8].val = 'q';
	
	options[9].name = "verbose";
	options[9].has_arg = 0;
	options[9].flag = NULL;
	options[9].val = 'v';
	
	memset(&options[12], 0, sizeof(struct option));
	
	return options;
}


static gint
compareUpdates(gconstpointer p1, gconstpointer p2) { /*, gpointer data) {*/
	/* Used by g_ptr_array_sort */
	AUpdate *u1 = (AUpdate*) p1, *u2 = (AUpdate*) p2;
	/*char* sortBy = (char*) data;*/
	
	/*if (lutil_strcaseeq(sortBy, "type"))*/
		return lutil_intcmp(u1->type, u2->type);
	/*else if (lutil_strcaseeq(sortBy, "date"))
		return luau_datecmp(u1->date, u2->date);
	else if (lutil_strcaseeq(sortBy, "id"))
		return strcmp(u1->id, u2->id);
	else
		ERROR("Unrecognized sort-by type: %s", sortBy);
	
	return 0;*/
}

static int
progressCallback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
	float fract;
	int i, n;
	
	if (dltotal != 0) {
		fract = (float) (dlnow / dltotal);
		n = (int) (fract * 50);
		
		printf("\r");
		for (i = 0; i < n; ++i)
			printf("#");
		for (i = n; i < 50; ++i)
			printf(".");
		
		printf(" : %d/%d (%d%%)", (int)dlnow, (int)dltotal, (int) (fract * 100));
		
		if (dlnow == dltotal)
			printf("\n");
		
		fflush(stdout);
	}
	
	return 0;
}

static int
prompt(const char * title, const char* msg, int nTotal, int nDefault, const char *choice1, va_list args) {
	const char *choice;
	char prompt[30], *input;
	int i, selection = 0;
	
	MSG(0, "\n");
	MSG(0, "%s:\n", title);
	MSG(0, "%s\n\n", msg);
	
	MSG(0, "1) %s\n", choice1);
	
	for (i = 1; i < nTotal; ++i) {
		choice = va_arg(args, const char *);
		MSG(0, "%d) %s\n", i+1, choice);
	}
	MSG(0, "\n");
	
	while (selection < 1 || selection > nTotal) {
		snprintf(prompt, 30, "Choice [%d]: ", nDefault+1);
		input = readline(prompt);
		if (input[0] == '\0')
			selection = nDefault+1;
		else
			selection = atoi(input);
		g_free(input);
	}
	
	MSG(0, "\n");
	
	return selection-1;
}

static void
send_email(const char *to, const char *from, const char *subject, const char *message)
{
	char *exec, *subject_c, *msg_c, *ptr;
	int len;
	
	len = strlen(subject) + strlen(message) + 255;
	exec = lutil_createString(len);
	
	subject_c = g_strdup(subject);
	msg_c = g_strdup(message);
	
	ptr = msg_c;
	while ( (ptr =  strchr(ptr, '\'')) != NULL) {
		ptr[0] = '"';
		++ptr;
	}
	ptr = subject_c;
	while ( (ptr =  strchr(ptr, '\'')) != NULL) {
		ptr[0] = '"';
		++ptr;
	}
	
	snprintf(exec, len, "echo '%s' | mail -r '%s' -s '%s' '%s'", msg_c, from, subject_c, to);
	
	system(exec);
	
	g_free(msg_c);
	g_free(subject_c);
}

static void
MSG(int level, const char *template, ...) {
	char *msg;
	va_list list;
	
	va_start(list, template);
	
	if (level <= verbosity) {
		if (outputToString) {
			msg = lutil_valistToString(template, list);
			g_string_append(outputString, msg);
			g_free(msg);
		} else {
			vprintf(template, list);
		}
	}
	
	va_end(list);
}
