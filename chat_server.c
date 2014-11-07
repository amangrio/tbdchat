/* Program:             Simple Chat Server
   Authors:             Matthew Owens, Michale Geitz, Shayne Wierbowski
   Date:                10/23/2014
   File Name:           chat_server.c
   Compile:             gcc -o chat_server chat_server.c -l pthread
   Run:                 ./chat_server IP_ADDRESS PORT

   The server program for a simple two way chat utility

*/

#include "chat_server.h"

int chat_serv_sock_fd; //server socket

int main(int argc, char **argv)
{
   
   if(argc < 3)
   {
      printf("%s --- Error:%s Usage: %s IP_ADDRESS PORT.\n", RED, NORMAL, argv[0]);
      exit(0);
   }
   
   // Handle CTRL+C
   signal(SIGINT, sigintHandler);
   
   //Client Sockets
   int clientA_sock_fd, clientB_sock_fd;
   
   // User IDs
   char clientA_usrID[32];
   char clientB_usrID[32];
   char clientA_name[32];
   char clientB_name[32];
   
   User *user_list = NULL;
   readUserFile(&user_list, "Users.txt");
   printList(&user_list);  
   // Open server socket
   chat_serv_sock_fd = get_server_socket(argv[1], argv[2]);
   
   // step 3: get ready to accept connections
   if(start_server(chat_serv_sock_fd, BACKLOG) == -1)
   {
      printf("start server error\n");
      exit(1);
   }	
   
   //Main execution loop   
   while(1)
   {
      //Accept client connections
      clientA_sock_fd = accept_client(chat_serv_sock_fd);
      if(clientA_sock_fd != -1)
      {
         establish_identity(clientA_sock_fd, (void *)&clientA_usrID, (void *)&clientA_name, &user_list);
         printf("%s connected as Client A at socket_fd %d\n", clientA_name, clientA_sock_fd);
      }
      
      clientB_sock_fd = accept_client(chat_serv_sock_fd);
      if(clientB_sock_fd != -1)
      {
         establish_identity(clientB_sock_fd, (void *)&clientB_usrID, (void *)&clientB_name, &user_list);
         printf("%s connected as Client B at socket_fd %d\n", clientB_name, clientB_sock_fd);
      }
      
      start_subserver(clientA_sock_fd, clientB_sock_fd, (void *)&clientA_name, (void *)&clientB_name); 
   
   }
   
   close(chat_serv_sock_fd);
}

//Copied from Dr. Bi's example
int get_server_socket(char *hostname, char *port)
{
   struct addrinfo hints, *servinfo, *p;
   int status;
   int server_socket;
   int yes = 1;
   
   memset(&hints, 0, sizeof hints);
   hints.ai_family = PF_UNSPEC;      // either ipv4 or ipv6
   hints.ai_socktype = SOCK_STREAM;  // TCP
   hints.ai_flags = AI_PASSIVE;      // Flag for returning bindable socket addr for either ipv4/6
   
   if((status = getaddrinfo(hostname, port, &hints, &servinfo)) != 0)
   {
      printf("getaddrinfo: %s\n", gai_strerror(status));
      exit(1);
   }
   
   for(p = servinfo; p != NULL; p = p ->ai_next)
   {
      // step 1: create a socket
      if((server_socket = socket(p->ai_family, p->ai_socktype,
                                 p->ai_protocol)) == -1)
      {
         printf("socket socket \n");
         continue;
      }
      // if the port is not released yet, reuse it.
      if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
      {
         printf("socket option\n");
         continue;
      }
      
      // step 2: bind socket to an IP addr and port
      if(bind(server_socket, p->ai_addr, p->ai_addrlen) == -1)
      {
         printf("socket bind \n");
         continue;
      }
      break;
   }
   //print_ip(servinfo);
   freeaddrinfo(servinfo);   // servinfo structure is no longer needed. free it.
   
   return server_socket;
}

