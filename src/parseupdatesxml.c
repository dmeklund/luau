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

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <glib.h>

#include "libuau.h"
#include "util.h"
#include "error.h"
#include "parse.h"
#include "parseupdates.h"

#ifdef WITH_DMALLOC
#  include <dmalloc.h>
#endif

G_LOCK_DEFINE_STATIC (parse);

typedef struct {
	char *id;
	GContainer *mirrors;
} AMirrorList;

typedef struct {
	char *id;
	char *url;
} AMirror;

typedef struct {
	GContainer *definedMirrors;
	GContainer *definedMirrorLists;
	GContainer *multAttributes;
	GContainer *singAttributes;
} ASetAttributes;

typedef struct {
	char *id;
	char *value;
} ASingAttribute;

typedef struct {
	char *id;
	GContainer *values;
} AMultAttribute;

static GContainer *updates;
static AUpdate *currUpdate;

static void parseUpdates  (xmlDocPtr doc, xmlNodePtr node);
static void parseUpdate   (xmlDocPtr doc, xmlNodePtr node, xmlChar *type, ASetAttributes *attributes);

static void parseProgInfo   (AProgInfo *progInfo, xmlDocPtr doc, xmlNodePtr node, GError **err);
static void parseProgInfoTag(AProgInfo *progInfo, xmlDocPtr doc, xmlNodePtr node);

static void parseMirrorList(xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes);
static void parseMirrorDef (xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes, char *list_id);
static void addToMirrorList(ASetAttributes *attributes, const char *list_id, const char *mirror_id);

static void parseSoftware   (xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes);
static void parsePackage    (xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes);
static void parseMessage    (xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes);
static void parseLibupdate  (xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes);
static void parseGenericInfo(xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes);
static void parseUpdateInfo (xmlDocPtr doc, xmlNodePtr node, AUpdateType type, ASetAttributes *attributes);
static void parsePkgGroup   (xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes);

static GContainer* parsePackageProperties  (xmlNodePtr node, APackage *pkg, ASetAttributes *attributes);
static int parsePackageAttrMirrors (APackage *pkg, ASetAttributes *attributes, const char *suffix, const char *loc);
static int parsePackageChildMirrors(xmlDocPtr doc, xmlNodePtr node, APackage *pkg, const char *suffix);

static gboolean parseQuantData(void **data, char *str, AQuantDataType quantDataType);

static AMultAttribute* appendStringToAttributeList(ASetAttributes *attributes, const char *id, const char *value);

static ASingAttribute* setAttributeValue (ASetAttributes *attributes, const char *id, const char *value);
static GContainer*     getAttributeList  (ASetAttributes *attributes, const char *id);
static char*           getAttributeString(ASetAttributes *attributes, const char *id);

static GContainer* parseMirrorIDs(GContainer *ids, ASetAttributes *attributes);

static void initializeSetAttributes(ASetAttributes *attributes);
static void copySetAttributes(ASetAttributes *newSet, ASetAttributes *attributes, gboolean deepCopy);
static void freeSetAttributes(ASetAttributes *attributes, gboolean deepFree);
static void freeSingAttribute(ASingAttribute *attr);
static void freeMultAttribute(AMultAttribute *attrs);


/**
 * Given an Luau XML repository file in memory (ie., as a character string), read
 * it in and parse it, returning the results (a list of updates) in a GContainer
 * of type GCONT_LIST.
 *
 * @arg contents is a string representing a Luau XML repository file
 * @arg err is a GError which will store any recoverable run-time errors
 * @return a GContainer of type GCONT_LIST of updates.
 */
GContainer *
luau_parseXML_updates(char *contents, GError **err) {
	xmlDocPtr doc;
	xmlNodePtr node;
	xmlChar *interfaceStr;
	AInterface xmlInterface, readableInterface;
	gboolean result;
	GContainer *ret;
	
	g_return_val_if_fail(err == NULL || *err == NULL, NULL);
	
	G_LOCK (parse);
	
	/* Tell libxml2 to store line number info for more helpful error output */
	xmlLineNumbersDefault(1);
	
	updates = g_container_new(GCONT_LIST);
	doc = xmlParseMemory(contents, strlen(contents));
	node = xmlDocGetRootElement(doc);
	if (node == NULL) {
		g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_FAILED, "XML file could not be parsed");
		xmlFreeDoc(doc);
		return NULL;
	}
	
	if (! xmlStrEqual(node->name, (const xmlChar *) "luau-repository")) {
		g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_FAILED, "XML error: Invalid root element");
		xmlFreeDoc(doc);
		return NULL;
	}
	
	interfaceStr = xmlGetProp(node, "interface");
	result = luau_parseInterface(&xmlInterface, interfaceStr);
	xmlFree(interfaceStr);
	
	if (result == FALSE)
		DBUGOUT("XML interface version either not specified or unreadable: may encounter problems");
	else {
		readableInterface.major = LUAU_XML_INTERFACE_MAJOR;
		readableInterface.minor = LUAU_XML_INTERFACE_MINOR;
		if (!luau_satisfiesInterface(&readableInterface, &xmlInterface))
			DBUGOUT("Unsupported XML interface specified - will continue, but will most likely encounter errors");
	}
	
	parseUpdates(doc, node);
	
	xmlFreeDoc(doc);
	
	ret = updates;
	
	G_UNLOCK (parse);
	
	return ret;
}

