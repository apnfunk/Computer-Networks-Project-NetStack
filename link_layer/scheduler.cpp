#pragma once

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <random>
#include <cmath>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <queue>
#include <set>
#include <map>
#include <memory>
#include <algorithm>

namespace sched {

// Data Structures
struct Packet {
    int id;
    int sourceId;
    double length;
    double weight;
    double arrivalTime;
    double serviceStartTime;
    double virtualStartTime;
    double virtualFinishTime;

    // For sorting by arrival time in the generator
    bool operator<(const Packet& other) const {
        return arrivalTime < other.arrivalTime;
    }
};

struct Source {
    int id;
    double packetRate;
    double minLength;
    double maxLength;
    double weight;
    double startTime;
    double endTime;
};

struct Stats {
    long long totalGenerated = 0;
    long long totalTransmitted = 0;
    long long totalDropped = 0;
    long long totalBytes = 0;
    double totalDelay = 0.0;
    double totalBusyTime = 0.0;
    std::map<int, long long> bytesPerSource;
    std::map<int, long long> generatedPerSource;

    void reset() {
        totalGenerated = 0;
        totalTransmitted = 0;
        totalDropped = 0;
        totalBytes = 0;
        totalDelay = 0.0;
        totalBusyTime = 0.0;
        bytesPerSource.clear();
        generatedPerSource.clear();
    }
};

// Scheduler Interface
class Scheduler {
public:
    virtual ~Scheduler() = default;
    // Returns true if dropped
    virtual bool addPacket(const Packet& packet, int bufferSize) = 0;
    virtual Packet getNext() = 0;
    virtual bool isEmpty() const = 0;
    virtual int size() const = 0;
    virtual void clear() = 0;
    virtual void updateVirtualTime(double vft) {}
};

// FCFS Scheduler
class FCFSScheduler : public Scheduler {
private:
    std::queue<Packet> q;
public:
    bool addPacket(const Packet& packet, int bufferSize) override {
        if ((int)q.size() >= bufferSize) {
            return true; // Tail-drop
        }
        q.push(packet);
        return false;
    }

    Packet getNext() override {
        Packet p = q.front();
        q.pop();
        return p;
    }

    bool isEmpty() const override { return q.empty(); }
    int size() const override { return q.size(); }
    void clear() override { while (!q.empty()) q.pop(); }
};

// WFQ Scheduler
struct WFQCompare {
    bool operator()(const Packet& a, const Packet& b) const {
        if (a.virtualFinishTime != b.virtualFinishTime)
            return a.virtualFinishTime < b.virtualFinishTime;
        return a.id < b.id; // tie-breaker
    }
};

class WFQScheduler : public Scheduler {
private:
    std::multiset<Packet, WFQCompare> q;
    std::map<int, double> lastFinishTimeForSource;
    double systemVirtualTime = 0.0;

public:
    bool addPacket(const Packet& p_in, int bufferSize) override {
        Packet p = p_in;
        // Calculate VFT
        double prevFinish = 0.0;
        if (lastFinishTimeForSource.count(p.sourceId)) {
            prevFinish = lastFinishTimeForSource[p.sourceId];
        }
        p.virtualStartTime = std::max(systemVirtualTime, prevFinish);
        p.virtualFinishTime = p.virtualStartTime + (p.length / p.weight);
        lastFinishTimeForSource[p.sourceId] = p.virtualFinishTime;

        if ((int)q.size() >= bufferSize) {
            if (bufferSize <= 0 || q.empty()) return true;
            auto worst_it = std::prev(q.end());
            if (p.virtualFinishTime < worst_it->virtualFinishTime) {
                // Head drop (drop worst VFT) and insert new
                q.erase(worst_it);
                q.insert(p);
                return true; // A packet was dropped (the evicted one)
            }
            return true; // Tail drop incoming
        }

        q.insert(p);
        return false;
    }

    Packet getNext() override {
        auto best = q.begin();
        Packet p = *best;
        q.erase(best);
        return p;
    }

    void updateVirtualTime(double vft) override {
        systemVirtualTime = vft;
    }

