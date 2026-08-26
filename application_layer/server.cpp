#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <TCP_PORT>\n";
        return 1;
    }
    
    int tcp_port = std::stoi(argv[1]);
    
    // Phase 1: TCP Negotiation
    int tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sockfd < 0) {
        std::cerr << "Failed to create TCP socket\n";
        return 1;
    }
    
    int opt = 1;
    if (setsockopt(tcp_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(tcp_port);
    
    if (bind(tcp_sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "TCP bind failed\n";
        return 1;
    }
    
    if (listen(tcp_sockfd, 1) < 0) {
        std::cerr << "Listen failed\n";
        return 1;
    }
    
    std::cout << "Server listening on TCP port " << tcp_port << "\n";
    
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(tcp_sockfd, (sockaddr*)&client_addr, &client_len);
    
    if (client_fd < 0) {
        std::cerr << "Accept failed\n";
        return 1;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    char buffer[1027];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
    if (bytes_read <= 0) {
        std::cerr << "Failed to read TCP request\n";
        return 1;
    }
    
    Message req;
    deserialize_message(buffer, req);
    
    if (req.type != 1) {
        std::cerr << "Invalid message type\n";
        return 1;
    }
    
    std::cout << "Received Type 1 Request. Payload: " << std::string(req.payload, req.length) << "\n";
    
    // Find random UDP port
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10000, 60000);
    
    int udp_port = 0;
    int udp_sockfd = -1;
    
    while (true) {
        udp_port = dis(gen);
        udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_sockfd < 0) continue;
        
        if (setsockopt(udp_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(udp_sockfd);
            continue;
        }
        
        sockaddr_in udp_addr{};
        udp_addr.sin_family = AF_INET;
        udp_addr.sin_addr.s_addr = INADDR_ANY;
        udp_addr.sin_port = htons(udp_port);
        
        if (bind(udp_sockfd, (sockaddr*)&udp_addr, sizeof(udp_addr)) == 0) {
            break; // Successfully bound
        }
        close(udp_sockfd);
    }
    
    Message res;
    res.type = 2;
    std::string port_str = std::to_string(udp_port);
    res.length = port_str.length();
    std::memcpy(res.payload, port_str.c_str(), res.length);
    
    serialize_message(res, buffer);
    send(client_fd, buffer, get_serialized_size(res), 0);
    
    close(client_fd);
    close(tcp_sockfd);
    
    std::cout << "Phase 1 complete: Negotiated UDP port " << udp_port << "\n";
    
    // Phase 2: UDP
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(udp_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    sockaddr_in udp_client_addr{};
    socklen_t udp_client_len = sizeof(udp_client_addr);
    
    bytes_read = recvfrom(udp_sockfd, buffer, sizeof(buffer), 0, (sockaddr*)&udp_client_addr, &udp_client_len);
    
    if (bytes_read < 0) {
        std::cerr << "UDP recvfrom failed or timed out\n";
        close(udp_sockfd);
        return 1;
    }
    
    Message data_req;
    deserialize_message(buffer, data_req);
    
    if (data_req.type != 3) {
        std::cerr << "Invalid message type for Phase 2\n";
        close(udp_sockfd);
        return 1;
    }
    
    std::cout << "Received Type 3 Data: " << std::string(data_req.payload, data_req.length) << "\n";
    
    Message data_res;
    data_res.type = 4;
    std::string ack = "ACK";
    data_res.length = ack.length();
    std::memcpy(data_res.payload, ack.c_str(), data_res.length);
    
    serialize_message(data_res, buffer);
    sendto(udp_sockfd, buffer, get_serialized_size(data_res), 0, (sockaddr*)&udp_client_addr, udp_client_len);
    
    close(udp_sockfd);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    
    std::cout << "Phase 2 complete: Data exchange successful\n";
    std::cout << "Total time: " << elapsed.count() << " seconds\n";
    std::cout << "Bytes transferred in UDP: " << bytes_read << "\n";
    
    return 0;
}