gboolean
luau_parseXML_progInfo(GString *contents, AProgInfo *progInfo, GError **err) {
	xmlDocPtr doc;
	xmlNodePtr node;
	xmlChar *interfaceStr;
	AInterface xmlInterface, readableInterface;
	gboolean result;
	
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
	
	memset(progInfo, 0, sizeof(AProgInfo));

	G_LOCK (parse);
	
	doc = xmlParseMemory(contents->str, contents->len);
	
	G_UNLOCK (parse);
	
	node = xmlDocGetRootElement(doc);
	if (node == NULL) {
		g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_FAILED, "XML file could not be parsed");
		xmlFreeDoc(doc);
		return FALSE;
	}
	
	if (! xmlStrEqual(node->name, (const xmlChar *) "luau-repository")) {
		g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_FAILED, "XML error: Invalid root element");
		xmlFreeDoc(doc);
		return FALSE;
	}
	
	interfaceStr = xmlGetProp(node, "interface");
	if (interfaceStr == NULL) {
		result = FALSE;
	} else {
		result = luau_parseInterface(&xmlInterface, interfaceStr);
		xmlFree(interfaceStr);
	}
	
	if (result == FALSE)
		DBUGOUT("XML interface version either not specified or unreadable: may encounter problems");
	else {
		readableInterface.major = LUAU_XML_INTERFACE_MAJOR;
		readableInterface.minor = LUAU_XML_INTERFACE_MINOR;
		if (!luau_satisfiesInterface(&readableInterface, &xmlInterface))
			DBUGOUT("Unsupported XML interface specified - will continue, but will most likely encounter errors");
	}
	
	parseProgInfo(progInfo, doc, node, err);
	
	xmlFreeDoc(doc);
	
	G_UNLOCK (parse);
	
	return TRUE;
}

/* Non-Interface Methods */

static void
parseUpdates(xmlDocPtr doc, xmlNodePtr node) {
	xmlChar *type;
	gboolean foundProgInfo = FALSE;
	ASetAttributes attributes;
	
	initializeSetAttributes(&attributes);
	
	for (node = node->xmlChildrenNode; node != NULL; node = node->next) {
		if (xmlStrEqual(node->name, (const xmlChar *) "update")) {
			type = xmlGetProp(node, "type");
			parseUpdate(doc, node, type, &attributes);
			xmlFree(type);
		} else if (xmlStrEqual(node->name, (const xmlChar *) "software")) {
			parseUpdate(doc, node, "software", &attributes);
			currUpdate->newVersion = xmlGetProp(node, "version");
			if (currUpdate->id == NULL)
				currUpdate->id = g_strdup(currUpdate->newVersion);
		} else if (xmlStrEqual(node->name, (const xmlChar *) "program-info")) {
			/* we can skip this since it isn't relevant to parsing updates - we do, however,
			   check to make sure only one <program-info> tag is specified */
		   if (foundProgInfo)
				DBUGOUT("Two <program-info> tags found - only one is allowed");
		   foundProgInfo = TRUE;
		} else if (xmlStrEqual(node->name, (const xmlChar *) "mirror-list")) {
			parseMirrorList(doc, node, &attributes);
		} else if (xmlStrEqual(node->name, (const xmlChar *) "comment")) {
			// do nothing
		} else if (! (xmlStrEqual(node->name, (const xmlChar *) "text")) ) {
			DBUGOUT("Only tags allowed in <luau-repository> root tag are <update>, <software>, and <program-info>; found '%s', skipping", (const char*) node->name);
		}
	}
	
	if (!foundProgInfo)
		DBUGOUT("No <program-info> tag found - required by DTD");
	
	freeSetAttributes(&attributes, TRUE);
}

static void
parseProgInfo(AProgInfo *progInfo, xmlDocPtr doc, xmlNodePtr node, GError **err) {
	gboolean found = FALSE;
	
	g_return_if_fail(err == NULL || *err == NULL);
	
	for (node = node->xmlChildrenNode; node != NULL; node = node->next) {
		if (xmlStrEqual(node->name, (const xmlChar *) "program-info")) {
			parseProgInfoTag(progInfo, doc, node);
			found = TRUE;
			break;
		}
	}
	
	if (!found)
		g_set_error(err, LUAU_BASE_ERROR, LUAU_BASE_ERROR_FAILED, "Couldn't parse program information: none available in provided file");
}

