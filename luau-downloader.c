/*
 * luau (Lib Update/Auto-Update): Simple Update Library
 * Copyright (C) 2003  David Eklund
 * Copyright (C) 2005  Mike Hearn
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

/* TODO: use the gerrors we define */

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <malloc.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <glib.h>
#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

#include "libuau.h"
#include "util.h"

#ifdef WITH_DMALLOC
#  include <dmalloc.h>
#endif

static void printerror(char *s, ...)
{
    va_list args;
    va_start(args, s);
    fprintf(stderr, "luau-downloader: error: ");
    vfprintf(stderr, s, args);
    fprintf(stderr, "\n");
}

#define bail(s...)				\
    do {					\
	printerror(s);				\
	exit(1);				\
    } while (0)

static int download_url_update(char *url, const char *update_name, const AInterface *interface, const char *version, const char *packageVersion, const char *instVersion, const char *instPackage, const char *output, gboolean look_for_meta);
static char* download_update(AProgInfo *progInfo, AUpdate *updateInfo, APkgType pkgtype, const char *output, gboolean lookForMeta);
static char* download_file(const char *url, const char *output);

static AUpdate *find_update(const AProgInfo *prog_info, const AInterface *interface, const char *version, const char *pkgVersion, const char *update_name);
static GList* get_updates_of_type(const AProgInfo *progInfo, APkgType type);

static gboolean curl_fetch(const char *url, const char *loc);
static gboolean curl_exists(const char *url);

static int progress_callback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow);
static size_t autofail_callback(void *ptr, size_t size, size_t nmemb, void *stream);

static void print_usage(void);
static struct option* get_long_options();

static char *outpipe = "/dev/stdin";

/* reads from the pipe into a temporary buffer. used to receive frontends "OK" response */
static void wait_for_frontend()
{
    char inbuf[25];
    FILE *f;

    f = fopen(outpipe, "r");
    if (fgets(inbuf, sizeof(inbuf), f) == NULL)
	printerror("read failed");
    fclose(f);
}

int main(int argc, char *argv[])
{
    struct option *options;
    gboolean look_for_meta = FALSE;
    char *updates = NULL, *name = NULL, *file = NULL, *output = NULL, *version = NULL;
    char *package = NULL, *instPackage = NULL, *instVersion = NULL;
    AInterface *interface = NULL;
    int c, ret = 0;

    if (argc < 2)
    {
	print_usage();
	exit(0);
    }


    options = get_long_options();
    luau_registerProgressCallback(progress_callback);

    while ((c = getopt_long(argc, argv, ":P:p:u:n:f:o:i:v:V:K:mh", options, NULL)) != -1)
    {
	switch (c) {
	case 'P':
	    outpipe = g_strdup(optarg);
	    break;
	case 'u':
	    updates = g_strdup(optarg);
	    break;
	case 'n':
	    name = g_strdup(optarg);
	    break;
	case 'f':
	    file = g_strdup(optarg);
	    break;
	case 'o':
	    output = g_strdup(optarg);
	    break;
	case 'i':
	    interface = (AInterface*) g_malloc(sizeof(AInterface));
	    sscanf(optarg, "%d.%d", &(interface->major), &(interface->minor));
	    break;
	case 'v':
	    version = g_strdup(optarg);
	    break;
	case 'k':
	    package = g_strdup(optarg);
	    break;
	case 'V':
	    instVersion = g_strdup(optarg);
	    break;
	case 'K':
	    instPackage = g_strdup(optarg);
	    break;
	case 'm':
	    look_for_meta = TRUE;
	    break;
	case 'h':
	    print_usage();
	    ret = 0;
	    goto end;
	case '?':
	    printerror("Invalid option: -%c\n", optopt);
	    print_usage();
	    ret = 1;
	    goto end;
	case ':':
	    printerror("Argument needed for -%c\n", optopt);
	    print_usage();
	    ret = 1;
	    goto end;
	default:
	    printerror("Unknown error: got character code 0%o\n", c);
	    print_usage();
	    ret = 1;
	    goto end;
	}
    }

    ret = 1;
    if (argc == optind)
    {
	if (updates)
	{
	    if (file)
		printerror("Only one of --file or --updates can be specified.\n");
	    else
		ret = download_url_update(updates, name, interface, version, package, instVersion, instPackage, output, look_for_meta);
	}
	else if (file)
	{
	    char *result = download_file(file, output);
	    ret = (result != NULL);
	    g_free(result);
	}
    }
    else
    {
	printerror("Too many arguments");
	print_usage();
	ret = 1;
    }

end:
    if (updates) g_free(updates);
    if (name) g_free(name);
    if (file) g_free(file);
    if (output) g_free(output);
    if (interface) g_free(interface);
    if (package) g_free(package);
    if (instPackage) g_free(instPackage);
    if (version) g_free(version);
    if (instVersion) g_free(instVersion);

    g_free(options);
    return ret;
}


