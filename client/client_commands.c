/* 
//   Program:             TBD Chat Client
//   File Name:           client_commands.c
//   Authors:             Matthew Owens, Michael Geitz, Shayne Wierbowski
*/

#include "chat_client.h"

/* Declared.  Defined and allocated in chat_client.c */
extern int serverfd;
extern volatile int currentRoom;
extern volatile int debugMode;
extern char username[64];
extern char realname[64];
extern char *config_file;
extern WINDOW *mainWin, *chatWin, *inputWin;
extern pthread_t chat_rx_thread;
extern pthread_mutex_t roomMutex;
extern pthread_mutex_t nameMutex;
extern pthread_mutex_t debugModeMutex;
extern pthread_mutex_t configFileMutex;

/* Process user commands and mutate buffer accordingly */
int userCommand(packet *tx_pkt) {

   // Handle exit command
   if (strncmp((void *)tx_pkt->buf, "/exit", strlen("/exit")) == 0) {
       tx_pkt->options = EXIT;
       return 1;;
   }
   // Handle quit command
   else if (strncmp((void *)tx_pkt->buf, "/quit", strlen("/quit")) == 0) {
       tx_pkt->options = EXIT;
       return 1;;
   }
   // Handle help command
   else if (strncmp((void *)tx_pkt->buf, "/help", strlen("/help")) == 0) {
       showHelp();
       return 0;
   }
   // Handle debug command
   else if (strncmp((void *)tx_pkt->buf, "/debug", strlen("/debug")) == 0) {
       pthread_mutex_lock(&debugModeMutex);
       if (debugMode) {
         debugMode = 0;
         wprintw(chatWin, "| --- Client: Debug disabled.\n");
       }
       else {
          debugMode = 1;
         wprintw(chatWin, "| --- Client: Debug enabled.\n");
       }
       pthread_mutex_unlock(&debugModeMutex);
       box(chatWin, 0, 0);
       wrefresh(chatWin);
       return 0;
   }
   // Handle connect command
   else if (strncmp((void *)tx_pkt->buf, "/connect", strlen("/connect")) == 0) {
      if (!newServerConnection((void *)tx_pkt->buf)) {
          wprintw(chatWin, "| --- Error: Server connect failed.\n");
          box(chatWin, 0, 0);
          wrefresh(chatWin);
      }
      return 0;
   }
   // Handle reconnect command
   else if (strncmp((void *)tx_pkt->buf, "/reconnect", strlen("/reconnect")) == 0) {
      if (!reconnect((void *)tx_pkt->buf)) {
          wprintw(chatWin, "| --- Error: Server connect failed.\n");
      }
      box(chatWin, 0, 0);
      wrefresh(chatWin);
      return 0;
   }
   // Handle autoconnect command
   else if (strncmp((void *)tx_pkt->buf, "/autoconnect", strlen("/autoconnect")) == 0) {
       if (toggleAutoConnect()) {
          wprintw(chatWin, "| --- Client: Autoconnect enabled.\n");
       }
       else {
          wprintw(chatWin, "| --- Client: Autoconnect disabled.\n");
       }
       box(chatWin, 0, 0);
       wrefresh(chatWin);
       return 0;
   }
   // Handle register command
   else if (strncmp((void *)tx_pkt->buf, "/register", strlen("/register")) == 0) {
      return (serverRegistration(tx_pkt));
   }
   // Handle login command
   else if (strncmp((void *)tx_pkt->buf, "/login", strlen("/login")) == 0) {
      return (serverLogin(tx_pkt));
   }
   // Handle setname command
   else if (strncmp((void *)tx_pkt->buf, "/setname", strlen("/setname")) == 0) {
      return setName(tx_pkt);
   }
   // Handle setpass command
   else if (strncmp((void *)tx_pkt->buf, "/setpass", strlen("/setpass")) == 0) {
      if (!setPassword(tx_pkt)) {
         wprintw(chatWin, "| --- Error: Password mismatch.\n");
         box(chatWin, 0, 0);
         wrefresh(chatWin);
         return 0;
      }
      else {
         return 1;
      }
   }
   // Handle motd command
   else if (strncmp((void *)tx_pkt->buf, "/motd", strlen("/motd")) == 0) {
       tx_pkt->options = GETMOTD;
       return 1;;
   }
   // Handle invite command
   else if (strncmp((void *)tx_pkt->buf, "/invite", strlen("/invite")) == 0) {
      return validInvite(tx_pkt);
   }
   // Handle join command
   else if (strncmp((void *)tx_pkt->buf, "/join", strlen("/join")) == 0) {
       return validJoin(tx_pkt);
   }
   // Handle leave command
   else if (strncmp((void *)tx_pkt->buf, "/leave", strlen("/leave")) == 0) {
       tx_pkt->options = LEAVE;
       pthread_mutex_lock(&roomMutex);
       memset(&tx_pkt->buf, 0, sizeof(tx_pkt->buf));
       wprintw(chatWin, tx_pkt->buf, "/leave %d", currentRoom);
       pthread_mutex_unlock(&roomMutex);
       return 1;
   }
   // Handle who command
   else if (strncmp((void *)tx_pkt->buf, "/who", strlen("/who")) == 0) {
       if (strcmp((void *)tx_pkt->buf, "/who all") == 0) {
          tx_pkt->options = GETALLUSERS;
          return 1;
       }
       else if (strlen(tx_pkt->buf) > strlen("/who ")) {
          tx_pkt->options = GETUSER;
          return 1;
       }
       tx_pkt->options = GETUSERS;
       pthread_mutex_lock(&roomMutex);
       sprintf( tx_pkt->buf, "%s %d", tx_pkt->buf, currentRoom);
       pthread_mutex_unlock(&roomMutex);
       return 1;
   }
   // Handle rooms command
   else if (strncmp((void *)tx_pkt->buf, "/list", strlen("/list")) == 0) {
       tx_pkt->options = GETROOMS;
       return 1;
   }
   // If it wasn't any of that, invalid command
   else {
      wprintw(chatWin, "| --- Error: Invalid command.\n");
      box(chatWin, 0, 0);
      wrefresh(chatWin);
      return 0;
   }
}


