#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "console.hpp"
#include "dvars.hpp"
#include "network.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/hook.hpp>

namespace experimental
{
	namespace
	{
		static constexpr auto MAX_VOICE_PACKET_DATA = 256;
		static constexpr auto MAX_SERVER_QUEUED_VOICE_PACKETS = 40;
		static constexpr std::size_t MAX_CLIENTS = 18;

		game::VoicePacket_t voice_packets[MAX_CLIENTS][MAX_SERVER_QUEUED_VOICE_PACKETS];
		int voice_packet_count[MAX_CLIENTS];

		bool mute_list[MAX_CLIENTS];
		bool s_playerMute[MAX_CLIENTS];
		//int s_clientTalkTime[MAX_CLIENTS];

		game::dvar_t* sv_voice = nullptr;
#ifdef DEBUG
		game::dvar_t* voice_debug = nullptr;
#endif

		/*
			server voice chat code
		*/
		inline bool sv_voice_enabled()
		{
			return (sv_voice && sv_voice->current.enabled);
		}

		inline void sv_clear_muted_list()
		{
			std::memset(mute_list, 0, sizeof(mute_list));
		}

		inline void sv_mute_client(const int client_index)
		{
			mute_list[client_index] = true;
		}

		inline void sv_unmute_client(const int client_index)
		{
			mute_list[client_index] = false;
		}

		int get_max_clients()
		{
			static const auto max_clients = game::Dvar_FindVar("sv_maxclients");
			if (max_clients)
			{
				return max_clients->current.integer;
			}
			return 1; // idk
		}

		inline bool sv_server_has_client_muted(const int talker)
		{
			return mute_list[talker];
		}

		void sv_queue_voice_packet(const int talker, const int client_num, const game::VoicePacket_t* packet)
		{
			const auto max_clients = get_max_clients();

			auto packet_count = voice_packet_count[client_num];
			if (packet_count > MAX_SERVER_QUEUED_VOICE_PACKETS)
			{
#ifdef DEBUG
				console::error("packet_count exceeds MAX_SERVER_QUEUED_VOICE_PACKETS (%d/%d)\n", packet_count, MAX_SERVER_QUEUED_VOICE_PACKETS);
#endif
				return;
			}

			if (packet->dataSize <= 0 || packet->dataSize > MAX_VOICE_PACKET_DATA)
			{
				return;
			}

			voice_packets[client_num][voice_packet_count[client_num]].dataSize = packet->dataSize;
			std::memcpy(voice_packets[client_num][voice_packet_count[client_num]].data, packet->data, packet->dataSize);

			voice_packets[client_num][voice_packet_count[client_num]].talker = static_cast<char>(talker);
			++voice_packet_count[client_num];
		}

		bool on_same_team(const game::mp::gentity_s* ent, const game::mp::gentity_s* other_ent)
		{
			if (!ent->client || !other_ent->client)
			{
				return false;
			}

			if (ent->client->team)
			{
				return ent->client->team == other_ent->client->team;
			}

			return false;
		}

		inline bool is_session_state(game::mp::gclient_s* client, game::mp::sessionState_t state)
		{
			return (client && client->sessionState == state);
		}

		inline bool is_session_state_same(game::mp::gclient_s* client, game::mp::gclient_s* other_client)
		{
			return (client && other_client && client->sessionState == other_client->sessionState);
		}

		inline bool g_dead_chat_enabled()
		{
			static const auto dead_chat = game::Dvar_FindVar("g_deadchat");
			return (dead_chat && dead_chat->current.enabled);
		}

		void g_broadcast_voice(game::mp::gentity_s* talker, const game::VoicePacket_t* packet)
		{
#ifdef DEBUG
			if (voice_debug->current.enabled)
			{
				console::debug("broadcasting voice from %d to other players...\n", talker->s.number);
			}
#endif

			for (auto other_player = 0; other_player < get_max_clients(); ++other_player)
			{
				auto* target_ent = &game::mp::g_entities[other_player];
				auto* target_client = target_ent->client;

				auto* talker_client = talker->client;

				// TODO: check for non-bots
				if (target_client && talker != target_ent && !sv_server_has_client_muted(talker->s.number)
					&& (is_session_state(target_client, game::mp::SESS_STATE_INTERMISSION)
						|| on_same_team(talker, target_ent) 
						|| talker_client->team == game::mp::TEAM_FREE) 
					&& (is_session_state_same(target_client, talker_client) ||
						(is_session_state(target_client, game::mp::SESS_STATE_DEAD) || 
							(g_dead_chat_enabled() && is_session_state(talker_client, game::mp::SESS_STATE_DEAD)))))
				{
#ifdef DEBUG
					if (voice_debug->current.enabled)
					{
						console::debug("sending voice data to %d\n", other_player);
					}
#endif

					sv_queue_voice_packet(talker->s.number, other_player, packet);
				}
			}
		}

