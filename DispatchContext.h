#ifndef _DispatchContext_h
#define _DispatchContext_h

#include <boost/asio/spawn.hpp>
#include <boost/asio/deadline_timer.hpp>
#include "AuthToken.h"
#include "Logging.h"

class DispatchContext
{
public:
    DispatchContext(boost::asio::yield_context &y,  boost::asio::executor e, const AuthToken &t, wslog::logger &logger)
	: yield(y)
	, timer(e)
	, token(t)
	, admin_mode(false)
	, lg_(logger)
    {}
    DispatchContext(boost::asio::yield_context &y,  boost::asio::io_context &ioc, const AuthToken &t, wslog::logger &logger)
	: yield(y)
	, timer(ioc)
	, token(t)
	, admin_mode(false)
	, lg_(logger)
    {}

    
    boost::asio::yield_context yield;
    boost::asio::deadline_timer timer;
    AuthToken token;
    bool admin_mode;
    wslog::logger &lg_;
};


#endif
