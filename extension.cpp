/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Sample Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include <algorithm>
#include "extension.h"
#include <dt_send.h>
#include <unordered_map>
#include <vector>
#include <mathlib/vector.h>
#include <iserverentity.h>
#include <iservernetworkable.h>
#include <server_class.h>
#include <utility>
#include <mutex>
#include <CDetour/detours.h>
#include <memory>
#include "packed_entity.h"
#include <iclient.h>
#include <igameevents.h>
#include <cstdlib>

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

static Sample g_Sample;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_Sample);

static IGameConfig *gameconf{nullptr};
static ISDKHooks *g_pSDKHooks{nullptr};

class CFrameSnapshot;
class CClientFrame;

class CBaseClient : public IGameEventListener2, public IClient, public IClientMessageHandler
{
};

class CGameClient : public CBaseClient
{
};

class CBaseEntity : public IServerEntity
{
};

enum prop_types : unsigned char
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
	unknown,
};

static void global_send_proxy(const SendProp *pProp, const void *pStructBase, const void *pData, DVariant *pOut, int iElement, int objectID);

using tables_t = std::unordered_map<const SendTable *, std::unique_ptr<std::size_t>>;
static tables_t tables;

struct proxyrestore_t final
{
	inline proxyrestore_t(proxyrestore_t &&other) noexcept
	{ operator=(std::move(other)); }

	proxyrestore_t(SendProp *pProp_) noexcept
		: pProp{pProp_}, pRealProxy{pProp->GetProxyFn()}
	{
	#ifdef _DEBUG
		printf("set %s proxy func\n", pProp->GetName());
	#endif
		pProp->SetProxyFn(global_send_proxy);
	}

	~proxyrestore_t() noexcept {
		if(pProp && pRealProxy) {
		#ifdef _DEBUG
			printf("reset %s proxy func\n", pProp->GetName());
		#endif
			pProp->SetProxyFn(pRealProxy);
		}
	}

	proxyrestore_t &operator=(proxyrestore_t &&other) noexcept
	{
		pProp = other.pProp;
		other.pProp = nullptr;
		pRealProxy = other.pRealProxy;
		other.pRealProxy = nullptr;
		return *this;
	}

	SendProp *pProp{nullptr};
	SendVarProxyFn pRealProxy{nullptr};
	std::size_t ref{0};

private:
	proxyrestore_t(const proxyrestore_t &) = delete;
	proxyrestore_t &operator=(const proxyrestore_t &) = delete;
	proxyrestore_t() = delete;
};

static const CStandardSendProxies *std_proxies;

static const SendProp *m_nPlayerCond{nullptr};
static const SendProp *_condition_bits{nullptr};
static const SendProp *m_nPlayerCondEx{nullptr};
static const SendProp *m_nPlayerCondEx2{nullptr};
static const SendProp *m_nPlayerCondEx3{nullptr};
static const SendProp *m_nPlayerCondEx4{nullptr};

static prop_types guess_prop_type(const SendProp *pProp) noexcept
{
	if(pProp == m_nPlayerCond ||
		pProp == _condition_bits ||
		pProp == m_nPlayerCondEx ||
		pProp == m_nPlayerCondEx2 ||
		pProp == m_nPlayerCondEx3 ||
		pProp == m_nPlayerCondEx4) {
		return prop_types::unsigned_int;
	}

	switch(pProp->GetType()) {
		case DPT_Int: {
			SendVarProxyFn pRealProxy{pProp->GetProxyFn()};
			if(pProp->GetFlags() & SPROP_UNSIGNED) {
				if(pRealProxy == std_proxies->m_UInt8ToInt32) {
					if(pProp->m_nBits == 1) {
						return prop_types::bool_;
					}

					return prop_types::unsigned_char;
				} else if(pRealProxy == std_proxies->m_UInt16ToInt32) {
					return prop_types::unsigned_short;
				} else if(pRealProxy == std_proxies->m_UInt32ToInt32) {
					return prop_types::unsigned_int;
				} else {
					{
						if(pProp->m_nBits == NUM_NETWORKED_EHANDLE_BITS) {
							struct dummy_t {
								CBaseHandle val{};
							} dummy;

							DVariant out{};
							pRealProxy(pProp, static_cast<const void *>(&dummy), static_cast<const void *>(&dummy.val), &out, 0, 0);
							if(out.m_Int == INVALID_NETWORKED_EHANDLE_VALUE) {
								return prop_types::ehandle;
							}
						}
					}

				#if 0
					{
						if(pProp->m_nBits == 32) {
							struct dummy_t {
								color32 val{150, 150, 150, 150};
							} dummy;

							DVariant out{};
							pRealProxy(pProp, static_cast<const void *>(&dummy), static_cast<const void *>(&dummy.val), &out, 0, 0);
							if(out.m_Int == *reinterpret_cast<unsigned int *>(&dummy.val)) {
								return prop_types::color32_;
							}
						}
					}
				#endif

					return prop_types::unsigned_int;
				}
			} else {
				if(pRealProxy == std_proxies->m_Int8ToInt32) {
					return prop_types::char_;
				} else if(pRealProxy == std_proxies->m_Int16ToInt32) {
					return prop_types::short_;
				} else if(pRealProxy == std_proxies->m_Int32ToInt32) {
					return prop_types::int_;
				} else {
					{
						struct dummy_t {
							short val{SHRT_MAX-1};
						} dummy;

						DVariant out{};
						pRealProxy(pProp, static_cast<const void *>(&dummy), static_cast<const void *>(&dummy.val), &out, 0, 0);
						if(out.m_Int == dummy.val+1) {
							return prop_types::short_;
						}
					}

					return prop_types::int_;
				}
			}
		}
		case DPT_Float:
		{ return prop_types::float_; }
		case DPT_Vector: {
			if(pProp->m_fLowValue == 0.0f && pProp->m_fHighValue == 360.0f) {
				return prop_types::qangle;
			} else {
				return prop_types::vector;
			}
		}
		case DPT_VectorXY:
		{ return prop_types::vector; }
		case DPT_String:
		{ return prop_types::cstring; }
		case DPT_Array:
		{ return prop_types::unknown; }
		case DPT_DataTable:
		{ return prop_types::unknown; }
	}

	return prop_types::unknown;
}

