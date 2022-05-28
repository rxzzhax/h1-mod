#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "dvars.hpp"
#include "version.h"
#include "command.hpp"
#include "console.hpp"
#include "network.hpp"
#include "scheduler.hpp"
#include "filesystem.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/flags.hpp>

namespace patches
{
	namespace
	{
		const char* live_get_local_client_name()
		{
			return game::Dvar_FindVar("name")->current.string;
		}

		utils::hook::detour sv_kick_client_num_hook;

		void sv_kick_client_num(const int client_num, const char* reason)
		{
			// Don't kick bot to equalize team balance.
			if (reason == "EXE_PLAYERKICKED_BOT_BALANCE"s)
			{
				return;
			}
			return sv_kick_client_num_hook.invoke<void>(client_num, reason);
		}

		std::string get_login_username()
		{
			char username[UNLEN + 1];
			DWORD username_len = UNLEN + 1;
			if (!GetUserNameA(username, &username_len))
			{
				return "Unknown Soldier";
			}

			return std::string{username, username_len - 1};
		}

		utils::hook::detour com_register_dvars_hook;

		void com_register_dvars_stub()
		{
			if (game::environment::is_mp())
			{
				// Make name save
				dvars::register_string("name", get_login_username().data(), game::DVAR_FLAG_SAVED, "Player name.");

				// Disable data validation error popup
				dvars::register_int("data_validation_allow_drop", 0, 0, 0, game::DVAR_FLAG_NONE, "");
			}

			return com_register_dvars_hook.invoke<void>();
		}

		utils::hook::detour set_client_dvar_from_server_hook;

		void set_client_dvar_from_server_stub(void* a1, void* a2, const char* dvar, const char* value)
		{
			if (dvar == "cg_fov"s || dvar == "cg_fovMin"s)
			{
				return;
			}

			set_client_dvar_from_server_hook.invoke<void>(0x11AA90_b, a1, a2, dvar, value);
		}

		const char* db_read_raw_file_stub(const char* filename, char* buf, const int size)
		{
			std::string file_name = filename;
			if (file_name.find(".cfg") == std::string::npos)
			{
				file_name.append(".cfg");
			}

			const auto file = filesystem::file(file_name);
			if (file.exists())
			{
				snprintf(buf, size, "%s\n", file.get_buffer().data());
				return buf;
			}

			// DB_ReadRawFile
			return utils::hook::invoke<const char*>(SELECT_VALUE(0x1F4D00_b, 0x3994B0_b), filename, buf, size);
		}

		void bsp_sys_error_stub(const char* error, const char* arg1)
		{
			if (game::environment::is_dedi())
			{
				game::Sys_Error(error, arg1);
			}
			else
			{
				scheduler::once([]()
				{
					command::execute("reconnect");
				}, scheduler::pipeline::main, 1s);
				game::Com_Error(game::ERR_DROP, error, arg1);
			}
		}

		utils::hook::detour cmd_lui_notify_server_hook;
		void cmd_lui_notify_server_stub(game::mp::gentity_s* ent)
		{
			const auto svs_clients = *game::mp::svs_clients;
			if (svs_clients == nullptr)
			{
				return;
			}

			command::params_sv params{};
			const auto menu_id = atoi(params.get(1));
			const auto client = &svs_clients[ent->s.entityNum];

			// 22 => "end_game"
			if (menu_id == 22 && client->header.remoteAddress.type != game::NA_LOOPBACK)
			{
				return;
			}

			cmd_lui_notify_server_hook.invoke<void>(ent);
		}

		void sv_execute_client_message_stub(game::mp::client_t* client, game::msg_t* msg)
		{
			if (client->reliableAcknowledge < 0)
			{
				client->reliableAcknowledge = client->reliableSequence;
				console::info("Negative reliableAcknowledge from %s - cl->reliableSequence is %i, reliableAcknowledge is %i\n",
					client->name, client->reliableSequence, client->reliableAcknowledge);
				network::send(client->header.remoteAddress, "error", "EXE_LOSTRELIABLECOMMANDS", '\n');
				return;
			}

			utils::hook::invoke<void>(0x54EC50_b, client, msg);
		}

		void aim_assist_add_to_target_list(void* a1, void* a2)
		{
			if (!dvars::aimassist_enabled->current.enabled)
			{
				return;
			}

			game::AimAssist_AddToTargetList(a1, a2);
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			// Register dvars
			com_register_dvars_hook.create(SELECT_VALUE(0x385BE0_b, 0x15BB60_b), &com_register_dvars_stub);

			// Unlock fps in main menu
			utils::hook::set<BYTE>(SELECT_VALUE(0x1B1EAB_b, 0x34396B_b), 0xEB);

			if (!game::environment::is_dedi())
			{
				// Fix mouse lag
				utils::hook::nop(SELECT_VALUE(0x4631F9_b, 0x5BFF89_b), 6);
				scheduler::loop([]()
				{
					SetThreadExecutionState(ES_DISPLAY_REQUIRED);
				}, scheduler::pipeline::main);
			}

			// Make cg_fov and cg_fovscale saved dvars
			dvars::override::register_float("cg_fov", 65.f, 40.f, 200.f, game::DvarFlags::DVAR_FLAG_SAVED);
			dvars::override::register_float("cg_fovScale", 1.f, 0.1f, 2.f, game::DvarFlags::DVAR_FLAG_SAVED);
			dvars::override::register_float("cg_fovMin", 1.f, 1.0f, 90.f, game::DvarFlags::DVAR_FLAG_SAVED);

			// Allow kbam input when gamepad is enabled
			utils::hook::nop(SELECT_VALUE(0x1AC0CE_b, 0x135EFB_b), 2);
			utils::hook::nop(SELECT_VALUE(0x1A9DDC_b, 0x13388F_b), 6);

			// Allow executing custom cfg files with the "exec" command
			utils::hook::call(SELECT_VALUE(0x376EB5_b, 0x156D41_b), db_read_raw_file_stub);

			if (!game::environment::is_sp())
			{
				patch_mp();
			}
		}

