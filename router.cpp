#include <iostream>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <cassert>
#include <memory>
#include <string>
#include <iostream>
#include <chrono>
#include <set>
#include <csignal>
#include <map>
#include <memory>
#include <vector>
#include <bitset>

#define DEBUG 0
#define NETWORK(ip,mask) (ip & (~(UINT32_MAX << mask)))
#define BROADCAST(ip,mask) (ip | (UINT32_MAX << mask))

const uint32_t INF = (unsigned  int)(((long)1 << 32)-1);
const uint32_t SMALL_INF = 15;
const uint32_t PORT = 54321;

std::string ipToString(uint32_t ip){
    char buf[40];
    inet_ntop(AF_INET, &ip, buf, 40);
    return std::string(buf);
}

void cidrToIpAndMask(std::string cidr, in_addr  &ip, uint8_t &mask){
    uint32_t slashPos = cidr.find('/');
    std::string ip_str = cidr.substr(0, slashPos);
    if (inet_pton(AF_INET,ip_str.c_str(),&ip) <= 0) {
        std::cout << "inet_pton error: [%s]\n" << strerror(errno);
        exit(EXIT_FAILURE);
    }
    mask = atoi(cidr.substr(slashPos + 1).c_str());
}

class Endpoint{
public:
    std::string ip_str;
    in_addr  ip;
    uint8_t   mask;
    std::chrono::high_resolution_clock::time_point lastActive;
    bool isDirect;
    in_addr via; /// jeżeli isDirect = true, a jest ustawione via, oznacza to że droga jest pośrednia
    std::string via_str; /// jeżeli isDirect = true, a jest ustawione via, oznacza to że droga jest pośrednia
    uint32_t distance = INF;
    uint32_t directDistance; /// tylko dla sasiadow chcemy pamietac poczatkowe ustawienie odleglosci


    Endpoint(){}

    Endpoint(in_addr  ip, uint8_t mask, uint32_t dist){
        ip.s_addr = NETWORK(ip.s_addr, mask);
        this->mask = mask;
        this->ip = ip;
        ip_str = ipToString(ip.s_addr);
        distance = directDistance = dist;
        isDirect = true;
        lastActive = std::chrono::high_resolution_clock::now() - std::chrono::duration<int>(3600);
    }

    void print(){
        std::cout << ip_str << " ";
        if(distance == INF)
            std::cout << "unreachable ";
        else
            std::cout << "distance " << distance << " ";

        if(isDirect)
            std::cout << "connected directly" << std::endl;
        else
            std::cout << "via " << via_str << std::endl;
    }

};

class VectorEndpoints{
    std::vector<std::shared_ptr<Endpoint> > endpoints;
    int sendfd;
    int recfd;
    std::set<uint32_t> myIPs;