		void sv_user_voice(game::mp::client_t* cl, game::msg_t* msg)
		{
			game::VoicePacket_t packet{};

			if (!sv_voice_enabled())
			{
				return;
			}

			const auto packet_count = game::MSG_ReadByte(msg);
			for (auto packet_itr = 0; packet_itr < packet_count; ++packet_itr)
			{
				packet.dataSize = game::MSG_ReadByte(msg);
				if (packet.dataSize <= 0 || packet.dataSize > MAX_VOICE_PACKET_DATA)
				{
					return;
				}

				game::MSG_ReadData(msg, packet.data, packet.dataSize);
				g_broadcast_voice(cl->gentity, &packet);
			}
		}

		void sv_pre_game_user_voice(game::mp::client_t* cl, game::msg_t* msg)
		{
			game::VoicePacket_t packet{};

			if (!sv_voice_enabled())
			{
				return;
			}

			const auto talker = cl - *game::mp::svs_clients;
			const auto max_clients = get_max_clients();

			const auto packet_count = game::MSG_ReadByte(msg);
			for (auto packet_itr = 0; packet_itr < packet_count; ++packet_itr)
			{
				packet.dataSize = game::MSG_ReadByte(msg);
				if (packet.dataSize <= 0 || packet.dataSize > MAX_VOICE_PACKET_DATA)
				{
					return;
				}

				game::MSG_ReadData(msg, packet.data, packet.dataSize);
				for (auto other_player = 0; other_player < max_clients; ++other_player)
				{
					const auto client_ = game::mp::svs_clients[other_player];
					if (other_player != talker && 
						game::mp::svs_clients[other_player]->header.state >= game::CS_CONNECTED && 
						!sv_server_has_client_muted(talker))
					{
						sv_queue_voice_packet(talker, other_player, &packet);
					}
				}
			}
		}

		void sv_voice_packet(game::netadr_s from, game::msg_t* msg)
		{
			const auto qport = game::MSG_ReadShort(msg);
			auto* cl = utils::hook::invoke<game::mp::client_t*>(0x1CB1F0_b, from, qport);
			if (!cl || cl->header.state == game::CS_ZOMBIE)
			{
				return;
			}

#ifdef DEBUG
			if (voice_debug->current.enabled)
			{
				console::debug("sv_voice_packet called, continuing onwards\n");
			}
#endif

			cl->lastPacketTime = *game::mp::svs_time;
			if (cl->header.state < game::CS_ACTIVE)
			{
				sv_pre_game_user_voice(cl, msg);
			}
			else
			{
				sv_user_voice(cl, msg);
			}
		}

		void sv_write_voice_data_to_client(const int client_num, game::msg_t* msg)
		{
			game::MSG_WriteByte(msg, voice_packet_count[client_num]);
			for (auto packet = 0; packet < voice_packet_count[client_num]; ++packet)
			{
				game::MSG_WriteByte(msg, voice_packets[client_num][packet].talker);

				game::MSG_WriteByte(msg, voice_packets[client_num][packet].dataSize);
				game::MSG_WriteData(msg, voice_packets[client_num][packet].data, voice_packets[client_num][packet].dataSize);
			}
		}

		void sv_send_client_voice_data(game::mp::client_t* client)
		{
			game::msg_t msg{};
			const auto client_num = client - *game::mp::svs_clients;

			const auto msg_buf_large = std::make_unique<unsigned char[]>(0x20000);
			auto* msg_buf = msg_buf_large.get();

			if (client->header.state != game::CS_ACTIVE || voice_packet_count[client_num] < 1)
			{
				return;
			}

			game::MSG_Init(&msg, msg_buf, 0x20000);

			game::MSG_WriteString(&msg, "v");
			sv_write_voice_data_to_client(client_num, &msg);

			if (msg.overflowed)
			{
				console::warn("voice msg overflowed for %s\n", client->name);
			}
			else
			{
				game::NET_OutOfBandVoiceData(game::NS_SERVER, const_cast<game::netadr_s*>(&client->header.remoteAddress), msg.data, msg.cursize);
				voice_packet_count[client_num] = 0;
			}
		}