static void
parseProgInfoTag(AProgInfo *progInfo, xmlDocPtr doc, xmlNodePtr node) {
	static const char *symbols[ ] = {"shortname", "s",
	                                 "fullname",  "f",
	                                 "desc",      "d",
	                                 "keyword",   "k",
	                                 "url",       "u",
									 "text",      "t",
									 "comment",   "c",
	                                  NULL };
	char result, *temp;
	
	progInfo->id = xmlGetProp(node, "id");
	progInfo->interface.major = -1;
	progInfo->interface.minor = -1;
	
	for (node = node->xmlChildrenNode; node != NULL; node = node->next) {
		result = lutil_parse_parseSymbolArray((const char *)node->name, symbols);
		switch (result) {
			case 's':
				temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
				progInfo->shortname = g_strdup(lutil_parse_deleteWhitespace(temp));
				xmlFree(temp);
				break;
			case 'f':
				temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
				progInfo->fullname = g_strdup(lutil_parse_deleteWhitespace(temp));
				xmlFree(temp);
				break;
			case 'd':
				temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
				progInfo->desc = g_strdup(lutil_parse_deleteWhitespace(temp));
				xmlFree(temp);
				break;
			case 'k':
				temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
				if (progInfo->keywords == NULL)
					progInfo->keywords = g_ptr_array_new();
				
				g_ptr_array_add(progInfo->keywords, g_strdup(lutil_parse_deleteWhitespace(temp)));
				xmlFree(temp);
				break;
			case 'u':
				temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
				progInfo->url = g_strdup(lutil_parse_deleteWhitespace(temp));
				xmlFree(temp);
				break;
			case -1:
				DBUGOUT("Unrecognized <%s> tag in <program-info> in XML file: Skipping", node->name);
				break;
		}
	}
}

static void
parseMirrorList(xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes)
{
	char *id;
	
	id = (char*) xmlGetProp(node, "id");
	
	for (node = node->xmlChildrenNode; node != NULL; node = node->next)
	{
		if (xmlStrEqual(node->name, (const xmlChar *) "mirror-def"))
		{
			parseMirrorDef(doc, node, attributes, id);
		}
		else if (!xmlStrEqual(node->name, (const xmlChar *) "text") &&
		         !xmlStrEqual(node->name, (const xmlChar *) "comment"))
		{
			DBUGOUT("Unrecognized tag <%s> in <mirror-list>", node->name);
		}
	}
	
	g_free(id);
}

static void
parseMirrorDef(xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes, char *list_id)
{
	ASingAttribute *attr;
	char *temp, *url, *id;
	
	id = (char*) xmlGetProp(node, "id");
	if (id == NULL || id[0] == '\0')
	{
		xmlFree(id);
		DBUGOUT("No mirror ID specified at line %ld", xmlGetLineNo(node));
		return;
	}
	
	temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
	url = g_strdup(lutil_parse_deleteWhitespace(temp));
	xmlFree(temp);
	if (url == NULL || url[0] == '\0')
	{
		xmlFree(url);
		DBUGOUT("No URL specified for mirror '%s' at line %ld", id, xmlGetLineNo(node));
		return;
	}
	
	attr = g_malloc(sizeof(ASingAttribute));
	attr->id = id;
	attr->value = url;
	
	g_container_add(attributes->definedMirrors, attr);
	if (list_id != NULL)
		addToMirrorList(attributes, list_id, id);
}

static void
addToMirrorList(ASetAttributes *attributes, const char *list_id, const char *mirror_id)
{
	AMirrorList *mirrLst;
	GIterator iter;
	gboolean found, result;
	
	found = FALSE;
	
	result = g_container_get_iter(&iter, attributes->definedMirrorLists);
	if (result)
	{
		while (!found && g_iterator_hasNext(&iter))
		{
			mirrLst = g_iterator_next(&iter);
			
			if (lutil_streq(list_id, mirrLst->id))
			{
				g_container_add(mirrLst->mirrors, g_strdup(mirror_id));
				found = TRUE;
			}
		}
	}
	if (!found)
	{
		mirrLst = g_malloc(sizeof(AMirrorList));
		mirrLst->id = g_strdup(list_id);
		mirrLst->mirrors = g_container_new(GCONT_LIST);
		g_container_add(mirrLst->mirrors, g_strdup(mirror_id));
		g_container_add(attributes->definedMirrorLists, mirrLst);
	}
}
                                                                                            
	
static void
parseUpdate(xmlDocPtr doc, xmlNodePtr node, xmlChar *type, ASetAttributes *attributes) {
	if (type == NULL) {
		DBUGOUT("No type specified for update: skipping");
		return;
	}
	
	/* Create and initialize a new AUpdate object */
	currUpdate = (AUpdate*) g_malloc(sizeof(AUpdate));
	memset(currUpdate, 0, sizeof(AUpdate));
	currUpdate->keywords = g_ptr_array_new();
	currUpdate->packages = g_ptr_array_new();
	currUpdate->quantifiers = NULL;
	g_container_add(updates, currUpdate); /* ... and add it to the list */
	
	
	if      (xmlStrcasecmp(type, "software")    == 0) { currUpdate->type = LUAU_SOFTWARE;  }
	else if (xmlStrcasecmp(type, "message" )    == 0) { currUpdate->type = LUAU_MESSAGE;   }
	else if (xmlStrcasecmp(type, "luau-config") == 0) { currUpdate->type = LUAU_LIBUPDATE; }
	else { 
		DBUGOUT("Unknown 'update' type specified: %s - Skipping", type);
		return;
	}
	DBUGOUT("Found update of type %s", type);
	
	parseUpdateInfo(doc, node, currUpdate->type, attributes);
}

static void
parseUpdateInfo(xmlDocPtr doc, xmlNodePtr node, AUpdateType type, ASetAttributes *attributes) {
	parseGenericInfo(doc, node, attributes);
	if (type == LUAU_SOFTWARE)
		parseSoftware(doc, node, attributes);
	if (type == LUAU_MESSAGE)
		parseMessage(doc, node, attributes);
	else if (type == LUAU_LIBUPDATE)
		parseLibupdate(doc, node, attributes);
}

