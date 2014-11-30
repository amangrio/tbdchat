/* 
//   Program:             TBD Chat Client
//   File Name:           chat_client.c
//   Authors:             Matthew Owens, Michael Geitz, Shayne Wierbowski
//   Date Started:        10/23/2014
//   Compile:             gcc -Wformat -Wall -lpthread -lncurses visual.c client_commands.c chat_client.c -o chat_client
//   Run:                 ./chat_client
//
//   The client for a simple chat utility
*/
#include "chat_client.h"

int serverfd = 0;
pthread_mutex_t roomMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t nameMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t debugModeMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t configFileMutex = PTHREAD_MUTEX_INITIALIZER;
volatile int currentRoom;
volatile int debugMode;
char realname[64];
char username[64];
pthread_t chat_rx_thread;
char *config_file;
WINDOW *mainWin, *inputWin, *chatWin, *chatWinBox, *inputWinBox, *infoLine, *infoLineBottom;


int main(int argc, char **argv) {
   int bufSize, send_flag;
   packet tx_pkt;
   char *config_file_name = CONFIG_FILENAME;
   char full_config_path[64];
   packet *tx_pkt_ptr = &tx_pkt;

   // Sig Handlers
   signal(SIGINT, sigintHandler);
   signal(SIGWINCH, resizeHandler);

   // Start everything curses
   initializeCurses();

   // Get home dir, check config
   strcpy(full_config_path, getenv("HOME"));
   strcat(full_config_path, config_file_name);
   config_file = full_config_path;
   // If config does not exist, create the default config file
   if (access(config_file, F_OK) == -1) {
      buildDefaultConfig();
   }

   // Check autoconnect, run if set
   if (auto_connect()) {
      wprintFormatNotice(chatWin, time(NULL), "Auto connecting to most recently connected host . . .");
      reconnect(tx_pkt.buf);
   }
  
   // Primary execution loop 
   while (1) {
      wrefresh(chatWin);
      wcursyncup(inputWin);
      wrefresh(inputWin);
      // Wipe packet space
      memset(&tx_pkt, 0, sizeof(packet));
      // Set packet options as untouched
      tx_pkt.options = INVALID;
      // Read user kb input, return number of chars
      bufSize = userInput(tx_pkt_ptr);
      // Set default send flag to True
      send_flag = 1;
      // If the input buffer is not empty
      if(bufSize > 0 && tx_pkt.buf[bufSize] != EOF) {
         // Check if the input should be read as a command, if so process the command
         if(strncmp("/", (void *)tx_pkt.buf, 1) == 0) {
             send_flag = userCommand(tx_pkt_ptr);
         }
         // If connected and handling a packet flagged to be sent
         if (send_flag && serverfd) {
            // Copy current username and realname to packet
            pthread_mutex_lock(&nameMutex);
            strcpy(tx_pkt.username, username);
            strcpy(tx_pkt.realname, realname);
            pthread_mutex_unlock(&nameMutex);
            // Timestamp packet
            tx_pkt.timestamp = time(NULL);
            // If sending a message, print the message client side
            pthread_mutex_lock(&roomMutex);
            if (currentRoom >= 1000 && tx_pkt.options == -1) {
               //timestamp = localtime(&(tx_pkt.timestamp));
               wprintFormat(chatWin, tx_pkt.timestamp, tx_pkt.realname, tx_pkt.buf, 2);
               tx_pkt.options = currentRoom;
            }
            pthread_mutex_unlock(&roomMutex);
            // If packet options has been altered appropriately, send it
            if (tx_pkt.options > 0) {
               send(serverfd, (void *)&tx_pkt, sizeof(packet), MSG_NOSIGNAL);
            }
         }
         // If send flag is true but serverfd is still 0, print error
         else if (send_flag && !serverfd) {
            wprintFormatError(chatWin, time(NULL), "Not connected to any server");
         } 
      }
      // If an exit packet was just transmitted, break from primary execution loop
      if (tx_pkt.options == EXIT) {
         break;
      }
   }
   
   // Safely close connection
   wprintFormatNotice(chatWin, time(NULL), " Preparing to exit . . .");
   wrefresh(chatWin);
   close(serverfd);
   // Join chatRX if it was launched
   if (chat_rx_thread) {
      if(pthread_join(chat_rx_thread, NULL)) {
         wprintFormatError(chatWin, time(NULL), "chatRX thread not joining");
      }
   }
   // Destroy mutexes
   pthread_mutex_destroy(&nameMutex);
   pthread_mutex_destroy(&debugModeMutex);
   pthread_mutex_destroy(&configFileMutex);
   pthread_mutex_destroy(&roomMutex);
   // Close curses
   wprintFormatNotice(chatWin, time(NULL), " Exiting client");
   wrefresh(chatWin);
   endwin();
   printf("\33[2J\33[H");
   printf("Thanks for using TBD Chat.\n");
   exit(0);
}