/* Uses first word after /invite as username arg, appends currentroom */
int validInvite(packet *tx_pkt) {
   int i;
   char *args[16];
   char cpy[BUFFERSIZE];
   char *tmp = cpy;
   strcpy(tmp, tx_pkt->buf);

   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
       args[++i] = strsep(&tmp, " \t");
   }

   if (i > 1) {
      tx_pkt->options = INVITE;
      memset(&tx_pkt->buf, 0, sizeof(tx_pkt->buf));
      pthread_mutex_lock(&roomMutex);
      sprintf(tx_pkt->buf, "%s %d", args[1], currentRoom);
      pthread_mutex_unlock(&roomMutex);
      return 1;
   }
   else {
      wprintw(chatWin, "| --- Error: Usage: /invite username\n");
      box(chatWin, 0, 0);
      wrefresh(chatWin);
      return 0;
   }
}


/* Uses first word after /join as roomname arg, appends currentroom */
int validJoin(packet *tx_pkt) {
   int i;
   char *args[16];
   char cpy[BUFFERSIZE];
   char *tmp = cpy;
   strcpy(tmp, tx_pkt->buf);

   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
       args[++i] = strsep(&tmp, " \t");
   }

   if (i > 1) {
      tx_pkt->options = JOIN;
      memset(&tx_pkt->buf, 0, sizeof(tx_pkt->buf));
      pthread_mutex_lock(&roomMutex);
      sprintf( tx_pkt->buf, "%s %d", args[1], currentRoom);
      pthread_mutex_unlock(&roomMutex);
      return 1;
   }
   else {
      wprintw(chatWin, "| --- Error: Usage: /join roomname\n");
      box(chatWin, 0, 0);
      wrefresh(chatWin);
      return 0;
   }
}


