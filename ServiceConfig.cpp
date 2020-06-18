#include "ServiceConfig.h"

ServiceConfig::ServiceConfig(const std::string &service)
  : service_name_(service)
{
    const char *p = 0;
    if ((p = std::getenv("KB_SERVICE_NAME")) && service.empty() )
	service_name_ = p;
    if (p = std::getenv("KB_DEPLOYMENT_CONFIG"))
	config_file_ = p;
}

bool ServiceConfig::parse()
{
    if (config_file_.empty())
    {
	std::cerr << "ServiceConfig::parse: no file set\n";
	return false;
    }
    parser_ = std::make_unique<INIReader>(config_file_);

    if (parser_->ParseError() != 0)
    {
	std::cerr << "ServiceConfig::parse failed\n";
	return false;
    }

    return true;
}
