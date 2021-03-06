#ifndef _WorkspaceErrors_h
#define _WorkspaceErrors_h


#include <boost/system/error_code.hpp>  // bring in boost::system::error_code et al
#include <iostream>
#include <string>  // for string printing

// This is the custom error code enum
enum class WorkspaceErrc
{
    Success = 0,  // 0 should not represent an error
	InvalidJsonRpcRequest = 1,
	ServiceNotFound = 2,
	MethodNotFound = 3,
	InvalidServiceRequest = 4,
	PermissionDenied = 5,
	};

namespace boost
{
      namespace system
      {
	  // Tell the C++ 11 STL metaprogramming that enum WorkspaceErrc
	  // is registered with the standard error code system
	  template <> struct is_error_code_enum<WorkspaceErrc> : std::true_type
	  {
	  };
      }  // namespace system
}  // namespace boost

namespace detail
{
    // Define a custom error code category derived from boost::system::error_category
    class WorkspaceErrc_category : public boost::system::error_category
    {
    public:
	// Return a short descriptive name for the category
	virtual const char *name() const noexcept override final { return "WorkspaceError"; }
	// Return what each enum means in text
	virtual std::string message(int c) const override final
	{
	    switch(static_cast<WorkspaceErrc>(c))
	    {
	    case WorkspaceErrc::Success:
		return "success";
	    case WorkspaceErrc::InvalidJsonRpcRequest:
		return "invalid JSONRPC request";
	    case WorkspaceErrc::ServiceNotFound:
		return "service not found";
	    case WorkspaceErrc::MethodNotFound:
		return "method not found";
	    case WorkspaceErrc::InvalidServiceRequest:
		return "invalid service request";
	    case WorkspaceErrc::PermissionDenied:
		return "permission denied";
	    default:
		return "unknown";
	    }
	}
	// OPTIONAL: Allow generic error conditions to be compared to me
	virtual boost::system::error_condition default_error_condition(int c) const noexcept override final
	{
	    switch(static_cast<WorkspaceErrc>(c))
	    {
	    case WorkspaceErrc::InvalidJsonRpcRequest:
	    case WorkspaceErrc::ServiceNotFound:
	    case WorkspaceErrc::MethodNotFound:
	    case WorkspaceErrc::InvalidServiceRequest:
	    case WorkspaceErrc::PermissionDenied:
		return make_error_condition(boost::system::errc::invalid_argument);
	    default:
		// I have no mapping for this code
		return boost::system::error_condition(c, *this);
	    }
	}
    };
}  // namespace detail

// Define the linkage for this function to be used by external code.
// This would be the usual __declspec(dllexport) or __declspec(dllimport)
// if we were in a Windows DLL etc. But for this example use a global
// instance but with inline linkage so multiple definitions do not collide.
#define THIS_MODULE_API_DECL extern inline

// Declare a global function returning a static instance of the custom category
THIS_MODULE_API_DECL const detail::WorkspaceErrc_category &WorkspaceErrc_category()
{
    static detail::WorkspaceErrc_category c;
    return c;
}


// Overload the global make_error_code() free function with our
// custom enum. It will be found via ADL by the compiler if needed.
inline boost::system::error_code make_error_code(WorkspaceErrc e)
{
    return {static_cast<int>(e), WorkspaceErrc_category()};
}

#endif
