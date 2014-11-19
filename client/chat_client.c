/* 
//   Program:             TBD Chat Client
//   File Name:           chat_client.c
//   Authors:             Matthew Owens, Michael Geitz, Shayne Wierbowski
//   Date Started:        10/23/2014
//   Compile:             gcc -Wall -l pthread client_commands.c chat_client.c -o chat_client
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
char *USERCOLORS[4] = {BLUE, CYAN, MAGENTA, GREEN};


int main(int argc, char **argv) {
   int bufSize, send_flag;
   packet tx_pkt;
   struct tm *timestamp;
   char *config_file_name = CONFIG_FILENAME;
   char full_config_path[64];
   packet *tx_pkt_ptr = &tx_pkt;
   
   // Handle CTRL+C
   signal(SIGINT, sigintHandler);

   strcpy(full_config_path, getenv("HOME"));
   strcat(full_config_path, config_file_name);
   config_file = full_config_path;
   pthread_mutex_lock(&configFileMutex);
   if (access(config_file, F_OK) == -1) {
      buildDefaultConfig();
   }
   pthread_mutex_unlock(&configFileMutex);

   printf("\33[2J\33[H");
   asciiSplash();

   pthread_mutex_lock(&configFileMutex);
   if (auto_connect()) {
      printf("%sAuto connecting to most recently connected host . . .%s\n", WHITE, NORMAL);
      reconnect(tx_pkt.buf);
   }
   pthread_mutex_unlock(&configFileMutex);
   
   while (1) {
      memset(&tx_pkt, 0, sizeof(packet));
      tx_pkt.options = INVALID;
      bufSize = userInput(tx_pkt_ptr);
      send_flag = 1;
      if(bufSize > 0 && tx_pkt.buf[bufSize] != EOF) {
         if(strncmp("/", (void *)tx_pkt.buf, 1) == 0) {
             send_flag = userCommand(tx_pkt_ptr);
         }
         if (send_flag && serverfd) {
            pthread_mutex_lock(&nameMutex);
            strcpy(tx_pkt.username, username);
            strcpy(tx_pkt.realname, realname);
            pthread_mutex_unlock(&nameMutex);
            tx_pkt.timestamp = time(NULL);
            pthread_mutex_lock(&roomMutex);
            if (currentRoom >= 1000 && tx_pkt.options == -1) {
               timestamp = localtime(&(tx_pkt.timestamp));
               printf("%s%d:%d:%d %s| [%s%s%s]%s %s\n", NORMAL,timestamp->tm_hour, timestamp->tm_min, timestamp->tm_sec, WHITE, RED, tx_pkt.realname,
                   WHITE, NORMAL, tx_pkt.buf);
               tx_pkt.options = currentRoom;
            }
            pthread_mutex_unlock(&roomMutex);
            if (tx_pkt.options > 0) {
               send(serverfd, (void *)&tx_pkt, sizeof(packet), MSG_NOSIGNAL);
            }
         }
         else if (send_flag && !serverfd)  {
            printf("%s --- %sError:%s Not connected to any server. See /help for command usage.\n", WHITE, RED, NORMAL);
         } 
      }
      if (tx_pkt.options == EXIT) {
         break;
      }
   }
   
   // Close connection
   printf("%sPreparing to exit . . .%s\n", WHITE, NORMAL);
   close(serverfd);
   if (chat_rx_thread) {
      if(pthread_join(chat_rx_thread, NULL)) {
         printf("%s --- %sError:%s chatRX thread not joining.\n", WHITE, RED, NORMAL);
      }
   }
   pthread_mutex_destroy(&nameMutex);
   pthread_mutex_destroy(&debugModeMutex);
   pthread_mutex_destroy(&configFileMutex);
   pthread_mutex_destroy(&roomMutex);
   printf("%sExiting client.%s\n", WHITE, NORMAL);
   exit(0);
}


/* Build the default config file */
void buildDefaultConfig() {
      FILE *configfp;
      configfp = fopen(config_file, "a+");
      fputs("###\n#\n#\tTBD Chat Configuration\n#\n###\n\n\n", configfp);
      fputs("## Stores auto reconnect state\nauto-reconnect: 0\n\n", configfp);
      fputs("## Stores last client connection\nlast connection:\n", configfp);
      fclose(configfp);
}


