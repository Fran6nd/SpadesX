-- restock_command.lua — /restock command

server.register_command("/restock", "Refill blocks and grenades", function(player_id, args)
    player.restock(player_id)
    player.send_notice(player_id, "Restocked!")
end, 0)
