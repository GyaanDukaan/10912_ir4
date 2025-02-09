#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>

#define SERVER_IP "127.0.0.1" // The server IP address
#define SERVER_PORT 8080      // The server port

// Function to handle connection and sending requests to the server
void sendRequestToServer(const std::string& request) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address or address not supported");
        close(sock);
        return;
    }

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return;
    }

    // Send the request to the server
    ssize_t bytes_sent = send(sock, request.c_str(), request.size(), 0);
    if (bytes_sent == -1) {
        perror("Send failed");
        close(sock);
        return;
    }

    std::cout << "Request sent: " << request << std::endl;

    // Receive the response from the server (if any)
    char buffer[1024] = {0};
    ssize_t bytes_received = recv(sock, buffer, sizeof(buffer), 0);
    if (bytes_received > 0) {
        std::cout << "Server response: " << buffer << std::endl;
    } else {
        std::cout << "No response or connection closed by server" << std::endl;
    }

    // Close the socket
    close(sock);
}

int main() {
    // Example commands to send to the server
    std::cout << "Connecting to server..." << std::endl;

    // Send "insertBid" request
    sendRequestToServer("insertBid");

    // Send "insertAsk" request
    sendRequestToServer("insertAsk");

    // Send "displayOrderBook" request to get the current state of the order book
    sendRequestToServer("displayOrderBook");

    // Wait and perform more requests if needed
    std::this_thread::sleep_for(std::chrono::seconds(2));

    return 0;
}
