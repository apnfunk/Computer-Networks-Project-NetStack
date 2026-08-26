#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <map>
#include <queue>
#include <set>
#include <limits>
#include <iomanip>
#include <algorithm>

namespace mpls {

// ==========================================
// topology.h contents
// ==========================================

struct Edge {
    int to_router_id;
    int cost;
};

class Router {
public:
    int id;
    std::string name;
    std::vector<Edge> adj_list;

    Router(int i, const std::string& n) : id(i), name(n) {}
};

class Graph {
public:
    std::map<int, std::shared_ptr<Router>> routers;

    void addRouter(int id, const std::string& name) {
        routers[id] = std::make_shared<Router>(id, name);
    }

    void addLink(int u, int v, int cost) {
        routers[u]->adj_list.push_back({v, cost});
        routers[v]->adj_list.push_back({u, cost});
    }

    void dijkstra(int start_id, std::unordered_map<int, int>& dist, std::unordered_map<int, int>& prev) {
        for (const auto& pair : routers) {
            dist[pair.first] = std::numeric_limits<int>::max();
            prev[pair.first] = -1;
        }
        dist[start_id] = 0;

        using pii = std::pair<int, int>;
        std::priority_queue<pii, std::vector<pii>, std::greater<pii>> pq;
        pq.push({0, start_id});

        while (!pq.empty()) {
            int d = pq.top().first;
            int u = pq.top().second;
            pq.pop();

            if (d > dist[u]) continue;

            for (const auto& edge : routers[u]->adj_list) {
                int v = edge.to_router_id;
                int weight = edge.cost;

                if (dist[u] + weight < dist[v]) {
                    dist[v] = dist[u] + weight;
                    prev[v] = u;
                    pq.push({dist[v], v});
                }
            }
        }
    }
};

// ==========================================
// mpls_network.h contents
// ==========================================

struct RouteEntry {
    int destination;
    int nextHop;
    int totalCost;
};

enum class MPLSOp {
    PUSH,
    SWAP,
    POP,
    NONE
};

inline std::string opToString(MPLSOp op) {
    switch (op) {
        case MPLSOp::PUSH: return "PUSH";
        case MPLSOp::SWAP: return "SWAP";
        case MPLSOp::POP: return "POP ";
        default: return "NONE";
    }
}

struct LFIBEntry {
    MPLSOp operation;
    int inLabel;
    int outLabel;
    int nextHop;
    std::string fec;
};

struct Packet {
    int source;
    int destination;
    int label;
    bool hasLabel;
};

class MPLSNetwork {
public:
    Graph topology;
    std::map<int, std::vector<RouteEntry>> routing_tables;
    std::map<int, std::vector<LFIBEntry>> lfib_tables;
    int next_label = 100;

    void computeRIBs() {
        for (const auto& pair : topology.routers) {
            int start_id = pair.first;
            std::unordered_map<int, int> dist;
            std::unordered_map<int, int> prev;
            topology.dijkstra(start_id, dist, prev);

            for (const auto& target : topology.routers) {
                int dest_id = target.first;
                if (dest_id == start_id) continue;

                int curr = dest_id;
                int next_hop = dest_id;
                while (prev[curr] != start_id && prev[curr] != -1) {
                    curr = prev[curr];
                    next_hop = curr;
                }
                if (prev[curr] != -1) {
                    routing_tables[start_id].push_back({dest_id, next_hop, dist[dest_id]});
                }
            }
        }
    }

    void printRIBs() {
        std::cout << "=== Routing Tables (RIB) ===" << std::endl;
        for (const auto& pair : routing_tables) {
            std::cout << "Router " << topology.routers[pair.first]->name << " RIB:" << std::endl;
            for (const auto& entry : pair.second) {
                std::cout << "  Dest: " << topology.routers[entry.destination]->name
                          << " | Next Hop: " << topology.routers[entry.nextHop]->name
                          << " | Cost: " << entry.totalCost << std::endl;
            }
            std::cout << std::endl;
        }
    }

    void setupFEC(int src, int dst) {
        std::string fec_name = topology.routers[src]->name + "->" + topology.routers[dst]->name;

        std::unordered_map<int, int> dist;
        std::unordered_map<int, int> prev;
        topology.dijkstra(src, dist, prev);

        if (dist[dst] == std::numeric_limits<int>::max()) return;

        std::vector<int> path;
        int curr = dst;
        while (curr != -1) {
            path.push_back(curr);
            if (curr == src) break;
            curr = prev[curr];
        }
        std::reverse(path.begin(), path.end());

        if (path.size() == 2) {
            // Direct neighbor: no label needed, plain forwarding
            lfib_tables[src].push_back({MPLSOp::NONE, -1, -1, dst, fec_name});
            return;
        }

        int current_out_label = -1;

        // Reverse path traversal to assign labels from dst to src
        for (int i = (int)path.size() - 2; i >= 0; --i) {
            int u = path[i];
            int v = path[i + 1];

            if (u == src) {
                // PUSH at ingress
                lfib_tables[u].push_back({MPLSOp::PUSH, -1, current_out_label, v, fec_name});
            } else if (v == dst) {
                // POP at penultimate node
                int in_label = next_label++;
                lfib_tables[u].push_back({MPLSOp::POP, in_label, -1, v, fec_name});
                current_out_label = in_label;
            } else {
                // SWAP at transit
                int in_label = next_label++;
                lfib_tables[u].push_back({MPLSOp::SWAP, in_label, current_out_label, v, fec_name});
                current_out_label = in_label;
            }
        }
    }