static void
parseGenericInfo(xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes) {
	static const char *symbols[ ] = {"id",       "i",
	                                 "short",    "s",
	                                 "long",     "l",
	                                 "keyword",  "k",
	                                 "date",     "d",
	                                 "valid",    "v",
	                                  NULL };
	char result, *temp;
	AQuantifier *quant = NULL;
	AQuantDataType quantDataType;
	
	for (node = node->xmlChildrenNode; node != NULL; node = node->next) {
		result = lutil_parse_parseSymbolArray((const char *)node->name, symbols);
		switch (result) {
			case 'i':
				temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
				currUpdate->id = g_strdup(lutil_parse_deleteWhitespace(temp));
				xmlFree(temp);
				break;
			case 's':
				temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
				currUpdate->shortDesc = g_strdup(lutil_parse_deleteWhitespace(temp));
				xmlFree(temp);
				break;
			case 'l':
				temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
				currUpdate->fullDesc = g_strdup(lutil_parse_deleteWhitespace(temp));
				xmlFree(temp);
				break;
			case 'k':
				temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
				g_ptr_array_add(currUpdate->keywords, g_strdup(lutil_parse_deleteWhitespace(temp)));
				xmlFree(temp);
				break;
			case 'd':
				temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
				currUpdate->date = g_malloc(sizeof(ADate));
				luau_parseDate(currUpdate->date, lutil_parse_deleteWhitespace(temp));
				xmlFree(temp);
				break;
			case 'v':
				temp = (char*) xmlGetProp(node, "type");
				
				quantDataType = luau_parseQuantDataType(temp);
				
				if (quantDataType == LUAU_QUANT_DATA_INVALID) {
					DBUGOUT("Unsupported quantifier data type: '%s'", temp);
					xmlFree(temp);
					break;
				}
				xmlFree(temp);
				
				if (currUpdate->quantifiers == NULL)
					currUpdate->quantifiers = g_ptr_array_new();
				
				temp = (char*) xmlGetProp(node, "from");
				if (temp != NULL) {
					quant = g_malloc(sizeof(AQuantifier));
					quant->qtype = LUAU_QUANT_FROM;
					quant->dtype = quantDataType;
					parseQuantData(&(quant->data), temp, quantDataType);
					
					g_ptr_array_add(currUpdate->quantifiers, quant);
					
					xmlFree(temp);
				}
				
				temp = (char*) xmlGetProp(node, "to");
				if (temp != NULL) {
					quant = g_malloc(sizeof(AQuantifier));
					quant->qtype = LUAU_QUANT_TO;
					quant->dtype = quantDataType;
					parseQuantData(&(quant->data), temp, quantDataType);
					
					g_ptr_array_add(currUpdate->quantifiers, quant);
					
					xmlFree(temp);
				}
				
				temp = (char*) xmlGetProp(node, "for");
				if (temp != NULL) {
					quant = g_malloc(sizeof(AQuantifier));
					quant->qtype = LUAU_QUANT_FOR;
					quant->dtype = quantDataType;
					parseQuantData(&(quant->data), temp, quantDataType);
					
					g_ptr_array_add(currUpdate->quantifiers, quant);
					
					xmlFree(temp);
				}
				
				break;
		}
	}
}

static void
parseSoftware(xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes) { 
	static const char *symbols[ ] = {"package",         "p",
									 "interface",       "i",
									 "package-group",   "g",
									 "display-version", "d",
	                                  NULL };
	char result, *temp;
	
	for (node = node->xmlChildrenNode; node != NULL; node = node->next) {
		result = lutil_parse_parseSymbolArray((const char *)node->name, symbols);
		switch (result) {
			case 'p':
				parsePackage(doc, node, attributes);
				break;
				
			case 'i':
				temp = (char*) xmlGetProp(node, "version");
				luau_parseInterface(&(currUpdate->interface), temp);
				xmlFree(temp);
				
				break;
				
			case 'g':
				parsePkgGroup(doc, node, attributes);
				break;
				
			case 'd':
				temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
				currUpdate->newDisplayVersion = g_strdup(lutil_parse_deleteWhitespace(temp));
				xmlFree(temp);
				break;
		}
	}
}

