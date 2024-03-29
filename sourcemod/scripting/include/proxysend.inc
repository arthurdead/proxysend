#if defined __PROXYSEND_INC
	#endinput
#endif
#define __PROXYSEND_INC

#include <sdktools>

typeset proxysend_callbacks
{
	function Action (int entity, const char[] prop, int &value, int element, int client);
	function Action (int entity, const char[] prop, RoundState &value, int element, int client);
	function Action (int entity, const char[] prop, RenderMode &value, int element, int client);
	function Action (int entity, const char[] prop, RenderFx &value, int element, int client);

	function Action (int entity, const char[] prop, bool &value, int element, int client);

	function Action (int entity, const char[] prop, float &value, int element, int client);

	function Action (int entity, const char[] prop, float value[3], int element, int client);

	function Action (int entity, const char[] prop, int &r, int &g, int &b, int &a, int element, int client);

	function Action (int entity, const char[] prop, char[] value, int size, int element, int client);
};

native void proxysend_hook(int entity, const char[] prop, proxysend_callbacks callback, bool per_client);
native void proxysend_unhook(int entity, const char[] prop, proxysend_callbacks callback);

#if defined _tf2_included || defined _tf2_stocks_included
	#include <proxysend_tf2>
#endif

#if !defined REQUIRE_EXTENSIONS
public __ext_proxysend_SetNTVOptional()
{
	MarkNativeAsOptional("proxysend_hook");
	MarkNativeAsOptional("proxysend_unhook");
}
#endif

public Extension __ext_proxysend = 
{
	name = "proxysend",
	file = "proxysend.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};
