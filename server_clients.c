#include "chat_server.h"


extern int chat_serv_sock_fd; //server socket
extern int numRooms;
extern pthread_mutex_t registered_users_mutex;
extern pthread_mutex_t active_users_mutex;
extern pthread_mutex_t rooms_mutex;
extern User *registered_users_list;
extern User *active_users_list;
extern Room *room_list;
extern char *server_MOTD;


/*
 *Main thread for each client.  Receives all messages
 *and passes the data off to the correct function.  Receives
 *a pointer to the file descriptor for the socket the thread
 *should listen on
 */
void *client_receive(void *ptr) {
   int client = *(int *) ptr;
   int received;
   int logged_in = 0;
   packet in_pkt, *client_message_ptr = &in_pkt;
   while (1) {
      received = recv(client, &in_pkt, sizeof(packet), 0);
      if (received) {
         // Sanitize client buffer
         if (strlen(in_pkt.buf) > 0) {
            sanitizeBuffer((void *)&in_pkt.buf);
         }
         // Print packet contents
         debugPacket(client_message_ptr);
         // Responses to not logged in clients
         if (!logged_in) {
            if(in_pkt.options == REGISTER) {
               logged_in = register_user(&in_pkt, client);
            }
            else if(in_pkt.options == LOGIN) {
               logged_in = login(&in_pkt, client);
            }
            else if(in_pkt.options == EXIT) {
               close(client);
               return NULL;
            }
            else {
               packet ret;
               ret.timestamp = time(NULL);
               ret.options = SERV_ERR;
               strcpy(ret.username, SERVER_NAME);
               strcpy(ret.realname, SERVER_NAME);
               strcpy(ret.buf, "Not logged in.");
               send(client, &ret, sizeof(packet), MSG_NOSIGNAL);
            }
         }
         // Responses to logged in clients
         else if (logged_in) {
            if (in_pkt.options < 1000) {
               //if(in_pkt.options == REGISTER) { 
               //   register_user(&in_pkt, client);
               //}
               if(in_pkt.options == SETPASS) {
                  set_pass(&in_pkt, client);
               }
               else if(in_pkt.options == SETNAME) {
                  set_name(&in_pkt, client);
               }
               //else if(in_pkt.options == LOGIN) {
               //   login(&in_pkt, client);
               //}
               else if(in_pkt.options == EXIT) {
                  exit_client(&in_pkt, client);
                  return NULL;
               }
               else if(in_pkt.options == INVITE) {
                  invite(&in_pkt, client);
               }
               else if(in_pkt.options == JOIN) {
                  join(&in_pkt, client);
               }
               else if(in_pkt.options == LEAVE) {
                  leave(&in_pkt, client);
               }
               else if(in_pkt.options == GETALLUSERS) {
                  get_active_users(client);
               }
               else if(in_pkt.options == GETUSERS) {
                  get_room_users(&in_pkt, client);
               }
               else if(in_pkt.options == GETUSER) {
                  user_lookup(&in_pkt, client);
               }
               else if(in_pkt.options == GETROOMS) {
                  get_room_list(client);
               }
               else if(in_pkt.options == GETMOTD) {
                  sendMOTD(client);
               }
               else if(in_pkt.options == 0) {
                  printf("%s --- Error:%s Potential abrupt disconnect on client.\n", RED, NORMAL);
               }
               else {
                  printf("%s --- Error:%s Unknown message received from client.\n", RED, NORMAL);
               }
            }
            // Handle conversation message
            else {
               send_message(&in_pkt, client);
            }
         }
         else {
            printf("%s --- Error:%s client trying to cause problems.\n", RED, NORMAL);
         }
         memset(&in_pkt, 0, sizeof(packet));
      }
   }
   return NULL;
}


/* Replace any char in buffer not listed in safe_chars */
void sanitizeBuffer(char *buf) {
   char safe_chars[] = "abcdefghijklmnopqrstuvwxyz"
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                       " _,.-/@()*~`&^%$#!?<>'\";:+=[]{}|"
                       "1234567890";
   char *end = buf + strlen(buf);
   for (buf += strspn(buf, safe_chars); buf != end; buf += strspn(buf, safe_chars)) {
      *buf = '_';
   }
}


