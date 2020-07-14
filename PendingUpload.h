#ifndef _PendingUpload_h
#define _PendingUpload_h

#include <boost/chrono/include.hpp>
#include <boost/chrono/chrono_io.hpp>
#include <string>
#include <iostream>

/**
 * A PendingUpload represents a workspace object which has been
 * created with createUploadNodes=true and whose actual
 * upload status is not yet known.
 */

class PendingUpload
{
private:

    using clock = boost::chrono::steady_clock;
    
    std::string object_id_;
    std::string shock_url_;
    AuthToken auth_token_;
    mutable size_t size_;
    mutable bool updated_;
    clock::time_point creation_time_;
    
public:
    explicit PendingUpload(const std::string &object_id, const std::string &shock_url, const AuthToken &token)
	: object_id_(object_id)
	, shock_url_(shock_url)
	, auth_token_(token)
	, size_{}
	, updated_{false}
	, creation_time_{clock::now()} {
    }

    const std::string &object_id() const { return object_id_; }
    const std::string &shock_url() const { return shock_url_; }
    const AuthToken &auth_token() const { return auth_token_; }
    size_t size() const { return size_; }
    bool updated() const { return updated_; }

    void set_size(size_t s) { size_ = s; updated_ = true; }
    clock::time_point creation_time() const { return creation_time_; }
    clock::duration age() const { return clock::now() - creation_time(); }

    friend bool operator<(const PendingUpload &l, const PendingUpload &r) {
	return l.object_id_ < r.object_id_;
    }

};

inline std::ostream &operator<<(std::ostream &os, const PendingUpload &p)
{
    os << "PU(" << p.object_id()
       << "," << p.updated()
       << "," << p.shock_url() 
       << "," << p.age() 
       << "," << p.creation_time()
       << ")";
    return os;
}


#endif
