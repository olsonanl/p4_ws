#ifndef _WorkspaceTypes_h
#define _WorkspaceTypes_h

#include <iostream>

enum class WSPermission : char
{
    invalid = 0,
	write = 'w',
	read = 'r',
	owner = 'o',
	admin = 'a',
	public_ = 'p',
	none = 'n',

};

inline std::string to_string(const WSPermission &p)
{
    char c[2];
    c[0] = static_cast<char>(p);
    c[1] = 0;
    return std::string(c);
}

inline WSPermission to_permission(char c)
{
    switch (std::tolower(c)) {
    case 'w':
	return WSPermission::write;
    case 'r':
	return WSPermission::read;
    case 'a':
	return WSPermission::admin;
    case 'n':
	return WSPermission::none;
    case 'o':
	return WSPermission::owner;
    case 'p':
	return WSPermission::public_;
    default:
	return WSPermission::invalid;
    }
}

inline WSPermission to_permission(const std::string &str)
{
    return to_permission(str.front());
}

inline std::ostream &operator<<(std::ostream &os, WSPermission p)
{
    switch (p)
    {
    case WSPermission::invalid:
	os << "invalid";
	break;
    case WSPermission::write:
	os << "write";
	break;
    case WSPermission::read:
	os << "read";
	break;
    case WSPermission::admin:
	os << "admin";
	break;
    case WSPermission::owner:
	os << "owner";
	break;
    case WSPermission::public_:
	os << "public";
	break;
    case WSPermission::none:
	os << "none";
	break;
    }
    return os;
}



#endif
