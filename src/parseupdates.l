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

%s SOFT
%s MSG
%s LIB
%s PKGS
%x INVALID
%x LONGDESC

%option yylineno
%option noyywrap

%{

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "libuau.h"
#include "util.h"
#include "error.h"
#include "parse.h"
#include "parseupdates.h"

#ifdef WITH_DMALLOC
#  include <dmalloc.h>
#endif

GPtrArray *updates;
GString *tempString;
AUpdate *curr;
int lastCond;

static APackage* getPkgInfo(char* string);
static void initializeUpdate(AUpdate *update);

%}

/* DATE format: MM/DD/YYYY HH:MM:SS */
DATE	[[:digit:]]{1,2}"/"[[:digit:]]{1,2}"/"[[:digit:]]{4}
SPACE	[ \n\t]
ID	[a-z0-9\\\/\\.-_]+

%%

<INITIAL>{
	"UPDATE"[[:blank:]]+[[:alpha:]]+	{
		char* type = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "UPDATE ")); /* skip string "UPDATE" and any blanks */
		/* Create a new AUpdate object */
		curr = (AUpdate*) g_malloc(sizeof(AUpdate));
		memset(curr, 0, sizeof(AUpdate));
		curr->keywords = g_ptr_array_new();
		g_ptr_array_add(updates, curr); /* ... and add it to the list */

		//initializeUpdate(curr);
		
		if      (lutil_strcaseeq(type, "SOFTWARE")) { curr->type = LUAU_SOFTWARE ; BEGIN(SOFT); }
		else if (lutil_strcaseeq(type, "MESSAGE" )) { curr->type = LUAU_MESSAGE  ; BEGIN(MSG);  }
		else if (lutil_strcaseeq(type, "LIBUPDATE")){ curr->type = LUAU_LIBUPDATE; BEGIN(LIB);  }
		else { 
			ERROR("Unknown UPDATE type specified at line %d: %s - Skipping", yylineno, type);
			BEGIN(INVALID);
		}
		DBUGOUT("Found new update of type %s (%d)", type, curr->type);
	}
}

<SOFT>{
  <MSG>{ 
   <LIB>{
	"ID"[[:blank:]]+{ID}	{
		char* id = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "ID ")); /* skip past "ID " and any blank */
		DBUGOUT("... ID = %s", id);
		curr->id = g_strdup(id);
	}
	"KEYWORDS"[[:blank:]]+.+	{
		char *string = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "KEYWORDS "));
		char *keyword = lutil_parse_nextToken(string);
		GPtrArray *allKeywords = g_ptr_array_new();
		
		g_ptr_array_add(allKeywords, keyword);
		while ((keyword = lutil_parse_nextToken(NULL)) != NULL) {
			g_ptr_array_add(allKeywords, g_strdup(keyword));
		}
		
		curr->keywords = allKeywords;
	}
	"DATE"[[:blank:]]+{DATE}	{
		char* date = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "DATE "));
		DBUGOUT("... DATE = %s", date);
		curr->date = g_malloc(sizeof(ADate));
		luau_parseDate(curr->date, date);
	}
	"SHORT"[[:blank:]]+.+	{
		char *shortDesc = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "SHORT "));
		DBUGOUT("... Short desc = %s", shortDesc);
		curr->shortDesc = g_strdup(shortDesc);
	}
	"LONG"[[:blank:]]*\n	{
		lastCond = YY_START;
		DBUGOUT("... Found long desc ...");
		tempString = g_string_new("");
		BEGIN(LONGDESC);
	}
	"ENDUPDATE"	{
		DBUGOUT("End of this update (type %d).", curr->type);
		BEGIN(INITIAL);
	}
	
	"VALIDFROM"[[:blank:]]+{DATE}	{
		char* date = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "VALIDFROM "));
		DBUGOUT("... ValidFrom = %s", date);
		curr->validFrom = g_malloc(sizeof(ADate));
		luau_parseDate(curr->validFrom, date);
	}
	"VALIDTO"[[:blank:]]+{DATE}	{
		char* date = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "VALIDTO "));
		DBUGOUT("... ValidTo = %s", date);
		curr->validTo = g_malloc(sizeof(ADate));
		luau_parseDate(curr->validTo, date);
	}
}}}

