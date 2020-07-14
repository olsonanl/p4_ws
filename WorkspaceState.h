#ifndef _WorkspaceState_h
#define _WorkspaceState_h

/*
 * Manage the global state required for the workspace service.
 */

#include <memory>
#include <boost/json.hpp>

#include "AuthToken.h"
#include "SigningCerts.h"
#include "WorkspaceConfig.h"
#include "Shock.h"
#include "UserAgent.h"
#include "Base64.h"

class WorkspaceState : public std::enable_shared_from_this<WorkspaceState>
{
    SigningCerts certs_;
    WorkspaceConfig &config_;
    Shock shock_;
    UserAgent user_agent_;
    AuthToken ws_auth_;

public:
    WorkspaceState(WorkspaceConfig &config,
		   Shock &&shock,
		   UserAgent &&user_agent)
	: config_(config)
	, shock_(std::move(shock))
	, user_agent_(std::move(user_agent)) {
    }
    ~WorkspaceState() { std::cerr << "destroy  WorkspaceState\n"; }

    const SigningCerts &signing_certs () const { return certs_; }
    bool validate_certificate(const AuthToken &tok) { return certs_.validate(tok); }

    WorkspaceConfig &config() { return config_; }
    const WorkspaceConfig &config() const { return config_; }
    Shock &shock() { return shock_; }

    AuthToken &ws_auth(boost::asio::yield_context yield) {
	if (!ws_auth_.valid())
	{
	    std::string auth = "Basic " + base64EncodeText(config().get_string("wsuser") + ":" +
							   config().get_string("wspassword"));
	    std::string token;
	    URL u("https://rast.nmpdr.org/goauth/token?grant_type=client_credentials");
	    user_agent_.request("GET", u, {{ "Authorization", auth }}, [&token ] (const UserAgent::Response &r)
		{
		    token = r.body();
		}, yield);
	    try {
		auto doc = boost::json::parse(token);
		auto obj = doc.as_object();
		auto tok = obj["access_token"].as_string();
		ws_auth_.parse(std::string(tok.data(), tok.size()));
	    } catch (std::exception &e) {
		std::cerr << "Cannot parse WS token: " << e.what() << "\n";
	    }
	}
	return ws_auth_;
    };
};

#endif