		void sv_send_client_messages_stub(game::mp::client_t* client, game::msg_t* msg, unsigned char* buf)
		{
			// SV_EndClientSnapshot
			utils::hook::invoke<void>(0x561B50_b, client, msg, buf);

			sv_send_client_voice_data(client);
		}

		/*
			client voice chat code
		*/
		inline bool cl_voice_enabled()
		{
			static const auto cl_voice = game::Dvar_FindVar("cl_voice");
			return (cl_voice && cl_voice->current.enabled);
		}

		inline void cl_clear_muted_list()
		{
			std::memset(mute_list, 0, sizeof(mute_list));
		}

		utils::hook::detour cl_write_voice_packet_hook;
		void cl_write_voice_packet_stub(const int local_client_num)
		{
			if (!game::CL_IsCgameInitialized() || game::VirtualLobby_Loaded() || !cl_voice_enabled())
			{
				return;
			}

			const auto connection_state = game::CL_GetLocalClientConnectionState(local_client_num);
			if (connection_state < game::CA_LOADING)
			{
				return;
			}

			const auto* clc = game::mp::clientConnections[local_client_num];
			const auto vc = *game::mp::cl_voiceCommunication;

			unsigned char packet_buf[0x800]{};
			game::msg_t msg{};

			game::MSG_Init(&msg, packet_buf, sizeof(packet_buf));
			game::MSG_WriteString(&msg, "v");
			game::MSG_WriteShort(&msg, clc->qport);
			game::MSG_WriteByte(&msg, vc.voicePacketCount);

			for (auto packet = 0; packet < vc.voicePacketCount; ++packet)
			{
				game::MSG_WriteByte(&msg, vc.voicePackets[packet].dataSize);
				game::MSG_WriteData(&msg, vc.voicePackets[packet].data, vc.voicePackets[packet].dataSize);
			}

			game::NET_OutOfBandVoiceData(clc->netchan.sock, const_cast<game::netadr_s*>(&clc->serverAddress), msg.data, msg.cursize);
#ifdef DEBUG
			if (voice_debug->current.enabled)
			{
				console::debug("sending voice packet of %d to server\n", vc.voicePacketCount);
			}
#endif
		}

		/*
		bool voice_is_xuid_talking_stub([[maybe_unused]] void* session, __int64 xuid)
		{
			auto current_time = game::Sys_Milliseconds();
			auto client_talk_time = s_clientTalkTime[saved_talking_client];
			if (!client_talk_time)
			{
				return true;
			}

			auto res = (current_time - client_talk_time) < 300;
			return res;
		}
		*/

		bool cl_is_player_muted_stub([[maybe_unused]] void* session, int mute_client_index)
		{
			return s_playerMute[mute_client_index];
		}

		void voice_mute_member_stub([[maybe_unused]] void* session, const int mute_client_index)
		{
#ifdef DEBUG
			if (voice_debug->current.enabled)
			{
				console::debug("muting client number %d\n", mute_client_index);
			}
#endif
			s_playerMute[mute_client_index] = true;
		}

		void voice_unmute_member_stub([[maybe_unused]] void* session, const int mute_client_index)
		{
#ifdef DEBUG
			if (voice_debug->current.enabled)
			{
				console::debug("unmuting client number %d\n", mute_client_index);
			}
#endif
			s_playerMute[mute_client_index] = false;
		}

		void cl_toggle_player_mute(const int local_client_num, const int mute_client_index)
		{
			if (cl_is_player_muted_stub(nullptr, mute_client_index))
			{
				voice_unmute_member_stub(nullptr, mute_client_index);
			}
			else
			{
				voice_mute_member_stub(nullptr, mute_client_index);
			}
		}

