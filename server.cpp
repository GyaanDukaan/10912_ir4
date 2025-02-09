#include <iostream>
#include <map>
#include <string>
#include <iomanip>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <random>
#include <chrono>
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>

class OrderBook {
private:
    std::multimap<double, double> bid;  // Bid orders
    std::multimap<double, double> ask;  // Ask orders
    std::mutex mtx;                    // Mutex for thread safety
    std::atomic<bool> is_operational;   // Atomic flag for operational status

public:
    OrderBook() : is_operational(true) {}

    void addBid(double price, double quantity) {
        if (price <= 0 || quantity <= 0) {
            logError("Invalid bid price or quantity");
            return;
        }
        std::lock_guard<std::mutex> lock(mtx);
        bid.insert({ price, quantity });
    }

    void addAsk(double price, double quantity) {
        if (price <= 0 || quantity <= 0) {
            logError("Invalid ask price or quantity");
            return;
        }
        std::lock_guard<std::mutex> lock(mtx);
        ask.insert({ price, quantity });
    }

    void aggregateOrders() {
        std::lock_guard<std::mutex> lock(mtx);
        std::multimap<double, double> new_bid;
        std::multimap<double, double> new_ask;

        // Aggregating bids
        for (auto it = bid.rbegin(); it != bid.rend(); ) {
            auto current = it++;
            double price = current->first;
            double quantity = current->second;

            while (it != bid.rend() && it->first == price) {
                quantity += it->second;
                ++it;
            }
            new_bid.insert({ price, quantity });
        }

        // Aggregating asks
        for (auto it = ask.begin(); it != ask.end(); ) {
            auto current = it++;
            double price = current->first;
            double quantity = current->second;

            while (it != ask.end() && it->first == price) {
                quantity += it->second;
                ++it;
            }
            new_ask.insert({ price, quantity });
        }

        bid = std::move(new_bid);
        ask = std::move(new_ask);
    }

    void displayOrderBook() {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "Order Book:\n";
        std::cout << std::setw(10) << "Bid Price" << std::setw(15) << "Bid Quantity"
                  << std::setw(10) << "Ask Price" << std::setw(15) << "Ask Quantity" << "\n";

        auto bidIt = bid.rbegin();
        auto askIt = ask.begin();

        while (bidIt != bid.rend() || askIt != ask.end()) {
            if (bidIt != bid.rend()) {
                std::cout << std::setw(10) << std::fixed << std::setprecision(2) << bidIt->first
                          << std::setw(15) << bidIt->second;
                ++bidIt;
            } else {
                std::cout << std::setw(10) << " " << std::setw(15) << " ";
            }

            if (askIt != ask.end()) {
                std::cout << std::setw(10) << std::fixed << std::setprecision(2) << askIt->first
                          << std::setw(15) << askIt->second;
                ++askIt;
            } else {
                std::cout << std::setw(10) << " " << std::setw(15) << " ";
            }
            std::cout << "\n";
        }
    }

    bool operational() const {
        return is_operational.load();
    }

    void setOperational(bool operational) {
        is_operational.store(operational);
    }

private:
    void logError(const std::string& errorMessage) const {
        std::cerr << "ERROR: " << errorMessage << std::endl;
    }
};

// Function to generate random bid/ask orders
void generateRandomOrders(OrderBook& orderBook, size_t numOrders) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> disPrice(100.0, 200.0); // Random price between 100 and 200
    std::uniform_real_distribution<> disQuantity(1.0, 50.0); // Random quantity between 1 and 50

    for (size_t i = 0; i < numOrders; ++i) {
        double price = disPrice(gen);
        double quantity = disQuantity(gen);
        orderBook.addBid(price, quantity); // Add bid
        orderBook.addAsk(price, quantity); // Add ask
    }
}

// Set a socket to non-blocking mode
void setNonBlocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }
}

// Simple TCP Server using epoll to handle multiple clients concurrently
void startServer(OrderBook& orderBook, const std::string& host, int port) {
    int server_fd, epoll_fd;
    struct sockaddr_in address;
    struct epoll_event event, events[10];
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    setNonBlocking(server_fd); // Set the server socket to non-blocking

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    event.data.fd = server_fd;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("epoll_ctl: server_fd");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server listening on " << host << ":" << port << "\n";

    while (orderBook.operational()) {
        int num_events = epoll_wait(epoll_fd, events, 10, -1);
        if (num_events == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < num_events; ++i) {
            if (events[i].data.fd == server_fd) {
                int client_fd = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                if (client_fd == -1) {
                    perror("accept");
                    continue;
                }

                setNonBlocking(client_fd); // Set the client socket to non-blocking

                event.data.fd = client_fd;
                event.events = EPOLLIN | EPOLLET; // Edge-triggered mode
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("epoll_ctl: client_fd");
                    exit(EXIT_FAILURE);
                }
            } else if (events[i].events & EPOLLIN) {
                char buffer[1024] = {0};
                int client_fd = events[i].data.fd;
                ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));
                if (bytes_read == -1) {
                    perror("read");
                    close(client_fd);
                    continue;
                }

                std::string request(buffer);
                if (request == "insertBid") {
                    orderBook.addBid(150.0, 10.0);  // Example bid
                } else if (request == "insertAsk") {
                    orderBook.addAsk(155.0, 10.0);  // Example ask
                } else if (request == "displayOrderBook") {
                    orderBook.displayOrderBook();
                }

                close(client_fd);
            }
        }
    }

    close(server_fd);
}

int main() {
    OrderBook orderBook;

    // Start the server
    std::thread serverThread(startServer, std::ref(orderBook), "127.0.0.1", 8080);

    // Generate some random orders
    generateRandomOrders(orderBook, 1000);

    // Allow server to run for 60 seconds
    std::this_thread::sleep_for(std::chrono::seconds(60));

    // Shutdown the order book gracefully
    orderBook.setOperational(false);
    serverThread.join();

    return 0;
}
