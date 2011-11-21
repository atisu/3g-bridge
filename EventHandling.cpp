// -*- mode: c++; coding: utf-8-unix -*-

#include "EventHandling.h"
#include "mkstr"
#include "Util.h"

using namespace events;

/// Removes an element from a list.
template <class T>
void remove(list<T> &lst, const T& item)
{
	typename list<T>::iterator i = lst.begin();
	while (i!=lst.end() && *i != item) i++;
	if (i!=lst.end()) lst.erase(i);
}

CSTR_C CommonEvents::ProcessExiting = "exiting";
CSTR_C CommonEvents::JobCancelled = "job-cancelled";

/////////////////////////////////////////////////////////////////////////
// InvalidEventData
/////////////////////////////////////////////////////////////////////////

InvalidEventData::InvalidEventData(const string& handlerType,
				   const string& eventName,
				   const string& expectedType)
	: msg(MKStr() << "Invalid event data for event '"
	      << eventName << "' in '" << handlerType
	      << "'. Expected '" << expectedType << "'")
{}

/////////////////////////////////////////////////////////////////////////
// Event
/////////////////////////////////////////////////////////////////////////

Event::Event(const string &name)
	: _name(name) {}
Event::~Event() {}
void Event::operator+=(EventHandler &h) { listeners.push_back(&h); }
void Event::operator-=(EventHandler &h) { remove(listeners, &h); }
void Event::operator()(EventData *data)
{
	for (EHLIt i = listeners.begin(); i!=listeners.end(); i++)
	{
		(*i)->handle(_name, data);
	}
}

/////////////////////////////////////////////////////////////////////////
// EventPool
/////////////////////////////////////////////////////////////////////////

EventPool * EventPool::_instance;

EventPool::EventPool()  { }
EventPool::~EventPool()
{
	for (DispatcherTable::iterator i = events.begin(); i!=events.end(); i++)
		delete i->second;
}
EventPool &EventPool::instance()
{
	if (!_instance)
		_instance = new EventPool();
	return *_instance;
}

Event& EventPool::operator[](const string &name)
{
	if (events.find(name) == events.end())
		events[name] = new Event(name);;
	return *events[name];
}