/* Check if auto-reconnect is set enabled */
int auto_connect() {
   FILE *configfp;
   char line[128];
   
   configfp = fopen(config_file, "r");
   if (configfp != NULL) {
      while (!feof(configfp)) {
         if (fgets(line, sizeof(line), configfp)) {
            if (strncmp(line, "auto-reconnect:", strlen("auto-reconnect:")) == 0) {
               fclose(configfp);
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
   return 0;
}


/* Read keyboard input into buffer */
int userInput(packet *tx_pkt) {
   int i = 0;
   
   // Read up to 126 input chars into packet buffer until newline or EOF (CTRL+D)
   tx_pkt->buf[i] = getc(stdin);
   while(tx_pkt->buf[i] != '\n' && tx_pkt->buf[i] != EOF) {
      tx_pkt->buf[++i] = getc(stdin);
      // Automatically send message once it reaches 127 characters
      if (i >= 126) {
         tx_pkt->buf[++i] = '\n';
         break;
      }
   }
   printf("\33[1A\33[J");
   // If EOF is read, exit?
   if(tx_pkt->buf[i] == EOF) {
      exit(1);
   }
   else { 
      tx_pkt->buf[i] = '\0';
   }
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
         pthread_mutex_lock(&debugModeMutex);
         if (debugMode) {
            debugPacket(rx_pkt_ptr);
         }
         pthread_mutex_unlock(&debugModeMutex);
         if (rx_pkt.options >= 1000) {
            timestamp = localtime(&(rx_pkt.timestamp));
            if(strcmp(rx_pkt.realname, SERVER_NAME) == 0) {
               printf("%s%d:%d:%d %s| [%s%s%s]%s %s\n", NORMAL,timestamp->tm_hour, timestamp->tm_min, \
                      timestamp->tm_sec, WHITE, YELLOW, rx_pkt.realname,
                   WHITE, NORMAL, rx_pkt.buf);
            }
            else {
               int i = hash(rx_pkt.username);
               printf("%s%d:%d:%d %s| [%s%s%s]%s %s\n", NORMAL,timestamp->tm_hour, timestamp->tm_min, \
                      timestamp->tm_sec, WHITE, USERCOLORS[i], rx_pkt.realname,
                   WHITE, NORMAL, rx_pkt.buf);
            }
         }
         else if (rx_pkt.options > 0 && rx_pkt.options < 1000) {
            serverResponse(rx_pkt_ptr);
         }
         else {
            printf("%sCommunication with server has terminated.%s\n", WHITE, NORMAL);
            break;
         }
      }
      memset(&rx_pkt, 0, sizeof(packet));
   }
   return NULL;
}


/* Handle non message packets from server */
void serverResponse(packet *rx_pkt) {
   if (rx_pkt->options == SERV_ERR) {
      printf("%s --- %sError:%s %s\n", WHITE, RED, NORMAL, rx_pkt->buf);
   }
   else if (rx_pkt->options == REGSUC) {
      pthread_mutex_lock(&roomMutex);
      // Hardcoded lobby room
      currentRoom = DEFAULT_ROOM;
      pthread_mutex_unlock(&roomMutex);
      printf("%s --- %sSuccess:%s Registration successful!\n", WHITE, GREEN, NORMAL);
   }
   else if (rx_pkt->options == LOGSUC) {
      pthread_mutex_lock(&nameMutex);
      strcpy(username, rx_pkt->username);
      strcpy(realname, rx_pkt->realname);
      pthread_mutex_unlock(&nameMutex);
      pthread_mutex_lock(&roomMutex);
      // Hardcoded lobby room
      currentRoom = DEFAULT_ROOM;
      pthread_mutex_unlock(&roomMutex);
      printf("%s --- %sSuccess:%s Login successful!\n", WHITE, GREEN, NORMAL);
   }
   else if (rx_pkt->options == GETUSERS || \
            rx_pkt->options == GETALLUSERS || \
            rx_pkt->options == GETUSER) {
      printf("%s --- %sUser:%s %s\n", WHITE, WHITE, NORMAL, rx_pkt->buf);
   }
   else if (rx_pkt->options == PASSSUC) {
      printf("%s --- %sSuccess:%s Password change successful!\n", WHITE, GREEN, NORMAL);
   }
   else if (rx_pkt->options == NAMESUC) {
      pthread_mutex_lock(&nameMutex);
      memset(&realname, 0, sizeof(realname));
      strncpy(realname, rx_pkt->buf, sizeof(realname));
      pthread_mutex_unlock(&nameMutex);
      printf("%s --- %sSuccess:%s Name change successful!\n", WHITE, GREEN, NORMAL);
   }
   else if (rx_pkt->options == JOINSUC) {
      newRoom((void *)rx_pkt->buf);
   }
   else if (rx_pkt->options == INVITE) {
      printf("%s --- %sInvite: %s%s\n", WHITE, MAGENTA, NORMAL, rx_pkt->buf);
   }
   else if (rx_pkt->options == INVITESUC) {
      printf("%s --- %sSuccess:%s Invite sent!\n", WHITE, GREEN, NORMAL);
   }
   else if (rx_pkt->options == GETROOMS) {
      printf("%s --- %sRoom:%s %s\n", WHITE, YELLOW, NORMAL, rx_pkt->buf);
   }
   else if (rx_pkt->options == MOTD) {
      printf("%s ------------------------------------------------------------------- %s\n", BLACK, NORMAL);
      printf("%s%s%s\n", CYAN, rx_pkt->buf, NORMAL);
      printf("%s ------------------------------------------------------------------- %s\n", BLACK, NORMAL);
   }
   else if(rx_pkt->options == EXIT) {
      printf("%sServer has closed its connection with you.%s\n", WHITE, NORMAL);
      printf("%sClosing socket connection with server.%s\n", WHITE, NORMAL);
      close(serverfd);
   }
   else {
      printf("%s --- %sError:%s Unknown message received from server.\n", WHITE, RED, NORMAL);
   }
}


/* Change the clients current room (for sending) */
void newRoom(char *buf) {
   int i = 0, roomNumber;
   char *args[16];
   char cpy[BUFFERSIZE];
   char *tmp = cpy;
   strcpy(tmp, buf);

   args[i] = strsep(&tmp, " \t");
   while ((i < sizeof(args) - 1) && (args[i] != '\0')) {
       args[++i] = strsep(&tmp, " \t");
   }
   if (i >= 1) {
      roomNumber = atoi(args[1]);
      pthread_mutex_lock(&roomMutex);
      if (roomNumber != currentRoom) {
         currentRoom = roomNumber;
         printf("%s --- %sSuccess:%s Joined room %s%s%s.\n", \
                WHITE, GREEN, NORMAL, WHITE, args[0], NORMAL);
      }
      pthread_mutex_unlock(&roomMutex);
   }
   else {
      printf("%s --- %sError:%s Problem reading JOINSUC from server.\n", WHITE, RED, NORMAL);

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
      printf("getaddrinfo: %s\n", gai_strerror(status));
      return -1;
   }
   
   print_ip(servinfo);
   for (p = servinfo; p != NULL; p = p ->ai_next) {
      if((serverfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
         printf("%s --- %sError:%s socket socket \n", WHITE, RED, NORMAL);
         continue;
      }
      
      if(connect(serverfd, p->ai_addr, p->ai_addrlen) == -1) {
         close(serverfd);
         printf("%s --- %sError:%s socket connect \n", WHITE, RED, NORMAL);
         return -1;
      }
      break;
   }
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
         ipver = "IPV4";
      }
      else {
         ipv6= (struct sockaddr_in6 *)p->ai_addr;
         addr = &(ipv6->sin6_addr);
         port = ipv4->sin_port;
         ipver = "IPV6";
      }
      // Write readable form of IP to ipstr
      inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
      // Print connection information
      printf("%sConnecting to %s: %s:%d . . .%s\n", WHITE, ipver, ipstr, ntohs(port), NORMAL);
   }
}


/* Handle SIGINT (CTRL+C) */
void sigintHandler(int sig_num) {
   printf("\b\b%s --- %sError:%s Forced Exit.\n", WHITE, RED, NORMAL);
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
            printf("%s --- %sError:%s chatRX thread not joining.\n", WHITE, RED, NORMAL);
         }
      }
   }
   pthread_mutex_destroy(&nameMutex);
   pthread_mutex_destroy(&debugModeMutex);
   pthread_mutex_destroy(&configFileMutex);
   pthread_mutex_destroy(&roomMutex);
   exit(0);
}


