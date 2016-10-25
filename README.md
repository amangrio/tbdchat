# TBDChat

![TBDChat][TBDChat1]
![TBDChat][TBDChat2]

[TBDChat1]: http://i.imgur.com/lGuTIT2.jpg
[TBDChat2]: http://i.imgur.com/2QfQJIA.jpg

### KNOWN BUGS
        
#### Server

- Surely there are bugs we haven't found. No urgent bugs is in itself a bug. 

#### Client

- After resize event, a key must be pressed before input is read into the buffer

- userInput is writing special keys to buffer we don't want there
- Some special keys print more chars in the input window than backspace removes
- Since the changes incorporating getch() into userInput, when parsed by strsep /login and /join return the base commands as args[8], oppose to expected result of args[0].
..* This does not happen for any other command. This is absolutely dumbfounding.
..* The packet buffer is read perfectly fine everywhere else
..* This is absolutely related to how the buffer is now populated with getch
        
        
### TO-DO LIST

#### Smaller adjustments

- [Client] /ignore username. Ignore all messages (dont print) all messages from a user
- [Client/Server] /friends and friends list
- [Server/Client] 2 person only rooms, /msg username, /tell username, or /query username
- [Client/Server] /away messages, if 2 person chat room attribute
- [Client/Server] /logout command (remove from rooms/active users, set logged out, keep connection)
- [Client/Server] /disconnect command (same as /exit but allows the client to /connect again)
- [Server/Client] Private / Public room attribute, room admins, other room attributes
- [client] Curses
..* Add client window to primary screen to display clients in current room
- [Client] /showlog to fork into editor to display a particular log file


#### Requires considerable time

- [Client] Add more to config, read entire config on startup
..* Color preferences
..* more stuff? anyone? config stuff?
- [Client/Server] Allow clients to be in n rooms at a time
- [Client/Server] Full encryption using something like openssl or ->***libsodium***<-
- [Server] Logging 3 levels (root, debug, info); connections, logins, regisrations, disconnects, etc
..* Worth logging all messages for now, but its utility for cost should be considered
..* Will finally bring an end to server spam


### CLIENT FEATURES

#### Commands

- /help
..* Description: Display a list of all available commands.
..* Usage: `/help`
..* Description: Display usage and description for a command.
..* Usage: `/help [command]`
..* Description: Display usage and descriptions for all commands.
..* Usage: `/help all`

- /exit
..* Description: Safely disconnect from the server and end.
..* Usage: `/exit`

- /quit
..* Description: Safely disconnect from the server and end.
..* Usage: `/quit`

- /register
..* Description: Register as a new user.
..* Usage: `/register [username] [password] [password]`

- /login
..* Description: Login as a previously registered user.
..* Usage: `/login [username] [password]`

- /who
..* Description: Print a list the list of users in the same room as you.
..* Usage: `/who`
..* Description: Print a list the list of all connected.
..* Usage: `/who all`
..* Description: Print a users real name.
..* Usage: `/who [username]`

- /invite
..* Description: Invite a user to your current room.
..* Usage: `/invite [username]`

- /join
..* Description: Requests to join the room of the name given, if it exists join it, otherwise create it and join it.
..* Usage: `/join [roomname]`

- /setpass
..* Description: Change your current users password.
..* Usage: `/setpass [current_password] [new_password] [new_password]`

- /setname
..* Description: Change your displayed name.
..* Usage: `/setname [newusername]`

- /connect
..* Description: Connects the client to a chat server.
..* Usage: `/connect [address] [port]`

- /debug
..* Description: Toggle debug mode [print out structured content of each packet received].
..* Usage: `/debug`

- /autoconnect
..* Description: Toggle auto-reconnect on startup.
..* Usage: `/autoconnect`

- /leave
..* Description: Leave the room you are current chatting in and return to the lobby.
..* Usage: `/leave`

- /motd
..* Description: Print out the servers message of the day.
..* Usage: `/motd`

- /list
..* Description: Print out a list of public rooms with active users in them.
..* Usage: `/list`

- /clear
..* Description: clear contents of chat window.
..* Usage: `/clear`


#### Features

- Supports orderless interaction
- Configuration file
- auto-reconnect
- Pretty awesome curses interface
     
 
### SERVER FEATURES

#### Features

- Register
..* Allows a single registration for any given unique username 
..* Stores a SHA256 hashed password associated with each username
..* Saves username/realname/passhash to a file so registration persists after server is shutdown

- Reacts accordingly to all client commands dependent on client state

- Chat with n clients at a time

- Rooms containing unique chat sessions simultaneously
..* Create or join rooms
..* Invite others to join your room 
..* Each room supports n clients

- Sanitizes input fields which require so accordingly

- SHA256 hashing for password storage