struct packed_entity_data_t final
{
	packed_entity_data_t(packed_entity_data_t &&) noexcept = default;
	packed_entity_data_t &operator=(packed_entity_data_t &&) noexcept = default;

	std::unique_ptr<char[]> packedData{};
	std::unique_ptr<bf_write> writeBuf{};
	int objectID{-1};

	packed_entity_data_t() noexcept = default;
	~packed_entity_data_t() noexcept = default;

	void allocate() noexcept {
		packedData.reset(static_cast<char *>(aligned_alloc(4, MAX_PACKEDENTITY_DATA)));
		writeBuf.reset(new bf_write{"SV_PackEntity->writeBuf", packedData.get(), MAX_PACKEDENTITY_DATA});
	}

private:
	packed_entity_data_t(const packed_entity_data_t &) = delete;
	packed_entity_data_t &operator=(const packed_entity_data_t &) = delete;
};

struct pack_entity_params_t final
{
	std::vector<std::vector<packed_entity_data_t>> entity_data{};
	std::vector<int> slots{};
	CBaseClient *client{nullptr};
	int current_slot{-1};
	bool in_compute_packs{false};
	std::vector<int> entities{};
	CFrameSnapshot *snapshot{nullptr};

	pack_entity_params_t(std::vector<int> &&slots_, std::vector<int> &&entities_, CFrameSnapshot *snapshot_) noexcept
		: slots{std::move(slots_)}, entities{std::move(entities_)}, snapshot{snapshot_}
	{
		entity_data.resize(slots.size());
		for(auto &it : entity_data) {
			it.reserve(entities_.size());
		}
	}
	~pack_entity_params_t() noexcept = default;

private:
	pack_entity_params_t(const pack_entity_params_t &) = delete;
	pack_entity_params_t &operator=(const pack_entity_params_t &) = delete;
	pack_entity_params_t(pack_entity_params_t &&) = delete;
	pack_entity_params_t &operator=(pack_entity_params_t &&) = delete;
};

static thread_local pack_entity_params_t *current_packentity_params{nullptr};

static void Host_Error(const char *error, ...) noexcept
{
	va_list argptr;
	char string[1024];

	va_start(argptr, error);
	Q_vsnprintf(string, sizeof(string), error, argptr);
	va_end(argptr);

	Error("Host_Error: %s", string);
}

using restores_t = std::unordered_map<SendProp *, std::unique_ptr<proxyrestore_t>>;
static restores_t restores;

struct table_reference_t
{
	table_reference_t(SendTable *pTable) noexcept
	{
		tables_t::iterator it_table{tables.find(pTable)};
		if(it_table == tables.end()) {
		#if defined _DEBUG
			printf("added %s\n", pTable->GetName());
		#endif
			it_table = tables.emplace(std::pair<const SendTable *, std::unique_ptr<std::size_t>>{pTable, new std::size_t{0}}).first;
		}
		table_ref = it_table->second.get();
		++(*table_ref);
	}

	virtual ~table_reference_t() noexcept
	{
		if(table_ref) {
			if(--(*table_ref) == 0) {
				tables_t::iterator it_table{tables.begin()};
				while(it_table != tables.end()) {
					if(it_table->second.get() == table_ref) {
						tables.erase(it_table);
						break;
					}
					++it_table;
				}
			}
		}
	}

	inline table_reference_t(table_reference_t &&other) noexcept
	{ operator=(std::move(other)); }

	table_reference_t &operator=(table_reference_t &&other) noexcept
	{
		table_ref = other.table_ref;
		other.table_ref = nullptr;
		return *this;
	}

	std::size_t *table_ref{nullptr};

private:
	table_reference_t(const table_reference_t &) = delete;
	table_reference_t &operator=(const table_reference_t &) = delete;
	table_reference_t() = delete;
};

struct prop_reference_t
{
	prop_reference_t(SendProp *pProp) noexcept
	{
		restores_t::iterator it_restore{restores.find(pProp)};
		if(it_restore == restores.end()) {
			it_restore = restores.emplace(std::pair<SendProp *, std::unique_ptr<proxyrestore_t>>{pProp, new proxyrestore_t{pProp}}).first;
		}
		restore = it_restore->second.get();
		++restore->ref;
	#ifdef _DEBUG
		printf("added ref %zu for %s\n", restore->ref, pProp->GetName());
	#endif
	}

	virtual ~prop_reference_t() noexcept
	{
		if(restore) {
		#ifdef _DEBUG
			printf("removed ref %zu for %s\n", restore->ref-1u, restore->pProp->GetName());
		#endif
			if(--restore->ref == 0) {
				restores_t::iterator it_restore{restores.begin()};
				while(it_restore != restores.end()) {
					if(it_restore->second.get() == restore) {
						restores.erase(it_restore);
						break;
					}
					++it_restore;
				}
			}
		}
	}

	inline prop_reference_t(prop_reference_t &&other) noexcept
	{ operator=(std::move(other)); }

	prop_reference_t &operator=(prop_reference_t &&other) noexcept
	{
		restore = other.restore;
		other.restore = nullptr;
		return *this;
	}

	proxyrestore_t *restore{nullptr};

private:
	prop_reference_t(const prop_reference_t &) = delete;
	prop_reference_t &operator=(const prop_reference_t &) = delete;
	prop_reference_t() = delete;
};

struct callback_t final : prop_reference_t
{
	callback_t(int index_, SendTable *pTable, SendProp *pProp, prop_types type_, std::size_t offset_) noexcept
		: prop_reference_t{pProp}, offset{offset_}, type{type_}, table{pTable}, prop{pProp}, index{index_}
	{
		if(type == prop_types::cstring) {
			fwd = forwards->CreateForwardEx(nullptr, ET_Hook, 6, nullptr, Param_Cell, Param_String, Param_String, Param_Cell, Param_Cell, Param_Cell);
		} else if(type == prop_types::color32_) {
			fwd = forwards->CreateForwardEx(nullptr, ET_Hook, 8, nullptr, Param_Cell, Param_String, Param_CellByRef, Param_CellByRef, Param_CellByRef, Param_CellByRef, Param_Cell, Param_Cell);
		} else {
			ParamType value_param_type;

			switch(type) {
				case prop_types::int_:
				case prop_types::short_:
				case prop_types::char_:
				case prop_types::unsigned_int:
				case prop_types::unsigned_short:
				case prop_types::unsigned_char:
				case prop_types::bool_:
				case prop_types::ehandle: {
					value_param_type = Param_CellByRef;
				} break;
				case prop_types::float_: {
					value_param_type = Param_FloatByRef;
				} break;
				case prop_types::vector:
				case prop_types::qangle: {
					value_param_type = Param_Array;
				} break;
			}

			fwd = forwards->CreateForwardEx(nullptr, ET_Hook, 5, nullptr, Param_Cell, Param_String, value_param_type, Param_Cell, Param_Cell);
		}
	}

