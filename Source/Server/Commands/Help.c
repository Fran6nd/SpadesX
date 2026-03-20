#include <Server/Commands/Commands.h>
#include <Server/Server.h>
#include <Util/Notice.h>
#include <Util/Utlist.h>

void cmd_help(void* p_server, command_args_t arguments)
{
    if (arguments.console) {
        return; // /help is a player-only command
    }

    server_t*  server = (server_t*) p_server;
    command_t* cmd    = NULL;

    send_server_notice(arguments.player, arguments.console, "Commands available to you:");

    LL_FOREACH(server->cmds_list, cmd)
    {
        if (player_has_permission(arguments.player, arguments.console, cmd->permissions) > 0 ||
            cmd->permissions == 0)
        {
            send_server_notice(arguments.player, arguments.console, "%s — %s", cmd->id, cmd->description);
        }
    }
}
