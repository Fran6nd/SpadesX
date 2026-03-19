-- bots.lua — spawn bots with a simple wander controller on map load

local bot_ids = {}

-- Controller: walk forward.
-- Orientation API uses Vector3D:
--   bot.look(id, Vector3D)           — set a direction vector directly
--   bot.lookat_point(id, Vector3D)   — aim toward a world-space position
--   bot.lookat_player(id, target_id) — aim toward another player's eye
local WanderController = {}
WanderController.__index = WanderController

function WanderController:update(id)
    bot.move(id, true, false, false, false)
end

on.map_load(function(map_name)
    for i = 1, 3 do
        local b0 = bot.create("Blue_Bot_" .. i)
        if b0 then
            bot.set_team(b0, bot.TEAM_A)
            bot.set_weapon(b0, bot.WEAPON_SMG)
            bot.set_controller(b0, WanderController)
            bot.spawn(b0)
            bot_ids[#bot_ids + 1] = b0
        end

        local b1 = bot.create("Red_Bot_" .. i)
        if b1 then
            bot.set_team(b1, bot.TEAM_B)
            bot.set_weapon(b1, bot.WEAPON_RIFLE)
            bot.set_controller(b1, WanderController)
            bot.spawn(b1)
            bot_ids[#bot_ids + 1] = b1
        end
    end
    log.info("Bots spawned: " .. #bot_ids .. " total")
end)

on.map_unload(function(map_name)
    for _, id in ipairs(bot_ids) do
        bot.destroy(id)
    end
    bot_ids = {}
end)
