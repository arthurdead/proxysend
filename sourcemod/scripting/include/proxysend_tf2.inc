#if defined __PROXYSEND_TF2_INC
	#endinput
#endif
#define __PROXYSEND_TF2_INC

#include <tf2>

#if !defined __PROXYSEND_INC
	#include <proxysend>
#endif

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
