# Multi Player mini-game

This project uses sockets for client-server communication. There are two independent implementations:
1. Processes (fork / semaphore)
2. Threads (pthread / mutex)

## Installation on Linux

Compile the project by writing:
```
make
```

## How to play

First you need to create an inventory file like this:

```
gold 10
armor 50
ammo 40
lumber 20
magic 15
rock 50
```
<br>

Then you have to run the server by writing:

```
./gameserver –p <num_of_players> -i <game_inventory> -q <quota_per_player>
```

The argument `<num_of_players>` defines the maximum number of players allowed per game.

The argument `<game_inventory>` is the name of the inventory file.

The argument `<quota_per_player>` is the maximum amount of resources that each player is allowed to use.

<br>

Then, you need to create the inventory files for the players like this:
```
ammo 1
magic 2
rock 1
```

<br>

Finally, start playing by writing:

```
./player –n <name> -i <inventory> <server_host>
```

The argument `<name>` is the name of the player.

The argument `<inventory>` is the name of the inventory file of the player.

The argument `<server_host>` is the hostname of the game server (default: `server`).


## Usage example

You can run the demo by writing:
```
run_tests
```

You can see all the active games and their inventories by pressing `Ctrl+Z` in the server terminal.

The players in every game can communicate with one another by writing in their terminals and can exit the game by pressing `Ctrl+C`.
The other players in the same game, as well as the server, are notified with the corresponding message.

The game ends once all the players have exited. You can terminate the game by pressing `Ctrl+C` in the server terminal.
