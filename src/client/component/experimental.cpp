#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console.hpp"
#include "dvars.hpp"
#include "filesystem.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>

#ifdef DEBUG

#define DEBUG_TRANSIENT_ZONES

namespace experimental
{
	namespace
	{
		utils::memory::allocator rawfile_allocator;

		enum sync_type
		{
			MP_INVALID_ASSET = -1,
			MP_CHAR_SHIRT	= 0x0, // 18
 			MP_CHAR_HEAD	= 0x1, // ^
			MP_CHAR_GLOVES	= 0x2, // ^
			MP_WORLDWEAP	= 0x3, // 52
			MP_VIEWARM		= 0x4, // 4
			MP_VIEWWEAP		= 0x5, // 6
			MP_ASSET_COUNT	= 0x6,
		};

		enum transient_pool_type
		{
			MP_INVALID_POOL		= 0x0,
			MP_CHAR_HEAD_POOL	= 0x50,
			MP_VIEWARM_POOL		= 0xB0,
			MP_VIEWWEAP_POOL	= 0xC0,
		};

		// 0x4E7BB0
		sync_type Com_StreamSync_CategoryNameToSyncType(const char* name)
		{
			if (!strcmp(name, "mp_char_shirt"))
				return sync_type::MP_CHAR_SHIRT;
			else if (!strcmp(name, "mp_char_head"))
				return sync_type::MP_CHAR_HEAD;
			else if (!strcmp(name, "mp_char_gloves"))
				return sync_type::MP_CHAR_GLOVES;
			else if (!strcmp(name, "mp_worldweap"))
				return sync_type::MP_WORLDWEAP;
			else if (!strcmp(name, "mp_viewarm"))
				return sync_type::MP_VIEWARM;
			else if (!strcmp(name, "mp_viewweap"))
				return sync_type::MP_VIEWWEAP;

			return MP_INVALID_ASSET;
		}

		// 0x4E7E80
		int Com_StreamSync_GetCountMax(sync_type type)
		{
			if (type < MP_WORLDWEAP)
				return 18;
			else if (type == MP_WORLDWEAP)
				return 52;
			else if (type == MP_VIEWARM)
				return 4;
			return 6;
		}

