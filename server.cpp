//
// Simple chat server for TSAM
//
// Command line: ./server <port>
//
// Author(s):
//    Jacky Mallett (jacky@ru.is)
//    Stephan Schiffel (stephans@ru.is)

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <vector>
#include <list>
#include <poll.h>

#include <iostream>
#include <sstream>
#include <thread>
#include <map>

#include <fstream>
#include <string>
#include <ctime>
#include <iomanip>

#include "identity.h"
#include "log_event.h"

#include <arpa/inet.h>

#include <unistd.h>

#include <cerrno>

// fix SOCK_NONBLOCK for OSX
#ifndef SOCK_NONBLOCK
#include <fcntl.h>
#define SOCK_NONBLOCK O_NONBLOCK
#endif

#define BACKLOG 5 // Allowed length of queue of waiting connections

// Simple class for handling connections from clients.
//
// Client(int socket) - socket to send/receive traffic from client.
class Client
{
public:
    int sock;                                 // socket of client connection
    std::string name;                         // Limit length of name of client's user
    std::string ip;                           // Client's IP address
    int port = 0;                              // Client's port number
    std::vector<unsigned char> receiveBuffer; // Create a receive buffer so that if TCP doesn´t send the whole frame at once.
                                              // We can store the data in the buffer and wait for the rest of it to come

    Client(int socket) : sock(socket) {}

    ~Client() {} // Virtual destructor defined for base class
};

std::map<int, Client *> clients; // Lookup table for per Client information

// Struct for handling messages
struct Message
{
    std::string from;
    std::string message;
};
// Maps the users to a vector containing all of their messages.
std::map<std::string, std::vector<Message>> messages;

struct Server
{
  std::string ip;
  int port;
};
// Maps each server's group id to its IP address and port
std::map<std::string, Server> knownServers;

// Open socket for specified port.
//
// Returns -1 if unable to create the socket for any reason.

// Function declaration
void logMessage(const std::string &message);
int connectToServer(const char* ip, int port);
bool sendMessage(int clientSocket, const std::string& response);
void closeClient(int clientSocket, std::vector<struct pollfd> *fds);

int open_socket(int portno)
{
    struct sockaddr_in sk_addr; // address settings for bind()
    int sock;                   // socket opened for this port
    int set = 1;                // for setsockopt

    // Create socket for connection. Set to be non-blocking, so recv will
    // return immediately if there isn't anything waiting to be read.

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Failed to open socket");
        return (-1);
    }

    // Turn on SO_REUSEADDR to allow socket to be quickly reused after
    // program exit.

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
    {
        perror("Failed to set SO_REUSEADDR:");
    }
    set = 1;
#ifdef __APPLE__
    if (setsockopt(sock, SOL_SOCKET, SOCK_NONBLOCK, &set, sizeof(set)) < 0)
    {
        perror("Failed to set SOCK_NOBBLOCK");
    }
#endif
    memset(&sk_addr, 0, sizeof(sk_addr));

    sk_addr.sin_family = AF_INET;
    sk_addr.sin_addr.s_addr = INADDR_ANY;
    sk_addr.sin_port = htons(portno);

    // Bind to socket to listen for connections from clients

    if (bind(sock, (struct sockaddr *)&sk_addr, sizeof(sk_addr)) < 0)
    {
        perror("Failed to bind to socket:");
        return (-1);
    }
    else
    {
        return (sock); // -1 means wait indefinitely until something happens
    }
}
// Connect to peer servers
// It connects to the server
// Saves the server information
// Send HELO to the server after connection
bool connectToPeer(const std::string& ip, int port,
                   std::vector<struct pollfd>& fds)
{
  // Connect to the server to join the botnet
  int peerSock = connectToServer(ip.c_str(), port);

  if (peerSock < 0)
  {
    return false;
  }

  std::string message;
  // Add peer spcket to the list of fds being polled
  fds.push_back({peerSock, POLLIN, 0});
  printf("Connected to server\n");

  clients[peerSock] = new Client(peerSock); // Creates a Client object for the outgoing connection

  clients[peerSock] ->ip = ip; // Stores the ip address
  clients[peerSock]->port = port; // Stores the port number

  log_event("CONN", ip.c_str(), port, NULL,
      "outgoing connection established");

  // Send helo HELO,<FROM_GROUP_ID> after connectiong to the server
  message += "HELO,";
  message += GROUP_ID;

  if (!sendMessage(peerSock, message))
  {
    closeClient(peerSock, &fds);
    return false;
  }

  return true;
}

