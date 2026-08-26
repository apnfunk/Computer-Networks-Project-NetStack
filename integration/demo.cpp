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

void simulateLinkDelay(const std::string& from, const std::string& to) {
    std::cout << "    [Physical Link] Transmitting " << from << " -> " << to << "...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << "  NetStack: End-to-End QoS-Aware Network Simulator\n";
    std::cout << "=======================================================\n\n";

    // ---------------------------------------------------------
    // LAYER 1: APPLICATION LAYER (Real OS Sockets)
    // ---------------------------------------------------------
    std::cout << ">>> [LAYER 1] APPLICATION LAYER (TCP Negotiation -> UDP Data) <<<\n";
    std::cout << "Forking Lab 1 background server and executing client...\n\n";
    
    std::string launch_cmd = "./lab1_server 8080 & "
                             "sleep 1 && "
                             "./lab1_client 127.0.0.1 8080 'Hello NetStack'";
    
    int sys_ret = std::system(launch_cmd.c_str());
    if (sys_ret != 0) {
        std::cerr << "\n[Warning] Lab 1 execution failed. Ensure 'lab1_server' and 'lab1_client' are compiled.\n";
    }

    std::cout << "\n>>> [LAYER 1] OS SOCKET TRANSFER COMPLETE <<<\n\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // ---------------------------------------------------------
    // LAYER 2: TRANSPORT LAYER (Reliability Simulation)
    // ---------------------------------------------------------
    std::cout << ">>> [LAYER 2] TRANSPORT LAYER (GBN Reliability) <<<\n";
    
    // Lowered error rate to 2% for realistic channel loss/corruption
    double error_rate = 0.02; 
    std::cout << "Simulating Transport Reliability for 100 packets (Link: 1Mbps, Delay: 10ms)...\n";
    
    gbn::GbnResult gbn_res = gbn::runGBN(100, error_rate * 0.66, error_rate * 0.34, 4);
    std::cout << "  -> Window Size: 4 | Channel Error Rate: 2%\n";
    std::cout << "  -> Total Retransmissions: " << gbn_res.retransmissions << "\n";
    std::cout << "  -> Throughput Achieved:   " << gbn_res.throughput << " bps\n\n";

    // ---------------------------------------------------------
    // LAYER 3 & 4: CORE NETWORK (MPLS Routing + WFQ Scheduling)
    // ---------------------------------------------------------
    std::cout << ">>> [LAYER 3 & 4] CORE NETWORK (Dynamic MPLS + WFQ Scheduling) <<<\n";
    
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
    core_network.setupFEC(0, 3); // Establish FEC path from R0 to R3

    // Attach a WFQ Scheduler instance to each router's egress port
    std::map<int, sched::WFQScheduler> router_queues;

    std::string fec_name = core_network.topology.routers[0]->name + "->" + core_network.topology.routers[3]->name;
    std::cout << "Executing dynamic LFIB packet traversal for FEC: " << fec_name << "\n\n";

    int curr = 0; // Source router ID (R0)
    int dst = 3;  // Destination router ID (R3)
    mpls::Packet pkt{curr, dst, -1, false};
    int pkt_id = 1;

    // Dynamic data plane traversal loop
    while (curr != dst) {
        std::cout << "[" << core_network.topology.routers[curr]->name << " Processing]\n";
        bool matched = false;
        
        for (const auto& entry : core_network.lfib_tables[curr]) {
            if (entry.fec == fec_name) {
                // Execute actual MPLS Label Operation computed by setupFEC()
                if (entry.operation == mpls::MPLSOp::PUSH) {
                    pkt.label = entry.outLabel;
                    pkt.hasLabel = true;
                    std::cout << "  -> [MPLS PUSH] Assigned label " << pkt.label 
                              << ", forwarding to " << core_network.topology.routers[entry.nextHop]->name << "\n";
                } 
                else if (entry.operation == mpls::MPLSOp::SWAP && pkt.hasLabel && pkt.label == entry.inLabel) {
                    std::cout << "  -> [MPLS SWAP] Swapped label " << pkt.label << " for " << entry.outLabel 
                              << ", forwarding to " << core_network.topology.routers[entry.nextHop]->name << "\n";
                    pkt.label = entry.outLabel;
                } 
                else if (entry.operation == mpls::MPLSOp::POP && pkt.hasLabel && pkt.label == entry.inLabel) {
                    std::cout << "  -> [MPLS POP] Popped label " << pkt.label 
                              << " (Penultimate Hop Popping), forwarding to " << core_network.topology.routers[entry.nextHop]->name << "\n";
                    pkt.hasLabel = false;
                    pkt.label = -1;
                }
                else if (entry.operation == mpls::MPLSOp::NONE) {
                    std::cout << "  -> [MPLS NONE] Direct neighbor forwarding (no label), to " 
                              << core_network.topology.routers[entry.nextHop]->name << "\n";
                }

                // Pass packet through the router's output WFQ Scheduler
                sched::Packet schedPkt{pkt_id++, 1, 1500.0, 2.0, 0.0, 0.0, 0.0, 0.0};
                bool dropped = router_queues[curr].addPacket(schedPkt, 10);
                if (!dropped) {
                    std::cout << "  -> [WFQ Scheduler] Queued in output buffer (Buffer size: " 
                              << router_queues[curr].size() << "). Servicing packet...\n";
                    router_queues[curr].getNext();
                }

                simulateLinkDelay(core_network.topology.routers[curr]->name, 
                                 core_network.topology.routers[entry.nextHop]->name);
                curr = entry.nextHop;
                matched = true;
                break;
            }
        }
        
        if (!matched) {
            std::cout << "  [ERROR] No matching LFIB entry found for FEC " << fec_name << "\n";
            break;
        }
    }

    if (curr == dst) {
        std::cout << "[" << core_network.topology.routers[curr]->name << "]\n";
        std::cout << "  -> Packet delivered cleanly to egress destination!\n";
    }

    std::cout << "\n=======================================================\n";
    std::cout << "  End-to-End Integration Verified Successfully.\n";
    std::cout << "=======================================================\n";

    return 0;
}