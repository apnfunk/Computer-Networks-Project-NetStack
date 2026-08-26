#include <iostream>
#include <string>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol.h"

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <SERVER_IP> <SERVER_TCP_PORT> [message]\n";
        return 1;
    }
    
    std::string server_ip = argv[1];
    int tcp_port = std::stoi(argv[2]);
    std::string message = (argc == 4) ? argv[3] : "Hello from NetStack client! This data was sent over UDP after TCP negotiation.";
    
    auto t0 = std::chrono::high_resolution_clock::now();
    
    // Phase 1: TCP Negotiation
    int tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sockfd < 0) {
        std::cerr << "Failed to create TCP socket\n";
        return 1;
    }
    
    int opt = 1;
    setsockopt(tcp_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(tcp_port);
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address\n";
        return 1;
    }
    
    if (connect(tcp_sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "TCP connect failed\n";
        return 1;
    }
    
    Message req;
    req.type = 1;
    std::string req_data = "CLIENT_001|Description";
    req.length = req_data.length();
    std::memcpy(req.payload, req_data.c_str(), req.length);
    
    char buffer[1027];
    serialize_message(req, buffer);
    send(tcp_sockfd, buffer, get_serialized_size(req), 0);
    
    ssize_t bytes_read = recv(tcp_sockfd, buffer, sizeof(buffer), 0);
    if (bytes_read <= 0) {
        std::cerr << "Failed to read TCP response\n";
        return 1;
    }
    
    Message res;
    deserialize_message(buffer, res);
    
    if (res.type != 2) {
        std::cerr << "Invalid message type\n";
        return 1;
    }
    
    std::string udp_port_str(res.payload, res.length);
    int udp_port = std::stoi(udp_port_str);
    
    close(tcp_sockfd);
    
    auto t1 = std::chrono::high_resolution_clock::now();
    
    std::cout << "Phase 1 complete: Server assigned UDP port " << udp_port << "\n";
    
    // Phase 2: UDP
    int udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sockfd < 0) {
        std::cerr << "Failed to create UDP socket\n";
        return 1;
    }
    
    setsockopt(udp_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(udp_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    sockaddr_in udp_server_addr{};
    udp_server_addr.sin_family = AF_INET;
    udp_server_addr.sin_port = htons(udp_port);
    inet_pton(AF_INET, server_ip.c_str(), &udp_server_addr.sin_addr);
    
    Message data_req;
    data_req.type = 3;
    data_req.length = message.length();
    std::memcpy(data_req.payload, message.c_str(), data_req.length);
    
    serialize_message(data_req, buffer);
    sendto(udp_sockfd, buffer, get_serialized_size(data_req), 0, (sockaddr*)&udp_server_addr, sizeof(udp_server_addr));
    
    sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);
    bytes_read = recvfrom(udp_sockfd, buffer, sizeof(buffer), 0, (sockaddr*)&from_addr, &from_len);
    
    if (bytes_read < 0) {
        std::cerr << "UDP recvfrom failed or timed out\n";
        close(udp_sockfd);
        return 1;
    }
    
    Message data_res;
    deserialize_message(buffer, data_res);
    
    close(udp_sockfd);
    
    auto t2 = std::chrono::high_resolution_clock::now();
    
    std::cout << "Phase 2 complete: Server acknowledged data\n";
    
    std::chrono::duration<double> tcp_time = t1 - t0;
    std::chrono::duration<double> udp_time = t2 - t1;
    std::chrono::duration<double> total_time = t2 - t0;
    
    std::cout << "TCP negotiation time: " << tcp_time.count() << " seconds\n";
    std::cout << "UDP transfer time: " << udp_time.count() << " seconds\n";
    std::cout << "Total time: " << total_time.count() << " seconds\n";
    
    return 0;
}