//Copied from Dr. Bi's example
int start_server(int serv_socket, int backlog)
{
   int status = 0;
   if((status = listen(serv_socket, backlog)) == -1)
   {
      printf("socket listen error\n");
   }
   return status;
}

//Copied from Dr. Bi's example
int accept_client(int serv_sock)
{
   int reply_sock_fd = -1;
   socklen_t sin_size = sizeof(struct sockaddr_storage);
   struct sockaddr_storage client_addr;
   //char client_printable_addr[INET6_ADDRSTRLEN];
   
   // accept a connection request from a client
   // the returned file descriptor from accept will be used
   // to communicate with this client.
   if((reply_sock_fd = accept(serv_sock,
      (struct sockaddr *)&client_addr, &sin_size)) == -1)
   {
      printf("socket accept error\n");
   }
   return reply_sock_fd;
}


/*
 *Main thread for client execution.  Tracks both incoming 
 *and outgoing messages.
 */
void *client_thread(void *ptr)
{
   session *current_session = (session *)ptr;
   packet client_message;
   char *timestamp;
   int received;
   int client = current_session->this_client;
   
   //Send username to other client
   strcpy(client_message.buf, current_session->aliases[client]);
   send(current_session->clients[1-client], (void *)&client_message, 
        sizeof(packet), 0); 
   
   while(current_session->running)
   {
      received = recv(current_session->clients[client], (void *)&client_message,
                             sizeof(packet), 0);
     
      //Check if other user has ended conversation
      if(received <= 0)
      {
         current_session->running = 0;
         break;
      }
      
      client_message.options = 0;
      //Format timestamp and remove \n
      timestamp = asctime(localtime(&(client_message.timestamp)));
      timestamp[strlen(timestamp) -1] = '\0';
      
      //Print messages as they are recieved, each client in a different color
      if(current_session->running)
      {
         if(client == 0)
         {
            printf("%s%s [%s]:%s%s\n", RED, timestamp, current_session->aliases[client],
                   NORMAL, client_message.buf);
         }
         else if(client == 1)
         {
            printf("%s%s [%s]:%s%s\n", BLUE, timestamp, current_session->aliases[client],
                   NORMAL, client_message.buf);
         }
      }
      
      //Handle "EXIT" message
      if(strcmp(client_message.buf, "EXIT") == 0)
      {
         client_message.options = -1;
         send(current_session->clients[1-client], (void *)&client_message, 
              sizeof(packet), 0);
         printf("Session between %d and %d ended.\n", current_session->clients[0],
            current_session->clients[1]);
         
         current_session->running = 0;
         break;
      }
      
      //Send message to both clients, changing options to display
      //correct username client-side
      else
      {
         client_message.options = 0;
         send(current_session->clients[client], (void *)&client_message,
              sizeof(packet), 0);
         client_message.options = 1;
         send(current_session->clients[1 - client], (void *)&client_message,
              sizeof(packet), 0);
      }
   }

   end(current_session);
   return NULL;
}


/*
 *Used to safely end a chat session.  Closes the sockets
 *for each client and sets a flag telling the threads 
 *for that session to exit.
 */
void end(session *ptr)
{
   close(ptr->clients[0]);
   close(ptr->clients[1]);
   free(ptr);
}

/*
 *Sets up necessary information to start a subserver
 *for a chat session.
 */
void start_subserver(int A_fd, int B_fd, char* clientA_usrID, char* clientB_usrID) 
{
   //Set up struct to pass to subserver
   session *newSession = (session *)malloc(sizeof(session));
   newSession->clients[0] = A_fd;
   newSession->clients[1] = B_fd;
   strcpy(newSession->aliases[0], clientA_usrID);
   strcpy(newSession->aliases[1], clientB_usrID);
   newSession->running = 1;
   
   //Start subserver thread
   pthread_t new_session_thread;
   int iret;
   
   iret = pthread_create(&new_session_thread, NULL, subserver, (void *)newSession);
}

/*
 *Subserver thread for a conversation.  Starts the main
 *threads for each client.
 */
