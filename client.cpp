//
// Simple chat client for TSAM
//
// Command line: ./client <ip> <port>
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
#include <poll.h>

#include <iostream>
#include <sstream>
#include <map>

bool sendCommand(int serverSocket, const std::string &command)
{
    if (command.size() > 5000)
    {
        std::cerr << "Command size is to big. 5000 bytes limit\n";
        return false;
    }

    uint16_t frameLength = command.size() + 5;   // The size of the frame is the command + the 5 bytes of stcture
    uint16_t networkLength = htons(frameLength); // Convert the host byte order to network byte order

    std::vector<char> frame; // A vector is created that will contain the frame

    // Stucture of the message: <SOH><length><STX><command><ETX>
    // We create that strucure here
    frame.push_back(0x1);
    frame.resize(3);
    memcpy(&frame[1], &networkLength, sizeof(networkLength));
    frame.push_back(0x2);
    frame.insert(frame.end(), command.begin(), command.end());
    frame.push_back(0x3);

    // Send the message. Do that until it sends the whole command.
    size_t remaining = frame.size();
    const char *p = frame.data();

    while (remaining > 0)
    {
        int bytesSent = send(serverSocket, p, remaining, 0);
        if (bytesSent <= 0)
        {
            return false;
        }
        p += bytesSent;         // Move forward in the vector
        remaining -= bytesSent; // Bytes remaining decrease
    }
    return true;
}

bool receiveResponse(int serverSocket, std::vector<char> &receiveBuffer)
{
    // Firts the bytes are stored in this buffer but later are stored in the reveivingBuffer where we extract the repsonse
    unsigned char buffer[4096];
    int bytesReceived = recv(serverSocket, buffer, sizeof(buffer), 0);

    // Instert the bytes received into the receive buffer
    if (bytesReceived > 0)
    {
        receiveBuffer.insert(
            receiveBuffer.end(),   // Insert at the end
            buffer,                // Where the new data starts
            buffer + bytesReceived // Wheere the new data ends
        );
    }

    // We must check if te data has 4 bytes. If that is the case we can see the length of the response
    while (receiveBuffer.size() >= 4)
    {
        // Declare the length. We want to find out the lengt of the response
        uint16_t length;

        // Copy the lenth section of the repsonse into lenth that we just declared
        memcpy(
            &length,           // We copy it to lenght
            &receiveBuffer[1], // Start at index[1] of the buffer because that is where the length section starts
            sizeof(length)     // Copy 2 bytes so we copy index [1] and [2]
        );

        uint16_t frameLength = ntohs(length); // Convert from network byte order to host byte order

        // If the response is also under 5 bytes is means that the stucture is wrong.
        if (frameLength < 5)
        {
            std::cerr << "Invaliid frame length\n";
            return false;
        }

        // If the receive buffer is smaller than the frame length it means that we have not received the whole response
        // So we wait until it has arrived
        if (receiveBuffer.size() < frameLength)
        {
            break;
        }

        // If the whole packet has been sent we make sure that the stucture of the packet is correct
        // SOH, STX and ETX are correct
        if (receiveBuffer[0] == 0x1 &&             // Make sure that SOH is 0x1
            receiveBuffer[3] == 0x2 &&             // Make sure that STX is 0x2
            receiveBuffer[frameLength - 1] == 0x3) // Make sure that ETX is 0x3
        {
            std::string response(
                receiveBuffer.begin() + 4,              // Start at index 4 because that is wher the response starts
                receiveBuffer.begin() + frameLength - 1 // Go to the end
            );

            // Print out the response
            std::cout << response << "\n";

            // Remove the reponse that we just processed. 
            // To make sure that we are not going backt to it again
            receiveBuffer.erase(
                receiveBuffer.begin(),
                receiveBuffer.begin() + frameLength
            );
        }
        else
        {
            std::cerr << "Invalid response frame\n";
            return false;
        }   
    }
    return true;
}

int main(int argc, char *argv[])
{
    struct addrinfo hints, *svr;  // Network host entry for server
    struct sockaddr_in serv_addr; // Socket address for server
    int serverSocket;             // Socket used for server
    bool finished;
    int set = 1; // Toggle for setsockopt

    if (argc != 3)
    {
        printf("Usage: chat_client <ip  port>\n");
        printf("Ctrl-C to terminate\n");
        exit(0);
    }

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET; // IPv4 only addresses
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(argv[1], argv[2], &hints, &svr) != 0)
    {
        perror("getaddrinfo failed: ");
        exit(0);
    }

    struct hostent *server;
    server = gethostbyname(argv[1]);

    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(atoi(argv[2]));

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    // Turn on SO_REUSEADDR to allow socket to be quickly reused after
    // program exit.

    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
    {
        printf("Failed to set SO_REUSEADDR for port %s\n", argv[2]);
        perror("setsockopt failed: ");
    }

    if (connect(serverSocket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        // EINPROGRESS means that the connection is still being setup. Typically this
        // only occurs with non-blocking sockets. (The serverSocket above is explicitly
        // not in non-blocking mode, so this check here is just an example of how to
        // handle this properly.)
        if (errno != EINPROGRESS)
        {
            printf("Failed to open socket to server: %s\n", argv[1]);
            perror("Connect failed: ");
            exit(0);
        }
    }

    // Watch both stdin (user typing a message) and the server socket
    // (server sending us something) with a single poll() call, instead of
    // using a separate thread for one of them.
    std::vector<struct pollfd> fds;
    fds.push_back({STDIN_FILENO, POLLIN, 0});
    fds.push_back({serverSocket, POLLIN, 0});

    // Store incoming data from the server
    std::vector<char> receiveBuffer;

    finished = false;
    while (!finished)
    {
        int n = poll(fds.data(), fds.size(), -1);

        if (n < 0)
        {
            perror("poll failed: ");
            finished = true;
            continue;
        }

        // Something typed by the user - read a line and send it to the server.
        if (fds[0].revents & POLLIN)
        {
            std::string command;

            if (!std::getline(std::cin, command)) // reads the command that the user typed
            {
                finished = true;
                continue;
            }

            if (!sendCommand(serverSocket, command))
            {
                std::cerr << "Failed to send\n";
                finished = true;
            }
        }

        // Something received from the server - print it out.
        if (fds[1].revents & POLLIN)
        {
            if (!receiveResponse(serverSocket, receiveBuffer))
            {
                std::cerr << "Receive failed\n";
                finished = true;
            }
        }
    }
}
