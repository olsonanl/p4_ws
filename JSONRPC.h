#ifndef _JSONRPC_h
#define _JSONRPC_h

// Wrap a JSON-RPC message.

#include <string>
#include <boost/json.hpp>
#include <iostream>

#include "WorkspaceErrors.h"

class JsonRpcRequest
{
    boost::json::string id_;
    boost::json::string raw_method_;
    boost::json::string method_;
    boost::json::string service_;
    boost::json::array params_;

public:
    JsonRpcRequest() {}
    void parse(boost::json::value &req, boost::system::error_code &ec) {
	try {
	    auto &obj = req.as_object();
	    id_ = obj["id"].as_string();
	    raw_method_ = obj["method"].as_string();
	    params_ = obj["params"].as_array();

	    size_t n = raw_method_.find('.');
	    if (n == boost::json::string::npos)
	    {
		method_ = raw_method_;
		service_ = "";
	    }
	    else
	    {
		method_ = raw_method_.subview(n + 1);
		service_ = raw_method_.subview(0, n);
	    }

	    
	}
	catch (boost::json::type_error e) {
	    ec = WorkspaceErrc::InvalidJsonRpcRequest;
	    return;
	}
    }

    const boost::json::string &id() const { return id_; }
    const boost::json::string &raw_method() const { return raw_method_; }
    const boost::json::string &method() const { return method_; }
    const boost::json::string &service() const { return service_; }
    const boost::json::array &params() const { return params_; }
    
    friend std::ostream &operator<<(std::ostream &os, const JsonRpcRequest &req);
};

class JsonRpcResponse
{
    boost::json::string id_;
    boost::json::string error_;
    boost::json::array result_;

public:
    JsonRpcResponse() {}

    const boost::json::string &id() const { return id_; }
    const boost::json::string &error() const { return error_; }
    const boost::json::array &result() const { return result_; }
    boost::json::array &result() { return result_; }
    
    void id(const boost::json::string &id) { id_ = id; }
    void error(const boost::json::string &error) { error_ = error; }
    void result(const boost::json::array &result) { result_ = result; }
	
    friend std::ostream &operator<<(std::ostream &os, const JsonRpcResponse &req);
};

inline std::ostream &operator<<(std::ostream &os, const JsonRpcRequest &req)
{
    return os << "JSONRPCRequest("
	      << req.service_ << ","
	      << req.method_ << ","
	      << req.id_ << ","
	      << req.params_ << ")";
}

inline std::ostream &operator<<(std::ostream &os, const JsonRpcResponse &req)
{
    return os << "JSONRPCResponse("
	      << req.id_ << ","
	      << req.error_ << ","
	      << req.result_ << ")";
}

#endif // _JSONRPC_h