		void parse_asslist_asset()
		{
			std::string data_;
			if (!utils::io::read_file("h2m-mod/_transient/patch_common_mp.asslist", &data_))
			{
				console::error("failed to find patch_common_mp.asslist file\n");
			}

			printf("[parse_asslist_asset] parsing...\n");

			std::vector<std::uint8_t> data;
			data.assign(data_.begin(), data_.end());

			auto data_ptr = data.data();
			unsigned char* current_char_in_buffer = data.data() + 5;

			// 0x1 is the amount of transient pools to iterate over (5 from example)
			auto pool_count = data_ptr[1] | (data_ptr[2] << 8) | (data_ptr[3] << 16) | (data_ptr[4] << 24);
			if (pool_count) // common_mp should return 5
			{
				auto pools_left_to_handle = pool_count;
				do
				{
					auto current_tr_pool = current_char_in_buffer; // this is changing throughout iterations of loop
					printf("[parse_asslist_asset] parsing data for tr pool \"%s\"\n", current_tr_pool);

					// go past transient pool name and end up at the null terminator
					auto string_length = -1;
					do
					{
						++string_length;
					} while (current_char_in_buffer[string_length]);

					auto bytes_after_name = &current_char_in_buffer[string_length + 1];
					std::uint64_t tr_zone_count_array = (bytes_after_name[2] << 16) | *(unsigned __int16*)bytes_after_name | (bytes_after_name[3] << 24);
					
					auto zone_count = bytes_after_name[4];
					auto tr_zone_count = (*(current_char_in_buffer - 1) << 24) | (*(current_char_in_buffer - 2) << 16) | (bytes_after_name[5] << 8) | zone_count;

					printf("[parse_asslist_asset] pool \"%s\" has %d zones to register\n", current_tr_pool, tr_zone_count); // this prints readable values

					//std::uint8_t out_pool_index;
					//int left_slots;
					auto registered_pool = 0;//CL_TransientMem_RegisterPool(current_tr_pool, &tr_zone_count_array, 1, (unsigned __int8*)&out_pool_index, &left_slots);

					auto sync_type = Com_StreamSync_CategoryNameToSyncType(reinterpret_cast<char*>(current_tr_pool));
					auto count_max = Com_StreamSync_GetCountMax(sync_type);

					// doesnt even matter
					/*
					// prepare to parse data that occurs every 4 bytes until the transient zones for pool array
					int v8_count_unk = 0;
					int index_ = 0;
					if (tr_zone_count)
					{
						while (index_ < tr_zone_count)
						{
							auto v24 = *current_char_in_buffer;
							auto v25 = current_char_in_buffer[1];
							auto v26 = current_char_in_buffer[2];
							auto v27 = current_char_in_buffer[3];
							current_char_in_buffer += 4;
							if (index_ < count_max)
								v8_count_unk += v24 | ((v25 | ((v26 | (v27 << 8)) << 8)) << 8);
							++index_;
						}
						auto v43_count_unk = v8_count_unk;
					}
					*/

					for (auto i = 0; i < tr_zone_count; ++i)
					{
						auto tr_zone_name = current_char_in_buffer;
						printf("[parse_asslist_asset] handling tr file \"%s\" (pool: \"%s\")\n", tr_zone_name, current_tr_pool);

						string_length = -1;
						do
							++string_length;
						while (current_char_in_buffer[string_length]); // current_char_in_buffer == null terminator

						auto dlc_or_patch = 0 || 0;

						unsigned int file_index = 0;/*CL_TransientMem_RegisterFile(filename, outPoolIndex, i + leftSlots, dlc_or_patch);*/

						auto count_ptr = &current_char_in_buffer[(string_length + 1)]; // a byte after the string is the asset count for the zone
						auto tr_zone_asset_count = *count_ptr;
						current_char_in_buffer = count_ptr + 1;

						if (tr_zone_asset_count) // checks if it isnt 0
						{
							do
							{
								// this builds together some sort of hash for the xmodel asset entries within the tr zone
								auto name_and_type_hash = (current_char_in_buffer[2] << 16) | *current_char_in_buffer;
								auto name_and_type_hash1 = current_char_in_buffer[3] << 24;
								unsigned int xmodel_asset_hash = name_and_type_hash1 | name_and_type_hash;

								// not sure if order matters but just following it as PDB goes lol
								//const auto xmodel_asset_hash = ( *(current_char_in_buffer[4]))

								current_char_in_buffer += 4; // check the next name and type hash
								printf("CL_RegisterTransientAsset(%lu, %d, %d);\n", xmodel_asset_hash, file_index, dlc_or_patch);

								//CL_RegisterTransientAsset(name_and_type_hash1 | name_and_type_hash, file_index, dlc_or_patch);
								--tr_zone_asset_count;
							} while (tr_zone_asset_count);
						}
					}

					--pools_left_to_handle;
				} while (pools_left_to_handle);
			}
		}

		unsigned int hash_xmodel_name(const char* xmodel_name)
		{
			int name_and_type_hash = 7;
			for (auto i = *xmodel_name; *xmodel_name; i = *xmodel_name)
			{
				++xmodel_name;
				name_and_type_hash = i ^ (0x1000193 * name_and_type_hash);
			}

			// TODO: break result into 4 different variables because it needs pushed back into a std::vector<std::uint8_t>
			// basically, if the return value is 0x86E2AAEA, it needs to be broken into 0x86, 0xE2, 0xAA, 0xEA

			return name_and_type_hash;
		}