	inline bool has_any_per_client_func() const noexcept
	{ return !per_client_funcs.empty(); }

	void change_edict_state() noexcept
	{
		if(index != -1) {
			edict_t *edict{gamehelpers->EdictOfIndex(index)};
			if(edict) {
				gamehelpers->SetEdictStateChanged(edict, offset);
			}
		}
	}

	void add_function(IPluginFunction *func, bool per_client) noexcept
	{
		fwd->RemoveFunction(func);
		fwd->AddFunction(func);

		if(per_client) {
			bool found{false};

			per_client_funcs_t::const_iterator it_func{per_client_funcs.cbegin()};
			while(it_func != per_client_funcs.cend()) {
				if(it_func->func == func) {
					found = true;
					break;
				}
				++it_func;
			}

			if(!found) {
				per_client_funcs.emplace_back(per_client_func_t{table, func});
			}
		}

		change_edict_state();
	}

	void remove_function(IPluginFunction *func) noexcept
	{
		fwd->RemoveFunction(func);

		per_client_funcs_t::const_iterator it_func{per_client_funcs.cbegin()};
		while(it_func != per_client_funcs.cend()) {
			if(it_func->func == func) {
				per_client_funcs.erase(it_func);
				break;
			}
			++it_func;
		}

		change_edict_state();
	}

	void remove_functions_of_plugin(IPlugin *plugin) noexcept
	{
		per_client_funcs_t::const_iterator it_func{per_client_funcs.cbegin()};
		while(it_func != per_client_funcs.cend()) {
			if((*it_func).func->GetParentContext() == plugin->GetBaseContext()) {
				it_func = per_client_funcs.erase(it_func);
				continue;
			}
			++it_func;
		}

		fwd->RemoveFunctionsOfPlugin(plugin);

		change_edict_state();
	}

	~callback_t() noexcept override final {
		if(fwd) {
			forwards->ReleaseForward(fwd);
		}
		change_edict_state();
	}

	static int get_current_client_slot() noexcept
	{
		if(!current_packentity_params ||
			current_packentity_params->current_slot == -1) {
			return -1;
		}

		return current_packentity_params->current_slot;
	}

	static int get_current_client_entity() noexcept
	{
		int slot{get_current_client_slot()};
		if(slot == -1) {
			return -1;
		}

		return slot+1;
	}

	void fwd_call_ehandle(const SendProp *pProp, const void *pStructBase, const void *pData, DVariant *pOut, int iElement, int objectID) const noexcept
	{
		fwd->PushCell(objectID);
		fwd->PushString(pProp->GetName());
		const CBaseHandle &hndl{*reinterpret_cast<const CBaseHandle *>(pData)};
		edict_t *edict{gamehelpers->GetHandleEntity(const_cast<CBaseHandle &>(hndl))};
		cell_t sp_value{static_cast<cell_t>(edict ? gamehelpers->IndexOfEdict(edict) : -1)};
		fwd->PushCellByRef(&sp_value);
		fwd->PushCell(iElement);
		fwd->PushCell(get_current_client_entity());
		cell_t res{Pl_Continue};
		fwd->Execute(&res);
		if(res == Pl_Changed) {
			edict = gamehelpers->EdictOfIndex(sp_value);
			CBaseHandle new_value{};
			if(edict) {
				gamehelpers->SetHandleEntity(new_value, edict);
			}
			restore->pRealProxy(pProp, pStructBase, static_cast<const void *>(&new_value), pOut, iElement, objectID);
		} else {
			restore->pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
		}
	}

	void fwd_call_color32(const SendProp *pProp, const void *pStructBase, const void *pData, DVariant *pOut, int iElement, int objectID) const noexcept
	{
		fwd->PushCell(objectID);
		fwd->PushString(pProp->GetName());
		const color32 &clr{*reinterpret_cast<const color32 *>(pData)};
		cell_t sp_r{static_cast<cell_t>(clr.r)};
		cell_t sp_g{static_cast<cell_t>(clr.g)};
		cell_t sp_b{static_cast<cell_t>(clr.b)};
		cell_t sp_a{static_cast<cell_t>(clr.a)};
		fwd->PushCellByRef(&sp_r);
		fwd->PushCellByRef(&sp_g);
		fwd->PushCellByRef(&sp_b);
		fwd->PushCellByRef(&sp_a);
		fwd->PushCell(iElement);
		fwd->PushCell(get_current_client_entity());
		cell_t res{Pl_Continue};
		fwd->Execute(&res);
		if(res == Pl_Changed) {
			const color32 new_value{static_cast<byte>(sp_r), static_cast<byte>(sp_g), static_cast<byte>(sp_b), static_cast<byte>(sp_a)};
			restore->pRealProxy(pProp, pStructBase, static_cast<const void *>(&new_value), pOut, iElement, objectID);
		} else {
			restore->pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
		}
	}

