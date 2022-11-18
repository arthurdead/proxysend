#pragma once

#include <IShareSys.h>

#define SMINTERFACE_PROXYSEND_NAME "proxysend"
#define SMINTERFACE_PROXYSEND_VERSION 3

class proxysend : public SourceMod::SMInterface
{
public:
	virtual const char *GetInterfaceName() override final
	{ return SMINTERFACE_PROXYSEND_NAME; }
	virtual unsigned int GetInterfaceVersion() override final
	{ return SMINTERFACE_PROXYSEND_VERSION; }

	class parallel_pack_listener
	{
	public:
		virtual bool is_allowed() const noexcept { return true; }
		virtual void pre_pack_entity(CBaseEntity *pEntity) const noexcept {}
		virtual void pre_write_deltas() const noexcept {}
		virtual void post_write_deltas() const noexcept {}
	};

	virtual bool add_listener(const parallel_pack_listener *ptr) noexcept = 0;
	virtual bool remove_listener(const parallel_pack_listener *ptr) noexcept = 0;
	virtual bool remove_serverclass_from_cache(ServerClass *ptr) noexcept = 0;

	enum class prop_types : unsigned char
	{
		int_,
		short_,
		char_,
		unsigned_int,
		unsigned_short,
		unsigned_char,
		float_,
		vector,
		qangle,
		cstring,
		ehandle,
		bool_,
		color32_,
		tstring,
		unknown
	};

	virtual prop_types guess_prop_type(const SendProp *prop, const SendTable *table) const noexcept = 0;
};