/* Build the default config file */
void buildDefaultConfig() {
      pthread_mutex_lock(&configFileMutex);
      FILE *configfp;
      configfp = fopen(config_file, "a+");
      fputs("###\n#\n#\tTBD Chat Configuration\n#\n###\n\n\n", configfp);
      fputs("## Stores auto reconnect state\nauto-reconnect: 0\n\n", configfp);
      fputs("## Stores last client connection\nlast connection:\n", configfp);
      fclose(configfp);
      pthread_mutex_unlock(&configFileMutex);
}


/* Check if auto-reconnect is set enabled */
int auto_connect() {
   FILE *configfp;
   char line[128];
   
   pthread_mutex_lock(&configFileMutex);
   configfp = fopen(config_file, "r");
   if (configfp != NULL) {
      while (!feof(configfp)) {
         if (fgets(line, sizeof(line), configfp)) {
            if (strncmp(line, "auto-reconnect:", strlen("auto-reconnect:")) == 0) {
               fclose(configfp);
               pthread_mutex_unlock(&configFileMutex);
               strncpy(line, line + strlen("auto-reconnect: "), 1);
               if (strncmp(line, "0", 1) == 0) {
                  return 0;
               }
               else {
                  return 1;
               }
            }
         }
      }
   }
   fclose(configfp);
   pthread_mutex_unlock(&configFileMutex);
   return 0;
}


/* Read keyboard input into buffer */
int userInput(packet *tx_pkt) {
   int i = 0;
   int ch;
   wmove(inputWin, 0, 0);
   wrefresh(inputWin);
   // Read 1 char at a time
   while ((ch = getch()) != '\n') {
      // Backspace
      if (ch == 8 || ch == 127 || ch == KEY_LEFT) {
         if (i > 0) {
            wprintw(inputWin, "\b \b\0");
            tx_pkt->buf[--i] = '\0';
            wrefresh(inputWin);
         }
         else {
            wprintw(inputWin, "\b \0");
         }
      }
      else if (ch == KEY_RESIZE) {
        continue;
      }
      // Otherwise put in buffer
      else if (ch != ERR) {
         if (i < BUFFERSIZE - 1) {
            strcat(tx_pkt->buf, (char *)&ch);
            i++;
            wprintw(inputWin, (char *)&ch);
            wrefresh(inputWin);
         }
         // Unless buffer is full
         else {
            wprintw(inputWin, "\b%s", (char *)&ch);
            tx_pkt->buf[(i - 1)] = '\0';
            strcat(tx_pkt->buf, (char *)&ch);
            wrefresh(inputWin);
         }
      }
   }
   // Null terminate, clear input window
   tx_pkt->buf[i] = '\0';
   wclear(inputWin);
   wrefresh(inputWin);
   return i;
}


/* Print messages as they are received */
void *chatRX(void *ptr) {
   packet rx_pkt;
   packet *rx_pkt_ptr = &rx_pkt;
   int received;
   int *serverfd = (int *)ptr;
   struct tm *timestamp;
   while(1) {
      // Wait for message to arrive..
      received = recv(*serverfd, (void *)&rx_pkt, sizeof(packet), 0);
      
      if(received) {
         // If debug mode is enabled, dump packet contents
         pthread_mutex_lock(&debugModeMutex);
         if (debugMode) {
            debugPacket(rx_pkt_ptr);
         }
         pthread_mutex_unlock(&debugModeMutex);
         // If the received packet is a message packet, print accordingly
         if (rx_pkt.options >= 1000) {
            timestamp = localtime(&(rx_pkt.timestamp));
            if(strcmp(rx_pkt.realname, SERVER_NAME) == 0) {
               wprintFormat(chatWin, rx_pkt.timestamp, rx_pkt.realname, rx_pkt.buf, 3);
            }
            else {
               int i = hash(rx_pkt.username, 3);
               wprintFormat(chatWin, rx_pkt.timestamp, rx_pkt.realname, rx_pkt.buf, i + 4);  
               beep();
            }
         }
         // If the received packet is a nonmessage option, handle option response
         else if (rx_pkt.options > 0 && rx_pkt.options < 1000) {
            serverResponse(rx_pkt_ptr);
         }
         // If the received packet contains 0 as the option, we likely received and empty packet, end transmission
         else {
            wprintFormatNotice(chatWin, time(NULL), "Communication with server has terminated.");
            break;
         }
         // Wipe packet space
         wrefresh(chatWin);
         wcursyncup(inputWin);
         wrefresh(inputWin);
         memset(&rx_pkt, 0, sizeof(packet));
      }
   }
   return NULL;
}


