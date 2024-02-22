local installed_content = {}
installed_content.list_files_res = nil
installed_content.mods_list_files_res = nil
installed_content.usermaps_list_files_res = nil

local function get_installed_content_from_folder(menu, name)
    if (io.directoryexists(name)) then
        installed_content.list_files_res = io.listfiles(name .. "/")
        local mods = installed_content.list_files_res
        for i = 1, #mods do
            local mod_name_ = mods[i]
            if (io.directoryexists(mod_name_) and not io.directoryisempty(mod_name_)) then
                installed_content[mod_name_] = #installed_content;
            end
        end
    end
end

LUI.MenuBuilder.registerType("aurora_hub", function(menu_)
    game:addlocalizedstring("@MENU_AURORAHUB", "AURORA HUB") -- TODO
    local menu = LUI.MenuTemplate.new(menu_, {
        menu_title = "@MENU_AURORAHUB",
        exclusiveController = 0,
        menu_width = GenericMenuDims.OptionMenuWidth,
        menu_top_indent = LUI.MenuTemplate.spMenuOffset,
        showTopRightSmallBar = false,
        uppercase_title = true
    })

    game:addlocalizedstring("@LUA_MENU_AVAILABLE_CONTENT", "AVAILABLE CONTENT") -- TODO
    createdivider(menu, Engine.Localize("@LUA_MENU_AVAILABLE_CONTENT"))

    get_installed_content_from_folder(menu, "mods")
    if (installed_content.list_files_res ~= nil) then
        installed_content.mods_list_files_res = installed_content.list_files_res
    end

    get_installed_content_from_folder(menu, "usermaps")
    if (installed_content.list_files_res ~= nil) then
        installed_content.usermaps_list_files_res = installed_content.list_files_res
    end

    -- get available content
    local available_content = aurora_hub.get_available_content_data()
    for key, value in pairs(available_content) do
        --print("key: " .. key .. ", value: " .. value)

        menu:AddButton(key, function()
            if (installed_content[key] ~= nil) then
                Engine.Exec("loadmod " .. key)
                return
            end

            download.manual_start_download(key)
        end, nil, true, nil, {
            desc_text = desc
        })
    end

    menu:AddBackButton(function(menu)
        Engine.PlaySound(CoD.SFX.MenuBack)
        LUI.FlowManager.RequestLeaveMenu(menu)
    end)

    LUI.Options.InitScrollingList(menu.list, nil)
    menu:CreateBottomDivider()
    menu.optionTextInfo = LUI.Options.AddOptionTextInfo(menu)

    return menu
end)

Engine.GetLuiRoot():registerEventHandler("mod_download_start", function(element, event)
    local popup = LUI.openpopupmenu("generic_waiting_popup_", {
        oncancel = function()
            download.abort()
        end,
        withcancel = true,
        text = "Downloading files..."
    })

    local file = ""

    popup:registerEventHandler("mod_download_set_file", function(element, event)
        file = event.request.name
        popup.text:setText(string.format("Downloading %s...", file))
    end)

    popup:registerEventHandler("mod_download_progress", function(element, event)
        popup.text:setText(string.format("Downloading %s (%i%%)...", file, math.floor(event.fraction * 100)))
    end)

    popup:registerEventHandler("mod_download_done", function()
        LUI.FlowManager.RequestLeaveMenu(popup)
    end)
end)
