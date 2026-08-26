// gbn_simulator.cpp
// Go-Back-N Protocol Simulator

#include <iostream>
#include <iomanip>
#include <random>
#include <queue>
#include <vector>
#include <string>

using namespace std;

namespace gbn {


enum EventType {
    EV_APP_SEND,
    EV_PKT_ARRIVE,
    EV_ACK_ARRIVE,
    EV_TIMEOUT
};

struct Event {
    double time;
    EventType type;
    int seqNum;
    int ackNum;
    bool isCorrupt;
    long long eventId;
    bool operator>(const Event& other) const {
       if (time != other.time) return time > other.time;
        return eventId > other.eventId; 
    }
};

class Simulator {
public:
    priority_queue<Event, vector<Event>, greater<Event>> events;
    double currentTime = 0;
    long long nextEventId = 0;
    void addEvent(double t, EventType type, int seq = 0, int ack = 0, bool corrupt = false) {
        events.push({t, type, seq, ack, corrupt,nextEventId++});
    }
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

    void sendPacket(Simulator& sim, int seqNum) {
        if (dis(gen) < lossProb) return;
        bool corrupt = (dis(gen) < corruptProb);
        sim.addEvent(sim.currentTime + transTime + propDelay, EV_PKT_ARRIVE, seqNum, 0, corrupt);
    }

    void sendAck(Simulator& sim, int ackNum) {
        if (dis(gen) < lossProb) return;
        bool corrupt = (dis(gen) < corruptProb);
        sim.addEvent(sim.currentTime + transTime + propDelay, EV_ACK_ARRIVE, 0, ackNum, corrupt);
    }
};

class GbnSender {
public:
    int base;
    int nextSeqNum;
    int windowSize;
    int totalSent;
    int retransmissions;
    double timeoutInterval;
    double expectedTimer;

    GbnSender(int w, double timeout) : base(0), nextSeqNum(0), windowSize(w), totalSent(0), retransmissions(0), timeoutInterval(timeout), expectedTimer(0) {}

    void sendPackets(Simulator& sim, UnreliableChannel& channel, int maxPackets) {
        while (nextSeqNum < base + windowSize && nextSeqNum < maxPackets) {
            totalSent++;
            channel.sendPacket(sim, nextSeqNum);
            if (base == nextSeqNum) {
                expectedTimer = sim.currentTime + timeoutInterval;
                sim.addEvent(expectedTimer, EV_TIMEOUT, base);
            }
            nextSeqNum++;
        }
    }

    void receiveAck(Simulator& sim, UnreliableChannel& channel, int ackNum, bool corrupt, int maxPackets) {
        if (!corrupt && ackNum >= base) {
            base = ackNum + 1;
            if (base == nextSeqNum) {
                expectedTimer = 0;
            } else {
                expectedTimer = sim.currentTime + timeoutInterval;
                sim.addEvent(expectedTimer, EV_TIMEOUT, base);
            }
            sendPackets(sim, channel, maxPackets);
        }
    }

    void handleTimeout(Simulator& sim, UnreliableChannel& channel, int maxPackets, double evTime) {
        if (expectedTimer == 0 || evTime < expectedTimer - 0.0001 || evTime > expectedTimer + 0.0001) return;
        
        expectedTimer = sim.currentTime + timeoutInterval;
        sim.addEvent(expectedTimer, EV_TIMEOUT, base);
        
        for (int i = base; i < nextSeqNum; i++) {
            totalSent++;
            retransmissions++;
            channel.sendPacket(sim, i);
        }
    }
};

class GbnReceiver {
public:
    int expectedSeq;
    int delivered;

    GbnReceiver() : expectedSeq(0), delivered(0) {}

