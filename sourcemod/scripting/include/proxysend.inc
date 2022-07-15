#if defined __PROXYSEND_INC
	#endinput
#endif
#define __PROXYSEND_INC

#include <tf2>

typeset proxysend_callbacks
{
	function Action (int entity, const char[] prop, int &value, int element, int client);
	function Action (int entity, const char[] prop, bool &value, int element, int client);
	function Action (int entity, const char[] prop, float &value, int element, int client);
	function Action (int entity, const char[] prop, float value[3], int element, int client);
	function Action (int entity, const char[] prop, int &r, int &g, int &b, int &a, int element, int client);
	function Action (int entity, const char[] prop, char[] value, int size, int element, int client);
};

native void proxysend_hook(int entity, const char[] prop, proxysend_callbacks callback, bool per_client);
native void proxysend_unhook(int entity, const char[] prop, proxysend_callbacks callback);

stock int get_bit_for_cond(TFCond cond)
{
	int icond = view_as<int>(cond);
	switch(icond / 32) {
		case 0: return (1 << icond);
		case 1: return (1 << (icond - 32));
		case 2: return (1 << (icond - 64));
		case 3: return (1 << (icond - 96));
		case 4: return (1 << (icond - 128));
		default: ThrowError("Invalid TFCond value %d", icond);
	}
	return 0;
}

stock void get_prop_name_for_cond(TFCond cond, char[] name, int len)
{
	int icond = view_as<int>(cond);
	switch(icond / 32) {
		case 0: strcopy(name, len, "m_nPlayerCond");
		case 1: strcopy(name, len, "m_nPlayerCondEx");
		case 2: strcopy(name, len, "m_nPlayerCondEx2");
		case 3: strcopy(name, len, "m_nPlayerCondEx3");
		case 4: strcopy(name, len, "m_nPlayerCondEx4");
		default: ThrowError("Invalid TFCond value %d", icond);
	}
}

stock void proxysend_hook_cond(int entity, TFCond cond, proxysend_callbacks callback, bool per_client)
{
	int icond = view_as<int>(cond);
	switch(icond / 32) {
		case 0: proxysend_hook(entity, "m_nPlayerCond", callback, per_client);
		case 1: proxysend_hook(entity, "m_nPlayerCondEx", callback, per_client);
		case 2: proxysend_hook(entity, "m_nPlayerCondEx2", callback, per_client);
		case 3: proxysend_hook(entity, "m_nPlayerCondEx3", callback, per_client);
		case 4: proxysend_hook(entity, "m_nPlayerCondEx4", callback, per_client);
		default: ThrowError("Invalid TFCond value %d", icond);
	}
}

stock void proxysend_unhook_cond(int entity, TFCond cond, proxysend_callbacks callback)
{
	int icond = view_as<int>(cond);
	switch(icond / 32) {
		case 0: proxysend_unhook(entity, "m_nPlayerCond", callback);
		case 1: proxysend_unhook(entity, "m_nPlayerCondEx", callback);
		case 2: proxysend_unhook(entity, "m_nPlayerCondEx2", callback);
		case 3: proxysend_unhook(entity, "m_nPlayerCondEx3", callback);
		case 4: proxysend_unhook(entity, "m_nPlayerCondEx4", callback);
		default: ThrowError("Invalid TFCond value %d", icond);
	}
}

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