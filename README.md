# NetStack: An End-to-End QoS-Aware Network Architecture Simulator

NetStack is a modular C++ network emulator that demonstrates the complete lifecycle of data transmission across core tiers of the OSI networking model (simulating **Layers 2, 3, 4, and 7**) . It unifies application-layer socket communication, reliable transport protocols, dynamic network routing, and link-layer Quality of Service (QoS) scheduling into a single end-to-end simulation framework. 

Specifically, NetStack covers the following OSI layers:
*   **Application Layer (Layer 7):** Implements real OS-level socket communication and a two-stage TCP/UDP client-server negotiation protocol.
*   **Transport Layer (Layer 4):** Features reliable data transfer (RDT) engines using Alternating Bit Protocol (ABP) and Go-Back-N (GBN) over custom discrete-event loss/corruption error models.
*   **Network Layer (Layer 3):** Handles Dijkstra’s shortest path routing, Routing Information Base (RIB) computation, Label Forwarding Information Base (LFIB) generation, and Multi-Protocol Label Switching (MPLS) operations including Penultimate Hop Popping (PHP).
*   **Link Layer (Layer 2):** Manages multithreaded traffic generators and compares First-Come-First-Serve (FCFS) against Weighted Fair Queuing (WFQ) to optimize delay, drop rates, and Jain's Fairness Index.
---

## Project Architecture

```text
NetStack/
├── application_layer/       # Application Layer (TCP/UDP Sockets)
├── transport_layer/         # Transport Layer (Reliable Data Transfer)
├── link_layer/              # Link Layer (QoS Packet Scheduling)
├── network_layer/           # Network Layer (Dijkstra Routing & MPLS)
└── integration/                  # End-to-End Execution Pipeline
```

---

## Features

- Two-stage TCP and UDP socket communication
- Reliable transport using Go-Back-N and Alternating Bit Protocol (ABP)
- Multithreaded packet scheduling with FCFS and Weighted Fair Queuing (WFQ)
- Dijkstra shortest path routing with MPLS label switching
- Fully integrated end-to-end network simulation pipeline

---

# Execution Guide

## 1. Application Layer

### Overview

Implements a two-stage client-server communication protocol. The client first establishes a TCP connection to negotiate a dynamic communication port with the server. After the TCP connection terminates, communication continues over UDP using the assigned port.

### Build & Run

```bash
# Terminal 1 (Server)
g++ -std=c++17 -Wall application_layer/server.cpp -o lab1_server
./lab1_server 8080

# Terminal 2 (Client)
g++ -std=c++17 -Wall application_layer/client.cpp -o lab1_client
./lab1_client 127.0.0.1 8080 "NetStack Application Test"
```

### Verification

- Verify that the TCP socket closes before UDP communication begins using:

```bash
lsof -i
```

- Confirm successful dynamic port negotiation and UDP message delivery.

---

## 2. Transport Layer

### Overview

Implements reliable data transfer over lossy channels using:

- Go-Back-N (GBN)
- Alternating Bit Protocol (ABP)

A probabilistic error model simulates packet loss and corruption to evaluate protocol performance.

### Build & Run

```bash
g++ -std=c++17 -Wall transport_layer/standalone/gbn_simulator.cpp -o gbn
./gbn
```

```bash
g++ -std=c++17 -Wall transport_layer/standalone/abp_simulator.cpp -o abp
./abp
```

### Verification

- Run with 0% packet loss to verify zero retransmissions.
- Increase packet loss probability to observe retransmission behavior.
- Compare throughput between GBN and ABP under varying error conditions.

---

## 3. Link Layer

### Overview

Simulates concurrent packet generation from multiple sources using multithreading (`std::thread` and `std::mutex`). The scheduler compares:

- First-Come-First-Serve (FCFS)
- Weighted Fair Queuing (WFQ)

over a finite-capacity output queue.

### Build & Run

```bash
g++ -std=c++17 -Wall link_layer/scheduler.cpp -o scheduler
./scheduler link_layer/input_A.txt
```

### Verification

- Measure link utilization.
- Compute Jain's Fairness Index.
- Compare fairness achieved by FCFS and WFQ scheduling.

---

## 4. Network Layer

### Overview

Constructs a router topology and computes shortest paths using Dijkstra's algorithm. The simulator builds:

- Routing Information Base (RIB)
- Label Forwarding Information Base (LFIB)

and performs MPLS operations including:

- PUSH
- SWAP
- POP

### Build & Run

```bash
g++ -std=c++17 -Wall network_layer/mpls_network.cpp -o mpls
./mpls
```

### Verification

- Verify the shortest path from **R0 → R3**.
- Confirm correct MPLS label allocation.
- Validate Penultimate Hop Popping (PHP) behavior.
- Observe PUSH, SWAP, and POP operations during packet forwarding.

---

## 5. Integration Layer

### Overview

Combines all networking layers into a single execution pipeline, demonstrating complete packet flow from application-level communication to transport reliability, MPLS routing, and QoS-aware scheduling.

### Build & Run

```bash
g++ -std=c++17 -Wall -pthread integration/demo.cpp -o demo
./demo
```

### Step 6: Interactive Live TUI Dashboard (Optional Visual Mode)
* **What is being done:** Provides an ANSI-colored, real-time terminal dashboard (`live_demo.cpp`) that visually steps through each layer with animated delays, live progress status indicators, and network topology maps.
* **How to run:**
  ```bash
  g++ -std=c++17 -Wall -pthread integration/live_demo.cpp -o live_demo
  ./live_demo
  ```

### Verification & Terminal Insights:

- Execute the integrated simulator.
- Follow console logs through each networking layer.
- Watch the terminal dynamically update execution states with green success indicators and color-coded packet flows.
- Verify end-to-end packet delivery and routing statistics.

---

# Technologies Used

- C++17
- POSIX Socket Programming
- TCP & UDP
- Multithreading (`std::thread`)
- Mutex Synchronization (`std::mutex`)
- Go-Back-N Protocol
- Alternating Bit Protocol (ABP)
- Dijkstra's Shortest Path Algorithm
- MPLS Label Switching
- Weighted Fair Queuing (WFQ)

---

# Learning Outcomes

This project demonstrates the implementation and interaction of fundamental networking concepts across multiple layers of the protocol stack, including:

- Client-server socket programming
- Reliable transport protocols
- Concurrent packet scheduling
- Network routing algorithms
- MPLS forwarding
- End-to-end network architecture integration

The modular design allows each networking layer to be executed independently while also supporting complete end-to-end system simulation.