/* Connect to a new server */
int newServerConnection(char *buf) {
   int i = 0;
   char *args[16];
   char cpy[BUFFERSIZE];
   char *tmp = cpy;
   strcpy(tmp, buf);
   FILE *configfp;
   char line[128];

   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
       args[++i] = strsep(&tmp, " \t");
   }
   if (i > 2) {
      if((serverfd = get_server_connection(args[1], args[2])) == -1) {
         wprintw(chatWin, "| --- Error: Could not connect to server.\n");
         box(chatWin, 0, 0);
         wrefresh(chatWin);
         return 0;
      }
      if(pthread_create(&chat_rx_thread, NULL, chatRX, (void *)&serverfd)) {
         wprintw(chatWin, "| --- Error:  chatRX thread not created.\n");
         box(chatWin, 0, 0);
         wrefresh(chatWin);
         return 0;
      }
      wprintw(chatWin, "| Connected.\n");
      i = 0;
      pthread_mutex_lock(&configFileMutex);
      configfp = fopen(config_file, "r+");
      if (configfp != NULL) {
         while (!feof(configfp)) {
            if (fgets(line, sizeof(line), configfp)) {
               if (strncmp(line, "last connection:", strlen("last connection:")) == 0) {
                  fseek(configfp, -strlen(line), SEEK_CUR);
                  for (i = 0; i < strlen(line); i++) {
                     fputs(" ", configfp);
                  }
                  fseek(configfp, -strlen(line), SEEK_CUR);
                  fputs("last connection: ", configfp);
                  fputs(buf + strlen("/connect "), configfp);
               }
            }
         }
      }
      fclose(configfp);
      pthread_mutex_unlock(&configFileMutex);
      box(chatWin, 0, 0);
      wrefresh(chatWin);
      return 1;
   }
   else {
       wprintw(chatWin, "| --- Error: Usage: /connect address port\n");
       box(chatWin, 0, 0);
       wrefresh(chatWin);
       return 0;
   }
}


/* Reconnect using the last connection settings */
int reconnect(char *buf) {
   FILE *configfp;
   char line[128];

   pthread_mutex_lock(&configFileMutex);
   configfp = fopen(config_file, "r");
   if (configfp != NULL) {
      while (!feof(configfp)) {
         if (fgets(line, sizeof(line), configfp)) {
            if (strncmp(line, "last connection:", strlen("last connection:")) == 0) {
               if (strlen(line) > (strlen("last connection: "))) {
                  strcpy(buf, "/connect ");
                  strcat(buf, line + strlen("last connection: "));
                  fclose(configfp);
                  pthread_mutex_unlock(&configFileMutex);
                  return newServerConnection(buf);
               }
               else {
                  fclose(configfp);
                  pthread_mutex_unlock(&configFileMutex);
                  wprintw(chatWin, "| --- Error: No previous connection to reconnect to.\n");
                  box(chatWin, 0, 0);
                  wrefresh(chatWin);
                  return 0;
               }
            }
         }
      }
   }
   fclose(configfp);
   pthread_mutex_unlock(&configFileMutex);
   return 0;
}


/* Toggle autoconnect state in config file */
int toggleAutoConnect() {
   FILE *configfp;
   char line[128];
   int ret;
 
   pthread_mutex_lock(&configFileMutex);
   configfp = fopen(config_file, "r+");
   if (configfp != NULL) {
      while (!feof(configfp)) {
         if (fgets(line, sizeof(line), configfp)) {
            if (strncmp(line, "auto-reconnect:", strlen("auto-reconnect:")) == 0) {
               fseek(configfp, -2, SEEK_CUR);
               if (strncmp(line + strlen("auto-reconnect: "), "0", 1) == 0) {
                  fputs("1", configfp);
                  ret = 1;
               }
               else {
                  fputs("0", configfp);
                  ret = 0;
               }
            }
         }
      }
   }
   fclose(configfp);
   pthread_mutex_unlock(&configFileMutex);
   return ret;
}


/* Connect to a new server */
int serverLogin(packet *tx_pkt) {
   char *args[16];
   char cpy[64];
   int i;
   strcpy(cpy, tx_pkt->buf);
   char *tmp = cpy;
   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
      args[++i] = strsep(&tmp, " \t");
   }
   if (i == 3) {
      tx_pkt->options = LOGIN;
      strcpy(tx_pkt->username, args[1]);
      strcpy(tx_pkt->realname, args[1]);
      return 1;
   }
   else {
      wprintw(chatWin, "| --- Error: Usage: /login username password\n");
      box(chatWin, 0, 0);
      wrefresh(chatWin);
      return 0;
   }
}


/* Handle registration for server  */
int serverRegistration(packet *tx_pkt) {
   int i = 0;
   char *args[16];
   char cpy[128];
   char *tmp = cpy;
   strcpy(tmp, tx_pkt->buf);
   
   // Split command args
   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
      args[++i] = strsep(&tmp, " \t");
   }
   if (i == 4) {
      // if the passwords patch mark options
      if (strcmp(args[2], args[3]) == 0) {
         tx_pkt->options = REGISTER;
         strcpy(tx_pkt->username, args[1]);
         strcpy(tx_pkt->realname, args[1]);
         return 1;
      }
      else {
         wprintw(chatWin, "| --- Error: Password mismatch\n");
         box(chatWin, 0, 0);
         wrefresh(chatWin);
         return 0;
      }
   }
   else {
      wprintw(chatWin, "| --- Error: Usage: /register username password password\n");
      box(chatWin, 0, 0);
      wrefresh(chatWin);
      return 0;
   }
}