// connectToServer creates an outgoing connection to another server.
// Retunrns the connected socket or -1 if it failed.
int connectToServer(const char* ip, int port)
{
  struct sockaddr_in serverAddr{};
  int sock;
  // 1. Create a TCP socket
  
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    perror("Failed to open socket");
    return (-1);
  }

  // 2. Prepare the destination address
  serverAddr.sin_family = AF_INET;  // IPv4 address 
  serverAddr.sin_port = htons(port); // Store the port number and covnert port number from host binary order to network binary order
  
  // Check if it is a valid IP address
  if (inet_pton(AF_INET, ip, &serverAddr.sin_addr) != 1) // Converts the IP addres from text to binary
  {
    printf("Invalid IP address\n");
    close(sock); // Close the socket since it failed and we won't need it 
    return -1;
  }
  // 3. Connect to the destination server
  if (connect(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("Conncetion Failed");
        close(sock); // Close the socket since it failed and we won't need it 
        return -1;
    }

  // 4. Return the connected socket
  return sock;
}

// Connect to the servers that we know about
void connectToKnownServers(std::vector<struct pollfd>& fds)
{
  int connectionCount = 0;

  for (const auto& [sock, client] : clients){
    if (!client->name.empty())
    {
      connectionCount ++;
    }
  }

  for(const auto& [groupId, server] : knownServers)
  {
    bool connected = false;

    // Skip our server
    if (groupId == GROUP_ID){
      continue;
    }

    // Check if we are connected to the server
    // Servers that are connected are stored in the Client
    for(const auto& [sock, client] : clients)
    {
      if (client->name == groupId ||(client->ip == server.ip && client->port == server.port))
      {
        connected = true;
        break;
      }
    }
    // Skip server if it is connected
    if(connected)
    {
      continue;
    }

    // If we have reached 5 connected servers we stop connecting to more
    if (connectionCount >= 5){
      break;
    }

    // Extract the ip from the server and port 
    // Use that to connect to the server
    if (connectToPeer(server.ip, server.port, fds))
    {
      connectionCount ++;
    }
  }
}

// Close a client's connection, and remove it from the list of file
// descriptors being watched by poll().
//
// Note: this only removes the socket from the poll() watch list. It does
// NOT remove the corresponding entry from the "clients" map - callers are
// responsible for that (see the main loop below).

void closeClient(int clientSocket, std::vector<struct pollfd> *fds)
{
    printf("Client closed connection: %d\n", clientSocket);

    // Find the client information from the socket it is 
    auto it = clients.find(clientSocket);

    // Make sure that the client is found
    // If we get clients.end() than the client was not found
    if (it != clients.end()){
      Client* c = it->second;

      // Log the disconnetion
      log_event("CLOSE", c->ip.c_str(), c->port, NULL, "Client disconnected");

      // Delete the clients information
      delete c;
      clients.erase(it);
    }

    
    

    close(clientSocket);

    // Find and remove this socket's entry from the list of fds polled.
    auto fdIt = std::find_if(fds->begin(), fds->end(),
                             [clientSocket](const struct pollfd &pfd)
                             {
                                 return pfd.fd == clientSocket;
                             });

    if (fdIt != fds->end())
    {
        fds->erase(fdIt);
    }
}

// Send a message to a client. send() may not be able to accept the whole
// message at once, so keep going until everything has been written.
//
// Returns false if the client has gone away.