    void receivePacket(Simulator& sim, UnreliableChannel& channel, int seqNum, bool corrupt) {
        if (!corrupt && seqNum == expectedSeq) {
            delivered++;
            channel.sendAck(sim, expectedSeq);
            expectedSeq++;
        } else {
            if (expectedSeq > 0) {
                channel.sendAck(sim, expectedSeq - 1);
            }
        }
    }
};

struct GbnResult {
    int totalSent;
    int retransmissions;
    double windowUtilization;
    double throughput;
    double avgDelay;
};

GbnResult runGBN(int numPackets, double loss, double corrupt, int window) {
    Simulator sim;
    UnreliableChannel channel(loss, corrupt, 10.0, 8.0);
    GbnSender sender(window, 50.0);
    GbnReceiver receiver;
    
    double totalFlightArea = 0;

    sim.addEvent(0.0, EV_APP_SEND);

    while (!sim.events.empty() && receiver.delivered < numPackets) {
        Event ev = sim.events.top();
        sim.events.pop();
        
        if (ev.time < sim.currentTime) continue;
        
        totalFlightArea += (sender.nextSeqNum - sender.base) * (ev.time - sim.currentTime);
        sim.currentTime = ev.time;

        if (ev.type == EV_APP_SEND) {
            sender.sendPackets(sim, channel, numPackets);
        }
        else if (ev.type == EV_PKT_ARRIVE) {
            receiver.receivePacket(sim, channel, ev.seqNum, ev.isCorrupt);
        }
        else if (ev.type == EV_ACK_ARRIVE) {
            sender.receiveAck(sim, channel, ev.ackNum, ev.isCorrupt, numPackets);
        }
        else if (ev.type == EV_TIMEOUT) {
            sender.handleTimeout(sim, channel, numPackets, ev.time);
        }
    }

    GbnResult res;
    res.totalSent = sender.totalSent;
    res.retransmissions = sender.retransmissions;
    res.throughput = (numPackets * 8000.0) / (sim.currentTime / 1000.0);
    res.avgDelay = sim.currentTime / numPackets;
    res.windowUtilization = (totalFlightArea / sim.currentTime) / window;

    return res;
}
}
#ifndef INTEGRATION_BUILD
int main() {
    int numPackets = 200;
    
    cout << "Go-Back-N (GBN) Simulation\n";
    cout << "Packets: " << numPackets << ", Link: 1Mbps, Delay: 10ms\n\n";
    
    cout << "1. Impact of Window Size (Fixed Error Rate: 10%)\n";
    cout << "----------------------------------------------------------------------\n";
    cout << " Window | Total Sent | Retrans | Throughput(bps) | Avg Delay | Util \n";
    cout << "----------------------------------------------------------------------\n";
    
    int windows[] = {1, 2, 4, 8, 16};
    for (int w : windows) {
        double err = 0.10;
        gbn::GbnResult r = gbn::runGBN(numPackets, err*0.66, err*0.34, w);
        cout << fixed << setprecision(2);
        cout << setw(7) << w << " | "
             << setw(10) << r.totalSent << " | "
             << setw(7) << r.retransmissions << " | "
             << setw(15) << r.throughput << " | "
             << setw(7) << r.avgDelay << "ms | "
             << setw(4) << (r.windowUtilization * 100) << "%\n";
    }
    
    cout << "\n2. Impact of Error Rate (Fixed Window Size: 4)\n";
    cout << "----------------------------------------------------------------------\n";
    cout << " Error  | Total Sent | Retrans | Throughput(bps) | Avg Delay | Util \n";
    cout << "----------------------------------------------------------------------\n";
    
    double errors[] = {0.0, 0.05, 0.10, 0.20, 0.30};
    for (double err : errors) {
        gbn::GbnResult r = gbn::runGBN(numPackets, err*0.66, err*0.34, 4);
        cout << fixed << setprecision(2);
        cout << setw(6) << (err*100) << "% | "
             << setw(10) << r.totalSent << " | "
             << setw(7) << r.retransmissions << " | "
             << setw(15) << r.throughput << " | "
             << setw(7) << r.avgDelay << "ms | "
             << setw(4) << (r.windowUtilization * 100) << "%\n";
    }
    
    return 0;
}
#endif