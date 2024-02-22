local MPLobbyOnline = LUI.mp_menus.MPLobbyOnline

-- TODO
LUI.MenuBuilder.registerType("headquarters_menu", function(a1)
    Engine.Exec("ui_mapname mp_creek", 0 )

    local a1_ = LUI.mp_menus.MPLobbyOnline.saved_serverlist_data.a1
    local a2_ = LUI.mp_menus.MPLobbyOnline.saved_serverlist_data.a2

    --[[
    local menu = LUI.MenuTemplate.new(a1, {
        menu_title = "@MENU_HEADQUARTERS",
        exclusiveController = 0,
        menu_width = 400,
        menu_top_indent = LUI.MenuTemplate.spMenuOffset,
        showTopRightSmallBar = true,
        uppercase_title = true
    })

    local modfolder = game:getloadedmod()
    if (modfolder ~= "") then
        local name = getmodname(modfolder)
        createdivider(menu, Engine.Localize("@LUA_MENU_LOADED_MOD", name:truncate(24)))

        menu:AddButton("@LUA_MENU_UNLOAD", function()
            Engine.Exec("unloadmod")
        end, nil, true, nil, {
            desc_text = Engine.Localize("@LUA_MENU_UNLOAD_DESC")
        })
    end

    createdivider(menu, Engine.Localize("@LUA_MENU_AVAILABLE_MODS"))

    if (io.directoryexists("mods")) then
        local mods = io.listfiles("mods/")
        for i = 1, #mods do
            if (io.directoryexists(mods[i]) and not io.directoryisempty(mods[i])) then
                local name, desc = getmodname(mods[i])

                if (mods[i] ~= modfolder) then
                    game:addlocalizedstring(name, name)
                    menu:AddButton(name, function()
                        Engine.Exec("loadmod " .. mods[i])
                    end, nil, true, nil, {
                        desc_text = desc
                    })
                end
            end
        end
    end

    menu:AddBackButton(function(a1)
        Engine.PlaySound(CoD.SFX.MenuBack)
        LUI.FlowManager.RequestLeaveMenu(a1)
    end)

    LUI.Options.InitScrollingList(menu.list, nil)
    menu:CreateBottomDivider()
    menu.optionTextInfo = LUI.Options.AddOptionTextInfo(menu)
    --]]

    return menu
end)