<SOFT>{
	"PACKAGES"	{
		curr->packages = g_ptr_array_new();
		DBUGOUT("... Found some packages ...");
		BEGIN(PKGS);
	}
	"SETDATE"[[:blank:]]+{DATE}	{
		char* date = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "SETDATE "));
		DBUGOUT("... SetDate = %s", date);
		curr->newDate = luau_parseDate(date);
	}
	"SETVERSION"[[:blank:]]+.+ {
		char* version = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "SETVERSION "));
		DBUGOUT("... SetVersion = %s", version);
		curr->newVersion = version;
	}
}

<LIB>{
	"SETURL"[[:blank:]]+[^[:blank:]\n]+	{
		char* url = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "SETURL "));
		DBUGOUT("... SetURL = %s", url);
		curr->newURL = g_strdup(url);
	}
}

<LONGDESC>{
	.|"\n"	{
		g_string_append(tempString, yytext);
	}
	[[:blank:]]*"ENDLONG"[[:blank:]]*	{
		DBUGOUT("... End of long description.");
		curr->fullDesc = tempString->str;
		g_string_free(tempString, FALSE);
		BEGIN(lastCond);
	}
}

<PKGS>{
	"RPM"[[:blank:]]+[[:digit:]]+" "[[:alnum:]]{32}" "[^\n]+	{
		APackage *pkg;
		char *string = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "RPM "));
		
		DBUGOUT("... of type RPM");
		
		pkg = getPkgInfo(string);
		pkg->type = LUAU_RPM;
		
		g_ptr_array_add(curr->packages, pkg);
		curr->availableFormats = curr->availableFormats | LUAU_RPM;
	}

	"DEB"[[:blank:]]+[[:digit:]]+" "[[:alnum:]]{32}" "[^\n]+	{
		APackage *pkg;
		char *string = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "DEB "));
		
		DBUGOUT("... of type DEB");
		
		pkg = getPkgInfo(string);
		pkg->type = LUAU_DEB;
		
		g_ptr_array_add(curr->packages, pkg);
		curr->availableFormats = curr->availableFormats | LUAU_DEB;

	}

	"SRC"[[:blank:]]+[[:digit:]]+" "[[:alnum:]]{32}" "[^\n]+	{
		APackage *pkg;
		char *string = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "SRC "));
		
		DBUGOUT("... of type SRC");

		pkg = getPkgInfo(string);
		pkg->type = LUAU_SRC;
		
		g_ptr_array_add(curr->packages, pkg);
		curr->availableFormats = curr->availableFormats | LUAU_SRC;
	}
	
	"EXEC"[[:blank:]]+[[:digit:]]+" "[[:alnum:]]{32}" "[^\n]+	{
		APackage *pkg;
		char *string = lutil_parse_deleteWhitespace(lutil_parse_skipString(yytext, "EXEC "));
		
		DBUGOUT("... of type EXEC");
		
		pkg = getPkgInfo(string);
		pkg->type = LUAU_EXEC;
		
		g_ptr_array_add(curr->packages, pkg);
		curr->availableFormats = curr->availableFormats | LUAU_EXEC;
	}
	
	"ENDPACKAGES"	{
		DBUGOUT("... Finished parsing packages");
		BEGIN(SOFT);
	}
}	

<INVALID>{
	.|"\n"	/* Skip over the whole update, since we don't recognize it */
	"ENDUPDATE"	BEGIN(INITIAL);
}

{SPACE}+	/* discard extraneous whitespace */


<*>.	{
	ERROR("Extra character %s found at line %d: skipping", yytext, yylineno);
	}

%%

GPtrArray *
luau_parseUpdateFile(char *contents) {
	YY_BUFFER_STATE state = yy_scan_string(contents);
	
	updates = g_ptr_array_new();
	yylex();
	
	yy_delete_buffer(state);
	
	return updates;
}

/* Non-Interface Methods */

static APackage *
getPkgInfo(char* string) {
	APackage *info = (APackage*) g_malloc(sizeof(APackage));
	char *size, *md5, *url;
	
	memset(info, 0, sizeof(APackage));
	
	size = strtok(string, " ");
	md5 = strtok(NULL, " ");
	url = strtok(NULL, " ");
	
	info->size = atoll(size);
	info->loc = g_strdup(url);
	strncpy(info->md5sum, md5, 33);
	info->md5sum[32] = '\0';
	
	return info;
}