static int download_url_update(char *url, const char *update_name,
			       const AInterface *interface,
			       const char *version,
			       const char *packageVersion,
			       const char *instVersion,
			       const char *instPackage,
			       const char *output,
			       gboolean look_for_meta)
{
    AProgInfo prog_info;
    AUpdate *update_info;
    char *location;

    memset(&prog_info, 0, sizeof(AProgInfo));
    prog_info.url = url;
    prog_info.version = instVersion;
    prog_info.pkgVersion = instPackage;
    update_info = find_update(&prog_info, interface, version, packageVersion, update_name);

    if (!update_info)
    {
	printerror("No download was available that matched the requested constraints\n");
	return 1;
    }

    location = download_update(&prog_info, update_info, LUAU_AUTOPKG, output, look_for_meta);

    luau_freeUpdateInfo(update_info);
    g_free(update_info);

    return (location == NULL);
}

static char *download_update(AProgInfo *prog_info, AUpdate *update_info,
			     APkgType pkgtype, const char *output,
			     gboolean look_for_meta)
{
    APackage *package;
    char *loc, *url, *url2;
    GError *error = NULL;

    if (!(package = luau_getUpdatePackage(update_info, pkgtype, &error)))
    {
	g_assert(error);
	printerror("Specified format not available for selected update (%s)", error->message);
	g_error_free(error);
	return NULL;
    }

    if (!(url = luau_getPackageURL(package, &error)))
    {
	g_assert(error);
	printerror("Couldn't retrieve package URL: %s", error->message);
	g_error_free(error);
	return NULL;
    }

    /* in autopackage 1.2, having a .payload file is deprecated in
     * favour of just having a standard .package file downloaded
     * instead.
     */
    char *terminator = "";
    if (getenv("AUTOPACKAGETARGET") && !strcmp(getenv("AUTOPACKAGETARGET"), "1.0"))
    {
        terminator = ".payload";
    }
    
    url2 = g_strdup_printf("%s%s", url, look_for_meta ? ".meta" : terminator);
    loc = download_file(url2, output);
    g_free(url2);

    return loc;
}

/* We need to open/write/close each time because this is how bash does it when using echo/cat etc,
   and the frontend/backend sync can break if we don't do that. */
static void out(char *s, ...)
{
    va_list args;
    FILE *f;

    if (!(f = fopen(outpipe, "w")))
	bail("could not open %s", outpipe);

    va_start(args, s);
    vfprintf(f, s, args);
    va_end(args);

    fclose(f);
}

/* downloads the file at URL "url" to the output path "output" (can be
 * directory or file) then returns its full path or NULL on error.
 */
static char *download_file(const char *url, const char *output)
{
    char *loc, *realURL;
    int len, i;
    const char *filename;

    realURL = NULL;

    if (output == NULL || output[0] == '\0')
	output = "/tmp";

    if (!curl_exists(url)) return NULL;

    if (lutil_isDirectory(output))
    {
	/* invalid URLs include http://foo.org, http://foo.org/ etc .. it must have a file name */
	filename = strrchr(url, '/') + 1;
	if (filename == NULL || filename[0] == '\0')
	    bail("Invalid URL given to download (%s): aborting", url);

	loc = lutil_vstrcreate(output, "/", filename, NULL);

	/* make sure we don't overwrite anything by appending a number to the end */
	len = strlen(output) + strlen(filename) + 8;
	for (i = 1; lutil_fileExists(loc); ++i)
	{
	    g_free(loc);
	    loc = lutil_createString(len);
	    snprintf(loc, len+1, "%s/%s.%d", output, filename, i);
	}
    }
    else
    {
	loc = g_strdup(output);
    }

    /* make url short enough for frontend */
    char *urldesc = strdup(url);
    if (strrchr(urldesc, '/')) urldesc = strrchr(urldesc, '/');
    out("DOWNLOAD-START\n%s\nDescription?\n", ++urldesc);

    wait_for_frontend();

    out("DOWNLOAD-PREPARE\nCONNECTING\n\n");

    wait_for_frontend();

    /* ok, start the download */
    if (!curl_fetch(url, loc))
    {
	g_free(loc);
	loc = NULL;

	out("DOWNLOAD-FINISH\nFAILURE\n\n");
	wait_for_frontend();
    }
    else
    {
	out("DOWNLOAD-FINISH\nSUCCESS\n\n");
	wait_for_frontend();
    }

    if (realURL != NULL)
	g_free(realURL);

    return loc;
}

/* if update_name is non-null, returns the given update. if version is
   non-null, returns the package of that version (if any). if
   interface is non-null, returns the best implementation for that
   interface version. If all three params are NULL, returns the newest
   update for that package.
 */
