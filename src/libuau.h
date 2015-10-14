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

 
/** @file libuau.h
 * \brief Interface to the libuau library
 *
 * This is the one and only header file you'll need to use and understand in order
 * to interface with libuau from a third party program.  It provides all the 
 * necessary functionality for using libuau and is the only installed header file.
 */

#ifndef LIBUAU_H
#define LIBUAU_H

#include <stdlib.h>

#include <glib.h>

#define LUAU_VERSION_MAJOR 0
#define LUAU_VERSION_MINOR 1
#define LUAU_VERSION_PATCH 9

#define LUAU_XML_INTERFACE_MAJOR 1
#define LUAU_XML_INTERFACE_MINOR 2

#define LUAU_EMPTY   0
#define LUAU_RPM     1 << 0
#define LUAU_DEB     1 << 1
#define LUAU_SRC     1 << 2
#define LUAU_EXEC    1 << 3
#define LUAU_AUTOPKG 1 << 4
#define LUAU_UNKNOWN 1 << 7

#define LUAU_BASE_ERROR    g_quark_from_static_string("LUAU_BASE_ERROR")
#define LUAU_UTIL_ERROR    g_quark_from_static_string("LUAU_UTIL_ERROR")
#define LUAU_NET_ERROR     g_quark_from_static_string("LUAU_NET_ERROR")
#define LUAU_INSTALL_ERROR g_quark_from_static_string("LUAU_INSTALL_ERROR")
#define LUAU_DB_ERROR      g_quark_from_static_string("LUAU_DB_ERROR")

#ifdef _MSC_VER
#  define LUAU_DLL_EXPORT __declspec( dllexport )
#else
#  define LUAU_DLL_EXPORT
#endif