static void
parsePackage(xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes) {
	ASetAttributes newAttributes;
	GPtrArray *mirrors;
	GContainer *allocated;
	APackage *pkg;
	char *loc, *temp, *suffix;
	unsigned int i, total;
	
	copySetAttributes(&newAttributes, attributes, FALSE);
	
	pkg = (APackage*) g_malloc(sizeof(APackage));
	g_ptr_array_add(currUpdate->packages, pkg);
	pkg->mirrors = g_ptr_array_new();
	mirrors = pkg->mirrors;
	
	suffix = getAttributeString(&newAttributes, "filename");
	
	temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
	loc = g_strdup(lutil_parse_deleteWhitespace(temp));
	xmlFree(temp);
	
	if (loc != NULL && loc[0] == '\0') {
		g_free(loc);
		loc = NULL;
	}

	allocated = parsePackageProperties(node, pkg, &newAttributes);
	total  = parsePackageAttrMirrors(pkg, &newAttributes, suffix, loc);
	total += parsePackageChildMirrors(doc, node, pkg, suffix);
	
	if (total != 100 && total != 0) {
		int n, sum;
		float factor;
		
		factor = ((float)100 / total);
		sum = 0;
		
		DBUGOUT("Rescaling mirror weights by factor of %.01f (total = %d)", factor, total);
		for (i = 0; i < mirrors->len; i += 2) {
			if (i+2 == mirrors->len) /* last mirror */
				n = 100 - sum;
			else {
				n = GPOINTER_TO_INT (g_ptr_array_index(mirrors, i));
				n = (int) (n*factor);
			}
			
			mirrors->pdata[i] = GINT_TO_POINTER (n);
			
			sum += n;
		}
	}

	
#ifdef DEBUG
	for (i = 0; i < pkg->mirrors->len; i += 2)
		DBUGOUT("URL: %s; weight: %d\n",
				(char*)g_ptr_array_index(mirrors, i+1),
				GPOINTER_TO_INT (g_ptr_array_index(mirrors, i)));
#endif /* DEBUG */

	
	g_free(loc);
	if (allocated != NULL)
		g_container_destroy(allocated);
	
	freeSetAttributes(&newAttributes, FALSE);
}
	

static GContainer *
parsePackageProperties(xmlNodePtr node, APackage *pkg, ASetAttributes *attributes)
{
	GContainer *allocated;
	GPtrArray *mirrors;
	char *temp;
	
	allocated = NULL;
	
	mirrors = pkg->mirrors;
	
	temp = (char*) xmlGetProp(node, "md5");
	if (temp != NULL) {
		strncpy(pkg->md5sum, temp, 33);
		xmlFree(temp);
	} else if ( (temp = getAttributeString(attributes, "md5")) != NULL ) {
		strncpy(pkg->md5sum, temp, 33);
	} else {
		pkg->md5sum[0] = '\0';
	}
	
	temp = (char*) xmlGetProp(node, "size");
	if (temp != NULL) {
		pkg->size = atoi(temp);
		xmlFree(temp);
	} else if ( (temp = getAttributeString(attributes, "size")) != NULL ) {
		pkg->size = atoi(temp);
	} else {
		pkg->size = 0;
	}
	
	temp = (char*) xmlGetProp(node, "type");
	if (temp != NULL) {
		pkg->type = luau_parsePkgType(temp);
		xmlFree(temp);
	} else if ( (temp = getAttributeString(attributes, "type")) != NULL ) {
		pkg->type = luau_parsePkgType(temp);
	} else {
		pkg->type = LUAU_UNKNOWN;
	}
	
	temp = (char*) xmlGetProp(node, "mirror-id");
	if (temp != NULL)
	{
		AMultAttribute *attrs;
		
		allocated = g_container_new(GCONT_LIST);
		
		attrs = appendStringToAttributeList(attributes, "mirror-id", temp);
		g_container_add(allocated, attrs->id);
		g_container_add(allocated, attrs->values);
		g_container_concat(allocated, attrs->values);
		g_container_add(allocated, attrs);
		
		xmlFree(temp);
	}
	
	pkg->version = (char*) xmlGetProp(node, "version");
	if (pkg->version == NULL)
		pkg->version = getAttributeString(attributes, "version");
		
	currUpdate->availableFormats = (currUpdate->availableFormats | pkg->type);
	
	return allocated;
}

static int
parsePackageAttrMirrors(APackage *pkg, ASetAttributes *attributes, const char *suffix, const char *loc)
{
	GPtrArray *mirrors;
	GContainer *mirrorList, *mirrorIDs, *tmp;
	GIterator iter;
	gboolean result;
	char *temp, *prefix;
	int total = 0;
	
	mirrors = pkg->mirrors;
	
	mirrorList = getAttributeList(attributes, "mirror-url");
	mirrorIDs  = getAttributeList(attributes, "mirror-id");
	
	g_assert(mirrorList != NULL);
	g_assert(mirrorIDs  != NULL);
	
	if (mirrorIDs->len > 0)
	{
		tmp = parseMirrorIDs(mirrorIDs, attributes);
		g_container_concat(mirrorList, tmp);
		g_container_free(tmp, TRUE);
	}

	
	if (loc != NULL || suffix != NULL)
	{
		result = g_container_get_iter(&iter, mirrorList);
		if (result)
		{
			while (g_iterator_hasNext(&iter))
			{
				prefix = g_iterator_next(&iter);
				g_assert(prefix != NULL);
				
				/* We have to be careful not to pass any unintended NULL values
				   to vstrcreate, because a NULL value tells vstrcreate to terminate
				   (an unfortunate necessity to deal with C's unsavory handling of a
				   variable number of arguments) */
				if (suffix == NULL) /* loc != NULL */
					temp = lutil_vstrcreate(prefix, "/", loc, NULL);
				else if (loc == NULL) /* suffix != NULL */
					temp = lutil_vstrcreate(prefix, "/", suffix, NULL);
				else /* loc != NULL, suffix != NULL */
					temp = lutil_vstrcreate(prefix, "/", loc, "/", suffix, NULL);
				
				g_ptr_array_add(mirrors, GINT_TO_POINTER (100));
				g_ptr_array_add(mirrors, temp);
				total += 100;
			}
		}
	}
		
	if (mirrorList->len == 0 && loc != NULL)
	{
		if (suffix != NULL)
			temp = lutil_vstrcreate(loc, suffix, NULL);
		else
			temp = g_strdup(loc);
		
		g_ptr_array_add(mirrors, GINT_TO_POINTER (100));
		g_ptr_array_add(mirrors, temp);
		total += 100;
	}
	
	g_container_free(mirrorList, TRUE);
	g_container_free(mirrorIDs, TRUE);
	
	return total;
}

