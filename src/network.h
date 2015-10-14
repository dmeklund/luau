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

/** @file network.h
 * \brief Client/Server Functions and Networking Support
 *
 * Query luau servers for new updates and download selected (software) updates.
 */
#ifndef NETWORK_H
#define NETWORK_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "libuau.h"
#include "gcontainer.h"

/// Query a luau server for a list of updates
GContainer* luau_net_queryServer(const AProgInfo *info, GError **err);
/// Download the specified update to downloadTo
gboolean luau_net_downloadUpdate(const AProgInfo *info, const AUpdate *update, APkgType pkgType, const char* downloadTo, GError **err);

#endif /* NETWORK_H */

