// abp_simulator.cpp
// Alternating Bit Protocol Simulator

#include <iostream>
#include <iomanip>
#include <random>
#include <queue>
#include <string>

using namespace std;

enum EventType {
    EV_APP_SEND,      // Application wants to send a packet
    EV_PKT_ARRIVE,    // Packet arrives at receiver
    EV_ACK_ARRIVE,    // ACK arrives at sender
    EV_TIMEOUT        // Sender timeout
};

struct Event {
    double time;
    EventType type;
    int seqNum;
    int ackNum;
    bool isCorrupt;
    int gen = 0;  
   
    long long eventId; // ADDED: Unique tie-breaker for simultaneous events

    bool operator>(const Event& other) const {
        if (time != other.time) return time > other.time;
        return eventId > other.eventId; // ADDED: Maintains strict FIFO order
    }
};

class Simulator {
public:
    priority_queue<Event, vector<Event>, greater<Event>> events;
    double currentTime = 0;
    long long nextEventId = 0; // ADDED: Counter
    void addEvent(double t, EventType type, int seq = 0, int ack = 0, bool corrupt = false,int gen = 0) {
        events.push({t, type, seq, ack, corrupt,gen,nextEventId++});
    }
};

struct Packet {
    int seqNum;
    string data;
    bool isCorrupt;
};

struct AckPacket {
    int ackNum;
    bool isCorrupt;
};

class UnreliableChannel {
private:
    double lossProb;
    double corruptProb;
    mt19937 gen;
    uniform_real_distribution<> dis;
    double propDelay;
    double transTime;

public:
    UnreliableChannel(double l, double c, double pd, double tt) 
        : lossProb(l), corruptProb(c), gen(42), dis(0.0, 1.0), propDelay(pd), transTime(tt) {}

    void setParams(double l, double c) {
        lossProb = l;
        corruptProb = c;
    }

    void sendPacket(Simulator& sim, int seqNum) {
        if (dis(gen) < lossProb) return; // Dropped
        bool corrupt = (dis(gen) < corruptProb);
        sim.addEvent(sim.currentTime + transTime + propDelay, EV_PKT_ARRIVE, seqNum, 0, corrupt);
    }

    void sendAck(Simulator& sim, int ackNum) {
        if (dis(gen) < lossProb) return; // Dropped
        bool corrupt = (dis(gen) < corruptProb);
        sim.addEvent(sim.currentTime + transTime + propDelay, EV_ACK_ARRIVE, 0, ackNum, corrupt);
    }
};

class Sender {
public:
    int seq;
    int totalSent;
    int retransmissions;
    bool waiting;
    double timeoutInterval;
    int currentGen = 0;

    Sender(double timeout) : seq(0), totalSent(0), retransmissions(0), waiting(false), timeoutInterval(timeout) {}

    void sendPacket(Simulator& sim, UnreliableChannel& channel) {
        totalSent++;
        if (waiting) retransmissions++;
        waiting = true;
        currentGen++;
        channel.sendPacket(sim, seq);
        sim.addEvent(sim.currentTime + timeoutInterval, EV_TIMEOUT, seq, 0, false, currentGen);
    }

    void receiveAck(Simulator& sim, UnreliableChannel& channel, int ackNum, bool corrupt) {
        if (!corrupt && ackNum == seq && waiting) {
            waiting = false;
            seq = 1 - seq; // Toggle
            sim.addEvent(sim.currentTime + 0.001, EV_APP_SEND); // Send next
        }
    }

    void handleTimeout(Simulator& sim, UnreliableChannel& channel, int seqNum, int gen) {
        if (waiting && seqNum == seq && gen == currentGen) {
            sendPacket(sim, channel);
        }
    }
};

class Receiver {
public:
    int expectedSeq;
    int delivered;

    Receiver() : expectedSeq(0), delivered(0) {}

    void receivePacket(Simulator& sim, UnreliableChannel& channel, int seqNum, bool corrupt) {
        if (!corrupt && seqNum == expectedSeq) {
            delivered++;
            channel.sendAck(sim, expectedSeq);
            expectedSeq = 1 - expectedSeq;
        } else {
            // Duplicate or corrupt, send ACK for last correctly received
            channel.sendAck(sim, 1 - expectedSeq);
        }
    }
};

struct SimulationResult {
    int totalPacketsSent;
    int totalRetransmissions;
    double throughput; 
    double avgDelay; 
    double efficiency;
};

SimulationResult runABP(int numPackets, double loss, double corrupt) {
    Simulator sim;
    UnreliableChannel channel(loss, corrupt, 10.0, 8.0); // 10ms prop, 8ms trans
    Sender sender(50.0); // 50ms timeout
    Receiver receiver;

    sim.addEvent(0.0, EV_APP_SEND);

    while (!sim.events.empty() && receiver.delivered < numPackets) {
        Event ev = sim.events.top();
        sim.events.pop();
        
        // Ignore old timeouts
        if (ev.time < sim.currentTime) continue;
        
        sim.currentTime = ev.time;

        if (ev.type == EV_APP_SEND && !sender.waiting) {
            sender.sendPacket(sim, channel);
        }
        else if (ev.type == EV_PKT_ARRIVE) {
            receiver.receivePacket(sim, channel, ev.seqNum, ev.isCorrupt);
        }
        else if (ev.type == EV_ACK_ARRIVE) {
            sender.receiveAck(sim, channel, ev.ackNum, ev.isCorrupt);
        }
        else if (ev.type == EV_TIMEOUT) {
            sender.handleTimeout(sim, channel, ev.seqNum, ev.gen); 
        }
    }

    SimulationResult res;
    res.totalPacketsSent = sender.totalSent;
    res.totalRetransmissions = sender.retransmissions;
    res.throughput = (numPackets * 8000.0) / (sim.currentTime / 1000.0); // bps
    res.avgDelay = sim.currentTime / numPackets;
    res.efficiency = (double)numPackets / sender.totalSent;

    return res;
}

int main() {
    double errorRates[] = {0.0, 0.05, 0.10, 0.20, 0.30};
    int numPackets = 100;
    
    cout << "Alternating Bit Protocol (ABP) Simulation\n";
    cout << "Packets: " << numPackets << ", Link: 1Mbps, Delay: 10ms\n";
    cout << "----------------------------------------------------------------------------------\n";
    cout << "Error Rate | Total Sent | Retransmissions | Throughput(bps) | Avg Delay | Efficiency\n";
    cout << "----------------------------------------------------------------------------------\n";
    
    for (double err : errorRates) {
        double loss = err * 0.66;
        double corrupt = err * 0.34;
        
        SimulationResult r = runABP(numPackets, loss, corrupt);
        
        cout << fixed << setprecision(2);
        cout << setw(9) << (err * 100) << "% | "
             << setw(10) << r.totalPacketsSent << " | "
             << setw(15) << r.totalRetransmissions << " | "
             << setw(15) << r.throughput << " | "
             << setw(7) << r.avgDelay << "ms | "
             << setw(9) << (r.efficiency * 100) << "%\n";
    }
    
    return 0;
}
