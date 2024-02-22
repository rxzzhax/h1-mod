local Lobby = luiglobals.Lobby
local MPLobbyOnline = LUI.mp_menus.MPLobbyOnline
local MPLobbyPrivate = LUI.mp_menus.MPLobbyPrivate

local function LeaveLobby()
    LeaveXboxLive()
    if Lobby.IsInPrivateParty() == false or Lobby.IsPrivatePartyHost() then
        LUI.FlowManager.RequestLeaveMenuByName("menu_xboxlive")
        Engine.ExecNow("clearcontrollermap")
    end
end

local function menu_xboxlive(f16_arg0)
    local menu = LUI.MPLobbyBase.new(f16_arg0, {
        menu_title = "@PLATFORM_UI_HEADER_PLAY_MP_CAPS",
        memberListState = Lobby.MemberListStates.Prelobby
    })

    menu:setClass(LUI.MPLobbyOnline)

    local serverListButton = menu:AddButton("@LUA_MENU_SERVERLIST", function(a1)
        LUI.FlowManager.RequestAddMenu(a1, "menu_systemlink_join", true, nil)
    end)
    serverListButton:setDisabledRefreshRate(500)
    if Engine.IsCoreMode() then
        menu:AddCACButton()
        menu:AddBarracksButton()
        menu:AddPersonalizationButton()
        menu:AddDepotButton()

        menu:AddButton("@MENU_MODS", function(a1)
            LUI.FlowManager.RequestAddMenu(a1, "mods_menu", true, nil)
        end)
    end

    local privateMatchButton = menu:AddButton("@MENU_PRIVATE_MATCH", MPLobbyOnline.OnPrivateMatch,
        MPLobbyOnline.disablePrivateMatchButton)
    privateMatchButton:rename("menu_xboxlive_private_match")
    privateMatchButton:setDisabledRefreshRate(500)
    if not Engine.IsCoreMode() then
        local leaderboardButton = menu:AddButton("@LUA_MENU_LEADERBOARD", "OpLeaderboardMain")
        leaderboardButton:rename("OperatorMenu_leaderboard")
    end

    menu:AddOptionsButton()
    local natType = Lobby.GetNATType()
    if natType then
        local natTypeText = Engine.Localize("NETWORK_YOURNATTYPE", natType)
        local properties = CoD.CreateState(nil, nil, 2, -62, CoD.AnchorTypes.BottomRight)
        properties.width = 250
        properties.height = CoD.TextSettings.BodyFontVeryTiny.Height
        properties.font = CoD.TextSettings.BodyFontVeryTiny.Font
        properties.color = luiglobals.Colors.white
        properties.alpha = 0.25
        local self = LUI.UIText.new(properties)
        self:setText(natTypeText)
        menu:addElement(self)
    end

    menu:AddMenuDescription(1)
    menu:AddMarketingPanel(LUI.MarketingLocation.Featured, LUI.ComScore.ScreenID.PlayOnline)
    menu.isSignInMenu = true
    menu:registerEventHandler("gain_focus", LUI.MPLobbyOnline.OnGainFocus)
    menu:registerEventHandler("player_joined", luiglobals.Cac.PlayerJoinedEvent)
    menu:registerEventHandler("exit_live_lobby", LeaveLobby)

    if Engine.IsCoreMode() then
        Engine.ExecNow("eliteclan_refresh", Engine.GetFirstActiveController())
    end

    local root = Engine.GetLuiRoot()
    if (root.vltimer) then
        root.vltimer:close()
    end

    root.vltimer = LUI.UITimer.new(4000, "vl")
    root:addElement(root.vltimer)
    root:registerEventHandler("vl", function()
        if (Engine.GetDvarBool("virtualLobbyReady")) then
            root.vltimer:close()
            game:virtuallobbypresentable()
        end
    end)

    menu:AddHelp({
        name = "add_button_helper_text",
        button_ref = "",
        helper_text = "                                                                                                ",
        side = "left",
        priority = -9000,
        clickable = false
    })

    return menu
end

local function menu_xboxlive_privatelobby(f12_arg0)
    local mp_lobby_base = LUI.MPLobbyBase.new(f12_arg0, {
        menu_title = f12_arg1.menu_title or "@LUA_MENU_PRIVATE_MATCH_LOBBY",
        has_match_summary = true
    }, true)
    mp_lobby_base:setClass(LUI.MPLobbyPrivate)
    mp_lobby_base:SetBreadCrumb(Engine.ToUpperCase(Engine.Localize("@PLATFORM_PLAY_ONLINE")))

    if Lobby.IsGameHost() then
        local start_game_btn = mp_lobby_base:AddButton("@LUA_MENU_START_GAME", MPLobbyPrivate.OnStartGame, function()
            return IsStartGameDisabled(mp_lobby_base)
        end)
        start_game_btn:setDisabledRefreshRate(100)

        local game_setup_btn = mp_lobby_base:AddButton("@LUA_MENU_GAME_SETUP", MPLobbyPrivate.OnGameSetup, MPLobbyPrivate.IsGameSetupDisabled)
        if IsGameSetupDisabled(game_setup_btn) then
            game_setup_btn:setDisabledRefreshRate(500)
            LUI.MPLobbyBase.AddLoadingWidgetToButton(game_setup_btn)
        end
    end

    if Engine.IsCoreMode() then
        mp_lobby_base:AddCACButton(true)
        mp_lobby_base:AddPersonalizationButton()
        mp_lobby_base:AddDepotButton()
    end

    mp_lobby_base:AddOptionsButton()
    mp_lobby_base:AddMapDisplay(LUI.MPLobbyMap.new, false)

    if not Lobby.IsGameHost() then
        local self = LUI.UITimer.new(200, "ClientUpdateDescription")
        self.id = "MPLobbyPrivate_desc_timer"
        mp_lobby_base:addElement(self)
        mp_lobby_base:registerEventHandler("ClientUpdateDescription", ClientUpdateDescription)
    end

    mp_lobby_base:registerEventHandler("exit_private_lobby", function(element, event)
        LUI.FlowManager.RequestLeaveMenu(element)
    end)

    mp_lobby_base:registerEventHandler("player_joined", Cac.PlayerJoinedEvent)
    mp_lobby_base:registerEventHandler("loadout_request", Cac.PlayerJoinedEvent)

    if not Engine.GetSplitScreen() then
        Engine.Exec("forcenosplitscreencontrol mplobbyprivate_new")
    end

    mp_lobby_base:AddCurrencyInfoPanel()
    mp_lobby_base.description = mp_lobby_base:AddMenuDescription(4, 1)

    return mp_lobby_base
end

LUI.MenuBuilder.m_types_build["menu_xboxlive"] = menu_xboxlive
LUI.MenuBuilder.m_types_build["menu_xboxlive_privatelobby"] = menu_xboxlive_privatelobby
