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

 
/** @file install.h
 * \brief Package nstallation methods
 *
 * Allows Luau to automatically install supported package files (RPMs, DEBs, self-executing files, and autopackage
 * files).  May be preferable to override these functions and provide your own installation routines.
 */
 
#ifndef INSTALL_H
#define INSTALL_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

gboolean luau_install_rpm(const char *filename, GError **err);
gboolean luau_install_deb(const char *filename, GError **err);
gboolean luau_install_src(const char *filename, GError **err);
gboolean luau_install_exec(const char *filename, GError **err);
gboolean luau_install_autopkg(const char *filename, GError **err);

#endif /* INSTALL_H */