    int _findByIp(uint32_t ip){
        int i = 0;
        for (const auto &network : endpoints) {
            if(network->ip.s_addr == NETWORK(ip, network->mask)){
                return i;
            }
            i++;
        }
        return -1;
    }
    int _findByIpAndMask(uint32_t ip, uint8_t mask){
        int i = 0;
        for (const auto &network : endpoints) {
            if(network->ip.s_addr == ip && network->mask == mask){
                return i;
            }
            i++;
        }
        return -1;
    }
    bool _isIpMine(uint32_t ip){
        return myIPs.count(ip);
    }
public:
    VectorEndpoints(){
        sendfd = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if (sendfd < 0) {
            std::cout << "socket error: [%s]\n" <<  strerror(errno) << std::endl;
            exit(1);
        }
        int broadcast = 1;
        if (setsockopt(sendfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
            std::cout << "setsockopt error: [%s]\n" <<  strerror(errno) << std::endl;
            exit(1);
        }


        recfd = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if (recfd < 0) {
            std::cout << "socket error: [%s]\n" <<  strerror(errno) << std::endl;
            exit(1);
        }
        broadcast = 1;
        if (setsockopt(recfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
            std::cout << "setsockopt error: [%s]\n" <<  strerror(errno) << std::endl;
            exit(1);
        }

        struct sockaddr_in recaddr;
        memset(&recaddr,0,sizeof(recaddr));
        recaddr.sin_family = AF_INET;
        recaddr.sin_addr.s_addr = INADDR_ANY;
        recaddr.sin_port = htons(PORT);
        if (bind(recfd,(struct sockaddr *)&recaddr,sizeof(recaddr)) < 0) {
            std::cout << "bind error: [%s]\n" <<  strerror(errno) << std::endl;
            exit(1);
        }
    }
    void load(){
        std::cout << "Podaj dane wektora odleglosci:" << std::endl;
        int n;
        std::cin >> n;

        std::string tmp, cidr;
        uint32_t directDist;
        for (int i = 0; i<n; i++) {
            std::cin >> cidr >> tmp >> directDist;
            in_addr ip;
            uint8_t mask;
            cidrToIpAndMask(cidr, ip, mask);
            myIPs.insert(ip.s_addr);
            auto endpoint = std::make_shared<Endpoint>(ip, mask, directDist);
            endpoints.push_back(std::move(endpoint));
        }
        std::cout << std::endl;
    }
    void print(){
        for(const auto &endpoint : endpoints){
            endpoint->print();
        }
        std::cout << std::endl;
    }

    void sendNetworks(){
        for(const auto &endpoint : endpoints){
            if(!endpoint->isDirect)
                continue;
            uint32_t broadcast_ip = BROADCAST(endpoint->ip.s_addr, endpoint->mask);

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(PORT);
            addr.sin_addr.s_addr = broadcast_ip;
            for (const auto &network : endpoints) {
                if(&endpoint != &network && network->distance == INF)
                    continue;
                char buffer[9] = {0};
                *(uint32_t*)buffer = network->ip.s_addr;
                buffer[4] = network->mask;
                *(uint32_t*)((long)buffer + 5) = htonl(network->distance);

                if(sendto(sendfd, buffer, 9, 0, (struct sockaddr*)&addr, sizeof(addr)) <= 0) {
                    endpoint->distance = INF;
                    break;
                }
//                if(endpoint->directDistance < endpoint->distance){
//                    endpoint->distance = endpoint->directDistance;
//                    endpoint->via_str = "";
//                    endpoint->via.s_addr = 0;
//                }

//                endpoint->lastActive = std::chrono::high_resolution_clock::now();
            }
        }
    }

    void addNetwork(uint32_t ip, uint8_t mask, uint32_t distance, uint32_t viaIp){
        ip = NETWORK(ip, mask);
        int idxNetwork = _findByIpAndMask(ip, mask);
        int idxVia = _findByIp(viaIp);
        if(idxVia == -1) /// distance to nieskonczonosc
            return;
        auto via = endpoints[idxVia];

        via->lastActive = std::chrono::high_resolution_clock::now();
        if(via->distance > via->directDistance && via->isDirect){
            via->distance = via->directDistance;
            via->via.s_addr = 0;
            via->via_str = "";
        }
        long newDistance = (long)distance + via->distance;
        if(DEBUG)
            std::cout << "FROM: " << ipToString(viaIp) << " to " << ipToString(ip) << " distance is " << distance << "\t New dist: " << newDistance << std::endl;


        if(newDistance >= SMALL_INF)
            newDistance = INF;

        if(idxNetwork == -1){
            if(newDistance == INF)
                return;
            auto newNetwork = std::make_shared<Endpoint>();
            newNetwork->distance = newDistance;
            newNetwork->ip.s_addr = ip;
            newNetwork->ip_str = ipToString(ip);
            newNetwork->mask = mask;
            newNetwork->lastActive = std::chrono::high_resolution_clock::now();
            newNetwork->via.s_addr = viaIp;
            newNetwork->via_str = ipToString(viaIp);
            newNetwork->isDirect = false;
            endpoints.push_back(newNetwork);
        }
        else{
            auto network = endpoints[idxNetwork];
            if(network->via.s_addr == viaIp){
                network->distance = newDistance;
                if(network->distance != INF)
                    network->lastActive = std::chrono::high_resolution_clock::now();

            }
//            std::cout << "New distance: " << newDistance << " \t Old distance: " << network->distance << std::endl;
            if(newDistance < network->distance){
                network->via.s_addr = viaIp;
                network->via_str = ipToString(viaIp);
                network->distance = newDistance;
                if(network->distance != INF)
                    network->lastActive = std::chrono::high_resolution_clock::now();
            }
        }
    }
    void removeUnreachable(uint32_t timeSec){
        auto now = std::chrono::high_resolution_clock::now();
        std::vector<int> toRemove;
        for (int i = 0; i < endpoints.size(); i++) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - endpoints[i]->lastActive).count();
            if(elapsed > timeSec){
                endpoints[i]->distance = INF;
                if(!endpoints[i]->isDirect)
                    toRemove.push_back(i);
            }
        }
        for(int i = toRemove.size()-1; i>=0; i--){
            endpoints.erase(endpoints.begin()+toRemove[i]);
        }
    }
    void receiveFor(timeval &timeout){
        fd_set dsc;
        FD_ZERO(&dsc);
        FD_SET(recfd, &dsc);
        while (select(recfd+1, &dsc, NULL, NULL, &timeout)) {
            struct sockaddr_in 	sender;
            socklen_t 			sender_len = sizeof(sender);
            uint8_t 			data[9] = {};

            ssize_t p_len = recvfrom(
                    recfd,
                    data,
                    9,
                    MSG_DONTWAIT,
                    (struct sockaddr*)&sender,
                    &sender_len);

            if (p_len != 9)
                continue;

            uint32_t target_ip = *(uint32_t*)data;
            uint8_t  target_mask = data[4];
            uint32_t target_dist = ntohl(*(uint32_t*)(data + 5));
            uint32_t sender_ip = sender.sin_addr.s_addr;
            if(_isIpMine(sender_ip))
                continue;
            addNetwork(target_ip, target_mask, target_dist, sender_ip);
        }
    }

};

int main() {
    VectorEndpoints vector;
    vector.load();
    while(true){
        timeval time;
        time.tv_sec = 25;
        time.tv_usec = 0;

        vector.receiveFor(time);
        vector.removeUnreachable(60);
        vector.sendNetworks();
        vector.print();
    }
    return 0;
}
