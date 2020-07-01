#include "HTTPServer.h"

#include "WorkspaceTypes.h"
#include "Logging.h"
#include "WorkspaceService.h"
#include "WorkspaceState.h"
#include "WorkspaceDB.h"
#include "JSONRPC.h"
#include "ServiceDispatcher.h"
#include "DispatchContext.h"
#include "AuthToken.h"
#include "SigningCerts.h"
#include "Shock.h"
#include "RootCertificates.h"

using namespace ws_http_server;

// Report a failure

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
	std::cerr << "Usage: " << argv[0] << " port threads\n";
	return EXIT_FAILURE;
    }

    SSL_init();
    wslog::init("", wslog::normal, wslog::debug);
    wslog::logger lg(wslog::channel = "main");

    auto const address = net::ip::make_address("0.0.0.0");
    auto const port = static_cast<unsigned short>(std::atoi(argv[1]));
    auto const threads = std::max<int>(1, std::atoi(argv[2]));

    BOOST_LOG_SEV(lg, wslog::debug) << "Listening on " << port << " with " << threads << " threads\n";

    /*
     * Overall system configuration.
     *
     * We have the following subsystems:
     *
     * System configuration information managed in WorkspaceConfig.
     * Parses a config file as defined by the standard environment variables
     * KB_DEPLOYMENT_CONFIG and KB_SERVICE_NAME.
     * TODO add a command line parameter to override service name, perhaps
     *
     * Mongo database connectivity in WorkspaceDB. Requires config information to start.
     * Maintains internal thread pool for safely handling multiple requests while keeping
     * the main request threads from blocking.
     *
     * Workspace service execution core in WorkspaceService.
     * Uses the WorkspaceDB for database lookups, as well as the async methods
     * for other network access.
     * Uses the Shock module for Shock database manipulations
     * Uses the UserAgent module for other HTTP client access.
     *
     * Shock module provides interface to the Shock service. It is standalone
     * but requires io_context and ssl_context configuration.
     *
     * Logging is built using the Boost logging library
     * We define a logging base class and namespace in
     * Logging.h. The wslog::LoggerBase provides a per-class loggign
     * instance and a fail() method that logs the boost::system::error_code
     * that the Boost ASIO & related libraries generate.
     *
     */

    // IO context is required for I/O in C++ Networking

    net::io_context ioc{threads};

    // SSL support
    
    ssl::context ssl_ctx{ssl::context::tlsv12_client};
    std::ifstream certs("/etc/pki/tls/cert.pem");
    if (!certs)
    {
	std::cerr << "cannot load certs file\n";
	return 1;
    }
    boost::system::error_code ec;
    load_root_certificates(certs, ssl_ctx, ec);

    // Verify the remote server's certificate
    ssl_ctx.set_verify_mode(ssl::verify_peer);

    Shock shock(ioc, ssl_ctx);
    UserAgent user_agent(ioc, ssl_ctx);

    // Create our global state container.

    WorkspaceState global_state { std::move(shock), std::move(user_agent) };

    // Parse config
    if (!global_state.config().parse())
    {
	std::cerr << "Error parsing workspace configuration\n";
	return 1;
    }

    // Intialize database

    WorkspaceDB db{ global_state.config() };
    
    db.init_database(global_state.config().mongodb_url(),
		     global_state.config().mongodb_client_threads(),
		     global_state.config().mongodb_dbname());
    
    WorkspaceService  workspace_service{ db, global_state };
    ServiceDispatcher dispatcher;

    dispatcher.register_service("Workspace",
				 [&workspace_service]
				 (const JsonRpcRequest &req, JsonRpcResponse &resp,
				  DispatchContext &dc,
				  int &http_code) {
				     workspace_service.dispatch(req, resp, dc, http_code);
				 });


    // Create our listener and start it running.

    Server server(ioc, global_state.config().api_root(), dispatcher, workspace_service, shock);

    server.run(address, port, threads);

    dispatcher.unregister_service("Workspace");

    SSL_finish();
    
    return EXIT_SUCCESS;
}
