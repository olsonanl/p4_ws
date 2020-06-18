#include "Logging.h"

#include <cstddef>
#include <string>
#include <ostream>
#include <fstream>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/core.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/sources/basic_logger.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/detail/timestamp.hpp>

using namespace wslog;

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace expr = boost::log::expressions;
namespace sinks = boost::log::sinks;
namespace attrs = boost::log::attributes;
namespace keywords = boost::log::keywords;


void wslog::init(std::string output_file, severity_level file_sev, severity_level console_sev)
{
    typedef sinks::synchronous_sink< sinks::text_ostream_backend > text_sink;

    // Console output

    boost::shared_ptr< text_sink > sink = boost::make_shared< text_sink >();

    boost::shared_ptr< std::ostream > stream(&std::clog, boost::null_deleter());

    sink->locked_backend()->add_stream(stream);

    sink->set_formatter
    (
        expr::stream << "(" << boost::log::expressions::attr<std::string>("Channel") << ") "
	<< expr::smessage
    );

    sink->set_filter
	(
	    expr::attr<severity_level>("Severity").or_throw() >= console_sev
	);
    logging::core::get()->add_sink(sink);


    // File output

    if (!output_file.empty())
    {
	boost::shared_ptr< text_sink > sink = boost::make_shared< text_sink >();
	
	sink->locked_backend()->add_stream(
	    boost::make_shared< std::ofstream >(output_file));
	
	sink->set_formatter
	    (
		expr::stream << std::hex << std::setw(8) << std::setfill('0') << line_id << std::dec << std::setfill(' ')
		<< " "
		<< boost::log::expressions::attr<boost::posix_time::ptime>("TimeStamp")
		<< ": <"
		<< boost::log::expressions::attr<std::string>("Channel")
		<< ": " << severity << ">\t"
		<< expr::smessage
		);
	sink->set_filter
	    (
		expr::attr<severity_level>("Severity").or_throw() >= file_sev
		);
	
	logging::core::get()->add_sink(sink);
    }
    

    // Add attributes
    logging::add_common_attributes();
    logging::core::get()->add_global_attribute("Scope", attrs::named_scope());
}

