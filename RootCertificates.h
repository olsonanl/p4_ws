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
    std::string cert;

    while (istr)
    {
	std::array<char,10240> line;

	while (istr.getline(line.data(), line.size(), '\n'))
	{
	    if (std::strncmp(line.data(),  "-----BEGIN CERTIFICATE-----", line.size()) == 0)
	    {
		cert += line.data();
		cert += "\n";
		break;
	    }
	}
	while (istr.getline(line.data(), line.size(), '\n'))
	{
	    cert += line.data();
	    cert += "\n";
	    if (std::strncmp(line.data(), "-----END CERTIFICATE-----", line.size()) == 0)
	    {
		ctx.add_certificate_authority(
		    boost::asio::buffer(cert.data(), cert.size()), ec);
		if (ec)
		{
		    std::cerr << "error loading cert: " << ec.message() << "\n";
		}

		cert.clear();
		break;
	    }
	}
    }
}

#endif