/*
 *Register
 */
int register_user(packet *in_pkt, int fd) {
   int i = 0;
   char *args[16];
   char cpy[BUFFERSIZE];
   char *tmp = cpy;
   strcpy(tmp, in_pkt->buf);

   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
       args[++i] = strsep(&tmp, " \t");
   }
   if (i > 3) {
      pthread_mutex_lock(&registered_users_mutex);
      if(strcmp(get_real_name(&registered_users_list, args[1]), "ERROR") !=0 || \
                              !(strcmp(SERVER_NAME, args[1])) || \
                              strcmp(args[2], args[3]) != 0) {
         pthread_mutex_unlock(&registered_users_mutex);
         packet ret;
         ret.timestamp = time(NULL);
         ret.options = SERV_ERR;
         strcpy(ret.username, SERVER_NAME);
         strcpy(ret.realname, SERVER_NAME);
         strcpy(ret.buf, "Username unavailable.");
         send(fd, &ret, sizeof(packet), MSG_NOSIGNAL);
         return 0;
      }
      else { pthread_mutex_unlock(&registered_users_mutex); }

      User *user = (User *)malloc(sizeof(User));
      strcpy(user->username, args[1]);
      strcpy(user->real_name, args[1]);
      strcpy(user->password, args[2]);
      user->sock = fd;
      user->next = NULL;
      pthread_mutex_lock(&registered_users_mutex);
      insertUser(&registered_users_list, user);
      writeUserFile(&registered_users_list, USERS_FILE);
      pthread_mutex_unlock(&registered_users_mutex);

      memset(&in_pkt->buf, 0, sizeof(in_pkt->buf));
      sprintf(in_pkt->buf, "/login %s %s", args[1], args[2]);
      return login(in_pkt, fd);
   }
   else {
      printf("%s --- %sError:%s Malformed reg packet received from %s on %d, ignoring.\n", \
             WHITE, RED, NORMAL, args[1], fd);
   }
   return 0;
}


/*
 *Login
 */