/* Handle non message packets from server */
void serverResponse(packet *rx_pkt) {
   if (rx_pkt->options == SERV_ERR) {
      wprintFormat(chatWin, time(NULL), rx_pkt->realname, rx_pkt->buf, 3);
   }
   //else if (rx_pkt->options == REGSUC) {
   //   pthread_mutex_lock(&roomMutex);
   //   currentRoom = DEFAULT_ROOM;
   //   pthread_mutex_unlock(&roomMutex);
   //   wprintFormat(chatWin, time(NULL), "Client", " --- Success: Registration successful!\n");
   //}
   else if (rx_pkt->options == LOGSUC) {
      pthread_mutex_lock(&nameMutex);
      strcpy(username, rx_pkt->username);
      strcpy(realname, rx_pkt->realname);
      pthread_mutex_unlock(&nameMutex);
      pthread_mutex_lock(&roomMutex);
      currentRoom = DEFAULT_ROOM;
      werase(infoLine);
      wprintw(infoLine, " Current room: Lobby"); 
      wrefresh(infoLine);
      pthread_mutex_unlock(&roomMutex);
      wprintFormat(chatWin, rx_pkt->timestamp, rx_pkt->realname, "Login Successful!", 2);
   }
   else if (rx_pkt->options == GETUSERS || \
            rx_pkt->options == GETALLUSERS || \
            rx_pkt->options == GETUSER) {
      wprintFormat(chatWin, rx_pkt->timestamp, "USER", rx_pkt->buf, 6);
   }
   else if (rx_pkt->options == PASSSUC) {
      wprintFormat(chatWin, rx_pkt->timestamp, rx_pkt->realname, "Password change successful", 3);
   }
   else if (rx_pkt->options == NAMESUC) {
      pthread_mutex_lock(&nameMutex);
      memset(&realname, 0, sizeof(realname));
      strncpy(realname, rx_pkt->buf, sizeof(realname));
      pthread_mutex_unlock(&nameMutex);
      wprintFormat(chatWin, rx_pkt->timestamp, rx_pkt->realname, "Name change successful", 3);
   }
   else if (rx_pkt->options == JOINSUC) {
      newRoom(rx_pkt);
   }
   else if (rx_pkt->options == INVITE) {
      wprintFormat(chatWin, rx_pkt->timestamp, rx_pkt->realname, rx_pkt->buf, 3);
   }
   else if (rx_pkt->options == INVITESUC) {
      wprintFormat(chatWin, rx_pkt->timestamp, rx_pkt->realname, "Invite sent", 3);
   }
   else if (rx_pkt->options == GETROOMS) {
      wprintFormat(chatWin, rx_pkt->timestamp, "ROOM", rx_pkt->buf, 6);
   }
   else if (rx_pkt->options == MOTD) {
      wprintSeperatorTitle(chatWin, "MOTD", 1, 7);

      wprintFormatTime(chatWin, rx_pkt->timestamp);
      wattron(chatWin, COLOR_PAIR(2));
      wprintw(chatWin, "%s\n", rx_pkt->buf);
      wattroff(chatWin, COLOR_PAIR(2));

      wprintSeperator(chatWin, 1);
   }
   else if(rx_pkt->options == EXIT) {
      wprintFormat(chatWin, rx_pkt->timestamp, rx_pkt->realname, "Server has closed its connection with you", 3);
      wprintFormatNotice(chatWin, rx_pkt->timestamp, "Closing socket connection with server");
      close(serverfd);
   }
   else {
      wprintFormatError(chatWin, time(NULL), "Unknown message received from server");
   }
}