		static void patch_mp()
		{
			utils::hook::jump(0x5BB9C0_b, &live_get_local_client_name);

			// Disable data validation error popup
			dvars::override::register_int("data_validation_allow_drop", 0, 0, 0, game::DVAR_FLAG_NONE);

			// Patch SV_KickClientNum
			sv_kick_client_num_hook.create(game::SV_KickClientNum, &sv_kick_client_num);

			// block changing name in-game
			utils::hook::set<uint8_t>(0x54CFF0_b, 0xC3);

			// client side aim assist dvar
			dvars::aimassist_enabled = dvars::register_bool("aimassist_enabled", true,
				game::DvarFlags::DVAR_FLAG_SAVED,
				"Enables aim assist for controllers");
			utils::hook::call(0xE857F_b, aim_assist_add_to_target_list);

			// patch "Couldn't find the bsp for this map." error to not be fatal in mp
			utils::hook::call(0x39465B_b, bsp_sys_error_stub);

			// isProfanity
			utils::hook::set(0x361AA0_b, 0xC3C033);

			// disable elite_clan
			dvars::override::register_int("elite_clan_active", 0, 0, 0, game::DVAR_FLAG_NONE);
			utils::hook::set<uint8_t>(0x62D2F0_b, 0xC3); // don't register commands

			// disable codPointStore
			dvars::override::register_int("codPointStore_enabled", 0, 0, 0, game::DVAR_FLAG_NONE);

			// don't register every replicated dvar as a network dvar
			utils::hook::nop(0x47408E_b, 5); // dvar_foreach

			// patch "Server is different version" to show the server client version
			utils::hook::inject(0x54DCE5_b, VERSION);

			// prevent servers overriding our fov
			set_client_dvar_from_server_hook.create(0x11AA90_b, set_client_dvar_from_server_stub);
			utils::hook::nop(0x17DA96_b, 0x16);
			utils::hook::nop(0xE00BE_b, 0x17);
			utils::hook::set<uint8_t>(0x307F39_b, 0xEB);

			// some [data validation] anti tamper thing that kills performance
			dvars::override::register_int("dvl", 0, 0, 0, game::DVAR_FLAG_READ);

			// unlock safeArea_*
			utils::hook::jump(0x347BC5_b, 0x347BD3_b);
			utils::hook::jump(0x347BEC_b, 0x347C17_b);
			dvars::override::register_float("safeArea_adjusted_horizontal", 1, 0, 1, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("safeArea_adjusted_vertical", 1, 0, 1, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("safeArea_horizontal", 1, 0, 1, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("safeArea_vertical", 1, 0, 1, game::DVAR_FLAG_SAVED);

			// allow servers to check for new packages more often
			dvars::override::register_int("sv_network_fps", 1000, 20, 1000, game::DVAR_FLAG_SAVED);

			// Massively increate timeouts
			dvars::override::register_int("cl_timeout", 90, 90, 1800, game::DVAR_FLAG_NONE); // Seems unused
			dvars::override::register_int("sv_timeout", 90, 90, 1800, game::DVAR_FLAG_NONE); // 30 - 0 - 1800
			dvars::override::register_int("cl_connectTimeout", 120, 120, 1800, game::DVAR_FLAG_NONE); // Seems unused
			dvars::override::register_int("sv_connectTimeout", 120, 120, 1800, game::DVAR_FLAG_NONE); // 60 - 0 - 1800

			dvars::register_int("scr_game_spectatetype", 1, 0, 99, game::DVAR_FLAG_REPLICATED, "");

			dvars::override::register_bool("ui_drawCrosshair", true, game::DVAR_FLAG_WRITE);

			dvars::override::register_int("com_maxfps", 0, 0, 1000, game::DVAR_FLAG_SAVED);

			// Prevent clients from ending the game as non host by sending 'end_game' lui notification
			cmd_lui_notify_server_hook.create(0x412D50_b, cmd_lui_notify_server_stub);

			// Prevent clients from sending invalid reliableAcknowledge
			utils::hook::call(0x1CBD06_b, sv_execute_client_message_stub);

			// "fix" for rare 'Out of memory error' error
			if (utils::flags::has_flag("memoryfix"))
			{
				utils::hook::jump(0x6200C0_b, malloc);
				utils::hook::jump(0x61FFE0_b, _aligned_malloc);
				utils::hook::jump(0x620120_b, free);
				utils::hook::jump(0x620210_b, realloc);
				utils::hook::jump(0x620040_b, _aligned_realloc);
			}

			// Change default hostname and make it replicated
			dvars::override::register_string("sv_hostname", "^2H1-Mod^7 Default Server", game::DVAR_FLAG_REPLICATED);

			// Dont free server/client memory on asset loading (fixes crashing on map rotation)
			utils::hook::nop(0x132474_b, 5);
		}
	};
}

REGISTER_COMPONENT(patches::component)
