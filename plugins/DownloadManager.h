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

#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <map>
#include "Plugin.h"
#include "EventHandling.h"
#include "DLItem.h"
#include "QMException.h"
#include <glib.h>
#include "Mutex.h"
#include "types.h"
#include "DLEvents.h"

namespace dlmgr
{
	class DownloadManager : public Plugin, protected events::EventHandler
	{
		static int inst_count;

		GThreadPool *thread_pool;
		NamedMutexContainer _jobLock;

		const std::string instance;
		const int num_threads;
		const int max_retries;
		bool _aborted;

		virtual void handle(const std::string &eventName, events::EventData *);
		// Actual handlers
		void quit();
		void dladded(DLEventData *);
		void dlcancelled(events::JobEventData *);

		/// When started, load contents of cg_download continuing
		/// downloads
		void loadDB();
	public:
		DownloadManager(GKeyFile *config, CSTR instance);
		~DownloadManager();

		virtual void init(events::EventPool &ep);
		void queueItem(const string &jobId, const string &logicalFile,
			       const string &URL);
		bool aborted() const { return _aborted; }
		NamedMutexContainer &jobLock() { return _jobLock; }
	};
}

#endif /* DOWNLOADMANAGER_H */
