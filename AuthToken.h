#ifndef _AuthToken_h
#define _AuthToken_h

#include <ctime>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <map>

#include <boost/algorithm/string.hpp>
#include <boost/beast/core/string_type.hpp>

class AuthToken
{
private:
    std::map<std::string, std::string> parts_;
    std::string text_;
    std::vector<unsigned char> binary_signature_;
    std::string token_;
    
    bool valid_;
    
public:
    AuthToken(const std::string &str) : valid_(false) {
	std::stringstream ifstr(str);
	ifstr >> *this;
    }
    AuthToken() : valid_(false) { }

    AuthToken(const AuthToken &t)
	: parts_(t.parts_)
	, text_(t.text_)
	, binary_signature_(t.binary_signature_)
	, token_(t.token_)
	, valid_(t.valid_) {}

    void clear() {
	parts_.clear();
	text_ = "";
	token_.clear();
	binary_signature_.clear();
	valid_ = false;
    }

    void parse(boost::string_view &sv) {
	std::stringstream ifstr(std::string(sv.data(), sv.size()));
	ifstr >> *this;
    }

    const std::string &user() const { return parts_.at("un"); }
    const std::string &signing_subject() const { return parts_.at("SigningSubject"); }
    const std::string &signature() const { return parts_.at("sig"); }
    unsigned long expiry() const { return  std::stoul(parts_.at("expiry")); }
    const std::string &text() const { return text_; }
    const std::string &token() const { return token_; }
    const std::vector<unsigned char> & binary_signature() const { return binary_signature_; }
    
    bool is_expired() const {
	std::time_t now = std::time(nullptr);
	return expiry() < now;
    }

    bool valid() const { return valid_; }

    /*
     * A token may be invalidated if verification fails.
     */
    void invalidate() { valid_ = false; }

    friend std::istream& operator>>(std::istream& is, AuthToken& tok);
};

inline std::ostream &operator<<(std::ostream &os, const AuthToken &tok)
{
    if (tok.valid())
    {
	if (tok.is_expired())
	    os << "Expired";
	os << "Token(" <<
	    tok.user() <<
	    ", " << tok.token() <<
	    ")";
    }
    else
	os << "InvalidToken()";
    
    return os;
}

inline std::istream& operator>>(std::istream& is, AuthToken& tok)
{
    try {
	std::stringstream all;
	std::stringstream tok_str;
	bool first = true;
	while (is)
	{
	    char c;
	    std::stringstream sstr;
	    if (!is.get(*sstr.rdbuf(), '='))
		break;
	    
	    std::string k = std::move(sstr.str());
	    tok_str << k;
	    
	    if (k != "sig")
	    {
		if (!first)
		    all << "|";
		first = false;
		all << k;
	    }
	    if (is.get(c))
	    {
		tok_str << c;
		if (k != "sig")
		    all << c;
	    }

	    std::stringstream sstr2;
	    if (!is.get(*sstr2.rdbuf(), '|'))
		break;
	    std::string v = std::move(sstr2.str());
	    tok_str << v;
	    
	    if (v.back() == '\n')
		v.pop_back();
	    if (k != "sig")
		all << v;
	    if (is.get(c))
		tok_str << c;

	    if (k == "sig")
	    {
		std::istringstream hex_chars_stream(v);
		tok.binary_signature_.reserve(v.size() / 2);
		char g[3];
		g[2] = 0;
		while (hex_chars_stream.get(g, 3))
		{
		    unsigned int c = std::stoi(g, 0, 16);
		    tok.binary_signature_.push_back(c);
		}
	    }
	    tok.parts_.emplace(std::make_pair(k,v));
	}
	tok.text_ = std::move(all.str());
	tok.token_ = std::move(tok_str.str());

	// Ensure we have the fields we need.

	if (!tok.parts_.at("un").empty() &&
	    !tok.parts_.at("SigningSubject").empty() &&
	    !tok.parts_.at("sig").empty() &&
	    !tok.parts_.at("expiry").empty())
	{
	    tok.valid_ = true;
	}
       
    }
    catch (std::exception e)
    {
	tok.valid_ = false;
    }
    return is;
}



#endif