	template <typename T>
	void fwd_call_int(const SendProp *pProp, const void *pStructBase, const void *pData, DVariant *pOut, int iElement, int objectID) const noexcept
	{
		fwd->PushCell(objectID);
		fwd->PushString(pProp->GetName());
		cell_t sp_value{static_cast<cell_t>(*reinterpret_cast<const T *>(pData))};
		fwd->PushCellByRef(&sp_value);
		fwd->PushCell(iElement);
		fwd->PushCell(get_current_client_entity());
		cell_t res{Pl_Continue};
		fwd->Execute(&res);
		if(res == Pl_Changed) {
			const T new_value{static_cast<T>(sp_value)};
			if constexpr(std::is_same_v<T, unsigned int>) {
				if(pProp == m_nPlayerCond ||
					pProp == _condition_bits ||
					pProp == m_nPlayerCondEx ||
					pProp == m_nPlayerCondEx2 ||
					pProp == m_nPlayerCondEx3 ||
					pProp == m_nPlayerCondEx4) {
					std_proxies->m_UInt32ToInt32(pProp, pStructBase, static_cast<const void *>(&new_value), pOut, iElement, objectID);
					return;
				}
			}
			restore->pRealProxy(pProp, pStructBase, static_cast<const void *>(&new_value), pOut, iElement, objectID);
		} else {
			restore->pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
		}
	}

	void fwd_call_float(const SendProp *pProp, const void *pStructBase, const void *pData, DVariant *pOut, int iElement, int objectID) const noexcept
	{
		fwd->PushCell(objectID);
		fwd->PushString(pProp->GetName());
		float sp_value{static_cast<float>(*reinterpret_cast<const float *>(pData))};
		fwd->PushFloatByRef(&sp_value);
		fwd->PushCell(iElement);
		fwd->PushCell(get_current_client_entity());
		cell_t res{Pl_Continue};
		fwd->Execute(&res);
		if(res == Pl_Changed) {
			restore->pRealProxy(pProp, pStructBase, static_cast<const void *>(&sp_value), pOut, iElement, objectID);
		} else {
			restore->pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
		}
	}

	template <typename T>
	void fwd_call_vec(const SendProp *pProp, const void *pStructBase, const void *pData, DVariant *pOut, int iElement, int objectID) const noexcept
	{
		fwd->PushCell(objectID);
		fwd->PushString(pProp->GetName());
		const T &vec{*reinterpret_cast<const T *>(pData)};
		cell_t sp_value[3]{
			sp_ftoc(vec[0]),
			sp_ftoc(vec[1]),
			sp_ftoc(vec[2])
		};
		fwd->PushArray(sp_value, 3, SM_PARAM_COPYBACK);
		fwd->PushCell(iElement);
		fwd->PushCell(get_current_client_entity());
		cell_t res{Pl_Continue};
		fwd->Execute(&res);
		if(res == Pl_Changed) {
			const Vector new_value{
				sp_ctof(sp_value[0]),
				sp_ctof(sp_value[1]),
				sp_ctof(sp_value[2]),
			};
			restore->pRealProxy(pProp, pStructBase, static_cast<const void *>(&new_value), pOut, iElement, objectID);
		} else {
			restore->pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
		}
	}