int login(packet *pkt, int fd) {
   int i = 0;
   char *args[16];
   char cpy[BUFFERSIZE];
   char *tmp = cpy;
   strcpy(tmp, pkt->buf);

   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
       args[++i] = strsep(&tmp, " \t");
   }
   if (i > 2) {
      packet ret;

      // Check if user exists 
      pthread_mutex_lock(&registered_users_mutex);
      if (strcmp(get_real_name(&registered_users_list, args[1]), "ERROR") == 0) {
         pthread_mutex_unlock(&registered_users_mutex);
         ret.timestamp = time(NULL);
         ret.options = SERV_ERR;
         strcpy(ret.username, SERVER_NAME);
         strcpy(ret.realname, SERVER_NAME);
         strcpy(ret.buf, "Username not found.");
         send(fd, &ret, sizeof(packet), MSG_NOSIGNAL);
         return 0;
      }
      else { pthread_mutex_unlock(&registered_users_mutex); }

      // Check for password patch
      pthread_mutex_lock(&registered_users_mutex);
      char *password = get_password(&registered_users_list, args[1]);
      pthread_mutex_unlock(&registered_users_mutex);
      if (strcmp(args[2], password) != 0) {
         ret.timestamp = time(NULL);
         ret.options = SERV_ERR;
         strcpy(ret.username, SERVER_NAME);
         strcpy(ret.realname, SERVER_NAME);
         strcpy(ret.buf, "Incorrect password.");
         send(fd, &ret, sizeof(packet), MSG_NOSIGNAL);
         return 0;
      }

      //Login successful, send username to client and add to active_users
      pthread_mutex_lock(&registered_users_mutex);
      User *user = get_user(&registered_users_list, args[1]);
      pthread_mutex_unlock(&registered_users_mutex);
      user->sock = fd;
      user = clone_user(user);

      pthread_mutex_lock(&active_users_mutex);
      if(insertUser(&active_users_list, user) == 1) {
         pthread_mutex_unlock(&active_users_mutex);
         pthread_mutex_lock(&rooms_mutex);
         Room *defaultRoom = Rget_roomFID(&room_list, DEFAULT_ROOM);
         user = clone_user(user);
         insertUser(&(defaultRoom->user_list), user);
         RprintList(&room_list);
         pthread_mutex_unlock(&rooms_mutex);
         pthread_mutex_lock(&registered_users_mutex);
         strcpy(ret.realname, get_real_name(&registered_users_list, args[1]));
         pthread_mutex_unlock(&registered_users_mutex);
         strcpy(ret.username, args[1]);
         ret.options = LOGSUC;
         printf("%s logged in\n", ret.username);
      }
      else {
         pthread_mutex_unlock(&active_users_mutex);
         ret.options = SERV_ERR;
         strcpy(ret.realname, SERVER_NAME);
         strcpy(ret.username, SERVER_NAME);
         sprintf(ret.buf, "%s already logged in.", args[1]);
         printf("%s log in failed: already logged in", args[1]);
      }

      ret.timestamp = time(NULL);
      send(fd, &ret, sizeof(packet), MSG_NOSIGNAL);
      if (ret.options == LOGSUC) {
         memset(&ret, 0, sizeof(packet));
         ret.options = DEFAULT_ROOM;
         strcpy(ret.realname, SERVER_NAME);
         strcpy(ret.username, SERVER_NAME);
         sprintf(ret.buf, "%s has joined the lobby.", user->real_name);
         ret.timestamp = time(NULL);
         send_message(&ret, -1);
         sendMOTD(fd);
      }
      return 1;
   }
   else {
      printf("%s --- %sError:%s Malformed login packet received from %s on %d, ignoring.\n", \
             WHITE, RED, NORMAL, args[1], fd);
   }
   return 0;
}


/*
 *Invite
 */
void invite(packet *in_pkt, int fd) {
   int i = 0, roomNum;
   char *args[16];
   char *tmp = in_pkt->buf;
   packet ret;

   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
      args[++i] = strsep(&tmp, " \t");
   }
   if (i > 1) {
      roomNum = atoi(args[1]);
      Room *currRoom = Rget_roomFID(&room_list, roomNum);
      if (currRoom != NULL) {
         User *inviteUser = get_user(&active_users_list, args[0]);
         if (inviteUser != NULL) {
            ret.options = INVITE;
            strcpy(ret.username, SERVER_NAME);
            memset(&ret.buf, 0, sizeof(ret.buf));
            sprintf(ret.buf, "%s has invited you to join %s", \
                    in_pkt->realname, Rget_name(&room_list, roomNum));
            send(inviteUser->sock, &ret, sizeof(packet), MSG_NOSIGNAL);
            memset(&ret, 0, sizeof(packet));
            ret.options = INVITESUC;
            strcpy(ret.username, SERVER_NAME);
            send(fd, &ret, sizeof(packet), MSG_NOSIGNAL);
            return;
         }
      }
      else {
         printf("%s --- Error:%s Trying to read user info but room is null.\n", RED, NORMAL);
      }
   }
   else {
      printf("%s --- Error:%s Malformed buffer received, ignoring.\n", RED, NORMAL);
   }
   ret.options = SERV_ERR;
   strcpy(ret.username, SERVER_NAME);
   strcpy(ret.realname, SERVER_NAME);
   sprintf(ret.buf, "An invitation could not be sent to %s.", args[0]);
   send(fd, &ret, sizeof(packet), MSG_NOSIGNAL);
}


/*
 *Join a chat room
 */
