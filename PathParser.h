#ifndef _PathParser_h
#define _PathParser_h
#include <sstream>
#include <iostream>
#include <functional>
#include <vector>

#include "WorkspaceTypes.h"

class WSPathParser
{
public:

    enum state {
	s_start = 0,
	s_uuid,
	s_owner_start,
	s_owner,
	s_wsname_start,
	s_wsname,
	s_path_start,
	s_path
    };

    WSPathParser()
	: cur_state_(s_start) {
    }

    /*
    void set_callback(std::function<int(const std::string &id, const std::string &seq)> on_seq) {
	on_seq_ = on_seq;
    }
    
    void set_def_callback(std::function<int(const std::string &id, const std::string &def, const std::string &seq)> on_seq) {
	on_def_seq_ = on_seq;
    }

    void set_error_callback(std::function<bool(const std::string &err, int line, const std::string id)> cb)
    {
	on_error_ = cb;
    }
    */

    bool parse(const std::string &str) {
	std::istringstream is(str);
	return parse(is);
    }

    bool parse(const char *str) {
	std::istringstream is(str);
	return parse(is);
    }

    bool parse(std::istream &stream) {
	char c;
	init_parse();
	while (stream.get(c))
	{
	    bool ok = parse_char(c);
	    if (!ok)
		return false;
	}
	parse_complete();
	return true;
    }

    void init_parse() {
	cur_state_ = s_start;
	path_component_ = "";
	owner_ = "";
	wsname_ = "";
	path_.clear();
    }

    void parse_complete() {
	// only state that requires finishing up is the path parsing
	switch (cur_state_)
	{
	case s_path:
	    path_.emplace_back(std::move(path_component_));
	    path_component_ = "";
	    break;
	}
    }

    // return true to continue parsing
    inline bool parse_char(char c)
    {
	/*
	  std::cout << "top " << c << " " << (0+cur_state_) << std::endl;
	  std::cout << cur_id_ << "\n";
	  std::cout << cur_seq_ << "\n";
	*/
	std::string err;
	switch (cur_state_)
	{
	case s_start:
	    if (c == '/')
	    {
		cur_state_ = s_owner_start;
	    }
	    else if ((c >= '0' && c <= '9') ||
		     (c >= 'a' && c <= 'f') ||
		     (c >= 'A' && c <= 'F'))
	    {
		cur_state_ = s_uuid;
	    }
	    else
	    {
		err = "Invalid start";
	    }
	    break;

	case s_owner_start:
	    if (c == '/')
	    {
		// continue in this state
	    }
	    else
	    {
		owner_.push_back(c);
		cur_state_ = s_owner;
	    }
	    break;
	    
	case s_owner:
	    if (c == '/')
	    {
		cur_state_ = s_wsname_start;
	    }
	    else
	    {
		owner_.push_back(c);
	    }
	    break;

	case s_wsname_start:
	    if (c == '/')
	    {
		// continue in this state
	    }
	    else
	    {
		wsname_.push_back(c);
		cur_state_ = s_wsname;
	    }
	    break;
	    
	case s_wsname:
	    if (c == '/')
	    {
		cur_state_ = s_path_start;
	    }
	    else
	    {
		wsname_.push_back(c);
	    }
	    break;

	case s_path_start:
	    if (c == '/')
	    {
		// continue in this state
	    }
	    else
	    {
		path_component_.push_back(c);
		cur_state_ = s_path;
	    }
	    break;
	    
	case s_path:
	    if (c == '/')
	    {
		path_.emplace_back(std::move(path_component_));
		path_component_ = "";
		cur_state_ = s_path_start;
	    }
	    else
	    {
		path_component_.push_back(c);
	    }
	    break;
	}
	if (!err.empty())
	{
	    std::cerr << "Error found: " << err << "\n";
	    /*
	    if (on_error_)
	    {
		return on_error_(err, line_number, cur_id_);
	    }
	    */
	}
	return true;
    }

    const std::string &owner() const { return owner_; }
    const std::string &wsname() const { return wsname_; }
    const std::vector<std::string> path() const { return path_; }

    WSPath extract_path() {
	WSPath path;
	path.workspace.owner = std::move(owner_);
	path.workspace.name = std::move(wsname_);
	if (path_.size() > 0)
	{
	    path.name = path_.back();
	    path_.pop_back();
	}
		
	auto iter = path_.begin();
	if (iter != path_.end())
	{
	    path.path = *iter;
	    iter++;
	}
	while (iter != path_.end())
	{
	    path.path += "/";
	    path.path += *iter;
	    iter++;
	}
	path.empty = false;
	return path;
    }

private:
    int line_number;
    state cur_state_;
    std::vector<std::string> path_;
    std::string path_component_;
    std::string owner_;
    std::string wsname_;
};

inline std::ostream &operator<<(std::ostream &os, const WSPathParser &p)
{
    os << "PP(" << p.owner() << "," << p.wsname();
    for (auto x: p.path())
	os << "," << x;
    os << ")";
    return os;
}
	    

#endif
