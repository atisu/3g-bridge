/** -*- mode: c++; coding: utf-8-unix -*-
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

#ifndef __LOGMONMSG_H
#define __LOGMONMSG_H

#include <utility>
#include <list>
#include <map>
#include <string>
#include <glib.h>
#include "mkstr"
#include <stdint.h>
#include "types.h"

/**
 * Utilities for logging job events (or any key-value-pair set).
 *
 * Usage: createMessage() -- Create message object. Key value pairs can be added
 *                           to the message, then it should be saved. The
 *                           message in the right format will be appended to the
 *                           temporary log.
 *
 *   LogMon is a singleton. When first asking for an instance, configuration is
 *   read and logrotate thread is started.
 *
 *   Example:
 *        LogMon::instance(cfg)			\
 *                 .createMessage()		\
 *                 .add("event", "job_entry")	\
 *                 .add("job_id", uuid)		\
 *                 .add("application", appname)	\
 *                 .save();
 */
namespace logmon
{
	using std::string;
	using std::list;
	using std::map;
	using std::pair;

	typedef uint64_t timestamp_type;
	// KVP == key-value-pair
	typedef pair<string,string> KVP;
	typedef list<KVP> KVPList;

	class LogMon;
	class Message;

	/**
	 * Abstract class for log-building.
	 * Sub-classes can be created only with Builder::create()
	 */
	class Builder
	{
	protected:
		Builder() {}
		LogMon &parent() const;
		/**
		 * When overridden in a derived class, generates message text
		 * from KVPs.
		 */
		virtual string message(const KVPList& items) = 0;

	public:
		virtual ~Builder() {}
		
		/// Creates Builder instance according to type
		static Builder *create(const string &type);
		/**
		 * When overridden in a derived class, creates the temporary log
		 * file and adds neccesary content.
		 *
		 * Must only be called from inside a CriticalSection(..)
		 */
		virtual void beginFile(const timestamp_type &) {}
		/**
		 * When overridden in a derived class, adds closing content to
		 * temporary log file.
		 *
		 * Must only be called from inside a CriticalSection(..)
		 */
		virtual void endFile(const timestamp_type &) {}
		/**
		 * Adds message to the temporary log file thread-safely.
		 */
		virtual void saveMessage(const Message &msg);
	};

	/**
	 * Simple log format. All key-value-pairs are saved in the following
	 * format:
	 *   key1=value1 key2=value2 ...
	 * Each line is a different message.
	 */
	class SimpleBuilder : public Builder
	{
		// Can only be instantiated by Builder::create()
		friend class Builder;
		SimpleBuilder() {}
	protected:
		virtual string message(const KVPList& items);
	public:
		virtual ~SimpleBuilder() {}
	};
	/**
	 * XML log format. Keys may be transformed by pre-defined mapping. If
	 * unmapped, keys may be left out from the output.
	 */
	class XMLBuilder : public Builder
	{
		// Can only be instantiated by Builder::create()
		friend class Builder;
		XMLBuilder() {}

		/// For implementing complex `static const' field below.
		class KTMapping
		{
			typedef map<string,string> mapping_type;
			mapping_type _mapping;
		public:
			string operator[](const string &key) const;
			KTMapping();
		};
		static const KTMapping key_tag_mapping;

	protected:
		virtual string message(const KVPList& items);
		/**
		 * Creates file with opening XML tag. This tag will contain a
		 * timestamp attribute whose value is substituted with a
		 * placeholder. end() will change the placeholder to an actual
		 * value.
		 *
		 * Must only be called from inside a CriticalSection(..)
		 */
		virtual void beginFile(const timestamp_type &now);
		/**
		 * Adds closing tag to temporary log file, and changes the
		 * timestamp placeholder to the actual value.
		 *
		 * Must only be called from inside a CriticalSection(..)
		 */
		virtual void endFile(const timestamp_type &now);
	public:
		virtual ~XMLBuilder() {}
	};

	/// For when logging is turned off
	class DummyBuilder : public Builder
	{
	protected:
		virtual string message(const KVPList&) { return string(); }
		virtual void beginFile(const timestamp_type &) {}
		virtual void endFile(const timestamp_type &) {}
		virtual void saveMessage(const Message&) {}
	public:
		virtual ~DummyBuilder() {}
	};

	/**
	 * Convenience class to build lists of key-value-pairs.
	 * The `dt=<current datetime>' pair is always added automatically.
	 */
	class Message
	{
		// Messages can only be constructed by a LogMon instance
		friend class LogMon;
		friend class Builder;

		KVPList _items;
		Builder &_builder;
	protected:
		Message(Builder& builder);
	public:
		virtual ~Message() {}

		/// This makes it convenient
		template <class T>
		Message& add(const std::string &key, const T& value)
		{
			const string &val = MKStr() << value;
			typedef KVPList::iterator iter;
			bool found = false;
			for (iter i = _items.begin(); i != _items.end(); i++)
			{
				if (i->first == key)
				{
					i->second = value;
					found=true;
					break;
				}
			}
			if (!found)
				_items.push_back(KVP(key, val));
			return *this;
		}
		/// Instructs the associated Builder to append this message to
		/// the output.
		virtual void save();
	};
	/// For when logging is turned off
	class DummyMessage : public Message
	{
		friend class LogMon;

		DummyMessage(Builder& builder)
			: Message(builder) {}
	public:
		virtual void save() {}
	};

	/**
	 * Access point for logging job events (or anything else). Thread safe.
	 */
	class LogMon
	{
		friend class Builder;

		// Singleton; hide constructor, disable and hide
		// copy-constructor
		LogMon(Builder *builder,
		       const string &name,
		       const string &logfilename, const string &timezone,
		       timestamp_type rotateInterval,
		       const string &rotateFilenameFmt);
		LogMon(const LogMon &) : _rotateInterval(0) {}

		static LogMon *_instance;

		Builder *_builder;
		// Configuration
		const string _name, _logfilename, _timezone, _rotateFilenameFmt;
		const timestamp_type _rotateInterval;
		GMutex *_mutex;

	public:

		/**
		 * Returns the singleton instance of this class. When created,
		 * the instance is initialized. The argument `conf' may be NULL,
		 * if existence of the instance is assumed.
		 *
		 * @param group The group where configuration keys can be
		 *              found. If omitted, GROUP_DEFAULT (=="default")
		 *              will be used.
		 */
		static LogMon &instance(GKeyFile *conf = 0, CSTR group = 0);
		virtual ~LogMon();

		/**
		 * Creates and returns a Message object. It may be built and
		 * then saved.
		 */
		Message createMessage();
		/**
		 * Performs a log file rotation unconditionally.
		 */
		void logrotate();

		// Configuration as specified in the config file given to
		// instance()
		const string &logfilename() const { return _logfilename; }
		const string &timezone() const { return _timezone; }
		const timestamp_type &rotateInterval() const
		{
			return _rotateInterval;
		}
		const string &name() const { return _name; }
	};

	void startRotationThread(GKeyFile *conf, CSTR group = 0);
	void endRotationThread();
}

#endif // __LOGMONMSG_H