void join(packet *pkt, int fd) {
   int i = 0;
   char *args[16];
   char *tmp = pkt->buf;
   packet ret;

   // Split command args
   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
      args[++i] = strsep(&tmp, " \t");
   }
   pthread_mutex_lock(&rooms_mutex);
   if (i > 1) {
      // check if room exists
      printf("Checking if room exists . . .\n");
      if (Rget_ID(&room_list, args[0]) == -1) {
         // create if it does not exist
         createRoom(&room_list, numRooms, args[0]);
      }
      RprintList(&room_list);
      printf("Receiving room node for requested room.\n");
      Room *newRoom = Rget_roomFNAME(&room_list, args[0]);

      int currRoomNum = atoi(args[1]);
      // Should check if current room exists
      printf("Receiving room node for users current room.\n");
      Room *currentRoom = Rget_roomFID(&room_list, currRoomNum);//pkt->options);
      printf("Getting user node from current room user list.\n");
      if(currentRoom == NULL) {
         printf("Could not remove user: current room is NULL\n");
      }
      else {
         User *currUser = get_user(&(currentRoom->user_list), pkt->username);
         printf("Removing user from his current rooms user list\n");
         removeUser(&(currentRoom->user_list), currUser);

         currUser = clone_user(currUser);
         printf("Inserting user into new rooms user list\n");
         insertUser(&(newRoom->user_list), currUser);

         RprintList(&room_list);

         ret.options = JOINSUC;
         strcpy(ret.realname, SERVER_NAME);
         sprintf(ret.buf, "%s %d", args[0], newRoom->ID);
         send(fd, (void *)&ret, sizeof(packet), MSG_NOSIGNAL);
         memset(&ret, 0, sizeof(ret));

         ret.options = newRoom->ID;
         strcpy(ret.realname, SERVER_NAME);
         strncpy(ret.buf, currUser->real_name, sizeof(currUser->real_name));
         strcat(ret.buf, " has joined the room.");
         ret.timestamp = time(NULL);
         send_message(&ret, -1);
      }
   }
   else {
      printf("Problem in join.\n");
   }
   pthread_mutex_unlock(&rooms_mutex);
}


/* Remove a user from their current room and move them to lobby */
void leave(packet *pkt, int fd) {
   int i = 0, roomNum;
   char *args[16];
   char *tmp = pkt->buf;
   packet ret;

   // Split command args
   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
      args[++i] = strsep(&tmp, " \t");
   }
   if (i > 1) {
      roomNum = atoi(args[1]);
      if (roomNum != DEFAULT_ROOM) {
         Room *currRoom = Rget_roomFID(&room_list, roomNum);
         if (currRoom != NULL) {
            User *currUser = get_user(&(currRoom->user_list), pkt->username);
            if (currUser != NULL) {
               removeUser(&(currRoom->user_list), currUser);
               currUser = clone_user(currUser);
               Room *defaultRoom = Rget_roomFID(&room_list, DEFAULT_ROOM);

               insertUser(&(defaultRoom->user_list), currUser);
               ret.options = JOINSUC;
               strcpy(ret.realname, SERVER_NAME);
               sprintf(ret.buf, "%s %d", defaultRoom->name, defaultRoom->ID);
               send(fd, (void *)&ret, sizeof(packet), MSG_NOSIGNAL);
               memset(&ret, 0, sizeof(ret));

               ret.options = defaultRoom->ID;
               strcpy(ret.realname, SERVER_NAME);
               strncpy(ret.buf, currUser->real_name, sizeof(currUser->real_name));
               strcat(ret.buf, " has joined the room.");
               ret.timestamp = time(NULL);
               send_message(&ret, -1);
            }
         }
      }
   }
   pthread_mutex_unlock(&rooms_mutex);
}


/*
 *Set user real name
 */
