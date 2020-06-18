#ifndef _Logging_h
#define _Logging_h

#include <ostream>
#include <boost/log/expressions/keyword.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/keywords/channel.hpp>
#include <boost/log/sources/record_ostream.hpp>

namespace wslog {

    using namespace boost::log::keywords;

// We define our own severity levels
    enum severity_level
    {
	debug,
	normal,
	notification,
	warning,
	error,
	critical
    };

    BOOST_LOG_ATTRIBUTE_KEYWORD(line_id, "LineID", unsigned int)
    BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "TimeStamp", timestamp)
    BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", severity_level)
    
    inline std::ostream& operator<< (std::ostream& strm, severity_level level)
    {
	static const char* strings[] =
	    {
		"debug",
		"normal",
		"notification",
		"warning",
		"error",
		"critical"
	    };

	size_t l = static_cast<int>(level);
	if (l < sizeof(strings) / sizeof(*strings))
	    strm << strings[l];
	else
	    strm << l;

	return strm;
    }

    typedef boost::log::sources::severity_channel_logger<severity_level, std::string> logger;

    void init(std::string output_file, severity_level file_sev, severity_level console_sev);

    class LoggerBase
    {
    protected:
	wslog::logger lg_;

    public:
	LoggerBase(const std::string channel)
	    : lg_(wslog::channel = channel) {}

	void fail(boost::system::error_code ec, char const* what) {
	    BOOST_LOG_SEV(lg_, wslog::critical) << what << ": " << ec.message();
	}
    };



}

#endif