    bool isEmpty() const override { return q.empty(); }
    int size() const override { return q.size(); }
    void clear() override {
        q.clear();
        lastFinishTimeForSource.clear();
        systemVirtualTime = 0.0;
    }
};

// Simulation Globals
inline std::mutex mtx;
inline std::vector<Packet> sharedQueue;
inline int packetIdCounter = 0;

inline void sourceThreadFunc(const Source& src) {
    std::vector<Packet> localQueue;
    std::random_device rd;
    std::mt19937 gen(rd() ^ src.id);
    std::exponential_distribution<double> arrivalDist(src.packetRate);
    std::uniform_real_distribution<double> lengthDist(src.minLength, src.maxLength);

    double currentTime = src.startTime;
    while (currentTime < src.endTime) {
        currentTime += arrivalDist(gen);
        if (currentTime > src.endTime) break;

        Packet p;
        p.sourceId = src.id;
        p.length = lengthDist(gen);
        p.weight = src.weight;
        p.arrivalTime = currentTime;
        p.serviceStartTime = 0;
        p.virtualStartTime = 0;
        p.virtualFinishTime = 0;

        localQueue.push_back(p);
    }

    std::lock_guard<std::mutex> lock(mtx);
    for (auto& p : localQueue) {
        p.id = ++packetIdCounter;
        sharedQueue.push_back(p);
    }
}

inline void printResults(const std::string& algoName, int N, double T, const Stats& stats) {
    double util = stats.totalBusyTime / T;

    double sumX = 0;
    double sumX2 = 0;
    for (int i = 0; i < N; i++) {
        double bytes = stats.bytesPerSource.count(i) ? stats.bytesPerSource.at(i) : 0;
        sumX += bytes;
        sumX2 += (bytes * bytes);
    }
    double fairness = (sumX > 0) ? (sumX * sumX) / (N * sumX2) : 0.0;
    double avgDelay = (stats.totalTransmitted > 0) ? stats.totalDelay / stats.totalTransmitted : 0.0;
    double dropProb = (stats.totalGenerated > 0) ? (double)stats.totalDropped / stats.totalGenerated : 0.0;

    std::cout << "+------------------------------------------------+\n";
    std::cout << "| Results: " << std::setw(38) << std::left << algoName << " |\n";
    std::cout << "+------------------------------------------------+\n";
    std::cout << "| Server Utilization:      " << std::setw(22) << std::fixed << std::setprecision(4) << util << " |\n";
    std::cout << "| Jain's Fairness Index:   " << std::setw(22) << fairness << " |\n";
    std::cout << "| Avg Packet Delay (s):    " << std::setw(22) << avgDelay << " |\n";
    std::cout << "| Packet Drop Probability: " << std::setw(22) << dropProb << " |\n";
    std::cout << "+------------------------------------------------+\n";
    std::cout << "| Per-source Throughput (Bytes):                  |\n";
    for (int i = 0; i < N; i++) {
        long long b = stats.bytesPerSource.count(i) ? stats.bytesPerSource.at(i) : 0;
        std::cout << "|   Source " << i << ": " << std::setw(33) << b << " |\n";
    }
    std::cout << "+------------------------------------------------+\n\n";
}

inline void runSimulation(Scheduler* scheduler, const std::string& algoName, const std::vector<Packet>& packets, double C, int B, int N, double T, Stats& stats) {
    stats.reset();
    scheduler->clear();
    stats.totalGenerated = packets.size();

    for (const auto& p : packets) {
        stats.generatedPerSource[p.sourceId]++;
    }

    double serverFreeTime = 0.0;

    for (const auto& p : packets) {
        // Drain scheduler of packets that depart before 'p' arrives
        while (serverFreeTime <= p.arrivalTime && !scheduler->isEmpty()) {
            Packet nextP = scheduler->getNext();
            if (algoName == "WFQ") scheduler->updateVirtualTime(nextP.virtualFinishTime);

            double startService = std::max(serverFreeTime, nextP.arrivalTime);
            double serviceTime = nextP.length / C;
            serverFreeTime = startService + serviceTime;

            stats.totalTransmitted++;
            stats.totalDelay += (startService - nextP.arrivalTime);
            stats.totalBytes += nextP.length;
            stats.bytesPerSource[nextP.sourceId] += nextP.length;
            stats.totalBusyTime += serviceTime;
        }

        bool dropped = scheduler->addPacket(p, B);
        if (dropped) {
            stats.totalDropped++;
        }
    }

    // Drain remaining
    while (!scheduler->isEmpty()) {
        Packet nextP = scheduler->getNext();
        if (algoName == "WFQ") scheduler->updateVirtualTime(nextP.virtualFinishTime);

        double startService = std::max(serverFreeTime, nextP.arrivalTime);
        double serviceTime = nextP.length / C;
        serverFreeTime = startService + serviceTime;

        stats.totalTransmitted++;
        stats.totalDelay += (startService - nextP.arrivalTime);
        stats.totalBytes += nextP.length;
        stats.bytesPerSource[nextP.sourceId] += nextP.length;
        stats.totalBusyTime += serviceTime;
    }

    printResults(algoName, N, T, stats);
}

inline void processFile(const std::string& filename) {
    std::ifstream fin(filename);
    if (!fin) {
        std::cerr << "Cannot open " << filename << "\n";
        return;
    }

    std::string header;
    std::getline(fin, header);
    int N = 0;
    double T = 0, C = 0;
    int B = 0;

    // Parse "N=4 T=1200 C=100000 B=150"
    std::stringstream ss(header);
    std::string token;
    while (ss >> token) {
        if (token.find("N=") == 0) N = std::stoi(token.substr(2));
        else if (token.find("T=") == 0) T = std::stod(token.substr(2));
        else if (token.find("C=") == 0) C = std::stod(token.substr(2));
        else if (token.find("B=") == 0) B = std::stoi(token.substr(2));
    }

    std::vector<Source> sources(N);
    for (int i = 0; i < N; i++) {
        sources[i].id = i;
        double startFrac, endFrac;
        fin >> sources[i].packetRate >> sources[i].minLength >> sources[i].maxLength >> sources[i].weight >> startFrac >> endFrac;
        sources[i].startTime = startFrac * T;
        sources[i].endTime = endFrac * T;
    }

    std::cout << "================================================\n";
    std::cout << "Simulation for " << filename << "\n";
    std::cout << "================================================\n";

    sharedQueue.clear();
    packetIdCounter = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < N; i++) {
        threads.emplace_back(sourceThreadFunc, sources[i]);
    }

    for (auto& t : threads) {
        t.join();
    }

    // Sort generated packets by arrival time
    std::sort(sharedQueue.begin(), sharedQueue.end());

    Stats stats;

    FCFSScheduler fcfs;
    runSimulation(&fcfs, "FCFS", sharedQueue, C, B, N, T, stats);

    WFQScheduler wfq;
    runSimulation(&wfq, "WFQ", sharedQueue, C, B, N, T, stats);
}

} // namespace sched

#ifndef INTEGRATION_BUILD
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file>\n";
        return 1;
    }
    sched::processFile(argv[1]);
    return 0;
}
#endif