static AUpdate *find_update(const AProgInfo *prog_info, const AInterface *interface,
			    const char *version, const char *pkgVersion, const char *update_name)
{
    AUpdate *update = NULL;
    GList *all_updates, *current;
    GError *err = NULL;
    gboolean result;

    if (update_name)
    {
	g_assert( !version && !interface );

	update = g_malloc(sizeof(AUpdate));
	result = luau_getUpdateInfo(update, update_name, prog_info, &err);
	if (!result)
	{
	    g_assert(err != NULL);
	    printerror("Couldn't retrieve info for update '%s': %s", update_name, err->message);
	    g_error_free(err);
	    return NULL;
	}

	return update;
    }
    else if (version)
    {
	g_assert( !update_name && !interface );

	all_updates = get_updates_of_type(prog_info, LUAU_AUTOPKG);
	if (!all_updates) return NULL;

	current = all_updates;

	do {
	    update = current->data;
	    g_assert( update );

	    /* if we found a good version, return it */
	    if (update->newVersion && lutil_streq(update->newVersion, version))
	    {
		all_updates = g_list_remove_link(all_updates, current);
		luau_freeUpdateList(all_updates);
		return update;
	    }
	} while ((current = g_list_next(current)));

	return NULL;
    }
    else if (interface)
    {
	AUpdate *candidate = NULL;

	g_assert( !version && !update_name );

	all_updates = get_updates_of_type(prog_info, LUAU_AUTOPKG);
	if (!all_updates) return NULL;

	current = all_updates;
	do {
	    update = current->data;
	    if (!luau_satisfiesInterface(&update->interface, interface)) continue;
	    if (!candidate || (update->interface.minor > candidate->interface.minor))
		candidate = update;
	} while ((current = g_list_next(current)));

	return candidate;
    }
    else
    {
	AUpdate *candidate = NULL;

	all_updates = get_updates_of_type(prog_info, LUAU_AUTOPKG);
	if (!all_updates) return NULL;

	current = all_updates;
	do {
	    update = current->data;
	    if (!candidate || (update->newVersion && (luau_versioncmp(update->newVersion, candidate->newVersion) > 0)))
		candidate = update;
	} while ((current = g_list_next(current)));

	/* there must be at least one update */
	g_assert( candidate != NULL );
	
	if (prog_info->version)
	{
	    int cmp = luau_versioncmp(candidate->newVersion, prog_info->version);
	    if (cmp == 0 && prog_info->pkgVersion != NULL)
	    {
		char *newPkgVersion = luau_getMostRecentPkgVersion(candidate->packages);
		cmp = luau_versioncmp(newPkgVersion, prog_info->pkgVersion);
	    }
	    
	    if (cmp <= 0)
	    {
		luau_freeUpdateList(all_updates);
		return NULL;
	    }
	}

	all_updates = g_list_remove(all_updates, candidate);
	luau_freeUpdateList(all_updates);

	return candidate;
    }
}

/* return a list of any updates matching the given type */
static GList *get_updates_of_type(const AProgInfo *progInfo, APkgType type)
{
    GList *updates = NULL;
    GList *current;
    AUpdate *update;
    GError *error = NULL;

    if (!(updates = luau_checkForUpdates(progInfo, &error)))
    {
	g_assert(error != NULL);
	printerror("Couldn't retrieve updates for program: %s", error->message);
	g_error_free(error);
	return NULL;
    }

    current = updates;

    /* eliminate unsuitable updates from the list */
    do {
	update = (AUpdate *)current->data;

	if ((update->type != LUAU_SOFTWARE) || !luau_isOfType(update->availableFormats, type))
	{
	    luau_freeUpdateInfo(update);
	    g_free(update);
	    current->data = NULL;
	}
    } while ((current = g_list_next(current)));

    return g_list_remove_all(updates, NULL);
}

static gboolean curl_fetch(const char *url, const char *loc)
{
    CURL *curl_handle;
    CURLcode ret;
    FILE *tmp;
    time_t now = time(NULL);
    char buf[CURL_ERROR_SIZE];

    if (!(tmp = fopen(loc, "w")))
    {
	printerror("Couldn't download file to specified location %s: %s", loc, strerror(errno));
	return FALSE;
    }

    curl_handle = curl_easy_init();

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_FILE, (void *)tmp);
    curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER, buf);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 15);

    curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, FALSE);
    curl_easy_setopt(curl_handle, CURLOPT_PROGRESSFUNCTION, progress_callback);
    curl_easy_setopt(curl_handle, CURLOPT_PROGRESSDATA, &now);

    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, TRUE);

    curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1);

    ret = curl_easy_perform(curl_handle);

    curl_easy_cleanup(curl_handle);

    fclose(tmp);

    if (ret == 0) return TRUE;

    unlink(loc);
    printerror("Couldn't download %s: %s", url, buf);
    return FALSE;
}


