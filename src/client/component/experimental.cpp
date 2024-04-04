#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console.hpp"
#include "dvars.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>

#ifdef DEBUG
namespace experimental
{
	namespace
	{
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

		// 0x4E7BB0
		sync_type Com_StreamSync_CategoryNameToSyncType(const char* name)
		{
			if (!strcmp(name, "mp_char_shirt"))
				return MP_CHAR_SHIRT;
			else if (!strcmp(name, "mp_char_head"))
				return MP_CHAR_HEAD;
			else if (!strcmp(name, "mp_char_gloves"))
				return MP_CHAR_GLOVES;
			else if (!strcmp(name, "mp_worldweap"))
				return MP_WORLDWEAP;
			else if (!strcmp(name, "mp_viewarm"))
				return MP_VIEWARM;
			else if (!strcmp(name, "mp_viewweap"))
				return MP_VIEWWEAP;

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
			if (!utils::io::read_file("h2m-mod/test.asslist", &data_))
			{
				console::error("failed to find test.asslist file\n");
			}

			printf("[parse_asslist_asset] parsing...\n");

			std::vector<std::uint8_t> data;
			data.assign(data_.begin(), data_.end());

			auto data_ptr = data.data();
			unsigned char* current_data_ptr = data.data() + 5;

			// 0x0 is always 0xFE

			// 0x1 is the amount of transient pools to iterate over (5 from example)
			auto pool_count = data_ptr[1] | (data_ptr[2] << 8) | (data_ptr[3] << 16) | (data_ptr[4] << 24);
			if (pool_count)
			{
				auto pools_left_to_handle = pool_count;
				do
				{
					auto current_tr_pool = current_data_ptr; // this is changing throughout iterations of loop
					printf("[parse_asslist_asset] parsing data for tr pool \"%s\"\n", current_tr_pool);

					// go past transient pool name and end up at the null terminator
					auto counter = -1;
					do
					{
						++counter;
					} while (current_data_ptr[counter]);

					auto data_end = reinterpret_cast<std::uint64_t>(data_ptr + 0x30000);
					auto bytes_after_name = &current_data_ptr[counter + 1];
					if (reinterpret_cast<std::uint64_t>(bytes_after_name) > data_end)
					{
						printf("asslist exceeds the max buffer limit\n");
					}

					// idb version (optimized to death)
					auto tr_zone_count_array = (unsigned __int8)*bytes_after_name | (((unsigned __int8)bytes_after_name[1] | ((unsigned __int64)*((unsigned __int16*)bytes_after_name + 1) << 8)) << 8);

					// name + 0x6 is number of tr zones in the specific pool
					auto v18 = ((unsigned __int8)bytes_after_name[6] << 16) | *((unsigned __int16*)bytes_after_name + 2);
					auto v19 = (unsigned __int8)bytes_after_name[7] << 24;
					auto tr_zone_count = v19 | v18; // TODO: label names
					printf("[parse_asslist_asset] pool \"%s\" has %d zones to register\n", current_tr_pool, tr_zone_count);

					// elf version
					/*
					auto count = 0;
					std::uint64_t tr_zone_count_array[36];
					for (auto i = &current_tr_pool[counter + 1]; ; i += 4)
					{
						auto v29 = (unsigned __int16*)(i + 4);
						if ((unsigned __int64)(i + 4) > data_end) // 32?
						{
							printf("asslist exceeds something idfk\n");
						}

						auto tr_zone_count = *(unsigned __int16*)i | (*((unsigned __int8*)i + 2) << 16) | (*((unsigned __int8*)i + 3) << 24);
						if (count > 2)
							break;
						auto index = count++;
						tr_zone_count_array[index] = tr_zone_count;
					}
					*/

					//std::uint8_t out_pool_index;
					//int left_slots;
					auto registered_pool = 0;//CL_TransientMem_RegisterPool(current_tr_pool, tr_zone_count, &tr_zone_count_array, 1, (unsigned __int8*)&out_pool_index, &left_slots) | 0;

					auto sync_type = Com_StreamSync_CategoryNameToSyncType(reinterpret_cast<char*>(current_tr_pool));
					auto count_max = Com_StreamSync_GetCountMax(sync_type);

					// prepare to parse data that occurs every 4 bytes until the transient zones for pool array
					current_data_ptr = bytes_after_name + 8;

					// TODO: does this data even need parsed...?
					// literally no xrefs to anything outside of this if statement, and it doesn't seem to be used anywhere...
					auto v8 = 0;
					auto v41 = 0;
					if (tr_zone_count)
					{
						auto index_ = 0;
						do
						{
							// every 4 bytes 
							auto v24 = *current_data_ptr;		// 0x0 = 00
							auto v25 = current_data_ptr[1];		// 0x1 = 00
							auto count = current_data_ptr[2];	// 0x2 = 50 (type?)
							auto v27 = current_data_ptr[3];		// 0x3 = 20 (something of importance)
							current_data_ptr += 4;

							// wtf is v8 used for????
							/*
							if (index_ < count_max)
							{
								v8 += v24 | ((v25 | ((count | ((unsigned __int64)v27 << 8)) << 8)) << 8);
								printf("[parse_asslist_asset] adding %d to v8\n", v8);
							}
							*/

							++index_;
						} while (index_ < tr_zone_count);
					}

					for (auto i = 0; i < tr_zone_count; ++i)
					{
						auto tr_zone_name = current_data_ptr;
						printf("[parse_asslist_asset] handling tr file \"%s\" (pool: \"%s\")\n", tr_zone_name, current_tr_pool);

						counter = -1;
						do
							++counter;
						while (current_data_ptr[counter]); // current_data_ptr == null terminator

						auto dlc_or_patch = 0 || 0;

						auto file_index = 0;/*CL_TransientMem_RegisterFile(filename, outPoolIndex, i + leftSlots, dlc_or_patch);*/

						auto count_ptr = &current_data_ptr[(counter + 1)]; // a byte after the string is the asset count for the zone
						auto tr_zone_asset_count = *count_ptr;
						current_data_ptr = count_ptr + 1;

						if (tr_zone_asset_count) // checks if it isnt 0
						{
							do
							{
								// this builds together some sort of hash for the xmodel asset entries within the tr zone
								auto name_and_type_hash = (current_data_ptr[2] << 16) | *current_data_ptr;
								auto name_and_type_hash1 = current_data_ptr[3] << 24;
								unsigned int xmodel_asset_hash = name_and_type_hash1 | name_and_type_hash;

								current_data_ptr += 4;
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
			data.push_back(0x0); // null terminator

			// iterate through every pool we need to parse
			for (auto pool_count = 0; pool_count < pools.size(); ++pool_count)
			{
				PUSH_BACK_STRING(pools[pool_count]);		// pool name
				PUSH_BACK_STRING("mp_vm_ak47_base_tr"s);	// first zone name

				auto xmodel_count = 1;
				data.push_back(xmodel_count);				// xmodel count
				// iterate through xmodels
				for (auto i = 0; i < xmodel_count; ++i)
				{
					// 06 CF 85 02
					// contains a "name and type" hash for each xmodel in file

					// example data
					data.push_back(0x06);
					data.push_back(0xCF);
					data.push_back(0x85);
					data.push_back(0x02);
				}
			}

			data.push_back(0xFF); // end of file

			std::string buffer(data.begin(), data.end());
			utils::io::write_file("h2m-mod/generated.asslist", buffer);
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			command::add("parseasslist", []()
			{
				parse_asslist_asset();
			});

			command::add("writeasslist", []()
			{
				write_asslist_asset();
			});

			// fix static model's lighting going black sometimes
			//dvars::override::register_int("r_smodelInstancedThreshold", 0, 0, 128, 0x0);

			// change minimum cap to -2000 instead of -1000 (culling issue)
			dvars::override::register_float("r_lodBiasRigid", 0, -2000, 0, game::DVAR_FLAG_SAVED);
		}
	};
}

REGISTER_COMPONENT(experimental::component)
#endif
