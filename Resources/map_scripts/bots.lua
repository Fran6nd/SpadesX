-- bots.lua — spawn bots with a tracker controller on map load

local bot_ids = {}

-- Find the closest living enemy and return their id, or nil if none.
local function closest_enemy(id)
    local my_team = bot.get_team(id)
    local my_pos  = bot.get_position(id)
    if not my_pos then return nil end

    local best_id   = nil
    local best_dist = math.huge

    for pid, _ in player.iterate() do
        if pid ~= id
            and not player.is_bot(pid)
            and player.get_team(pid) ~= my_team
        then
            local pos = player.get_position(pid)
            if pos then
                local dx = pos.x - my_pos.x
                local dy = pos.y - my_pos.y
                local dz = pos.z - my_pos.z
                local d  = dx*dx + dy*dy + dz*dz
                if d < best_dist then
                    best_dist = d
                    best_id   = pid
                end
            end
        end
    end

    return best_id
end

-- Controller: walk forward and track the closest enemy.
local TrackerController = {}
TrackerController.__index = TrackerController

function TrackerController:update(id)
    bot.move(id, true, false, false, false)

    local target = closest_enemy(id)
    if target then
        bot.lookat_player(id, target)
    end
end

on.map_load(function(map_name)
    for i = 1, 3 do
        local b0 = bot.create("Blue_Bot_" .. i)
        if b0 then
            bot.set_team(b0, Team.A)
            bot.set_weapon(b0, Weapon.SMG)
            bot.set_controller(b0, TrackerController)
            bot.spawn(b0)
            bot_ids[#bot_ids + 1] = b0
        end

        local b1 = bot.create("Red_Bot_" .. i)
        if b1 then
            bot.set_team(b1, Team.B)
            bot.set_weapon(b1, Weapon.RIFLE)
            bot.set_controller(b1, TrackerController)
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
