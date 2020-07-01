#ifndef _parse_url_h
#define _parse_url_h

#include <string>
#include <iostream>
#include <sstream>
#include <boost/regex.hpp>

class URL
{
    std::string protocol_;
    std::string domain_;
    std::string port_;
    std::string path_;
    std::string query_;

    std::string url_;

public:
    URL(const std::string& url)
	: url_(url)
    {
	parse();
    }

    URL() 
    {
    }

    const std::string &protocol() const { return protocol_; }
    const std::string &domain() const { return domain_; }
    const std::string &path() const { return path_; }
    const std::string &query() const { return query_; }

    std::string port() const {
	if (port_.empty())
	{
	    if (protocol_ == "http")
		return "80";
	    else
		return "443";
	}
	else
	    return port_;
    }

    void protocol(const std::string &s) { protocol_ = s; }
    void domain(const std::string &s) { domain_ = s; }
    void port(const std::string &s) { port_ = s; }
    void path(const std::string &s) { path_ = s; }
    void query(const std::string &s) { query_ = s; }

    void path_append(const std::string &s) {
	if (path_.empty())
	{
	    path_ = s;
	}
	else
	{
	    if (path_[path_.size() - 1] != '/' && s[0] != '/')
		path_ += "/";
	    path_ += s;
	}
    }

    std::string construct() const
    {
	std::stringstream ss;
	ss << protocol_ << "://" << domain_;
	if (!port_.empty())
	    ss << ":" << port_;
	ss << path_;
	if (!query_.empty())
	    ss << "?" << query_;
	return ss.str();
    }

    std::string path_query() const
    {
	std::stringstream ss;
	ss << path_;
	if (!query_.empty())
	    ss << "?" << query_;
	return ss.str();
    }

    void parse()
    {
	static const boost::regex ex("(http|https)://([^/ :]+):?([^/ ]*)(/?[^ #?]*)\\x3f?([^ #]*)#?([^ ]*)");
	boost::cmatch what;
	if (regex_match(url_.c_str(), what, ex))
	{
	    protocol_ = std::string(what[1].first, what[1].second);
	    domain_   = std::string(what[2].first, what[2].second);
	    port_     = std::string(what[3].first, what[3].second);
	    path_     = std::string(what[4].first, what[4].second);
	    query_    = std::string(what[5].first, what[5].second);
	}
    }
};

inline std::ostream &operator<<(std::ostream &os, URL &url)
{
    os << "URL(" << url.protocol()
       << "," << url.domain()
       << "," << url.port()
       << "," << url.path()
       << "," << url.query()
       << ")";
    return os;
}

// NB this doesn't encode with the same case as the perl encoder
// which has inserted the data mongo. to properly fix we will
// need case insensitive matching on the permission names.
//
// Default set is the RFC3986 reserved characters set plus space.
//
inline bool url_encode(const std::string& in, std::string& out, std::string chars = ":/?#[]@!$&'()*+,;= ")
{
    std::ostringstream os;
    for (std::size_t i = 0; i < in.size(); ++i)
    {
	if (chars.find(in[i]) != std::string::npos)
	    os << "%" << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(in[i]);
	else
	    os << in[i];
    }
    out = std::move(os.str());
    return true;
}

inline bool url_decode(const std::string& in, std::string& out)
{
    out.clear();
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
    {
	if (in[i] == '%')
	{
	    if (i + 3 <= in.size())
	    {
		int value = 0;
		std::istringstream is(in.substr(i + 1, 2));
		if (is >> std::hex >> value)
		{
		    out += static_cast<char>(value);
		    i += 2;
		}
		else
		{
		    return false;
		}
	    }
	    else
	    {
		return false;
	    }
	}
	else if (in[i] == '+')
	{
	    out += ' ';
	}
	else
	{
	    out += in[i];
	}
    }
    return true;
}

#endif