		void cl_voice_packet(game::netadr_s* address, game::msg_t* msg)
		{
			if (!game::CL_IsCgameInitialized() || game::VirtualLobby_Loaded() || !cl_voice_enabled())
			{
				return;
			}

			auto* clc = game::mp::clientConnections[0];
			if (!utils::hook::invoke<bool>(0x4F1850_b, clc->serverAddress, *address))
			{
				return;
			}

			const auto num_packets = game::MSG_ReadByte(msg);
			if (num_packets < 0 || num_packets > MAX_SERVER_QUEUED_VOICE_PACKETS)
			{
#ifdef DEBUG
				console::error("num_packets was less than 0 or greater than MAX_SERVER_QUEUED_VOICE_PACKETS (%d/%d)\n", num_packets, MAX_SERVER_QUEUED_VOICE_PACKETS);
#endif
				return;
			}

			game::VoicePacket_t packet{};
			for (auto packet_itr = 0; packet_itr < num_packets; ++packet_itr)
			{
				packet.talker = static_cast<char>(game::MSG_ReadByte(msg));
				packet.dataSize = game::MSG_ReadByte(msg);
				if (packet.dataSize <= 0 || packet.dataSize > MAX_VOICE_PACKET_DATA)
				{
					return;
				}

				game::MSG_ReadData(msg, packet.data, packet.dataSize);

				if (packet.talker >= MAX_CLIENTS)
				{
					return;
				}

				if (!cl_is_player_muted_stub(nullptr, packet.talker))
				{
#ifdef DEBUG
					if (voice_debug->current.enabled)
					{
						console::debug("calling Voice_IncomingVoiceData with talker %d's data\n", packet.talker);
					}
#endif

					// Voice_IncomingVoiceData
					utils::hook::invoke<void>(0x5BF370_b, nullptr, packet.talker, reinterpret_cast<unsigned char*>(packet.data), packet.dataSize);
				
					s_clientTalkTime[packet.talker] = game::Sys_Milliseconds();
				}
			}
		}

		void ui_mute_player_stub([[maybe_unused]] void* session, const int some_index)
		{

		}

		utils::hook::detour client_disconnect_hook;
		void client_disconnect_stub(__int64 client_num, const char* a2)
		{
			sv_unmute_client(client_num);
			client_disconnect_hook.invoke<void>(client_num, a2);
		}

		int stricmp_stub(const char* a1, const char* a2)
		{
			return utils::hook::invoke<int>(0x5AF5F0_b, a1, "v");
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			// change minimum cap to -2000 instead of -1000 (culling issue)
			dvars::override::register_float("r_lodBiasRigid", 0, -2000, 0, game::DVAR_FLAG_SAVED);

			/*
				voice experiments
			*/
			std::memset(voice_packets, 0, sizeof(voice_packets));
			std::memset(voice_packet_count, 0, sizeof(voice_packet_count));

			sv_clear_muted_list();
			cl_clear_muted_list();

			//events::on_steam_disconnect(cl_clear_muted_list);
			client_disconnect_hook.create(0x404730_b, client_disconnect_stub);
			/*
			events::on_client_connect([](const game::mp::client_t* cl) -> void
			{
				if ()
			});
			*/

			// write voice packets to server instead of other clients
			cl_write_voice_packet_hook.create(0x13DB10_b, cl_write_voice_packet_stub);

			// disable 'v' OOB handler and use our own
			utils::hook::set<std::uint8_t>(0x12F2AD_b, 0xEB);
			network::on_raw("v", cl_voice_packet);

			// TODO: add support for UI_IsTalking and other things idek
			//utils::hook::jump(0x5BF7F0_b, voice_is_xuid_talking_stub, true);
			utils::hook::jump(0x1358B0_b, cl_is_player_muted_stub, true);

			utils::hook::call(0x5624F3_b, sv_send_client_messages_stub);

			// recycle server packet handler for icanthear
			utils::hook::call(0x1CAF25_b, stricmp_stub); // use v instead
			utils::hook::call(0x1CAF3E_b, sv_voice_packet);

			//utils::hook::jump(0x1E3A6F_b, ui_mute_player_stub, true);

			utils::hook::jump(0x5BF8B0_b, voice_mute_member_stub, true);
			utils::hook::jump(0x5BFC20_b, voice_unmute_member_stub, true);

			sv_voice = dvars::register_bool("sv_voice", false, game::DVAR_FLAG_NONE, "Use server side voice communications");
#ifdef DEBUG
			voice_debug = dvars::register_bool("voice_debug", true, game::DVAR_FLAG_NONE, "Debug voice chat stuff");
#endif
		}
	};
}

#ifdef DEBUG
REGISTER_COMPONENT(experimental::component)
#endif