bool sendMessage(int clientSocket, const std::string &response)
{

    uint16_t frameLength = response.size() + 5;
    uint16_t networkLength = htons(frameLength);

    std::vector<char> frame;
    // Stucture of the message: <SOH><length><STX><command><ETX>
    // First part of the message is SOH = 0x1
    frame.push_back(0x1);
    // Next part of the message is the length
    // Since the newtworkLength is 2 bytes we have to resize the frame to 3 indexes and place the network length in positions 1 and 2.
    // Pos 0 is already taken by SOH
    frame.resize(3);
    memcpy(&frame[1], &networkLength, sizeof(networkLength));
    // Next part of the message is the STX = 0x2
    frame.push_back(0x2);
    // Next part of the message is the response itself
    frame.insert(frame.end(), response.begin(), response.end());
    // The last part is the ETX = 0x3
    frame.push_back(0x3);

    // send might only send part of the fram so we have to make sure that it sends the whole frame
    // We do that by using a while loop that send the frame until the total amount of bytes sent equal the frame size.
    size_t remaining = frame.size(); // how many bytes there are left to send
    const char *p = frame.data();    // Where to start sending

    while (remaining > 0) // Keep going until we have sent all of the bytes
    {
        int bytesSent = send(clientSocket, p, remaining, 0); // bytes that are sent
        if (bytesSent <= 0)
        {
            return false;
        }
        p += bytesSent;         // move forward in the vecto
        remaining -= bytesSent; // reduce remaining bytes
    }
    auto it = clients.find(clientSocket);

    if (it != clients.end())
    {
        Client* c = it->second;

        log_event_bytes("SEND", c->ip.c_str(), c->port, NULL,
                        response.data(), response.size());
    }
    return true;
}

void logMessage(const std::string &message)
{
    std::ofstream logFile("server.log", std::ios::app); // Open the server.log file and append to it

    if (!logFile.is_open())
    {
        std::cerr << "Failed to open server.log\n";
        return;
    }

    // Get current time
    std::time_t now = std::time(nullptr);      // Get the current time
    std::tm *localTime = std::localtime(&now); // Covert the current time into local date and time

    // Write to log file the timestamp and the messge
    logFile << std::put_time(localTime, "%Y-%m-%d %H:%M:%S") << " " << message << "\n";
}

// Create the servers message
std::string serversMessage()
{
  // Set my server details at the front of the message 
  std::string ipAddress = "130.208.246.98";
  int port = 4045;
  std::string message = "SERVERS,";
  message += std::string(GROUP_ID) + "," + ipAddress + "," + std::to_string(port) + ";";

  for (const auto& [groupId, server] : knownServers)
  {
    message += groupId + ",";
    message += server.ip + ",";
    message += std::to_string(server.port) + ";";
    std:: cout << message;
  }
  return message;
}

