-- init.lua — server-wide script, loaded at startup.
-- All hooks defined here apply across every map rotation.

function on_server_init()
    log.info("Server started.")
end

function on_server_shutdown()
    log.info("Server shutting down.")
end

function on_map_load(map_name)
    log.info("Map loaded: " .. map_name)
end

function on_map_unload(map_name)
    log.info("Map unloaded: " .. map_name)
end

function on_player_connect(player_id)
    local name = player.get_name(player_id)
    server.broadcast(name .. " joined the game.")
end

function on_player_disconnect(player_id, reason)
    local name = player.get_name(player_id)
    server.broadcast(name .. " left the game.")
end

-- /players — available to everyone (permissions = 0)
server.register_command("/players", "List all connected players", function(player_id, args)
    local count = player.count()
    local msg = "Players online: " .. count
    for id, name in player.iterate() do
        msg = msg .. "\n  [" .. id .. "] " .. name
    end
    player.send_notice(player_id, msg)
end, 0)

-- /kill_all — admin-only: only players with the admin role (or higher) can see and run this.
server.register_command("/kill_all", "Kill all players on the server (admin only)", function(player_id, args)
    for id, name in player.iterate() do
        player.kill(id)
    end
    server.broadcast("All players were killed by an admin.")
end, Permission.ADMIN)
