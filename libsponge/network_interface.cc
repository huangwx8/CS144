#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address)
    , _ip_address(ip_address)
    , _arp_cache()
    , _last_query_timestamps()
    , _waiting_datagrams()
    , _time_limit_event_center() {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    EthernetFrame frame;

    auto it = _arp_cache.find(next_hop_ip);
    if (it != _arp_cache.end()) {
        EthernetAddress dst = it->second;

        frame.header().dst = dst;
        frame.header().src = _ethernet_address;
        frame.header().type = EthernetHeader::TYPE_IPv4;

        frame.payload() = dgram.serialize();

        _frames_out.push(frame);
    } else {
        if (_last_query_timestamps.find(next_hop_ip) == _last_query_timestamps.end()
            || _ms_since_first_tick - _last_query_timestamps[next_hop_ip] >= 5000) {  // broadcast arp requests
            ARPMessage msg;

            msg.sender_ethernet_address = _ethernet_address;
            msg.sender_ip_address = _ip_address.ipv4_numeric();
            msg.target_ethernet_address = {0,0,0,0,0,0};
            msg.target_ip_address = next_hop_ip;
            msg.opcode = ARPMessage::OPCODE_REQUEST;

            frame.header().dst = ETHERNET_BROADCAST;
            frame.header().src = _ethernet_address;
            frame.header().type = EthernetHeader::TYPE_ARP;

            frame.payload() = msg.serialize();

            _frames_out.push(frame);

            _last_query_timestamps[next_hop_ip] = _ms_since_first_tick;
        }

        // queue the IP datagram, until get arp response
        if (_waiting_datagrams.find(next_hop_ip) == _waiting_datagrams.end()) {
            _waiting_datagrams[next_hop_ip] = {};
        }
        _waiting_datagrams[next_hop_ip].push_back(dgram);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // filter
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return {};
    }

    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram data;
        if (data.parse(frame.payload()) == ParseResult::NoError) {
            return data;
        }
    } else if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage msg;
        if (msg.parse(frame.payload()) != ParseResult::NoError) {
            goto refuse;
        }
        if (msg.target_ip_address != _ip_address.ipv4_numeric()) {
            goto refuse;
        }
        // record
        _arp_cache[msg.sender_ip_address] = msg.sender_ethernet_address;
        // expires in 30 sec
        _time_limit_event_center.regist_event(_ms_since_first_tick, msg.sender_ip_address);
        // notify corresponding queued datagram
        auto it = _waiting_datagrams.find(msg.sender_ip_address);
        if (it != _waiting_datagrams.end()) {
            for (auto &dgram : _waiting_datagrams[msg.sender_ip_address]) {
                send_datagram(dgram, Address::from_ipv4_numeric(msg.sender_ip_address));
            }
            _waiting_datagrams.erase(msg.sender_ip_address);
        }
        // send arp reply
        if (msg.opcode == ARPMessage::OPCODE_REQUEST) {
            EthernetFrame reply_frame;
            ARPMessage reply_msg;

            reply_msg.target_ethernet_address = msg.sender_ethernet_address;
            reply_msg.target_ip_address = msg.sender_ip_address;
            reply_msg.sender_ethernet_address = _ethernet_address;
            reply_msg.sender_ip_address = _ip_address.ipv4_numeric();
            reply_msg.opcode = ARPMessage::OPCODE_REPLY;

            reply_frame.header().dst = reply_msg.target_ethernet_address;
            reply_frame.header().src = _ethernet_address;
            reply_frame.header().type = EthernetHeader::TYPE_ARP;

            reply_frame.payload() = reply_msg.serialize();

            _frames_out.push(reply_frame);
        }
    }

refuse:
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _ms_since_first_tick += ms_since_last_tick;

    auto evictees = _time_limit_event_center.tick_dispatch(_ms_since_first_tick);
    for (uint32_t ip_addr : evictees) {
        _arp_cache.erase(ip_addr);
    }
}