    void printLFIBs() {
        std::cout << "=== LFIB Tables ===" << std::endl;
        for (const auto& pair : lfib_tables) {
            std::cout << "Router " << topology.routers[pair.first]->name << " LFIB:" << std::endl;
            for (const auto& entry : pair.second) {
                std::cout << "  FEC: " << std::left << std::setw(8) << entry.fec
                          << "| Op: " << std::left << std::setw(5) << opToString(entry.operation)
                          << "| InLabel: " << std::left << std::setw(4) << (entry.inLabel == -1 ? "-" : std::to_string(entry.inLabel))
                          << "| OutLabel: " << std::left << std::setw(4) << (entry.outLabel == -1 ? "-" : std::to_string(entry.outLabel))
                          << "| NextHop: " << topology.routers[entry.nextHop]->name << std::endl;
            }
            std::cout << std::endl;
        }
    }

    void simulatePacketForwarding(int src, int dst, const std::string& fec_name) {
        std::cout << "=== Packet Forwarding Trace: " << fec_name << " ===" << std::endl;
        Packet pkt{src, dst, -1, false};

        int curr = src;
        while (curr != dst) {
            std::cout << "Router " << topology.routers[curr]->name << " Processing..." << std::endl;
            bool matched = false;
            for (const auto& entry : lfib_tables[curr]) {
                if (entry.fec == fec_name) {
                    if (entry.operation == MPLSOp::NONE) {
                        std::cout << "  [FWD]  Plain forward (no label), to " << topology.routers[entry.nextHop]->name << std::endl;
                        curr = entry.nextHop;
                        matched = true;
                        break;
                    } else if (entry.operation == MPLSOp::PUSH) {
                        pkt.label = entry.outLabel;
                        pkt.hasLabel = true;
                        std::cout << "  [PUSH] Assigned label " << pkt.label << ", forwarding to " << topology.routers[entry.nextHop]->name << std::endl;
                        curr = entry.nextHop;
                        matched = true;
                        break;
                    } else if (entry.operation == MPLSOp::SWAP && pkt.hasLabel && pkt.label == entry.inLabel) {
                        std::cout << "  [SWAP] Swapped label " << pkt.label << " for " << entry.outLabel << ", forwarding to " << topology.routers[entry.nextHop]->name << std::endl;
                        pkt.label = entry.outLabel;
                        curr = entry.nextHop;
                        matched = true;
                        break;
                    } else if (entry.operation == MPLSOp::POP && pkt.hasLabel && pkt.label == entry.inLabel) {
                        std::cout << "  [POP]  Popped label " << pkt.label << ", forwarding to " << topology.routers[entry.nextHop]->name << std::endl;
                        pkt.hasLabel = false;
                        pkt.label = -1;
                        curr = entry.nextHop;
                        matched = true;
                        break;
                    }
                }
            }
            if (!matched) {
                std::cout << "  [ERROR] No matching LFIB entry or label mismatch!" << std::endl;
                break;
            }
        }
        if (curr == dst) {
            std::cout << "Router " << topology.routers[curr]->name << " Received packet for destination!" << std::endl;
        }
        std::cout << std::endl;
    }
};

} // namespace mpls

#ifndef INTEGRATION_BUILD

int main() {
    mpls::MPLSNetwork net;
    net.topology.addRouter(0, "R0");
    net.topology.addRouter(1, "R1");
    net.topology.addRouter(2, "R2");
    net.topology.addRouter(3, "R3");

    net.topology.addLink(0, 1, 10);
    net.topology.addLink(0, 2, 20);
    net.topology.addLink(1, 3, 20);
    net.topology.addLink(2, 3, 10);

    std::cout << "=== Network Topology ===" << std::endl;
    std::cout << "  R0 --(10)-- R1" << std::endl;
    std::cout << "  |           |" << std::endl;
    std::cout << "(20)        (20)" << std::endl;
    std::cout << "  |           |" << std::endl;
    std::cout << "  R2 --(10)-- R3" << std::endl;
    std::cout << std::endl;

    net.computeRIBs();
    net.printRIBs();

    // Setup FECs dynamically (auto assigns paths based on shortest path)
    net.setupFEC(0, 3); // R0->R3
    net.setupFEC(1, 2); // R1->R2
    net.setupFEC(0, 1); // R0->R1 direct neighbor - exercises the new NONE path

    net.printLFIBs();

    net.simulatePacketForwarding(0, 3, "R0->R3");
    net.simulatePacketForwarding(1, 2, "R1->R2");
    net.simulatePacketForwarding(0, 1, "R0->R1");

    return 0;
}

#endif