#ifdef __cplusplus
extern "C" {
#endif

/* -- Data structures -- */

/// Describes a type of package (see LUAU_*type*, #define'd above)
typedef guint32 APkgType;

/// Describes an update type (software, message, luau config update)
typedef enum { LUAU_SOFTWARE,
               LUAU_MESSAGE,
               LUAU_LIBUPDATE } AUpdateType;
			   
typedef enum { LUAU_QUANT_FROM,
               LUAU_QUANT_TO,
               LUAU_QUANT_FOR } AQuantType;
			   
typedef enum { LUAU_QUANT_DATA_DATE, 
               LUAU_QUANT_DATA_VERSION,
               LUAU_QUANT_DATA_INTERFACE,
               LUAU_QUANT_DATA_KEYWORD,
               LUAU_QUANT_DATA_INVALID } AQuantDataType;

/* --- Luau Error Enumerations (used with the glib GError framework) --- */
typedef enum { LUAU_BASE_ERROR_PERMS,
               LUAU_BASE_ERROR_ABORTED,
               LUAU_BASE_ERROR_FAILED,
               LUAU_BASE_ERROR_INVALID_ARG } LuauBaseError;
			   
typedef enum { LUAU_NET_ERROR_ABORTED,
               LUAU_NET_ERROR_FAILED,
               LUAU_NET_ERROR_INVALID_ARG } LuauNetError;
			   
typedef enum { LUAU_INSTALL_ERROR_ABORTED,
               LUAU_INSTALL_ERROR_FAILED,
               LUAU_INSTALL_ERROR_INVALID_ARG } LuauInstallError;
			   
typedef enum { LUAU_UTIL_ERROR_ABORTED,
               LUAU_UTIL_ERROR_FAILED,
               LUAU_UTIL_ERROR_INVALID_ARG } LuauUtilError;
			   
typedef enum { LUAU_DB_ERROR_ABORTED,
               LUAU_DB_ERROR_FAILED,
               LUAU_DB_ERROR_INVALID_ARG } LuauDbError;

/* typedef void (*AErrorFunc) (const char * string, const char* filename, const char* function, int lineno); */
typedef int  (*APromptFunc)(const char * title, const char* msg, int nTotal, int nDefault, const char *choice1, va_list args);

typedef void (*ACallback) (void *data);
typedef void (*AFloatCallback) (float data);
typedef void (*ACallbackWithData) (void *callback_data, void *user_data);
typedef int  (*AProgressCallback) (void *clientp, double dltotal, double dlnow, double ultotal, double ulnow);

/// Date structure
typedef struct {
	short day;
	short month;
	int year;
} ADate;

/// Describe a specific package (ie, an RPM for an update)
typedef struct {
	APkgType type;      /**< Type of given package             */
	GPtrArray *mirrors; /**< list of mirrors for given package */
	char md5sum[33];    /**< Computed md5 sum of given package */
	char *version;      /**< Version number of this package    */
	guint32 size;       /**< Size (in bytes) of given package  */
} APackage;

/// Describe the interface of a program.  Only really relevant for libraries.
typedef struct {
	int major; /**< Specifies the major interface revision - will only work with other versions of the same major revision. */
	int minor; /**< Specifies a "patch" version - is compatible with all packages with lower minor number */
} AInterface;

/// Describe all aspects of any type of update (software, message, etc.)
typedef struct {
	/* Valid for all update types */
	char *id;                    /**< Update ID */
	GPtrArray *keywords;         /**< Array of all keywords assoc. with this update.  May be NULL. */
	AUpdateType type;            /**< Type of this update (software, message, luau-config) */
	ADate *date;                 /**< Date this update was issued. */
	char *shortDesc;             /**< Short description of this update (one-line). */
	char *fullDesc;              /**< Longer description (several sentences). */
	
	GPtrArray *quantifiers;      /**< Specify when and for what program versions this update is valid. */
	
	/* extra SOFTWARE parameters */
	APkgType availableFormats;   /**< Software formats in which this update is available (RPM, DEB, etc.) */
	GPtrArray *packages;         /**< Array of all available packages (APackage structs) */
	char *newVersion;            /**< New version of this update */
	char *newDisplayVersion;     /**< New version to display for this update. */
	AInterface interface;        /**< Interface version of this update */
	
	/* extra MESSAGE parameters: none */
	
	/* extra LIBUPDATE parameters */
	char *newURL;                /**< New location of Luau XML file. */
} AUpdate;

typedef struct {
	AQuantType qtype;
	AQuantDataType dtype;
	void *data;
} AQuantifier;

/// Describe an installed (and registered) program
typedef struct {
	char *id;
	char *shortname;
	char *fullname;
	char *desc;
	char *url;
	char *version;
	char *pkgVersion;
	char *displayVersion;
	ADate *date;
	AInterface interface;
	GPtrArray *keywords;
} AProgInfo;


/* Methods */

/// Read in program information from an XML file at specified URL
LUAU_DLL_EXPORT gboolean luau_getProgInfoFromXML_url(AProgInfo *progInfo, const char *url, GError **err);
/// Read in program information from an XML file in memory
LUAU_DLL_EXPORT gboolean luau_getProgInfoFromXML_data(AProgInfo *progInfo, const char *data, GError **err);

/// Retrieve update info (type, description, etc.) corresponding to the given ID (and program)
LUAU_DLL_EXPORT gboolean luau_getUpdateInfo(AUpdate *update, const char* updateID, const AProgInfo *progInfo, GError **err);
/// Retrieve any new updates for the specified program
LUAU_DLL_EXPORT GList* luau_checkForUpdates(const AProgInfo *info, GError **err);
/// Retrieve all updates from the specified URL
LUAU_DLL_EXPORT GList* luau_checkForUpdates_url(const char *url, GError **err);

/// Download and install an update of type \c type
LUAU_DLL_EXPORT gboolean luau_installUpdate(const AProgInfo *info, const AUpdate *newUpdate, const APkgType type, GError **err);
/// Download but do not install an update of type \c type to \c filename
LUAU_DLL_EXPORT char* luau_downloadUpdate(const AProgInfo *info, const AUpdate *newUpdate, const APkgType type, const char* filename, GError **err);
/// Install an already downloaded package
LUAU_DLL_EXPORT gboolean luau_installPackage(const char* filename, const APkgType type, GError **err);

/*/// Set a new function for outputting errors with
LUAU_DLL_EXPORT void luau_registerErrorFunc(AErrorFunc errorFunc);*/
/// Set a new function for prompting the user for input with
LUAU_DLL_EXPORT void luau_registerPromptFunc(APromptFunc promptFunc);
/// Set a new function to callback (in order to show download progress) when downloading 
LUAU_DLL_EXPORT void luau_registerProgressCallback(AProgressCallback callback);
/*/// Reset the error function to the default function
LUAU_DLL_EXPORT void luau_resetErrorFunc(void);*/
/// Reset the prompting function to the default function
LUAU_DLL_EXPORT void luau_resetPromptFunc(void);
/// Reset the download callback function to the default function
LUAU_DLL_EXPORT void luau_resetProgressCallback(void);


/* luau-specific utility functions */

/* Convert to string utilities */
/// Convert an ADate struct into a string
LUAU_DLL_EXPORT char* luau_dateString(const ADate *date);
/// Convert an APkgType (of many types) into a string
LUAU_DLL_EXPORT char* luau_multPackageTypeString(APkgType types);
/// Convert an APkgType (of one type) into a (constant) string
LUAU_DLL_EXPORT const char* luau_packageTypeString(APkgType type);
/// Convert an AProgInfo ptr into a string
LUAU_DLL_EXPORT char* luau_progInfoString(const AProgInfo *info);
/// Convert an array of keywords into a string
LUAU_DLL_EXPORT char* luau_keywordsString(const GPtrArray *keywords);
/// Convert an AInterface into a string
LUAU_DLL_EXPORT char* luau_interfaceString(const AInterface *interface);
/// Convert an AUpdateType into a string
LUAU_DLL_EXPORT const char* luau_updateTypeString(AUpdateType type);

/* Convert from string utiliies */
/// Convert a date string ("MM/DD/YYYY") into an ADate struct
LUAU_DLL_EXPORT gboolean luau_parseDate(ADate *date, const char *string);
/// Convert a package type string into an APkgType
LUAU_DLL_EXPORT APkgType luau_parsePkgType(const char* typeString);
/// Convert an interface string ("x.y") into an AInterface
LUAU_DLL_EXPORT gboolean luau_parseInterface(AInterface *interface, const char *intStr);

/* APackage utilities */
/// Convert an APkgType into a list of APkgType's
LUAU_DLL_EXPORT GSList* luau_packageTypeList(APkgType types);
/// Find the package of type \c pkgType in \c update
LUAU_DLL_EXPORT APackage* luau_getUpdatePackage(const AUpdate *update, APkgType pkgType, GError **err);
/// Check if package type \c type is included in package aggregate \c query
LUAU_DLL_EXPORT gboolean luau_isOfType(APkgType query, APkgType type);
LUAU_DLL_EXPORT char * luau_getPackageURL(APackage *pkgInfo, GError **err);
LUAU_DLL_EXPORT char * luau_getMostRecentPkgVersion(GPtrArray *packages);

/* Date utilites */
/// Compare two dates (works like \c strcmp but for ADate's)
LUAU_DLL_EXPORT int luau_datecmp(ADate *d1, ADate *d2);

/* Version utilities */
/// Compare two versions (works like \c strcmp but for version strings)
LUAU_DLL_EXPORT int luau_versioncmp(const char *required, const char *current);

/* Keyword utilities */
/// Sets a keyword
LUAU_DLL_EXPORT void luau_setKeyword(GPtrArray *keywords, const char *newKeyword);
/// Unsets a keyword
LUAU_DLL_EXPORT gboolean luau_unsetKeyword(GPtrArray *keywords, const char *oldKeyword);
/// Checks to see if a keyword is set
LUAU_DLL_EXPORT gboolean luau_checkKeyword(const GPtrArray *keywords, const char *needle);
/// See if an update has been marked as "Incompatible"
LUAU_DLL_EXPORT gboolean luau_isIncompatible(AUpdate *update);
/// See if an update has been marked as "Hidden"
LUAU_DLL_EXPORT gboolean luau_isHidden(AUpdate *update);
/// See if an update has been marked as "Old"
LUAU_DLL_EXPORT gboolean luau_isOld(AUpdate *update);
/// See if an updates is visible (has not been marked as old, hidden, or incompatible)
LUAU_DLL_EXPORT gboolean luau_isVisible(AUpdate *update);


/* Interface utilities */
/// Check if a given interface matches an interface that is needed
LUAU_DLL_EXPORT gboolean luau_satisfiesInterface(const AInterface *wanted, const AInterface *needed);

/* Quantifier utilities */
LUAU_DLL_EXPORT gboolean luau_satisfiesQuant(const AQuantifier *needed, const AProgInfo *installed);
LUAU_DLL_EXPORT AQuantDataType luau_parseQuantDataType(const char *str);

/* Structure copying utilities */
/// Copy an AUpdate struct
LUAU_DLL_EXPORT void luau_copyUpdate(AUpdate *dest, const AUpdate *src);
/// Copy an APackage struct
LUAU_DLL_EXPORT void luau_copyPackage(APackage *dest, const APackage *src);
/// Copy an AProgInfo struct
LUAU_DLL_EXPORT void luau_copyProgInfo(AProgInfo *dest, const AProgInfo *src);
/// Copy an AInterfacestruct
LUAU_DLL_EXPORT void luau_copyInterface(AInterface *dest, const AInterface *src);
/// Copy an ADate struct
LUAU_DLL_EXPORT void luau_copyDate(ADate *dest, const ADate *src);
/// Copy an array of AQuantifiers
LUAU_DLL_EXPORT void luau_copyQuants(GPtrArray *dest, const GPtrArray *src);
/// Copy an AQuantifier struct
LUAU_DLL_EXPORT void luau_copyQuant(AQuantifier *dest, const AQuantifier *src);

/* Structure memory managment utilities */
/// Free an array of AUpdate's
LUAU_DLL_EXPORT void luau_freeUpdateList(GList *updates);
/// Free an AProgInfo struct pointer
LUAU_DLL_EXPORT void luau_freeProgInfo(AProgInfo *ptr);
/// Free an AUpdate struct pointer
LUAU_DLL_EXPORT void luau_freeUpdateInfo(AUpdate *ptr);
/// Free an APackage struct pointer
LUAU_DLL_EXPORT void luau_freePkgInfo(APackage *ptr);

#ifdef __cplusplus
}
#endif

#endif /* LIBUAU_H */