static int
parsePackageChildMirrors(xmlDocPtr doc, xmlNodePtr node, APackage *pkg, const char *suffix)
{
	GPtrArray *mirrors;
	char *percentage, *loc, *temp;
	int i, total;
	
	total = 0;
	
	if (node->xmlChildrenNode != NULL) {
		mirrors = pkg->mirrors;
		
		for (node = node->xmlChildrenNode; node != NULL; node = node->next) {
			if (lutil_streq(node->name, "mirror")) {
				percentage = (char*) xmlGetProp(node, "weight");
				if (percentage == NULL) {
					DBUGOUT("Weight for mirror not specified - using default");
					g_ptr_array_add(mirrors, GINT_TO_POINTER (100));
					total += 100;
				} else {
					i = atoi(percentage);
					if (i <= 0) i = 1;
					g_ptr_array_add(mirrors, GINT_TO_POINTER (i));
					xmlFree(percentage);
					
					DBUGOUT("Weight for mirror: %d", i);
					
					total += i;
				}
				
				temp = (char*) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
				loc = g_strdup(lutil_parse_deleteWhitespace(temp));
				xmlFree(temp);
				if (suffix != NULL) {
					temp = lutil_vstrcreate(loc, suffix, NULL);
					g_free(loc);
					loc = temp;
				}
				g_ptr_array_add(mirrors, loc);
				
				DBUGOUT("Mirror location: %s", loc);
			} else if (!lutil_streq(node->name, "text") && !lutil_streq(node->name, "comment")) {
				DBUGOUT("Invalid tag '<%s>' in <package> section (only '<mirror>' allowed)", node->name);
			}
		}
		
	}
	
	return total;
}

static void
parseMessage(xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes) {
	/* Nothing to do here!  There are (currently) no XML tags specific to message updates */
}

static void
parseLibupdate(xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes) {
	static const char *symbols[ ] = { "set", "s",
	                                  NULL };
	char result;
	
	for (node = node->xmlChildrenNode; node != NULL; node = node->next) {
		result = lutil_parse_parseSymbolArray((const char *)node->name, symbols);
		switch (result) {
			case 's':
				currUpdate->newURL = (char*) xmlGetProp(node, "url");
				break;
		}
	}
}

static gboolean
parseQuantData(void **data, char *str, AQuantDataType quantDataType) {
	gboolean result;
	
	if (quantDataType == LUAU_QUANT_DATA_VERSION || quantDataType == LUAU_QUANT_DATA_KEYWORD) {
		*data = g_strdup(str);
		result = TRUE;
	} else if (quantDataType == LUAU_QUANT_DATA_INTERFACE) {
		*data = g_malloc(sizeof(AInterface));
		result = luau_parseInterface((AInterface*) *data, str);
	} else if (quantDataType == LUAU_QUANT_DATA_DATE) {
		*data = g_malloc(sizeof(ADate));
		result = luau_parseDate((ADate*) *data, str);
	} else {
		ERROR("Internal Error: Unrecognized quantifier data type (%d)", quantDataType);
		result = FALSE;
	}
	
	return result;
}

