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

#endif