/* Change the clients current room (for sending) */
void newRoom(packet *rx_pkt) {
   int i = 0, roomNumber;
   char *args[16];
   char cpy[BUFFERSIZE];
   char *tmp = cpy;
   strcpy(tmp, rx_pkt->buf);

   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
       args[++i] = strsep(&tmp, " \t");
   }
   if (i >= 1) {
      roomNumber = atoi(args[1]);
      pthread_mutex_lock(&roomMutex);
      if (roomNumber != currentRoom) {
         currentRoom = roomNumber;
         wprintFormatTime(chatWin, rx_pkt->timestamp);
         wprintw(chatWin, "Joined Room ");
         wattron(chatWin, COLOR_PAIR(3));
         wprintw(chatWin, "%s\n", args[0]);
         wattroff(chatWin, COLOR_PAIR(3));
         werase(infoLine);
         wbkgd(infoLine, COLOR_PAIR(3));
         wprintw(infoLine, " Current room: %s", args[0]); 
         wrefresh(infoLine);
      }
      pthread_mutex_unlock(&roomMutex);
   }
   else {
         wprintFormatError(chatWin, time(NULL), "Problem reading JOINSUC.");
   }
}


/* Establish server connection */
int get_server_connection(char *hostname, char *port) {
   int serverfd;
   struct addrinfo hints, *servinfo, *p;
   int status;
   
   memset(&hints, 0, sizeof hints);
   hints.ai_family = PF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   
   if((status = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
      wprintFormatTime(chatWin, time(NULL));
      wprintw(chatWin, "getaddrinfo: %s\n", gai_strerror(status));
      return -1;
   }
   
   for (p = servinfo; p != NULL; p = p ->ai_next) {
      if((serverfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
         wprintFormatError(chatWin, time(NULL), "socket socket");
         continue;
      }
      
      if(connect(serverfd, p->ai_addr, p->ai_addrlen) == -1) {
         close(serverfd);
         wprintFormatError(chatWin, time(NULL), "socket connect");
         return -1;
      }
      break;
   }
   print_ip(servinfo);
   freeaddrinfo(servinfo);
   return serverfd;
}


/* Print new connection information */
void print_ip( struct addrinfo *ai) {
   struct addrinfo *p;
   void *addr;
   char *ipver;
   char ipstr[INET6_ADDRSTRLEN];
   struct sockaddr_in *ipv4;
   struct sockaddr_in6 *ipv6;
   short port = 0;
   
   for (p = ai; p !=  NULL; p = p->ai_next) {
      if(p->ai_family == AF_INET) {
         ipv4 = (struct sockaddr_in *)p->ai_addr;
         addr = &(ipv4->sin_addr);
         port = ipv4->sin_port;
         ipver = "IPv4";
      }
      else {
         ipv6= (struct sockaddr_in6 *)p->ai_addr;
         addr = &(ipv6->sin6_addr);
         port = ipv4->sin_port;
         ipver = "IPv6";
      }
      // Write readable form of IP to ipstr
      inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
      // Print connection information
      wbkgd(infoLineBottom, COLOR_PAIR(3));
      wprintw(infoLineBottom, " Connected to [%s] %s:%d", ipver, ipstr, ntohs(port));
      wrefresh(infoLineBottom);
   }
}


/* Handle SIGINT (CTRL+C) */
void sigintHandler(int sig_num) {
   wprintFormatError(chatWin, time(NULL), "Forced Exit.");
   // If the client is connected, safely close the connection
   if (serverfd) { 
      packet tx_pkt;
      pthread_mutex_lock(&nameMutex);
      strcpy(tx_pkt.username, username);
      strcpy(tx_pkt.realname, realname);
      strcpy(tx_pkt.buf, "/exit");
      pthread_mutex_unlock(&nameMutex);
      tx_pkt.timestamp = time(NULL);
      tx_pkt.options = EXIT;
      send(serverfd, (void *)&tx_pkt, sizeof(packet), 0);
      close(serverfd); 
      if (chat_rx_thread) {
         if(pthread_join(chat_rx_thread, NULL)) {
            wprintFormatError(chatWin, time(NULL), "chatRX thread not joining.");
         }
      }
   }
   wrefresh(chatWin);
   pthread_mutex_destroy(&nameMutex);
   pthread_mutex_destroy(&debugModeMutex);
   pthread_mutex_destroy(&configFileMutex);
   pthread_mutex_destroy(&roomMutex);
   endwin();
   printf("\33[2J\33[H");
   printf("Thanks for using TBD Chat.\n");
   exit(0);
}


/* Return number between 0-(mod-1) determined from string passed in */
int hash(char *str, int mod) {
   unsigned long hash = 5381;
   int c;
   while ((c = *str++)) {
      //Binary shift left by 5, add c
      hash = ((hash << 5) + hash) + c; 
   } 
   // Return modded value
   return hash % mod;
}