static void
parsePkgGroup(xmlDocPtr doc, xmlNodePtr node, ASetAttributes *attributes)
{
	static const char *symbols[ ] = { "package", "p",
		                              "package-group", "g",
									  "text", "t",
									  "comment", "c",
	                                  NULL };
	static const char *sing_names[ ] = { "option", "type", "size", "md5", "filename", "version" };
	static const char *mult_names[ ] = { "mirror-url", "mirror-id" };
	static const int nSingNames = 6;
	static const int nMultNames = 2;
	
	ASingAttribute *singAttr;
	AMultAttribute *multAttr;
	ASetAttributes newAttributes;
	GContainer *newSingAttributes = g_container_new(GCONT_LIST);
	GContainer *newMultAttributes = g_container_new(GCONT_LIST);
	GIterator iter;
	char  *value, result;
	const char *name;
	int i;
	
	/* g_list_copy does only a shallow copy, but that's all right.  We do this so that
	   any new attributes we append on here will not affect the attributes passed in
	   by the calling function. */
	copySetAttributes(&newAttributes, attributes, FALSE);
	/* When we go through and eliminate any malloc'd memory at the end of this function
	   we need to be sure to -only- eliminate memory we allocated, not anything that
	   was in the list passed in.  So we keep a pointer to the last attribute given to
	   us and start with the one after that when we go through to free everything. */
	/*result = g_container_get_iter_last(&lastOldElement, attributes);
	if (!result)
		return;
	*/
	/* Collect any new attributes specified in this package-group tag */
	for (i = 0; i < nSingNames; ++i) {
		name = sing_names[i];
		value = (char*) xmlGetProp(node, name);
		if (value != NULL) {
			singAttr = setAttributeValue(&newAttributes, name, value);
			g_container_add(newSingAttributes, singAttr);
			xmlFree(value);
		}
	}
	for (i = 0; i < nMultNames; ++i) {
		name = mult_names[i];
		value = (char*) xmlGetProp(node, name);
		if (value != NULL) {
			multAttr = appendStringToAttributeList(&newAttributes, name, value);
			g_container_add(newMultAttributes, multAttr);
			xmlFree(value);
		}
	}
	
	/* Parse the children elements, passing along any attributes picked up */
	for (node = node->xmlChildrenNode; node != NULL; node = node->next) {
		result = lutil_parse_parseSymbolArray((const char *)node->name, symbols);
		
		switch (result) {
			case 'p':
				parsePackage(doc, node, &newAttributes);
				break;
			
			case 'g':
				parsePkgGroup(doc, node, &newAttributes);
				break;
			
			case -1:
				DBUGOUT("Unrecognized <%s> tag in <package-group> at line %ld in repository file: Skipping", node->name, xmlGetLineNo(node));
				break;

		}
	}
	
	/* Remember - we aren't actually freeing the attribute list passed into
	   the function (that would be a no no!): we're only freeing the -copy-
	   of those attributes we made at the beginning of this function, along
	   with any new ones we may've added) */
	result = g_container_get_iter(&iter, newSingAttributes);
	if (result)
	{
		while (g_iterator_hasNext(&iter))
		{
			singAttr = g_iterator_next(&iter);
			freeSingAttribute(singAttr);
			//g_free(singAttr);
		}
	}
	g_container_free(newSingAttributes, TRUE);
	
	result = g_container_get_iter(&iter, newMultAttributes);
	if (result)
	{
		while (g_iterator_hasNext(&iter))
		{
			multAttr = g_iterator_next(&iter);
			freeMultAttribute(multAttr);
			//g_free(multAttr);
		}
	}
	g_container_free(newMultAttributes, TRUE);
	
	/* A shallow free - again, not actually free'ing the pointers contained
	   in the list (except for what was done above) */
	freeSetAttributes(&newAttributes, FALSE);
}

static AMultAttribute *
appendStringToAttributeList(ASetAttributes *attributes, const char *id, const char *value)
{
	AMultAttribute *attrs;
	GContainer *newAttrs;
	
	g_assert(attributes != NULL);
	g_assert(id != NULL);
	g_assert(value != NULL);
	
	newAttrs = lutil_gsplit(";", value);
	
	attrs = g_malloc(sizeof(AMultAttribute));
	attrs->id = g_strdup(id);
	attrs->values = newAttrs;
	
	g_container_add(attributes->multAttributes, attrs);
	
	return attrs;
}
/*
static void
appendAttrToAttributeList(ASetAttributes *attributes, const char *id, ASingAttribute *new_attr)
{
	AMultAttribute *attr, *temp_attr;
	GIterator iter;
	gboolean found, result;
	
	found = FALSE;
	
	result = g_container_getIter(&iter, attributes->multAttributes
	for (curr = attributes; curr != NULL; curr = curr->next)
	{
		attr = curr->data;
		g_assert(attr != NULL);
		if (lutil_streq(id, attr->id))
		{
			found = TRUE;
			break;
		}
	}
	
	if (found)
	{
		found = FALSE;
		for (curr = attr->value; curr != NULL; curr = curr->next)
		{
			curr_attr = curr->data;
			g_assert(curr_attr != NULL);
			if (lutil_streq(curr_attr->id, new_attr->id))
			{
				curr_attr->value = g_list_append(curr_attr->value, new_attr->value);
				found = TRUE;
				break;
			}
		}
		
		if (!found)
		{
			temp_attr = g_malloc(sizeof(ANamedAttribute));
			temp_attr->id = new_attr->id;
			temp_attr->value = g_list_append(NULL, new_attr->value);
			attr->value = g_list_append(attr->value, temp_attr);
		}
	}
	else
	{
		temp_attr = g_malloc(sizeof(ANamedAttribute));
		temp_attr->id = new_attr->id;
		temp_attr->value = g_list_append(NULL, new_attr->value);
		
		attr = g_malloc(sizeof(ANamedAttribute));
		attr->id = id;
		attr->value = g_list_append(NULL, temp_attr);
		
		attributes = g_list_append(attributes, attr);
	}
	
	return attributes;
}
*/
                                                       
static GContainer *
getAttributeList(ASetAttributes *attributes, const char *id)
{
	AMultAttribute *attr;
	GContainer *results, *list;
	GIterator iter;
	gboolean result;
	
	g_assert(attributes != NULL);
	g_assert(id != NULL);
	
	results = g_container_new(GCONT_LIST);
	
	result = g_container_get_iter(&iter, attributes->multAttributes);
	if (!result)
		return results;
	
	while (g_iterator_hasNext(&iter))
	{
		attr = g_iterator_next(&iter);
		g_assert(attr != NULL);
		
		if (lutil_streq(id, attr->id))
		{
			list = attr->values;
			g_container_concat(results, list);
		}
	}
	
	return results;
}

static ASingAttribute *
setAttributeValue(ASetAttributes *attributes, const char *id, const char *value)
{
	ASingAttribute *attr;
	
	g_assert(attributes != NULL);
	g_assert(id != NULL);
	g_assert(value != NULL);
	
	attr = g_malloc(sizeof(ASingAttribute));
	
	attr->id = g_strdup(id);
	attr->value = g_strdup(value);
	g_container_add(attributes->singAttributes, attr);
	
	return attr;
}