		void write_asslist_asset()
		{
			std::vector<std::string> pools;
			pools.push_back("mp_viewweap");

			std::vector<std::uint8_t> data;

			data.push_back(0xFE);			// 0x0 - TRANSIENT_HEADER
			data.push_back(pools.size());	// 0x1 - transient pool count
			data.push_back(0x0);			// 0x2
			data.push_back(0x0);			// 0x3
			data.push_back(0x0);			// 0x4

			/*
			std::vector<std::uint8_t> pool_name_data;

			for (auto pool_count = 0; pool_count < pools.size(); ++pool_count)
			{
				for (auto i = 0; i < pools[pool_count].size(); ++i)
				{
					pool_name_data.push_back(static_cast<std::uint8_t>(pools[pool_count][i]));
				}
				pool_name_data.push_back(0x0);
			}
			*/

#define PUSH_BACK_STRING(string) \
			for (auto i = 0; i < string.size(); ++i) \
			{ \
				data.push_back(static_cast<std::uint8_t>(string[i]));\
			} \

#define PUSH_BACK_STRING_NULL_T(string) \
			for (auto i = 0; i < string.size(); ++i) \
			{ \
				data.push_back(static_cast<std::uint8_t>(string[i]));\
			} \
			data.push_back(0x0); // null terminator

			std::vector<std::string> tr_zones;
			tr_zones.push_back("mp_h2_vm_m4_base_tr");

			// iterate through every pool we need to parse
			for (auto pool_count = 0; pool_count < pools.size(); ++pool_count)
			{
				PUSH_BACK_STRING_NULL_T(pools[pool_count]); // pool name

				/*
						00 50 20 00 3B 00 00 00 (mp_char_head)
						00 B0 24 00 08 00 00 00 (mp_viewarm)
						00 C0 71 00 34 00 00 00 (mp_viewweap)
						00 C0 82 00 1D 00 00 00 (mp_char_shirt)

						byte breakdown:
						0 = idk_1		(0x00)
						1 = type		(0xC0)
						2 = count?		(0x24, idk what for)
						3 = idk			(0x00, maybe always 0)
						4 = zone count	(0x34)
						5-8 is just 0x0
				*/

				data.push_back(0x0);	// 0x0
				data.push_back(0x0);	// 0x80
				data.push_back(0x0);	// 0x9A
				data.push_back(0x0);	// 0
				data.push_back(0x1);	// 0x52
				for (auto zero_spam = 0; zero_spam < 3; ++zero_spam)
				{
					data.push_back(0x00); // bunch of 0s idek
				}

				/*
						00 C0 71 00 // mp_vm_ak47_base_tr
						00 20 4D 00 // mp_vm_ak47_btw_tr

						00 C0 71 00 // mp_vm_ak74u_base_tr

						00 A0 4C 00 // mp_vm_barrett_base_tr
						00 20 4D 00 // mp_vm_barrett_asn_tr

						00 10 46 00 // mp_vm_beretta_base_tr
						00 A0 42 00 // mp_vm_colt45_base_tr

						mp_vm_m4_base_tr
				*/
				/*
				// reiterate through all assets in pool (TODO: is this needed?)
				for (auto tr_zone_count = 0; tr_zone_count < tr_zones.size(); ++tr_zone_count)
				{
					// this data occurs every 4 bytes
					data.push_back(0x00);
					data.push_back(0xC0);
					data.push_back(0x71);
					data.push_back(0x00);
				}
				*/

				// reiterate through all assets in pool again
				for (auto tr_zone_count = 0; tr_zone_count < tr_zones.size(); ++tr_zone_count)
				{
					PUSH_BACK_STRING_NULL_T(tr_zones[tr_zone_count]); // tr zone name

					auto xmodel_count = 1; // make this not hardcoded lol
					data.push_back(xmodel_count);
					// iterate through xmodels
					for (auto i = 0; i < xmodel_count; ++i)
					{
						// contains a "name and type" hash for each xmodel in file
						const auto name_and_type_hash = hash_xmodel_name("wpn_h2_m4a1_vm");
						data.push_back( static_cast<std::uint8_t>( (name_and_type_hash >> 24) & 0xFF) );
						data.push_back( static_cast<std::uint8_t>( (name_and_type_hash >> 16) & 0xFF) );
						data.push_back( static_cast<std::uint8_t>( (name_and_type_hash >> 8) & 0xFF) );
						data.push_back( static_cast<std::uint8_t>( name_and_type_hash & 0xFF) );
					}
				}
			}

			data.push_back(0xFF); // end of file

			std::string buffer(data.begin(), data.end());
			utils::io::write_file("h2m-mod/generated.asslist", buffer);
		}

		utils::hook::detour bruh_hook;
		bool bruh_stub(const char* xmodel_name, const char type)
		{
			/*
			console::debug("CL_CanUseTransientAsset(%s, %d)\n", xmodel_name, a2);
			return bruh_hook.invoke<bool>(xmodel_name, a2);
			*/

			auto name_and_type_hash = 7; // type is 7
			for (auto i = *xmodel_name; *xmodel_name; i = *xmodel_name)
			{
				++xmodel_name;
				name_and_type_hash = i ^ (0x1000193 * name_and_type_hash);
			}

			printf("");

			return bruh_hook.invoke<bool>(xmodel_name, type);
		}

		game::RawFile* load_custom_asslist(const char* name)
		{
			std::string data;
			if (!filesystem::read_file(name, &data))
			{
				return nullptr;
			}

			const auto rawfile_ptr = static_cast<game::RawFile*>(rawfile_allocator.allocate(sizeof(game::RawFile)));
			rawfile_ptr->name = name;
			rawfile_ptr->compressedLen = 0;
			rawfile_ptr->len = data.size();

			rawfile_ptr->buffer = static_cast<char*>(rawfile_allocator.allocate(data.size() + 1));
			std::memcpy(const_cast<char*>(rawfile_ptr->buffer), data.data(), data.size());

			return rawfile_ptr;
		}

