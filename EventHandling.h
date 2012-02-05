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

#ifndef __EVENT_HANDLING_H
#define __EVENT_HANDLING_H

#include <string>
#include <list>
#include <map>
#include <exception>
#include "types.h"
#include "TypeInfo.h"
#include "Job.h"

namespace events
{
	class CommonEvents
	{
	public:
		static CSTR_C ProcessExiting; 
		static CSTR_C JobCancelled;   //JobEvent
	};
	
	using namespace std;

	/**
	 * Shall be raised by an EventHandler::handle() method if the supplied
	 *  EventData doesn't match the type the handler expects.
	 */
	class InvalidEventData : public exception
	{
		const string msg;
	public:
		InvalidEventData(const string& handlerType,
				 const string& eventName,
				 const string& expectedType);
		virtual ~InvalidEventData() throw() {}
		virtual CSTR what() const throw() { return msg.c_str(); }
	};

	/**
	 * Virtual base class for event arguments. Its subclasses can be used as
	 * event arguments. Rationale: dynamic_cast needs a virtual base class,
	 * (void*)-s can't be passed around. Used as a universal argument for
	 * EventHandler::handle()
	 */
	class EventData
	{
		bool _handled;
	public:
		EventData() : _handled(false) {}
		virtual ~EventData() {};

		bool handled() const { return _handled; }
		void setHandled(bool newVal) { _handled = newVal; }
	};

	/**
	 * General wrapper for arbitrary event data.
	 */
	template <class T>
	class WrapperEventData : public EventData
	{
		T _data;
	public:
		WrapperEventData(T data)
			:_data(data) {}
		virtual ~WrapperEventData() {}
		T get() const { return _data; }
		operator T() { return _data; }
	};

	/// Subclasses may handle events. In design pattern terms:
	/// Observer::Listener
	class EventHandler : protected TypeInfo
	{
	public:
		/**
		 * Checks whether the actual type of the event data matches the
		 * expected type. If not, InvalidEventData is thrown. If data is
		 * NULL, it is accepted.
		 *
		 * T : Expected type of the argument
		 * @param data Event arguments. Must have the dynamic type T.
		 * @param eventName Only for information in case of error
		 */
		template <class T>
		T* check(const string &eventName, EventData *data)
		{
			if (!data) return 0;

			T *d=dynamic_cast<T*>(data);
			if (!d) throw InvalidEventData(typeName(),
						       eventName,
						       ::typeName<T>());
			return d;
		}

		/**
		 * Overridden in a sub-class, handles raised events. Events can
		 * be identified by their names. The type-correctness of the
		 * event arguments should be checked using the check<T>()
		 * template function.
		 *
		 * Example override
		 *
		 * if (eventName == "myevent")
		 * {
		 *    ExpectedType *d = check<ExpectedType>(data);
		 *    // InvalidEventData may be passed up to caller
		 *    realHandle(*d);
		 * }
		 */
		virtual void handle(const string& eventName, EventData* data) = 0;
	};

	/// Implements an event class, in design pattern terms:
	/// Observer::Subject
	class Event
	{
		typedef list< EventHandler* > HandlerList;
		typedef HandlerList::iterator EHLIt;

		const string _name;
		// List of observers
		HandlerList listeners;
	public:
		Event(const string &name);
		virtual ~Event();

		/// The name of the event. Should be unique.
		const string &name() const { return _name; }

		/// Appends an EventHandler to the list of listeners
		void operator+=(EventHandler &);
		/// Removes the handler from the list of listeners
		void operator-=(EventHandler &);
		/// Triggers the event. EventHandlers are executed in the order
		/// they were added.
		void operator()(EventData *);
	};

	/**
	 * Implements a singleton event container. Events can be selected by
	 * name. The name of the events will be unique to each event. When a
	 * name is first addressed, the associated event is created. From then
	 * on, each time that name is addressed, the same Event will be
	 * returned. Events cannot be remove from the container.
	 */
	class EventPool
	{
	public:
		typedef map<string, Event* > DispatcherTable;
	private:
		DispatcherTable events;

		///Singleton instance
		static EventPool *_instance;
		EventPool();
	public:
		///Returns the single instance of the class.
		static EventPool &instance();
		virtual ~EventPool();

		///Returns the Event associated with the given name. Creates the
		///event as neccesary.
		Event& operator[](const string &name);
	};

	typedef events::WrapperEventData<Job *> JobEventData;
}

#endif //__EVENT_HANDLING_H
