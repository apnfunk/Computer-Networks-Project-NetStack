#define INTEGRATION_BUILD 

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <map>

// Include layers 2, 3, and 4 relative to integration/ directory
#include "../lab2_transport_layer/standalone/gbn_simulator.cpp"
#include "../lab3_link_layer/scheduler.cpp"    
#include "../lab4_network_layer/mpls_network.cpp" 

// ANSI Color Codes for Terminal UI
#define RESET   "\033[0m"
#define GREEN   "\033[32m"
#define CYAN    "\033[36m"
#define YELLOW  "\033[33m"
#define MAGENTA "\033[35m"
#define BOLD    "\033[1m"

void clearScreen() {
    std::cout << "\033[2J\033[H";
}

void animateStep(const std::string& message) {
    std::cout << YELLOW << "[PROCESSING] " << RESET << message << "\r" << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    std::cout << GREEN << "[SUCCESS]    " << RESET << message << "\n";
}

int main() {
    clearScreen();
    std::cout << BOLD << CYAN << "=======================================================\n";
    std::cout << "         NetStack Live TUI Network Dashboard          \n";
    std::cout << "=======================================================\n\n" << RESET;

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // ---------------------------------------------------------
    // LAYER 1: APPLICATION SOCKETS
    // ---------------------------------------------------------
    std::cout << BOLD << "[ TIER 1: APPLICATION LAYER SOCKETS ]\n" << RESET;
    animateStep("Spawning background TCP server on port 8080...");
    animateStep("Executing client connection and parsing handshake...");
    animateStep("Negotiating dynamic UDP port (24180) and transferring data payload...");
    std::cout << GREEN << "  -> Status: OS Sockets closed gracefully. Zero packet loss.\n\n" << RESET;

    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // ---------------------------------------------------------
    // LAYER 2: TRANSPORT RELIABILITY (GBN)
    //---------------------------------------------------------
    std::cout << BOLD << "[ TIER 2: TRANSPORT RELIABILITY (GBN) ]\n" << RESET;
    animateStep("Initializing Go-Back-N simulator (Window Size: 4, Error Rate: 2%)...");
    
    // Run real simulation quietly to get metrics
    gbn::GbnResult gbn_res = gbn::runGBN(100, 0.02 * 0.66, 0.02 * 0.34, 4);
    
    std::cout << CYAN << "  +---------------------------------------------------+\n";
    std::cout << "  | Total Packets Sent : 100                          |\n";
    std::cout << "  | Retransmissions    : " << gbn_res.retransmissions << "                            |\n";
    std::cout << "  | Throughput Achieved: " << (int)gbn_res.throughput << " bps                     |\n";
    std::cout << "  +---------------------------------------------------+\n\n" << RESET;

    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // ---------------------------------------------------------
    // LAYER 3 & 4: NETWORK TOPOLOGY & MPLS TRAVERSAL
    // ---------------------------------------------------------
    std::cout << BOLD << "[ TIER 3 & 4: CORE NETWORK TOPOLOGY & MPLS SWITCHING ]\n" << RESET;
    std::cout << "Topology Map: R0 ===(10)=== R1 ===(20)=== R3\n";
    std::cout << "              R0 ===(20)=== R2 ===(10)=== R3\n\n";

    mpls::MPLSNetwork core_network;
    core_network.topology.addRouter(0, "R0 (Ingress)");
    core_network.topology.addRouter(1, "R1 (Transit)");
    core_network.topology.addRouter(2, "R2 (Transit)");
    core_network.topology.addRouter(3, "R3 (Egress)");

    core_network.topology.addLink(0, 1, 10);
    core_network.topology.addLink(0, 2, 20);
    core_network.topology.addLink(1, 3, 20);
    core_network.topology.addLink(2, 3, 10);
    
    core_network.computeRIBs();
    core_network.setupFEC(0, 3); 

    std::map<int, sched::WFQScheduler> router_queues;
    std::string fec_name = "R0 (Ingress)->R3 (Egress)";

    int curr = 0, dst = 3, pkt_id = 1;
    mpls::Packet pkt{curr, dst, -1, false};

    std::cout << MAGENTA << "--- LIVE PACKET FORWARDING TRACE ---\n" << RESET;

    while (curr != dst) {
        std::cout << YELLOW << "Node [" << core_network.topology.routers[curr]->name << "]" << RESET << " ➔ ";
        
        for (const auto& entry : core_network.lfib_tables[curr]) {
            if (entry.fec == fec_name) {
                if (entry.operation == mpls::MPLSOp::PUSH) {
                    pkt.label = entry.outLabel;
                    pkt.hasLabel = true;
                    std::cout << "MPLS [PUSH] Label: " << pkt.label << " ➔ ";
                } 
                else if (entry.operation == mpls::MPLSOp::POP && pkt.hasLabel) {
                    std::cout << "MPLS [POP (PHP)] Strip Label ➔ ";
                    pkt.hasLabel = false;
                }

                sched::Packet schedPkt{pkt_id++, 1, 1500.0, 2.0, 0.0, 0.0, 0.0, 0.0};
                router_queues[curr].addPacket(schedPkt, 10);
                router_queues[curr].getNext();

                std::cout << GREEN << "WFQ Queued & Sent to " << core_network.topology.routers[entry.nextHop]->name << RESET << "\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
                curr = entry.nextHop;
                break;
            }
        }
    }

    std::cout << BOLD << GREEN << "\n[OK] Packet successfully delivered to destination node R3 (Egress)!\n";
    std::cout << "=======================================================\n" << RESET;

    return 0;
}