// Process command from client on the server
void clientCommand(int clientSocket, const std::string &command, std::vector<struct pollfd>& fds)
{
  // Search the client map for the socket that sent the command
   auto it = clients.find(clientSocket);

   // If the socket is not found return
    if (it == clients.end())
    {
        return;
    }

    // Get the client object connected to the socket 
    Client* c = it->second;
    // If the command is STATUSREQ the user gets sent back a response with a list of every users that has at leas one message
    if (command == "STATUSREQ")
    {
        std::string response;
        // Log the command 
        log_event_bytes("RECV", c->ip.c_str(), c->port, NULL,
                    command.data(), command.size());
        // If there are no messages we resonse to the client saying ir is empty
        if (messages.empty())
        {
            response += "EMPTY";
        }
        // Else we go through each user and see how many messages they have
        else
        {
            int counter = 0;
            int total_size = messages.size();
            // Loop through the users who have stored messages
            for (auto const &user : messages)
            {

                response += user.first; // The username
                response += ":";
                response += std::to_string(user.second.size()); // Get the size of the vector containg the user's messages.

                counter++;

                // When we have finished the last user we dont want to add a comma at the end.
                if (counter != total_size)
                {
                    response += ",";
                }
            }
        }
        sendMessage(clientSocket, response);
    }
    // We have to process the message that was sent.
    // 1. Extract the sender's username
    // 2. Extract the recipent's username
    // 3. Extract the message
    // 4. Store the message in the recipent's message queue
    else if (command.starts_with("SENDMSG,"))
    {
        Message newMessage;
        // Find the from
        size_t comma = command.find(",", 8);

        if (comma == std::string::npos)
    {
        // Log the invalid SENDMSG format
        log_event_bytes("DROP", c->ip.c_str(), c->port, NULL,
                        command.data(), command.size());

        sendMessage(clientSocket, "Invalid SENDMSG format");
        return;
    }
        
        std::string from = command.substr(8, comma - 8); // Find the from person. It starts at index 8 and stops at the next comma.

        // Find the second comma
        size_t secondComma = command.find(",", comma + 1);

        if (secondComma == std::string::npos)
        {
            log_event_bytes("DROP", c->ip.c_str(), c->port, NULL,
                            command.data(), command.size());

            sendMessage(clientSocket, "Invalid SENDMSG format");
            return;
        }
        std::string to = command.substr(comma + 1, secondComma - (comma + 1));

        // Find the message
        std::string message = command.substr(secondComma + 1);

        // Check if none of the fields are empty
        if (from.empty() || to.empty() || message.empty())
        {
            log_event_bytes("DROP", c->ip.c_str(), c->port, NULL,
                        command.data(), command.size());
            sendMessage(clientSocket, "Invalid SENDMSG format");
            return;
        }

        newMessage.from = from;
        newMessage.message = message;

        messages[to].push_back(newMessage);
        log_event_bytes("RECV", c->ip.c_str(), c->port, NULL,
                    command.data(), command.size());

        std::string response = "Message received";
        sendMessage(clientSocket, response);
    }

    // Extract the first message in the recipent's queue
    // 1. Extract the user
    // 2. Make sure that the user is in the data
    // 3. Construct the message that was found
    // 4. Send it to the client
    // 5. Remove the message from the queue
    // 6. If the user has no more messages we delete the user
    else if (command.starts_with("GETMSG,"))
    {
        // Check if username is empty
        std::string user = command.substr(7);

        // Check if the username is empty
        if (user.empty())
        {
            log_event_bytes("DROP", c->ip.c_str(), c->port, NULL,
                            command.data(), command.size());

            sendMessage(clientSocket, "Invalid GETMSG format");
            return;
        }

        // Log valid command
        log_event_bytes("RECV", c->ip.c_str(), c->port, NULL,
                        command.data(), command.size());

        std::string response;

        

        auto foundUser = messages.find(user);

        // If the user is not found it send back that the user was not found
        if (foundUser == messages.end())
        {
            response += "User not found";
            sendMessage(clientSocket, response);
        }


        // Else find the first message for the user
        else
        {
            // Point to the vector of messages and get the first message from the user.
            Message firstMessage = foundUser->second[0];

            // Create the response
            response += "To: ";
            response += user;
            response += ", From: ";
            response += firstMessage.from;
            response += ", ";
            response += firstMessage.message;
            if (sendMessage(clientSocket, response))
            {
                // Delete the message from the user storage
                foundUser->second.erase(foundUser->second.begin()); // Remove the first message from the vector
                log_note("Removed message for %s", user.c_str());
            };

            // If the user has no more messages left we have to delete the user
            if (foundUser->second.empty())
            {
                messages.erase(foundUser); // Remove the user from the map
            }
        }
    }
    else if(command.starts_with("HELO,"))
    {
      // Extract the group ID from the HELO command
      std::string groupId = command.substr(5);

      // Check if the group ID is empty
      if (groupId.empty())
      {
        // Log the invalid command as DROP
        log_event_bytes("DROP", c->ip.c_str(), c->port, NULL,
                            command.data(), command.size());
        return;
      }
      // Save the other server's group ID in its Client object
      c->name = groupId;

      // Log the successfully received HELO command
      log_event_bytes("RECV", c->ip.c_str(), c->port, c->name.c_str(),
                command.data(), command.size());

      std::string message = serversMessage();
      sendMessage(clientSocket, message);
    }

    else if(command.starts_with("SERVERS,"))
    {

      std::string serverList = command.substr(8);

      // lets us move through the string one by one
      std::stringstream ss(serverList);
      std::string entry;

      log_event_bytes("RECV", c->ip.c_str(), c->port,
                c->name.c_str(),
                command.data(), command.size());

      // Split each entry by the ;
      while (std::getline(ss, entry, ';'))
      {
        std::stringstream serverStream(entry);

        std::string groupId;
        std::string ip;
        std::string port;
        // Gets each detail by splitting it on the comma
        std::getline(serverStream, groupId, ',');
        std::getline(serverStream, ip, ',');
        std::getline(serverStream, port, ',');

        knownServers[groupId] = {ip, std::stoi(port)}; // Store the server in the knownServer map
        std::cout << groupId << " " << ip << " " << port << std::endl;
      }
      // All discovered servers have been stored so we try to connect to them
      connectToKnownServers(fds);
    }

    else
    {
        log_event_bytes("DROP", c->ip.c_str(), c->port, NULL,
                    command.data(), command.size());
                    
        // sendMessage(clientSocket, "Unknown command");
    }

    
}