void *subserver(void *ptr)
{
   // Threads
   pthread_t client_A_thread, client_B_thread;
   
   session *A_ptr = (session *)ptr;
   session *B_ptr = (session *)malloc(sizeof(session));
   
   A_ptr->this_client = 0;
   B_ptr->this_client = 1;
   B_ptr->clients[0] = A_ptr->clients[0];
   B_ptr->clients[1] = A_ptr->clients[1];
   strcpy(B_ptr->aliases[0], A_ptr->aliases[0]);
   strcpy(B_ptr->aliases[1], A_ptr->aliases[1]);
   B_ptr->running = A_ptr->running;
   
   int iret1, iret2;
   
   iret1 = pthread_create(&client_A_thread, NULL, client_thread, (void *)A_ptr);
   iret2 = pthread_create(&client_B_thread, NULL, client_thread, (void *)B_ptr);
   
   pthread_join(client_A_thread, NULL);
   pthread_join(client_B_thread, NULL);
   
   return NULL;
}

/* Handle SIGINT (CTRL+C) */
void sigintHandler(int sig_num)
{
   printf("\b\b%s --- Error:%s Forced Exit.\n", RED, NORMAL);
   close(chat_serv_sock_fd);
   exit(0);
}

/*
 *Used in the login/registration process for each new connection. 
 *Allows registration of a new user and login of an existing user.
 *Registration: prompts user to enter their desired username, real name,
 *and password.
 *Login: prompts user to enter their username and password
 */
void establish_identity(int fd, char *ID, char *name, User **user_list) {
   packet pck;
   int loop = 1;
   while(loop)
   {
      // Receive command and username
      recv(fd, &pck, sizeof(pck), 0);
      
      // Register
      if(pck.options == 0)
      {
         User *temp_user = (User *)malloc(sizeof(User));
         temp_user->next = NULL;
         strcpy(temp_user->username, pck.buf);
         
         if(strcmp(get_real_name(user_list, temp_user->username), "ERROR") != 0)
         {
            printf("%sERROR - %sUsername %s is already taken\n", RED, NORMAL,
                   temp_user->username);
            strcpy(pck.buf, "ERROR");
            send(fd, &pck, sizeof(pck), 0);
         }
         else
         {
            // Send valid username message
            send(fd, &pck, sizeof(pck), 0);
            
            // Receive and save password
            recv(fd, &pck, sizeof(pck), 0);
            strcpy(temp_user->password, pck.buf);
            
            // Receive and save name
            recv(fd, &pck, sizeof(pck), 0);
            strcpy(temp_user->real_name, pck.buf);
            
            strcpy(ID, temp_user->username);
            insert(user_list, temp_user);
            strcpy(name, pck.buf);
            printf("Client %d successfully registered with username %s and real name %s\n",
                   fd, ID, name);
            loop = 0;
         }
      }
      // Login
      else if(pck.options == 1)
      {
         strcpy(ID, pck.buf);
         
         strcpy(name, get_real_name(user_list, ID));
         if(strcmp(name, "ERROR") != 0) loop = 0;
         strcpy(pck.buf, name);
         send(fd, &pck, sizeof(pck), 0);

         //Ensure correct password
         recv(fd, &pck, sizeof(pck), 0);
  
         while(strcmp(pck.buf, get_password(user_list, ID)) != 0)
         {
            strcpy(pck.buf, "INVALID");
            send(fd, &pck, sizeof(pck), 0);
            recv(fd, &pck, sizeof(pck), 0);
         }
         strcpy(pck.buf, "VALID");
         send(fd, &pck, sizeof(pck), 0);
         
         strcpy(name, get_real_name(user_list, ID));
         if(strcmp(name, "ERROR") != 0) loop = 0;
         strcpy(pck.buf, name);
         send(fd, &pck, sizeof(pck), 0);
         printf("Client %d successfully logged in as %s\n",fd , ID);
      }
      else
      {
         printf("%sERROR - %sInvalid input received", RED, NORMAL);         
      }
   }
   writeUserFile(user_list, "Users.txt");
}
