#ifndef _RootCertificates_h
#define _RootCertificates_h

#include <openssl/x509v3.h>
#include <boost/asio/ssl.hpp>

#include <istream>
#include <sstream>
#include <array>

inline void load_root_certificates(std::istream &istr, boost::asio::ssl::context &ctx,
				   boost::system::error_code &ec)
{
    std::stringstream sstr;

    while (istr)
    {
	std::array<char,10240> line;

	while (istr.getline(line.data(), line.size()))
	{
	    if (std::strncmp(line.data(),  "-----BEGIN CERTIFICATE-----\n", line.size()) == 0)
	    {
		sstr << line.data();
		break;
	    }
	}
	while (istr.getline(line.data(), line.size()))
	{
	    sstr << line.data();
	    if (std::strncmp(line.data(), "-----END CERTIFICATE-----\n", line.size()) == 0)
	    {
		std::cerr << "got cert:" << sstr.str() << ":\n";
		sstr.str(std::string());
	    }
	}
    }
}

#endif


