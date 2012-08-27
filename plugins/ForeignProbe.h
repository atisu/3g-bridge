/* -*- mode: c++; coding: utf-8-unix -*-
 * Copyright (C) 2008-2010 MTA SZTAKI LPDS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * In addition, as a special exception, MTA SZTAKI gives you permission to link the
 * code of its release of the 3G Bridge with the OpenSSL project's "OpenSSL"
 * library (or with modified versions of it that use the same license as the
 * "OpenSSL" library), and distribute the linked executables. You must obey the
 * GNU General Public License in all respects for all of the code used other than
 * "OpenSSL". If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version.
 */

#ifndef FOREIGNPROBE_H
#define FOREIGNPROBE_H

#include "Plugin.h"
#include <list>
#include <map>
#include <string>
#include <glib.h>
#include "types.h"
#include <stdint.h>

namespace foreignprobe
{
	using std::list;
	using std::map;
	using std::string;

	typedef uint64_t t_timestamp;
	typedef const string &CR_string;

	struct AppDesc
	{
		int appid;
		string appname, spoof;
		AppDesc() {}
		AppDesc(int appid, CR_string appname, CR_string spoof);
	};
	typedef const AppDesc &CR_AppDesc;

	class MetricData
	{
		const string _name, _type, _unit;
	protected:
		virtual float runquery(CR_AppDesc app) const = 0;
	public:
		MetricData(CR_string name, CR_string type,
			   CR_string unit);
		virtual ~MetricData() {}

		CR_string name() const { return _name; }
		CR_string type() const { return _type; }
		CR_string unit() const { return _unit; }

		const string getcurrent(CR_AppDesc app,
					CR_string direction,
					t_timestamp ts) const;
	};

	class SimpleMetricData : public MetricData
	{
		const string _query;
	protected:
		virtual float runquery(CR_AppDesc app) const;
	public:
		SimpleMetricData(CR_string name, CR_string type,
				 CR_string unit, CR_string query);

		CR_string query() const { return _query; }
	};

	typedef map<string, AppDesc> appmap;
	typedef map<string, MetricData*> metrmap;
	typedef appmap::const_iterator appit;
	typedef metrmap::const_iterator metrit;

	class ForeignProbe : public Plugin
	{
		class Configuration
		{
			int _interval, _maxthreads;
			string _direction, _targetpath;
			appmap _apps;
			metrmap _queries;

			static const appmap getApps(
				GKeyFile *config, CSTR instance);
			static const metrmap getQueries(
				GKeyFile *config, CSTR instance);

		public:
			Configuration(GKeyFile *config, CSTR instance);

			int interval() const { return _interval; }
			int maxthreads() const { return _maxthreads; }
			CR_string direction() const { return _direction; }
			CR_string targetpath() const { return _targetpath; }
			const appmap &apps() const {return _apps;}
			const metrmap &queries() const
			{
				return _queries;
			}
		};

		GCond *_exit_condition;
		GMutex *_exit_condition_mx;
		GThread *_main_thread;
		const Configuration _conf;
		const string metrics(t_timestamp ts) const;

		static gpointer th_main(gpointer data);

	public:
		ForeignProbe(GKeyFile *config, CSTR instance);
		virtual ~ForeignProbe();

		virtual void init(events::EventPool &ep);
	};
}

#endif //FOREIGNPROBE_H