	void fwd_call_str(const SendProp *pProp, const void *pStructBase, const void *pData, DVariant *pOut, int iElement, int objectID) const noexcept
	{
		fwd->PushCell(objectID);
		fwd->PushString(pProp->GetName());
		static char sp_value[4096];
		strcpy(sp_value, reinterpret_cast<const char *>(pData));
		fwd->PushStringEx(sp_value, sizeof(sp_value), SM_PARAM_STRING_UTF8|SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
		fwd->PushCell(sizeof(sp_value));
		fwd->PushCell(iElement);
		fwd->PushCell(get_current_client_entity());
		cell_t res{Pl_Continue};
		fwd->Execute(&res);
		if(res == Pl_Changed) {
			restore->pRealProxy(pProp, pStructBase, static_cast<const void *>(sp_value), pOut, iElement, objectID);
		} else {
			restore->pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
		}
	}

	void proxy_call(const SendProp *pProp, const void *pStructBase, const void *pData, DVariant *pOut, int iElement, int objectID) const noexcept
	{
		if(!fwd || (has_any_per_client_func() && get_current_client_entity() == -1)) {
			restore->pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
			return;
		}

		switch(type) {
			case prop_types::int_: {
				fwd_call_int<int>(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
			case prop_types::bool_: {
				fwd_call_int<bool>(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
			case prop_types::short_: {
				fwd_call_int<short>(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
			case prop_types::char_: {
				fwd_call_int<char>(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
			case prop_types::unsigned_int: {
				fwd_call_int<unsigned int>(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
			case prop_types::unsigned_short: {
				fwd_call_int<unsigned short>(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
			case prop_types::unsigned_char: {
				fwd_call_int<unsigned char>(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
			case prop_types::float_: {
				fwd_call_float(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
			case prop_types::vector: {
				fwd_call_vec<Vector>(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
			case prop_types::qangle: {
				fwd_call_vec<QAngle>(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
			case prop_types::color32_: {
				fwd_call_color32(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
			case prop_types::ehandle: {
				fwd_call_ehandle(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
			case prop_types::cstring: {
				fwd_call_str(pProp, pStructBase, pData, pOut, iElement, objectID);
			} break;
		}
	}

	inline callback_t(callback_t &&other) noexcept
		: prop_reference_t{std::move(other)}
	{ operator=(std::move(other)); }

	callback_t &operator=(callback_t &&other) noexcept
	{
		fwd = other.fwd;
		other.fwd = nullptr;
		prop = other.prop;
		other.prop = nullptr;
		table = other.table;
		other.table = nullptr;
		offset = other.offset;
		type = other.type;
		index = other.index;
		other.index = -1;
		per_client_funcs = std::move(other.per_client_funcs);
		return *this;
	}

	IChangeableForward *fwd{nullptr};
	std::size_t offset{-1};
	prop_types type{prop_types::unknown};
	SendTable *table{nullptr};
	SendProp *prop{nullptr};
	int index{-1};

	struct per_client_func_t : table_reference_t
	{
		IPluginFunction *func{nullptr};

		inline per_client_func_t(SendTable *pTable, IPluginFunction *func_) noexcept
			: table_reference_t{pTable}, func{func_}
		{
		}

		inline per_client_func_t(per_client_func_t &&other) noexcept
			: table_reference_t{std::move(other)}
		{ operator=(std::move(other)); }

		per_client_func_t &operator=(per_client_func_t &&other) noexcept
		{
			func = other.func;
			other.func = nullptr;
			return *this;
		}

	private:
		per_client_func_t(const per_client_func_t &) = delete;
		per_client_func_t &operator=(const per_client_func_t &) = delete;
		per_client_func_t() = delete;
	};

	using per_client_funcs_t = std::vector<per_client_func_t>;
	per_client_funcs_t per_client_funcs{};

private:
	callback_t(const callback_t &) = delete;
	callback_t &operator=(const callback_t &) = delete;
	callback_t() = delete;
};

using callbacks_t = std::unordered_map<std::string, callback_t>;

struct proxyhook_t final : table_reference_t
{
	callbacks_t callbacks;
	int index{-1};

	inline proxyhook_t(int index_, SendTable *pTable) noexcept
		: table_reference_t{pTable}, index{index_}
	{
	}

	~proxyhook_t() noexcept
	{
		if(index != -1) {
			edict_t *edict{gamehelpers->EdictOfIndex(index)};
			if(edict) {
				for(callbacks_t::value_type &it : callbacks) {
					gamehelpers->SetEdictStateChanged(edict, it.second.offset);
					it.second.index = -1;
				}
			}
		}
	}

	void add_callback(std::string &&name, IPluginFunction *func, SendTable *pTable, SendProp *pProp, prop_types type, int offset, bool per_client) noexcept
	{
		callbacks_t::iterator it_callback{callbacks.find(name)};
		if(it_callback == callbacks.end()) {
			it_callback = callbacks.emplace(std::pair<std::string, callback_t>{std::move(name), callback_t{index, pTable, pProp, type, offset}}).first;
		}

		it_callback->second.add_function(func, per_client);
	}

	inline proxyhook_t(proxyhook_t &&other) noexcept
		: table_reference_t{std::move(other)}
	{ operator=(std::move(other)); }

	proxyhook_t &operator=(proxyhook_t &&other) noexcept
	{
		callbacks = std::move(other.callbacks);
		index = other.index;
		other.index = -1;
		return *this;
	}

private:
	proxyhook_t(const proxyhook_t &) = delete;
	proxyhook_t &operator=(const proxyhook_t &) = delete;
	proxyhook_t() = delete;
};

using hooks_t = std::unordered_map<int, proxyhook_t>;
static hooks_t hooks;

DETOUR_DECL_STATIC6(SendTable_Encode, bool, const SendTable *, pTable, const void *, pStruct, bf_write *, pOut, int, objectID, CUtlMemory<CSendProxyRecipients> *, pRecipients, bool, bNonZeroOnly)
{
#if 0
	const tables_t &ctables{tables};

	if(ctables.find(pTable) == ctables.cend()) {
		delete current_packentity_params;
		current_packentity_params = nullptr;
	}
#endif

	if(!current_packentity_params || !current_packentity_params->in_compute_packs) {
		return DETOUR_STATIC_CALL(SendTable_Encode)(pTable, pStruct, pOut, objectID, pRecipients, bNonZeroOnly);
	}

	{
		bool encoded{DETOUR_STATIC_CALL(SendTable_Encode)(pTable, pStruct, pOut, objectID, pRecipients, bNonZeroOnly)};
		if(!encoded) {
			Host_Error( "SV_PackEntity: SendTable_Encode returned false (ent %d).\n", objectID );
			return false;
		}
	}

	const std::vector<int> &entities{current_packentity_params->entities};

	if(std::find(entities.cbegin(), entities.cend(), objectID) != entities.cend()) {
		for(int i{0}; i < current_packentity_params->slots.size(); ++i) {
			packed_entity_data_t &packedData{current_packentity_params->entity_data[i].emplace_back()};

			packedData.objectID = objectID;
			packedData.allocate();

			current_packentity_params->current_slot = current_packentity_params->slots[i];
			bool encoded{DETOUR_STATIC_CALL(SendTable_Encode)(pTable, pStruct, packedData.writeBuf.get(), objectID, pRecipients, bNonZeroOnly)};
			current_packentity_params->current_slot = -1;
			if(!encoded) {
				Host_Error( "SV_PackEntity: SendTable_Encode returned false (ent %d).\n", objectID );
				return false;
			}
		}
	}

	return true;
}

DETOUR_DECL_STATIC8(SendTable_CalcDelta, int, const SendTable *, pTable, const void *, pFromState, const int, nFromBits, const void *, pToState, const int, nToBits, int *, pDeltaProps, int, nMaxDeltaProps, const int, objectID)
{
	if(!current_packentity_params || !current_packentity_params->in_compute_packs) {
		return DETOUR_STATIC_CALL(SendTable_CalcDelta)(pTable, pFromState, nFromBits, pToState, nToBits, pDeltaProps, nMaxDeltaProps, objectID);
	}

	int global_nChanges{DETOUR_STATIC_CALL(SendTable_CalcDelta)(pTable, pFromState, nFromBits, pToState, nToBits, pDeltaProps, nMaxDeltaProps, objectID)};

	const std::vector<int> &entities{current_packentity_params->entities};

	if(std::find(entities.cbegin(), entities.cend(), objectID) != entities.cend()) {
		if(global_nChanges < nMaxDeltaProps) {
			std::unique_ptr<int[]> client_deltaProps{new int[nMaxDeltaProps]{static_cast<unsigned int>(-1)}};

			for(int i{0}; i < current_packentity_params->slots.size(); ++i) {
				packed_entity_data_t &packedData{current_packentity_params->entity_data[i].back()};

				const int client_nChanges{DETOUR_STATIC_CALL(SendTable_CalcDelta)(pTable, pFromState, nFromBits, packedData.packedData.get(), packedData.writeBuf->GetNumBitsWritten(), client_deltaProps.get(), nMaxDeltaProps, objectID)};

				for(int j{0}; j < client_nChanges; ++j) {
					bool found{false};
					for(int k{0}; k < global_nChanges; ++k) {
						if(pDeltaProps[k] == client_deltaProps[j]) {
							found = true;
							break;
						}
					}
					if(!found) {
						pDeltaProps[global_nChanges++] = client_deltaProps[j];
						if(global_nChanges >= nMaxDeltaProps) {
							return nMaxDeltaProps;
						}
					}
				}
			}
		}
	}

	return global_nChanges;
}

class CFrameSnapshot
{
	DECLARE_FIXEDSIZE_ALLOCATOR( CFrameSnapshot );

public:

							CFrameSnapshot();
							~CFrameSnapshot();

	// Reference-counting.
	void					AddReference();
	void					ReleaseReference();

	CFrameSnapshot*			NextSnapshot() const;						


public:
	CInterlockedInt			m_ListIndex;	// Index info CFrameSnapshotManager::m_FrameSnapshots.

	// Associated frame. 
	int						m_nTickCount; // = sv.tickcount
	
	// State information
	class CFrameSnapshotEntry		*m_pEntities;	
	int						m_nNumEntities; // = sv.num_edicts

	// This list holds the entities that are in use and that also aren't entities for inactive clients.
	unsigned short			*m_pValidEntities; 
	int						m_nValidEntities;

	// Additional HLTV info
	class CHLTVEntityData			*m_pHLTVEntityData; // is NULL if not in HLTV mode or array of m_pValidEntities entries
	class CReplayEntityData		*m_pReplayEntityData; // is NULL if not in replay mode or array of m_pValidEntities entries

	class CEventInfo				**m_pTempEntities; // temp entities
	int						m_nTempEntities;

	CUtlVector<int>			m_iExplicitDeleteSlots;

private:

	// Snapshots auto-delete themselves when their refcount goes to zero.
	CInterlockedInt			m_nReferences;
};

DETOUR_DECL_MEMBER2(CFrameSnapshotManager_GetPackedEntity, PackedEntity *, CFrameSnapshot *, pSnapshot, int, entity)
{
	if(!current_packentity_params || !current_packentity_params->client || !current_packentity_params->snapshot) {
		return DETOUR_MEMBER_CALL(CFrameSnapshotManager_GetPackedEntity)(pSnapshot, entity);
	}

	PackedEntity *packed{DETOUR_MEMBER_CALL(CFrameSnapshotManager_GetPackedEntity)(pSnapshot, entity)};
	if(!packed) {
		return nullptr;
	}

	if(current_packentity_params->snapshot->m_ListIndex == pSnapshot->m_ListIndex) {
		const std::vector<int> &entities{current_packentity_params->entities};

		if(std::find(entities.cbegin(), entities.cend(), entity) != entities.cend()) {
			const int slot{current_packentity_params->client->GetPlayerSlot()};

			const packed_entity_data_t *packedData{nullptr};
			for(int i{0}; i < current_packentity_params->slots.size(); ++i) {
				if(current_packentity_params->slots[i] == slot) {
					for(const packed_entity_data_t &it : current_packentity_params->entity_data[i]) {
						if(it.objectID == entity) {
							packedData = &it;
							break;
						}
					}
					break;
				}
			}

			if(packedData) {
				packed->AllocAndCopyPadded(packedData->packedData.get(), packedData->writeBuf->GetNumBytesWritten());
			}
		}
	}

	return packed;
}

DETOUR_DECL_MEMBER4(CBaseServer_WriteDeltaEntities, void, CBaseClient *, client, CClientFrame *, to, CClientFrame *, from, bf_write &, pBuf)
{
	if(!current_packentity_params) {
		DETOUR_MEMBER_CALL(CBaseServer_WriteDeltaEntities)(client, to, from, pBuf);
		return;
	}

	if(client->IsFakeClient() ||
		client->IsHLTV() ||
		client->IsReplay()) {
		current_packentity_params->client = nullptr;
	} else {
		current_packentity_params->client = client;
	}
	DETOUR_MEMBER_CALL(CBaseServer_WriteDeltaEntities)(client, to, from, pBuf);
	current_packentity_params->client = nullptr;
}

DETOUR_DECL_STATIC3(SV_ComputeClientPacks, void, int, clientCount, CGameClient **, clients, CFrameSnapshot *, snapshot)
{
	if(current_packentity_params) {
		delete current_packentity_params;
		current_packentity_params = nullptr;
	}

	std::vector<int> entities{};

	const hooks_t &chooks{hooks};

	entities.reserve(snapshot->m_nValidEntities);

	bool any_per_client{false};

	for(int i{0}; i < snapshot->m_nValidEntities; ++i) {
		hooks_t::const_iterator it_hook{chooks.find(snapshot->m_pValidEntities[i])};
		if(it_hook != chooks.cend()) {
			edict_t *edict{gamehelpers->EdictOfIndex(snapshot->m_pValidEntities[i])};
			for(const auto &it_callback : it_hook->second.callbacks) {
				//gamehelpers->SetEdictStateChanged(edict, it_callback.second.offset);
				if(it_callback.second.has_any_per_client_func()) {
					any_per_client = true;
				}
			}
			if(any_per_client) {
				entities.emplace_back(snapshot->m_pValidEntities[i]);
			}
		}
	}

	std::vector<int> slots{};

	if(any_per_client) {
		slots.reserve(clientCount);

		for(int i{0}; i < clientCount; ++i) {
			CGameClient *client{clients[i]};
			if(client->IsFakeClient() ||
				client->IsHLTV() ||
				client->IsReplay()) {
				continue;
			}
			slots.emplace_back(client->GetPlayerSlot());
		}
	}

	if(any_per_client && !slots.empty() && !entities.empty()) {
		current_packentity_params = new pack_entity_params_t{std::move(slots), std::move(entities), snapshot};
	}

	if(current_packentity_params) {
		current_packentity_params->in_compute_packs = true;
	}
	DETOUR_STATIC_CALL(SV_ComputeClientPacks)(clientCount, clients, snapshot);
	if(current_packentity_params) {
		current_packentity_params->in_compute_packs = false;
	}
}

DETOUR_DECL_MEMBER1(CGameServer_SendClientMessages, void, bool, bSendSnapshots)
{
	DETOUR_MEMBER_CALL(CGameServer_SendClientMessages)(bSendSnapshots);

	if(current_packentity_params) {
		delete current_packentity_params;
		current_packentity_params = nullptr;
	}
}

static void global_send_proxy(const SendProp *pProp, const void *pStructBase, const void *pData, DVariant *pOut, int iElement, int objectID)
{
	const hooks_t &chooks{hooks};
	hooks_t::const_iterator it_hook{chooks.find(objectID)};
	if(it_hook != chooks.cend()) {
		const char *name_ptr{pProp->GetName()};
		const std::string prop{name_ptr};
		callbacks_t::const_iterator it_callback{it_hook->second.callbacks.find(prop)};
		if(it_callback != it_hook->second.callbacks.cend()) {
			it_callback->second.proxy_call(pProp, pStructBase, pData, pOut, iElement, objectID);
			return;
		}
	}

	const restores_t &crestores{restores};
	restores_t::const_iterator it_restore{crestores.find(const_cast<SendProp *>(pProp))};
	if(it_restore != crestores.cend()) {
		it_restore->second->pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
		return;
	}
}

struct sm_sendprop_info_ex_t final : sm_sendprop_info_t
{
	SendTable *table;
};

static bool UTIL_FindInSendTable(SendTable *pTable, 
						  const char *name,
						  sm_sendprop_info_ex_t *info,
						  unsigned int offset) noexcept
{
	int props = pTable->GetNumProps();
	for (int i = 0; i < props; ++i)
	{
		SendProp *prop = pTable->GetProp(i);

		// Skip InsideArray props (SendPropArray / SendPropArray2),
		// we'll find them later by their containing array.
		if (prop->IsInsideArray()) {
			continue;
		}

		const char *pname = prop->GetName();
		SendTable *pInnerTable = prop->GetDataTable();

		if (pname && strcmp(name, pname) == 0)
		{
			// get true offset of CUtlVector
			//TODO!!!!!!
			/*if (utlVecOffsetOffset != -1 && prop->GetOffset() == 0 && pInnerTable && pInnerTable->GetNumProps())
			{
				SendProp *pLengthProxy = pInnerTable->GetProp(0);
				const char *ipname = pLengthProxy->GetName();
				if (ipname && strcmp(ipname, "lengthproxy") == 0 && pLengthProxy->GetExtraData())
				{
					info->table = pTable;
					info->prop = prop;
					info->actual_offset = offset + *reinterpret_cast<size_t *>(reinterpret_cast<intptr_t>(pLengthProxy->GetExtraData()) + utlVecOffsetOffset);
					return true;
				}
			}*/
			info->table = pTable;
			info->prop = prop;
			info->actual_offset = offset + info->prop->GetOffset();
			return true;
		}
		if (pInnerTable)
		{
			if (UTIL_FindInSendTable(pInnerTable, 
				name,
				info,
				offset + prop->GetOffset())
				)
			{
				return true;
			}
		}
	}

	return false;
}

static std::unordered_map<std::string, std::unordered_map<std::string, sm_sendprop_info_ex_t>> propinfos;

static bool FindSendPropInfo(ServerClass *pClass, const std::string &name, sm_sendprop_info_ex_t *info) noexcept
{
	auto it_props{propinfos.find(std::string{pClass->GetName()})};
	if(it_props == propinfos.cend()) {
		it_props = propinfos.emplace(std::pair<std::string, std::unordered_map<std::string, sm_sendprop_info_ex_t>>{name, {}}).first;
		if(UTIL_FindInSendTable(pClass->m_pTable, name.c_str(), info, 0)) {
			it_props->second.emplace(std::pair<std::string, sm_sendprop_info_ex_t>{name, std::move(*info)}).second;
		}
	}
	if(it_props != propinfos.cend()) {
		auto it_prop{it_props->second.find(name)};
		if(it_prop != it_props->second.cend()) {
			*info = it_prop->second;
			return true;
		}
	}
	return false;
}

static cell_t proxysend_hook(IPluginContext *pContext, const cell_t *params) noexcept
{
	int idx{gamehelpers->ReferenceToBCompatRef(params[1])};
	CBaseEntity *pEntity{gamehelpers->ReferenceToEntity(params[1])};
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", idx);
	}

	char *name_ptr;
	pContext->LocalToString(params[2], &name_ptr);
	std::string name{name_ptr};

	IServerNetworkable *pNetwork{pEntity->GetNetworkable()};
	ServerClass *pServer{pNetwork->GetServerClass()};
	SendTable *pClassTable{pServer->m_pTable};

	sm_sendprop_info_ex_t info{};
	if(!FindSendPropInfo(pServer, name.c_str(), &info)) {
		return pContext->ThrowNativeError("Could not find prop %s", name.c_str());
	}
	SendTable *pTable{info.table};

	SendProp *pProp{info.prop};
	prop_types type{guess_prop_type(pProp)};
	if(type == prop_types::unknown) {
		return pContext->ThrowNativeError("Unsupported prop");
	}

#ifdef _DEBUG
	printf("added %s hook for %i\n", pProp->GetName(), idx);
#endif

	IPluginFunction *callback{pContext->GetFunctionById(params[3])};

	hooks_t::iterator it_hook{hooks.find(idx)};
	if(it_hook == hooks.end()) {
		it_hook = hooks.emplace(std::pair<int, proxyhook_t>{idx, proxyhook_t{idx, pClassTable}}).first;
	}

	it_hook->second.add_callback(std::move(name), callback, pTable, pProp, type, info.actual_offset, static_cast<bool>(params[4]));

	return 0;
}

static cell_t proxysend_unhook(IPluginContext *pContext, const cell_t *params) noexcept
{
	int idx{gamehelpers->ReferenceToBCompatRef(params[1])};
	CBaseEntity *pEntity{gamehelpers->ReferenceToEntity(params[1])};
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", idx);
	}

	IServerNetworkable *pNetwork{pEntity->GetNetworkable()};
	ServerClass *pServer{pNetwork->GetServerClass()};

	char *name_ptr;
	pContext->LocalToString(params[2], &name_ptr);
	const std::string name{name_ptr};

	IPluginFunction *callback{pContext->GetFunctionById(params[3])};

	hooks_t::iterator it_hook{hooks.find(idx)};
	if(it_hook != hooks.end()) {
		callbacks_t::iterator it_callback{it_hook->second.callbacks.find(name)};
		if(it_callback != it_hook->second.callbacks.end()) {
			it_callback->second.remove_function(callback);
		#ifdef _DEBUG
			printf("removed %s hook for %i\n", name.c_str(), idx);
		#endif
			if(it_callback->second.fwd->GetFunctionCount() == 0) {
				it_hook->second.callbacks.erase(it_callback);
			}
		}
		if(it_hook->second.callbacks.empty()) {
			hooks.erase(it_hook);
		}
	}

	return 0;
}

static constexpr const sp_nativeinfo_t natives[]{
	{"proxysend_hook", proxysend_hook},
	{"proxysend_unhook", proxysend_unhook},
	{nullptr, nullptr}
};

static CDetour *SendTable_CalcDelta_detour{nullptr};
static CDetour *SendTable_Encode_detour{nullptr};
static CDetour *SV_ComputeClientPacks_detour{nullptr};

static CDetour *CGameServer_SendClientMessages_detour{nullptr};
static CDetour *CFrameSnapshotManager_GetPackedEntity_detour{nullptr};
static CDetour *CBaseServer_WriteDeltaEntities_detour{nullptr};

bool Sample::SDK_OnLoad(char *error, size_t maxlen, bool late) noexcept
{
	gameconfs->LoadGameConfigFile("proxysend", &gameconf, nullptr, 0);

	CDetourManager::Init(smutils->GetScriptingEngine(), gameconf);

	SendTable_CalcDelta_detour = DETOUR_CREATE_STATIC(SendTable_CalcDelta, "SendTable_CalcDelta");
	SendTable_CalcDelta_detour->EnableDetour();

	SendTable_Encode_detour = DETOUR_CREATE_STATIC(SendTable_Encode, "SendTable_Encode");
	SendTable_Encode_detour->EnableDetour();

	SV_ComputeClientPacks_detour = DETOUR_CREATE_STATIC(SV_ComputeClientPacks, "SV_ComputeClientPacks");
	SV_ComputeClientPacks_detour->EnableDetour();

	CGameServer_SendClientMessages_detour = DETOUR_CREATE_MEMBER(CGameServer_SendClientMessages, "CGameServer::SendClientMessages");
	CGameServer_SendClientMessages_detour->EnableDetour();

	CFrameSnapshotManager_GetPackedEntity_detour = DETOUR_CREATE_MEMBER(CFrameSnapshotManager_GetPackedEntity, "CFrameSnapshotManager::GetPackedEntity");
	CFrameSnapshotManager_GetPackedEntity_detour->EnableDetour();

	CBaseServer_WriteDeltaEntities_detour = DETOUR_CREATE_MEMBER(CBaseServer_WriteDeltaEntities, "CBaseServer::WriteDeltaEntities");
	CBaseServer_WriteDeltaEntities_detour->EnableDetour();

	sharesys->AddDependency(myself, "sdkhooks.ext", true, true);

	sharesys->RegisterLibrary(myself, "proxysend");

	plsys->AddPluginsListener(this);

	sharesys->AddNatives(myself, natives);

	sm_sendprop_info_t info{};
	gamehelpers->FindSendPropInfo("CTFPlayer", "m_nPlayerCond", &info);
	m_nPlayerCond = info.prop;
	gamehelpers->FindSendPropInfo("CTFPlayer", "_condition_bits", &info);
	_condition_bits = info.prop;
	gamehelpers->FindSendPropInfo("CTFPlayer", "m_nPlayerCondEx", &info);
	m_nPlayerCondEx = info.prop;
	gamehelpers->FindSendPropInfo("CTFPlayer", "m_nPlayerCondEx2", &info);
	m_nPlayerCondEx2 = info.prop;
	gamehelpers->FindSendPropInfo("CTFPlayer", "m_nPlayerCondEx3", &info);
	m_nPlayerCondEx3 = info.prop;
	gamehelpers->FindSendPropInfo("CTFPlayer", "m_nPlayerCondEx4", &info);
	m_nPlayerCondEx4 = info.prop;

	return true;
}

bool Sample::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late) noexcept
{
	GET_V_IFACE_ANY(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);

	std_proxies = gamedll->GetStandardSendProxies();

	//TODO!!! make thread-safe code
	ConVar *sv_parallel_packentities{g_pCVar->FindVar("sv_parallel_packentities")};
	sv_parallel_packentities->SetValue(false);

	ConVar *sv_parallel_sendsnapshot{g_pCVar->FindVar("sv_parallel_sendsnapshot")};
	sv_parallel_sendsnapshot->SetValue(false);

	return true;
}

void Sample::OnCoreMapEnd() noexcept
{
	tables.clear();
	restores.clear();
	hooks.clear();
}

void Sample::SDK_OnUnload() noexcept
{
	SendTable_CalcDelta_detour->Destroy();
	SendTable_Encode_detour->Destroy();
	SV_ComputeClientPacks_detour->Destroy();
	CGameServer_SendClientMessages_detour->Destroy();
	CFrameSnapshotManager_GetPackedEntity_detour->Destroy();
	CBaseServer_WriteDeltaEntities_detour->Destroy();

	tables.clear();
	restores.clear();
	hooks.clear();

	gameconfs->CloseGameConfigFile(gameconf);

	plsys->RemovePluginsListener(this);
	g_pSDKHooks->RemoveEntityListener(this);
}

void Sample::OnEntityDestroyed(CBaseEntity *pEntity) noexcept
{
	if(!pEntity) {
		return;
	}

	const int idx{gamehelpers->EntityToBCompatRef(pEntity)};

	hooks_t::iterator it_hook{hooks.find(idx)};
	if(it_hook != hooks.end()) {
		hooks.erase(it_hook);
	}
}

void Sample::OnPluginUnloaded(IPlugin *plugin) noexcept
{
	hooks_t::iterator it_hook{hooks.begin()};
	while(it_hook != hooks.end()) {
		callbacks_t::iterator it_callback{it_hook->second.callbacks.begin()};
		while(it_callback != it_hook->second.callbacks.end()) {
			it_callback->second.remove_functions_of_plugin(plugin);
			if(it_callback->second.fwd->GetFunctionCount() == 0) {
				it_callback = it_hook->second.callbacks.erase(it_callback);
				continue;
			}
			++it_callback;
		}
		if(it_hook->second.callbacks.empty()) {
			it_hook = hooks.erase(it_hook);
			continue;
		}
		++it_hook;
	}
}

void Sample::SDK_OnAllLoaded() noexcept
{
	SM_GET_LATE_IFACE(SDKHOOKS, g_pSDKHooks);

	g_pSDKHooks->AddEntityListener(this);
}