int main(int argc, char *argv[])
{
    bool finished;
    int listenSock; // Socket for connections to server
    int clientSock; // Socket of connecting client

    // List of file descriptors being watched by poll(). By convention,
    // index 0 is always the listening socket; all other entries are
    // connected clients.
    std::vector<struct pollfd> fds;

    struct sockaddr_in client;
    socklen_t clientLen;
    unsigned char buffer[4096]; // buffer for reading from clients

    if (argc != 2)
    {
        printf("Usage: chat_server <ip port>\n");
        exit(0);
    }

    // Setup socket for server to listen to
    listenSock = open_socket(atoi(argv[1]));
    printf("Listening on port: %d\n", atoi(argv[1]));

    if (listen(listenSock, BACKLOG) < 0)
    {
        printf("Listen failed on port %s\n", argv[1]);
        exit(0);
    }
    else
    // Add listen socket to the list of fds being polled.
    {
        fds.push_back({listenSock, POLLIN, 0});

        // Log the server that has started
        log_event("START", NULL, 0, GROUP_ID, "Server started");

        // Connect to a peer server. First it needs to be the inital server to get info for the other servers
        if (!connectToPeer("130.208.246.98", 5001, fds))
        {
          // Log if connection failed
          log_note("Could not connect to initial server");
        }
    }

    finished = false;

    while (!finished)
    {
        memset(buffer, 0, sizeof(buffer));

        // Wait (indefinitely) until at least one of our sockets has
        // something to be read() on it.
        // fds.data() gices poll access to a vector that contains all of the sockets that are being watched
        // fds.size() tells poll how many sockets are being watched
        // -1 means wait indefinitely until something happens.
        int n = poll(fds.data(), fds.size(), -1);

        if (n < 0)
        {
            perror("poll failed - closing down\n");
            finished = true;
        }
        else
        {
            // Take a snapshot of the current fds/revents before processing
            // them below, since fds itself may be modified while we go
            // (new clients connecting, existing clients disconnecting).
            std::vector<struct pollfd> readyFds = fds;

            // First, accept any new connections to the server on the listening socket
            if (readyFds[0].revents & POLLIN)
            {
                clientLen = sizeof(client);
                clientSock = accept(listenSock, (struct sockaddr *)&client,
                                    &clientLen);
                if (clientSock < 0)
                {
                    perror("accept failed");
                }

                else
                {
                  std::string message;

                  printf("accept***\n");
                  // Add new client to the list of fds being polled
                  fds.push_back({clientSock, POLLIN, 0});

                  // create a new client to store information.
                  // Re-use the record for this socket if we have seen it
                  // before, so we don't leak a Client every time someone
                  // disconnects and reconnects.
                  if (clients.find(clientSock) == clients.end())
                  {
                      clients[clientSock] = new Client(clientSock);
                  }

                  // Get the clients ip address and port 
                  const char* ip = inet_ntoa(client.sin_addr);
                  int port = ntohs(client.sin_port);

                  // Save IP and port in the Client object
                  clients[clientSock]->ip = ip;
                  clients[clientSock]->port = port;

                  printf("Client connected on server: %d\n", clientSock);
                  log_event("ACCEPT", ip, port, NULL, "Client connected");

                  // Send HELO when a server connect to this server
                  message += "HELO,";
                  message += GROUP_ID;
                  sendMessage(clientSock, message);
                }
            }
            // Now check for commands from clients
            std::list<Client *> disconnectedClients;

            for (size_t i = 1; i < readyFds.size(); i++) // Loops through the clients sockets
            {
                if (readyFds[i].revents & POLLIN) // Tells is if the client socket has data available to read
                {
                    int sock = readyFds[i].fd;      // Finds the socket that has data to read
                    Client *client = clients[sock]; // Gives us the client object associated to the socket that was found

                    // recv() == 0 means client has closed connection
                    int bytesReceived = recv(sock, buffer, sizeof(buffer), MSG_DONTWAIT);
                    if (bytesReceived == 0)
                    {
                       // disconnectedClients.push_back(client);
                        closeClient(sock, &fds);
                        continue;
                    }

                    else if (bytesReceived > 0)
                    {
                        client->receiveBuffer.insert(
                            client->receiveBuffer.end(), // insert at the end,
                            buffer,                      // Where the new data starts
                            buffer + bytesReceived       // Where the new data ends.
                        );
                    }

                    else
                    {
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
                        {
                            perror("recv failed");

                            log_note("Error receiving data from client: %d", sock);
                            disconnectedClients.push_back(client);
                            closeClient(sock, &fds);
                        }
                        continue;
                    }
                    // Check if there is enough data in the buffer to read the header
                    // If it is 4 or higher we know for sure that the length has arrived so we can check that next
                    while (client->receiveBuffer.size() >= 4)
                    {
                        uint16_t length;

                        memcpy(
                            &length,                   // where to copy to
                            &client->receiveBuffer[1], // Where to start
                            sizeof(length)             // How many bytes we copy
                        );
                        uint16_t frameLength = ntohs(length); // Convert from network byte order to host byte order.

                        // The commands can be up too 5000 bytes. And the framing is 5 bytes so in total the whole frame can be from 5 bytes to 5005 bytes.
                        if (frameLength < 5 || frameLength > 5005)
                        {
                            std::cerr << "Invalid frame lengt\n";
                            log_note("Invalid frame length: %hu", frameLength);

                            log_event_bytes("DROP", client->ip.c_str(), client->port, NULL,
                                            client->receiveBuffer.data(),
                                            client->receiveBuffer.size());
                            client->receiveBuffer.clear(); // We remove the fram from the buffer because it has invalid size.
                            break;                         // Stop the while loop so we dont repeatedly tru to process the same invalid data.
                        }

                        // If the receive buffer is still smaller we wait for the rest of the frame
                        // When this isn't true anymore the whole packet has been sent
                        if (client->receiveBuffer.size() < frameLength)
                        {
                            break;
                        }

                        // We check if the whole packet has been sent.

                        // Make sure that the SOH, STX and ETX are correct
                        if (client->receiveBuffer[0] == 0x1 &&             // Check if SOH = 0x1
                            client->receiveBuffer[3] == 0x2 &&             // Check if STX = 0x2
                            client->receiveBuffer[frameLength - 1] == 0x3) // Find the last index and make sure it is the ETX. Should be 0x3
                        {
                            std::string command(
                                client->receiveBuffer.begin() + 4,              // Start at index 4 where the command starts
                                client->receiveBuffer.begin() + frameLength - 1 // Stop at the end
                            );

                            clientCommand(sock, command, fds);

                            // Remove the frame we just processed
                            client->receiveBuffer.erase(
                                client->receiveBuffer.begin(),
                                client->receiveBuffer.begin() + frameLength);
                        }
                        else
                        {
                            std::cerr << "Invalid frame\n";
                            log_event_bytes("DROP", client->ip.c_str(), client->port, NULL,
                            client->receiveBuffer.data(),
                            client->receiveBuffer.size());

                            client->receiveBuffer.clear();
                            break;
                        }
                    }
                }
            }
            // Remove client from the clients list
            for (auto const &c : disconnectedClients)
            {
                clients.erase(c->sock);
                delete c;
            }
        }
    }
}