static gboolean curl_exists(const char *url)
{
    CURL *curl_handle;
    CURLcode ret;

    curl_handle = curl_easy_init();

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, autofail_callback);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 15);
    curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1);

    ret = curl_easy_perform(curl_handle);

    curl_easy_cleanup(curl_handle);

    /* If the connection is made and the file exists, the write
       function 'autofail_callback' will be called (which then signals
       an error and causes libcurl to return a write error
       ("CURLE_WRITE_ERROR").  Otherwise, some other error will be
       returned, meaning the file cannot be retrieved. */
    return (ret == CURLE_WRITE_ERROR);
}


static int progress_callback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
    int remaining;
    float rate;
    static int last_dl = 0;

    GString *buf = g_string_new("DOWNLOAD-PROGRESS\n");
    rate = (dlnow - last_dl) / 1024;
    last_dl = dlnow;
    g_string_append_printf(buf, "%d\n", (int) rate);

    if ((dltotal != 0) && (dltotal >= dlnow) && (rate > 0))
    {
	g_string_append_printf(buf, "%d\n", (int) ((dlnow / dltotal) * 100) );
	remaining = (dltotal - dlnow) / (1024 * rate);
	g_string_append_printf(buf, "%02d:%02d\n", (int) (remaining / 60), (remaining % 60));
    }
    else if (dlnow == 0)
    {
	g_string_append_printf(buf, "0\n");
	g_string_append_printf(buf, "??:??\n");
    }
    else
    {
	g_string_append_printf(buf, "?\n");
	g_string_append_printf(buf, "??:??\n");
    }
    out(buf->str);
    g_string_free(buf, TRUE);
    wait_for_frontend();
    return 0;
}

static size_t autofail_callback(void *ptr, size_t size, size_t nmemb, void *stream)
{
    return 0; /* causes libcurl to disconnect with CURLE_WRITE_ERROR */
}


static void print_usage()
{
    printf("Usage: luau-download [OPTION] ...\n");
    printf("\n");
    printf("Download Luau provided files.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -u, --updates='URL'       updates URL to use\n");
    printf("  -f, --file='URL'          download the specified file\n\n");

    printf("  -n, --name='NAME'         name of update to download\n");
    printf("  -i, --interface='VERSION' specify an interface version to conform to\n");
    printf("  -v, --version='VERSION'   specify an exact version to download\n");
    printf("  -k, --package='VERSION'   specify an exact package version to download\n\n");
    
    printf("  -V, --instvers='VERSION'  installed program version number\n");
    printf("  -K, --instpkg='VERSION'   installed package version number\n\n");

    printf("  -m, --meta                check for and download .meta file, if available\n\n");

    printf("  -o, --output='PATH'       file or directory to download to\n");
    printf("  -h, --help                display this message\n");
    printf("\n");
    printf("  The options --url and --file are mutually exclusive.  Use the first to\n");
    printf("  download files where you know the location of the updates file, and use the\n");
    printf("  second when you know the exact URL of the file you want. The --name,\n");
    printf("  --interface, and --meta options may be used with --url, but not with --file.\n");
    printf("\n");
}

static struct option *get_long_options()
{
    struct option *options = (struct option *) calloc(12, sizeof(struct option));

    options[0].name = "version";
    options[0].has_arg = 1;
    options[0].flag = NULL;
    options[0].val = 'v';

    options[1].name = "updates";
    options[1].has_arg = 1;
    options[1].flag = NULL;
    options[1].val = 'u';

    options[2].name = "name";
    options[2].has_arg = 1;
    options[2].flag = NULL;
    options[2].val = 'n';

    options[3].name = "m";
    options[3].has_arg = 0;
    options[3].flag = NULL;
    options[3].val = 'm';

    options[4].name = "file";
    options[4].has_arg = 1;
    options[4].flag = NULL;
    options[4].val = 'f';

    options[5].name = "help";
    options[5].has_arg = 0;
    options[5].flag = NULL;
    options[5].val = 'h';

    options[6].name = "output";
    options[6].has_arg = 1;
    options[6].flag = NULL;
    options[6].val = 'o';

    options[7].name = "interface";
    options[7].has_arg = 1;
    options[7].flag = NULL;
    options[7].val = 'i';
    
    options[8].name = "package";
    options[8].has_arg = 1;
    options[8].flag = NULL;
    options[8].val = 'k';
    
    options[9].name = "instvers";
    options[9].has_arg = 1;
    options[9].flag = NULL;
    options[9].val = 'V';
    
    options[10].name = "instpkg";
    options[10].has_arg = 1;
    options[10].flag = NULL;
    options[10].val = 'K';

    memset(&options[11], 0, sizeof(struct option));

    return options;
}

