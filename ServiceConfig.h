#ifndef _Config_h
#define _Config_h


/* Service config parsing. */

#include <iostream>
#include <cstdlib>
#define INI_MAX_LINE 5000
#define INI_INLINE_COMMENT_PREFIXES "#;"
#include "INIReader.h"

#include <memory>

class ServiceConfig
{
    std::unique_ptr<INIReader> parser_;
    std::string config_file_;
    std::string service_name_;

public:
    ServiceConfig(const std::string &service = "");

    bool parse();

    std::string get_string(const std::string &key, const std::string &default_value = "") const {
	return parser_->Get(service_name_, key, default_value);
    }
    long get_long(const std::string &key, long default_value = 0L) const {
	return parser_->GetInteger(service_name_, key, default_value);
    }

    void config_file(const std::string &f) { config_file_ = f; }
    void service_name(const std::string &s) { service_name_ = s; }
    const std::string config_file() const { return config_file_; }
    const std::string service_name() const { return service_name_; }
};

#endif