/* Set user password */
int setPassword(packet *tx_pkt) {
   int i = 0;
   char *args[16];
   char cpy[128];
   char *tmp = cpy;
   strcpy(tmp, tx_pkt->buf);
   
   // Split command args
   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
      args[++i] = strsep(&tmp, " \t");
   }
   if (i == 4) {
      if (strcmp(args[2], args[3])  == 0) {
         tx_pkt->options = SETPASS;
         return 1;
      }
      else {
      wprintw(chatWin, "| --- Error: New password mismatch\n");
      box(chatWin, 0, 0);
      wrefresh(chatWin);
      return 0;
      }
   }
   else {
      wprintw(chatWin, "| --- Error: Usage: /setpass oldpassword newpassword newpassword\n");
      box(chatWin, 0, 0);
      wrefresh(chatWin);
      return 0;
   }
}


/* Set user real name */
int setName(packet *tx_pkt) {
   if(strlen(tx_pkt->buf) > strlen("/setname ")) {
      memmove(tx_pkt->buf, tx_pkt->buf + strlen("/setname "), sizeof(tx_pkt->buf));
      tx_pkt->options = SETNAME;
      return 1;
   }
   else {
      wprintw(chatWin, "| --- Error: Usage: /setname newname\n");
      box(chatWin, 0, 0);
      wrefresh(chatWin);
      return 0;
   }
}


/* Dump contents of received packet from server */
void debugPacket(packet *rx_pkt) {
   wprintw(chatWin, "| --------------------- PACKET REPORT --------------------- \n");
   wprintw(chatWin, "| Timestamp: %lu\n", rx_pkt->timestamp);
   wprintw(chatWin, "| User Name: %s\n", rx_pkt->username);
   wprintw(chatWin, "| Real Name: %s\n", rx_pkt->realname);
   wprintw(chatWin, "| Option: %d\n", rx_pkt->options);
   wprintw(chatWin, "| Buffer: %s\n", rx_pkt->buf);
   wprintw(chatWin, "| --------------------------------------------------------- \n");
   box(chatWin, 0, 0);
   wrefresh(chatWin);
}


/* Print helpful and unhelpful things */
void showHelp() {
   wprintw(chatWin, "|--------------------------------[ Command List ]--------------------------------------\n");
   wprintw(chatWin, "|\t/connect\t | Usage: /connect address port\n");
   wprintw(chatWin, "|\t/reconnect\t | Connect to last known host\n");
   wprintw(chatWin, "|\t/autoconnect\t | Toggle automatic connection to last known host on startup\n");
   wprintw(chatWin, "|\t/help\t\t | Display a list of commands\n");
   wprintw(chatWin, "|\t/debug\t\t | Toggle debug mode\n");
   wprintw(chatWin, "|\t/exit\t\t | Exit the client\n");
   wprintw(chatWin, "|\t/register\t | Usage: /register username password password\n");
   wprintw(chatWin, "|\t/login\t\t | Usage: /login username password\n");
   wprintw(chatWin, "|\t/setpass\t | Usage: /setpass oldpassword newpassword newpassword\n");
   wprintw(chatWin, "|\t/setname\t | Usage: /setname fname lname\n");
   wprintw(chatWin, "|\t/who\t\t | Return a list of users in your current room or a specific user real name\n");
   wprintw(chatWin, "|\t/who all\t | Return a list of all connected users\n");
   wprintw(chatWin, "|\t/list\t\t | Return a list of all public rooms with active users in them\n");
   wprintw(chatWin, "|\t/invite\t\t | Usage: /invite username\n");
   wprintw(chatWin, "|\t/join\t\t | Usage: /join roomname\n");
   wprintw(chatWin, "|\t/leave\t\t | Leave the room you are in and return to the lobby\n");
   wprintw(chatWin, "|------------------------------------------------------------------------------------------\n", BLACK, NORMAL);
   box(chatWin, 0, 0);
   wrefresh(chatWin);
}