void set_name(packet *pkt, int fd) {
   char name[64];
   packet ret;

   strncpy(name, pkt->buf, sizeof(name));
   strncpy(ret.buf, pkt->buf, sizeof(ret.buf));

   pthread_mutex_lock(&registered_users_mutex);
   //Submit name change to user list, write list
   User *user = get_user(&registered_users_list, pkt->username);

   if(user != NULL) {
      memset(user->real_name, 0, sizeof(user->real_name));
      strncpy(user->real_name, name, sizeof(name));
      writeUserFile(&registered_users_list, "Users.bin");
      ret.options = NAMESUC;
   }
   else {
      printf("%s --- Error:%s Trying to modify null user in user_list.\n", RED, NORMAL);
      strcpy(ret.buf, "Name change failed, for some reason we couldn't find you.");
      ret.options = SERV_ERR;
   }
   pthread_mutex_unlock(&registered_users_mutex);

   // Submit name change to active users
   pthread_mutex_lock(&active_users_mutex);
   user = get_user(&active_users_list, pkt->username);
   if(user != NULL) {
      memset(user->real_name, 0, sizeof(user->real_name));
      strncpy(user->real_name, name, sizeof(name));
   }
   else {
      printf("%s --- Error:%s Trying to modify null user in active_users.\n", RED, NORMAL);
      strcpy(ret.buf, "Name change failed, for some reason we couldn't find you.");
      ret.options = SERV_ERR;
   }
   pthread_mutex_unlock(&active_users_mutex);

   //printList(&registered_users_list);
   //printList(&active_users_list);
   ret.timestamp = time(NULL);
   send(fd, &ret, sizeof(packet), MSG_NOSIGNAL);
}


/*
 *Set user password
 */
void set_pass(packet *pkt, int fd) {
   int i = 0;
   char *args[16];
   char cpy[BUFFERSIZE];
   char *tmp = cpy;
   strcpy(tmp, pkt->buf);

   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
       args[++i] = strsep(&tmp, " \t");
   }
   if (i > 3) {
      pthread_mutex_lock(&registered_users_mutex);
      User *user = get_user(&registered_users_list, pkt->username);
      pthread_mutex_unlock(&registered_users_mutex);
      if (user != NULL) {
         if(strcmp(user->password, args[1]) == 0) {
            memset(user->password, 0, 32);
            strcpy(user->password, args[2]);
            pthread_mutex_lock(&registered_users_mutex);
            writeUserFile(&registered_users_list, "Users.bin");
            pthread_mutex_unlock(&registered_users_mutex);
            pkt->options = PASSSUC;
         }
         else {
            pkt->options = SERV_ERR;
            strcpy(pkt->buf, "Password change failed, password mismatch.");
         }
      }
      else {
         pkt->options = SERV_ERR;
         strcpy(pkt->buf, "Password change failed, for some reason we couldn't find you.");
      }
   }
   else {
      pkt->options = SERV_ERR;
      strcpy(pkt->buf, "Password change failed, malformed request.");
   }
   send(fd, (void *)pkt, sizeof(packet), MSG_NOSIGNAL);
}


/*
 *Exit
 */
void exit_client(packet *pkt, int fd) {
   packet ret;

   //User *user = get_user_from_fd(fd)
   //pthread_mutex_lock(&active_users_mutex);
   //if(insertUser(&active_users_list, user) == 1) {
   //   pthread_mutex_unlock(&active_users_mutex);
   //   removeUser(active_users_list, user);
   //
   //   // Obtain current (or all) rooms with user, somehow
   //   // remove them  
   //
   //}

   // Send disconnect message to lobby
   ret.options = DEFAULT_ROOM;
   strcpy(ret.realname, SERVER_NAME);
   strcpy(ret.username, SERVER_NAME);
   sprintf(ret.buf, "%s has disconnected.", pkt->realname);
   ret.timestamp = time(NULL);
   send_message(&ret, -1);

   memset(&ret, 0, sizeof(packet));
   strcpy(ret.realname, SERVER_NAME);
   strcpy(ret.username, SERVER_NAME);
   ret.options = EXIT;
   strcat(ret.buf, "Goodbye!");
   ret.timestamp = time(NULL);
   printf("Sending close message to %d\n", fd);
   send(fd, &ret, sizeof(packet), MSG_NOSIGNAL);
   close(fd);
}


/*
 *Send Message
 */
