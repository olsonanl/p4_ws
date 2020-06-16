#ifndef _DispatchContext_h
#define _DispatchContext_h

#include <boost/asio/spawn.hpp>
#include <boost/asio/deadline_timer.hpp>
#include "AuthToken.h"

class DispatchContext
{
public:
    DispatchContext(boost::asio::yield_context &y,  boost::asio::executor e, const AuthToken &t)
	: yield(y)
	, timer(e)
	, token(t)
    {}

    
    boost::asio::yield_context yield;
    boost::asio::deadline_timer timer;
    AuthToken token;
};


#endif
