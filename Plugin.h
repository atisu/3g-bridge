/* -*- mode: c++; coding: utf-8-unix -*-
 *
 * Copyright (C) 2009-2010 MTA SZTAKI LPDS
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * In addition, as a special exception, MTA SZTAKI gives you permission to link
 * the code of its release of the 3G Bridge with the OpenSSL project's "OpenSSL"

 * library (or with modified versions of it that use the same license as the
 * "OpenSSL" library), and distribute the linked executables. You must obey the
 * GNU General Public License in all respects for all of the code used other
 * than "OpenSSL". If you modify this file, you may extend this exception to
 * your version of the file, but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version.
 */

#ifndef __PLUGIN_H
#define __PLUGIN_H

#include <glib.h>
#include "EventHandling.h"
#include "types.h"

class Plugin
{
public:
	virtual ~Plugin() {}
	virtual void init(events::EventPool &) {}
};

/* Factory function prototype */
extern "C" typedef Plugin* (*handler_factory_func)(GKeyFile *config, const char *instance);

/* Name of the factory symbol in the loadable modules */
#define HANDLER_FACTORY_SYMBOL get_handler_instance

/* Factory function definition for modules */
#define HANDLER_FACTORY(config, instance) extern "C" Plugin *HANDLER_FACTORY_SYMBOL(GKeyFile *config, const char *instance)

#endif //__PLUGIN_H
