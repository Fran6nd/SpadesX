-- bots.lua — spawn bots with a simple wander controller on map load

local bot_ids = {}

-- Controller: walk forward every tick
local WanderController = {}
WanderController.__index = WanderController

function WanderController:update(id)
    bot.move(id, true, false, false, false)
end

on.map_load(function(map_name)
    for i = 1, 3 do
        local b0 = bot.create("Blue_Bot_" .. i, bot.TEAM_A, bot.WEAPON_SMG,   WanderController)
        local b1 = bot.create("Red_Bot_"  .. i, bot.TEAM_B, bot.WEAPON_RIFLE, WanderController)
        if b0 then bot.spawn(b0); bot_ids[#bot_ids + 1] = b0 end
        if b1 then bot.spawn(b1); bot_ids[#bot_ids + 1] = b1 end
    end
    log.info("Bots spawned: " .. #bot_ids .. " total")
end)

on.map_unload(function(map_name)
    for _, id in ipairs(bot_ids) do
        bot.destroy(id)
    end
    bot_ids = {}
end)
