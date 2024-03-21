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

					// go past transient pool name and end up at the next 0x0
					auto counter = -1;
					do
					{
						++counter;
					} while (current_data_ptr[counter]);

					auto bytes_after_name = &current_data_ptr[((unsigned int)(counter + 1))];
					if (bytes_after_name > (data_ptr + 0x30000))
					{
						printf("asslist extends the max buffer limit\n");
					}

					// 112
					auto size_array = (unsigned __int8)*bytes_after_name | (((unsigned __int8)bytes_after_name[1] | ((unsigned __int64)*((unsigned __int16*)bytes_after_name + 1) << 8)) << 8);

					auto v18 = ((unsigned __int8)bytes_after_name[6] << 16) | *((unsigned __int16*)bytes_after_name + 2);
					auto v19 = (unsigned __int8)bytes_after_name[7] << 24;
					auto tr_zone_count = v19 | v18; // TODO: label names

					current_data_ptr = bytes_after_name + 8;

					//auto registered_pool = 0;//CL_TransientMem_RegisterPool(asslistName, tr_zone_count, &size_array, 1i64, (unsigned __int8*)&outPoolIndex, &leftSlots) | 0;

					auto sync_type = Com_StreamSync_CategoryNameToSyncType(reinterpret_cast<char*>(current_tr_pool));
					auto count_max = Com_StreamSync_GetCountMax(sync_type);
					auto slot_counter = 0;

					// wtf is thissssss
					auto v8 = 0;
					auto v41 = 0;

					if (tr_zone_count)
					{
						do
						{
							auto v24 = *current_data_ptr;
							auto v25 = current_data_ptr[1];
							auto v26 = current_data_ptr[2];
							auto v27 = current_data_ptr[3]; // something of importance
							current_data_ptr += 4;

							if (slot_counter < count_max)
							{
								v8 += v24 | ((v25 | ((v26 | ((unsigned __int64)v27 << 8)) << 8)) << 8);
							}

							++slot_counter;
						} while (slot_counter < tr_zone_count);

						v41 = v8;
					}

					for (auto i = 0; i < tr_zone_count; ++i)
					{
						auto tr_zone_name = current_data_ptr;
						printf("[parse_asslist_asset] handling tr file \"%s\" (pool: \"%s\")\n", tr_zone_name, current_tr_pool);

						counter = -1;
						do
							++counter;
						while (current_data_ptr[counter]);

						auto dlc_or_patch = 0 || 0;

						auto v31 = &current_data_ptr[(counter + 1)];
						auto v32 = 0;/*CL_TransientMem_RegisterFile(filename, outPoolIndex, i + leftSlots, dlc_or_patch);*/
						auto tr_zone_asset_count = *v31;
						current_data_ptr = v31 + 1;

						if (tr_zone_asset_count) // checks if it isnt 0
						{
							auto file_index = v32;

							do
							{
								// this builds together some sort of hash for xmodel assets within the tr zone
								auto name_and_type_hash = (current_data_ptr[2] << 16) | *current_data_ptr;
								auto name_and_type_hash1 = current_data_ptr[3] << 24;
								const auto xmodel_asset_hash = name_and_type_hash1 | name_and_type_hash;

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
			pools.push_back("mp_viewarm");

			auto pool_count = 2;

			std::vector<std::uint8_t> data;

			data.push_back(0xFE);			// 0x0 - TRANSIENT_HEADER
			data.push_back(pool_count);		// 0x1 - transient pool count
			data.push_back(0x0);			// 0x2
			data.push_back(0x0);			// 0x3
			data.push_back(0x0);			// 0x4

			std::vector<std::uint8_t> pool_name_data;

			for (auto pool_count = 0; pool_count < pools.size(); ++pool_count)
			{
				for (auto i = 0; i < pools[pool_count].size(); ++i)
				{
					pool_name_data.push_back(static_cast<std::uint8_t>(pools[pool_count][i]));
				}
				data.push_back(0x0); // null terminator
			}

			if (pool_count)
			{
				auto pools_left_to_handle = pool_count;
				for (auto i = 0; i < pools.size(); ++i)
				{
					const auto current_tr_pool = pools[i]; // this is changing throughout iterations of loop
					printf("[parse_asslist_asset] writing data for tr pool \"%s\"\n", current_tr_pool.data());

					// skip past transient pool names
					auto counter = -1;
					do
					{
						++counter;
						data.push_back(pool_name_data.data()[counter]); // write name
					} while (pool_name_data.data()[counter]);
					data.push_back(0x0); // null terminate

					// TODO:

					++pools_left_to_handle;
				}
			}

			// skip past transient pool names
			auto counter = -1;
			do
			{
				++counter;
				data.push_back(pool_name_data.data()[counter]);
			} while (pool_name_data.data()[counter]);
			data.push_back(0x0);

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