		utils::hook::detour find_asslist_hook;
		game::RawFile* find_asslist_stub(game::XAssetType type, const char* name, int allow_create_default)
		{
			console::debug("looking for \"%s\"\n", name);

			auto* asslist = load_custom_asslist(name);
			if (asslist)
			{
				console::debug("loading custom asslist for \"%s\"\n", name);
				return asslist;
			}

			return game::DB_FindXAssetHeader(type, name, allow_create_default).rawfile;
		}

		std::unordered_map<unsigned int, __int16> hash_map;
		int custom_tr_zone_start = 7000;

		void load_transient_map()
		{
			hash_map = {};
			auto result = hash_xmodel_name("wpn_h2_m4a1_vm");
			hash_map[result] = custom_tr_zone_start;
			++custom_tr_zone_start;
		}

		utils::hook::detour register_tr_asset_hook;
		void register_tr_asset_stub(unsigned int name_and_type_hash, __int16 file_index, int dlc_or_patch)
		{
			hash_map[name_and_type_hash] = file_index;
			//console::debug("registering %u for index %hd\n", name_and_type_hash, file_index);
			register_tr_asset_hook.invoke<void>(name_and_type_hash, file_index, dlc_or_patch);
		}

		utils::hook::detour can_use_tr_asset_hook;
		int can_use_tr_asset_stub(const char* xmodel_name, const char type)
		{
			auto hash = hash_xmodel_name(xmodel_name);
			if (hash_map.contains(hash))
			{
				printf("can use tr asset for \"%s\" (%u)\n", xmodel_name, hash);
				return 1;
			}

			const auto res = can_use_tr_asset_hook.invoke<int>(xmodel_name, type);
			if (res)
			{
				console::debug("can use tr asset for \"%s\" (%u)\n", xmodel_name, hash);
			}

			return res;
		}

		utils::hook::detour find_tr_asset_index_hook;
		__int64 find_tr_asset_index_stub(const unsigned int name_and_type_hash)
		{
			if (hash_map.contains(name_and_type_hash))
			{
				auto index = hash_map[name_and_type_hash];
				printf("returning %hi\n", index);
				return hash_map[name_and_type_hash];
			}

			const auto res = find_tr_asset_index_hook.invoke<__int64>(name_and_type_hash);
			if (res != 0xFFFF)
			{
				//console::debug("find_tr_asset_index_stub isn't 0xFFFF (%u)\n", name_and_type_hash);
			}

			return res;
		}

		utils::hook::detour is_tr_asset_hook;
		int is_tr_asset_stub(const char* name, const char type)
		{
			auto hash = hash_xmodel_name(name);
			if (hash_map.contains(hash))
			{
				printf("is tr asset for \"%s\" (%u)\n", name, hash);
				return 1;
			}

			auto res = is_tr_asset_hook.invoke<int>(name, type);
			return res;
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			load_transient_map();

			command::add("parseasslist", []()
			{
				parse_asslist_asset();
			});

			command::add("writeasslist", []()
			{
				write_asslist_asset();
			});

			command::add("hash_xmodel_name", [](const command::params& params)
			{
				if (params.size() < 2)
				{
					console::info("hash_xmodel_name <name> : creates a hash for the xmodel name\n");
					return;
				}

				auto xmodel_name = params.get(1);

				const auto res = hash_xmodel_name(xmodel_name);

				printf("");
			});

			//bruh_hook.create(0x75000_b, bruh_stub);
			//find_asslist_hook.create(0x39918A_b, find_asslist_stub);

			// fix static model's lighting going black sometimes
			//dvars::override::register_int("r_smodelInstancedThreshold", 0, 0, 128, 0x0);

			// change minimum cap to -2000 instead of -1000 (culling issue)
			dvars::override::register_float("r_lodBiasRigid", 0, -2000, 0, game::DVAR_FLAG_SAVED);

			// debug registering
			register_tr_asset_hook.create(0x75820_b, register_tr_asset_stub);
			can_use_tr_asset_hook.create(0x75000_b, can_use_tr_asset_stub);
			find_tr_asset_index_hook.create(0x751A0_b, find_tr_asset_index_stub);
			is_tr_asset_hook.create(0x753A0_b, is_tr_asset_stub);
		}
	};
}

REGISTER_COMPONENT(experimental::component)
#endif