/* Print message on startup */
void asciiSplash() {
   printf("%s         __%s\n", GREEN, NORMAL);
   printf("%s        /_/%s\\        %s_____ %s____  %s____    %s ____ _           _   %s\n", GREEN, GREEN, CYAN, CYAN, CYAN, WHITE, NORMAL);
   printf("%s       / /%s\\ \\      %s|_   _%s| __ )%s|  _ \\ %s  / ___| |__   __ _| |_ %s\n", RED, GREEN, CYAN, CYAN, CYAN, WHITE, NORMAL);
   printf("%s      / / /%s\\ \\       %s| | %s|  _ \\%s| | | |%s | |   | '_ \\ / _` | __|%s\n", RED, GREEN, CYAN, CYAN, CYAN, WHITE, NORMAL);
   printf("%s     / / /%s\\ \\ \\      %s| | %s| |_) %s| |_| |%s | |___| | | | (_| | |_ %s\n", RED, GREEN, CYAN, CYAN, CYAN, WHITE, NORMAL);
   printf("%s    / /%s_%s/%s__%s\\ \\ \\     %s|_| %s|____/%s|____/ %s  \\____|_| |_|\\__,_|\\__|%s\n", RED, BLUE, RED, BLUE, GREEN, CYAN, CYAN, CYAN, WHITE, NORMAL);
   printf("%s   /_/%s______%s\\%s_%s\\%s/%s\\%s\n", RED, BLUE, GREEN, BLUE, GREEN, BLUE, BLUE, NORMAL);
   printf("%s   \\_\\%s_________\\/%s\n\n", RED, BLUE, NORMAL);
   printf("%sEnter /help to view a list of available commands.%s\n\n", WHITE, NORMAL);
}


/* Return number between 0-4 determined from string passed in */
int hash(char *str) {
   unsigned long hash = 5381;
   int c;
   while ((c = *str++)) {
      hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
   }
   return hash % 4;
}