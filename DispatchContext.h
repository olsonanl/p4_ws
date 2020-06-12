#ifndef _DispatchContext_h
#define _DispatchContext_h

#include <boost/asio/spawn.hpp>
#include <boost/asio/deadline_timer.hpp>

class DispatchContext
{
public:
    DispatchContext(boost::asio::yield_context &y,  boost::asio::executor e)
	: yield(y)
	, timer(e)
    {}

    boost::asio::yield_context yield;
    boost::asio::deadline_timer timer;
};


#endif
