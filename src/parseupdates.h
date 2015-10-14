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

/** @file parseupdates.h
 * \brief Software repository parsing routines
 *
 * Provides functionality to read in the Luau software repository file format
 * and output a GContainer (type List) of all available software updates.
 */
 
#ifndef PARSEUPDATES_H
#define PARSEUPDATES_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

GContainer* luau_parseXML_updates(char *contents, GError **err);
gboolean luau_parseXML_progInfo(GString *contents, AProgInfo *progInfo, GError **err);

#endif /* !PARSEUPDATES_H */