void send_message(packet *pkt, int clientfd) {
   pthread_mutex_lock(&rooms_mutex);
   Room *currentRoom = Rget_roomFID(&room_list, pkt->options);
   printList(&(currentRoom->user_list));
   User *tmp = currentRoom->user_list;

   while(tmp != NULL) {
      if (clientfd != tmp->sock) {
         send(tmp->sock, (void *)pkt, sizeof(packet), MSG_NOSIGNAL);
      }
      tmp = tmp->next;
   }
   pthread_mutex_unlock(&rooms_mutex);
}


/* Send the server MOTD to the socket passed in */
void sendMOTD(int fd) {
   packet ret;
   strcpy(ret.realname, SERVER_NAME);
   ret.options = MOTD;
   strcpy(ret.buf, server_MOTD);
   ret.timestamp = time(NULL);
   send(fd, &ret, sizeof(packet), MSG_NOSIGNAL);
}


/*
 *Get active users
 */
void get_active_users(int fd) {
   pthread_mutex_lock(&active_users_mutex);
   User *temp = active_users_list;
   packet ret;
   ret.options = GETALLUSERS;
   strcpy(ret.username, SERVER_NAME);
   while(temp != NULL ) {
      ret.timestamp = time(NULL);
      strcpy(ret.buf, temp->username);
      send(fd, &ret, sizeof(packet), MSG_NOSIGNAL);
      memset(&ret.buf, 0, sizeof(ret.buf));
      temp = temp->next;
   }
   pthread_mutex_unlock(&active_users_mutex);
}


/*
 *Get real name of user requested
 */
void user_lookup(packet *in_pkt, int fd) {
   int i = 0;
   char *args[16];
   char *tmp = in_pkt->buf;

   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
      args[++i] = strsep(&tmp, " \t");
   }
   if (i > 1) {
      packet ret;
      ret.options = GETUSER;
      strcpy(ret.username, SERVER_NAME);
      char *realname = get_real_name(&active_users_list, args[1]);
      if (strcmp(realname, "ERROR") == 0) {
         ret.options = SERV_ERR;
         sprintf(ret.buf, "%s not found.", args[1]);
      }
      else {
         strcpy(ret.buf, realname);
      }
      ret.timestamp = time(NULL);
      send(fd, &ret, sizeof(packet), MSG_NOSIGNAL);
   }
   else {
      printf("%s --- Error:%s Malformed buffer received, ignoring.\n", RED, NORMAL);
   }
}


/*
 *Get users from specific room
 */
void get_room_users(packet *in_pkt, int fd) {
   int i = 0, roomNum;
   char *args[16];
   char *tmp = in_pkt->buf;

   // Split command args
   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
      args[++i] = strsep(&tmp, " \t");
   }
   if (i > 1) {
      roomNum = atoi(args[1]);
      Room *currRoom = Rget_roomFID(&room_list, roomNum);
      if (currRoom != NULL) {
         User *temp = currRoom->user_list;
         packet ret;
         ret.options = GETUSERS;
         strcpy(ret.username, SERVER_NAME);
         while(temp != NULL ) {
            ret.timestamp = time(NULL);
            strcpy(ret.buf, temp->username);
            send(fd, &ret, sizeof(packet), MSG_NOSIGNAL);
            memset(&ret.buf, 0, sizeof(ret.buf));
            temp = temp->next;
         }
      }
      else {
         printf("%s --- Error:%s Trying to read user info but room is null.\n", RED, NORMAL);
      }
   }
   else {
      printf("%s --- Error:%s Malformed buffer received, ignoring.\n", RED, NORMAL);
   }
}


/*
 *Get list of rooms
 */
void get_room_list(int fd) {
   Room *temp = room_list;
   packet pkt;
   pkt.options = GETROOMS;
   strcpy(pkt.username, SERVER_NAME);
   while(temp != NULL ) {
      pkt.timestamp = time(NULL);
      strcpy(pkt.buf, temp->name);
      send(fd, &pkt, sizeof(pkt), MSG_NOSIGNAL);
      temp = temp->next;
   }
   pthread_mutex_unlock(&active_users_mutex);
}