static char *
getAttributeString(ASetAttributes *attributes, const char *id)
{
	ASingAttribute *attr;
	GIterator iter;
	gboolean result;
	char *value = NULL;
	
	g_assert(id != NULL);
	g_assert(attributes != NULL);
	
	result = g_container_get_iter_last(&iter, attributes->singAttributes);
	while (g_iterator_hasPrev(&iter))
	{
		attr = g_iterator_prev(&iter);
		g_assert(attr != NULL);
		
		if (lutil_streq(id, attr->id)) {
			value = attr->value;
			break;
		}
	}
	
	return value;
}

static GContainer *
parseMirrorIDs(GContainer *ids, ASetAttributes *attributes)
{
	AMirror *mirr;
	AMirrorList *mirrLst;
	GContainer *results;
	GIterator iter1, iter2;
	gboolean found, result;
	const char *id;
	
	g_assert(ids != NULL);
	g_assert(attributes != NULL);
	
	results = g_container_new(GCONT_LIST);
	result = g_container_get_iter(&iter1, ids);
	
	if (!result)
		return results;
	
	while (g_iterator_hasNext(&iter1))
	{
		id = g_iterator_next(&iter1);
		g_assert(id != NULL);
		found = FALSE;
		
		result = g_container_get_iter(&iter2, attributes->definedMirrors);
		if (!result)
			return results;
		
		while (g_iterator_hasNext(&iter2))
		{
			mirr = g_iterator_next(&iter2);
			g_assert(mirr != NULL);

			if (lutil_streq(id, mirr->id))
			{
				g_container_add(results, mirr->url);
				found = TRUE;
			}
		}
		
		if (!found)
		{
			result = g_container_get_iter(&iter2, attributes->definedMirrorLists);
			if (!result)
				return results;
			
			while (g_iterator_hasNext(&iter2))
			{
				mirrLst = g_iterator_next(&iter2);
				g_assert(mirrLst != NULL);
				
				if (lutil_streq(id, mirrLst->id))
				{
					GContainer *tmp = parseMirrorIDs(mirrLst->mirrors, attributes);
					g_container_concat(results, tmp);
					g_container_free(tmp, TRUE);
					found = TRUE;
				}
			}
		}
		
		if (!found)
			DBUGOUT("No such mirror ID: %s", id);
	}
	
	return results;
}
				
static void
initializeSetAttributes(ASetAttributes *attributes)
{
	attributes->definedMirrorLists = g_container_new(GCONT_LIST);
	attributes->definedMirrors = g_container_new(GCONT_LIST);
	attributes->singAttributes = g_container_new(GCONT_LIST);
	attributes->multAttributes = g_container_new(GCONT_LIST);
}

static void
copySetAttributes(ASetAttributes *newSet, ASetAttributes *attributes, gboolean deepCopy)
{
	if (deepCopy)
		ERROR("ASetAttributes deep copy not supported");
	
	g_assert(attributes != NULL);
	
	newSet->multAttributes = g_container_dup(attributes->multAttributes);
	newSet->singAttributes = g_container_dup(attributes->singAttributes);
	newSet->definedMirrors = g_container_dup(attributes->definedMirrors);
	newSet->definedMirrorLists = g_container_dup(attributes->definedMirrorLists);
}

static void
freeSetAttributes(ASetAttributes *attributes, gboolean deepFree)
{
	g_assert(attributes != NULL);

	if (deepFree)
	{
		AMultAttribute *attrs;
		ASingAttribute *attr;
		GIterator iter;
		gboolean result;
		
		result = g_container_get_iter(&iter, attributes->multAttributes);
		if (result)
		{
			while (g_iterator_hasNext(&iter))
			{
				attrs = g_iterator_next(&iter);
				freeMultAttribute(attrs);
			}
		}
		
		result = g_container_get_iter(&iter, attributes->singAttributes);
		if (result)
		{
			while (g_iterator_hasNext(&iter))
			{
				attr = g_iterator_next(&iter);
				freeSingAttribute(attr);
			}
		}
		
		result = g_container_get_iter(&iter, attributes->definedMirrors);
		if (result)
		{
			while (g_iterator_hasNext(&iter))
			{
				attr = g_iterator_next(&iter);
				freeSingAttribute(attr);
			}
		}
		
		result = g_container_get_iter(&iter, attributes->definedMirrorLists);
		if (result)
		{
			while (g_iterator_hasNext(&iter))
			{
				attrs = g_iterator_next(&iter);
				freeMultAttribute(attrs);
			}
		}
	}
	
	g_container_free(attributes->multAttributes, TRUE);
	g_container_free(attributes->singAttributes, TRUE);
	g_container_free(attributes->definedMirrors, TRUE);
	g_container_free(attributes->definedMirrorLists, TRUE);
}

static void
freeSingAttribute(ASingAttribute *attr)
{
	g_assert(attr != NULL);
	
	g_free(attr->id);
	g_free(attr->value);
	g_free(attr);
}

static void
freeMultAttribute(AMultAttribute *attrs)
{
	g_assert(attrs != NULL);
	g_assert(attrs->values != NULL);
	
	g_container_destroy(attrs->values);
	g_free(attrs->id);
	g_free(attrs);
}
