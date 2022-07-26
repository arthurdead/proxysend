#pragma once

#include <IShareSys.h>

#define SMINTERFACE_PROXYSEND_NAME "proxysend"
#define SMINTERFACE_PROXYSEND_VERSION 1

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
		virtual bool is_allowed() const noexcept = 0;
	};

	virtual bool add_listener(const parallel_pack_listener *ptr) noexcept = 0;
	virtual bool remove_listener(const parallel_pack_listener *ptr) noexcept = 0;
};
