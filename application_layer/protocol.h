#pragma once

#include <cstdint>
#include <cstring>
#include <arpa/inet.h>

// Message types for the 2-phase protocol
// Type 1: Request (Client->Server via TCP) - client requests UDP port
// Type 2: Response (Server->Client via TCP) - server provides UDP port  
// Type 3: Data (Client->Server via UDP) - actual data payload
// Type 4: Data Response (Server->Client via UDP) - acknowledgment

struct Message {
    uint8_t type;        // 1, 2, 3, or 4
    uint16_t length;     // Length of payload
    char payload[1024];  // Variable-length payload
};

inline void serialize_message(const Message& msg, char* buffer) {
    buffer[0] = msg.type;
    uint16_t net_length = htons(msg.length);
    std::memcpy(buffer + 1, &net_length, sizeof(uint16_t));
    std::memcpy(buffer + 3, msg.payload, msg.length);
}

inline void deserialize_message(const char* buffer, Message& msg) {
    msg.type = buffer[0];
    uint16_t net_length;
    std::memcpy(&net_length, buffer + 1, sizeof(uint16_t));
    msg.length = ntohs(net_length);
    std::memcpy(msg.payload, buffer + 3, msg.length);
}

inline size_t get_serialized_size(const Message& msg) {
    return 3 + msg.length; // 1 byte type + 2 bytes length + payload
}
