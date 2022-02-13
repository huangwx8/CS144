#ifndef SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
#define SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH

#include "ethernet_frame.hh"
#include "tcp_over_ip.hh"
#include "tun.hh"

#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

class TimeLimitEventCenter {
  public:
    TimeLimitEventCenter() : _expirations(), _ref_counts() {}

    std::vector<uint32_t> tick_dispatch(const size_t world_time) {
        std::vector<uint32_t> expired_events;
        while (!_expirations.empty()) {
            if (_expirations[0].expire <= world_time) {
                if (--_ref_counts[_expirations[0].event_id] == 0) {
                    expired_events.push_back(_expirations[0].event_id);
                    _ref_counts.erase(_expirations[0].event_id);
                }
                _expirations.pop_front();
            } else {
                break;
            }
        }
        return expired_events;
    }

    void regist_event(const size_t world_time, uint32_t event_id) {
        _expirations.push_back({world_time + _time_limit, event_id});
        auto it = _ref_counts.find(event_id);
        if (it != _ref_counts.end()) {
            it->second += 1;
        } else {
            _ref_counts[event_id] = 1;
        }
    }

  private:
    struct EventExpiration {
        size_t expire;
        uint32_t event_id;
    };

    std::deque<EventExpiration> _expirations;

    std::unordered_map<uint32_t, uint32_t> _ref_counts;

    const size_t _time_limit{30000};
};

//! \brief A "network interface" that connects IP (the internet layer, or network layer)
//! with Ethernet (the network access layer, or link layer).

//! This module is the lowest layer of a TCP/IP stack
//! (connecting IP with the lower-layer network protocol,
//! e.g. Ethernet). But the same module is also used repeatedly
//! as part of a router: a router generally has many network
//! interfaces, and the router's job is to route Internet datagrams
//! between the different interfaces.

//! The network interface translates datagrams (coming from the
//! "customer," e.g. a TCP/IP stack or router) into Ethernet
//! frames. To fill in the Ethernet destination address, it looks up
//! the Ethernet address of the next IP hop of each datagram, making
//! requests with the [Address Resolution Protocol](\ref rfc::rfc826).
//! In the opposite direction, the network interface accepts Ethernet
//! frames, checks if they are intended for it, and if so, processes
//! the the payload depending on its type. If it's an IPv4 datagram,
//! the network interface passes it up the stack. If it's an ARP
//! request or reply, the network interface processes the frame
//! and learns or replies as necessary.
class NetworkInterface {
  private:
    //! Ethernet (known as hardware, network-access-layer, or link-layer) address of the interface
    EthernetAddress _ethernet_address;

    //! IP (known as internet-layer or network-layer) address of the interface
    Address _ip_address;

    //! outbound queue of Ethernet frames that the NetworkInterface wants sent
    std::queue<EthernetFrame> _frames_out{};

    size_t _ms_since_first_tick{0};

    std::unordered_map<uint32_t, EthernetAddress> _arp_cache;

    std::unordered_map<uint32_t, size_t> _last_query_timestamps;

    std::unordered_map<uint32_t, std::vector<InternetDatagram>> _waiting_datagrams;

    TimeLimitEventCenter _time_limit_event_center;

  public:
    //! \brief Construct a network interface with given Ethernet (network-access-layer) and IP (internet-layer) addresses
    NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address);

    //! \brief Access queue of Ethernet frames awaiting transmission
    std::queue<EthernetFrame> &frames_out() { return _frames_out; }

    //! \brief Sends an IPv4 datagram, encapsulated in an Ethernet frame (if it knows the Ethernet destination address).

    //! Will need to use [ARP](\ref rfc::rfc826) to look up the Ethernet destination address for the next hop
    //! ("Sending" is accomplished by pushing the frame onto the frames_out queue.)
    void send_datagram(const InternetDatagram &dgram, const Address &next_hop);

    //! \brief Receives an Ethernet frame and responds appropriately.

    //! If type is IPv4, returns the datagram.
    //! If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
    //! If type is ARP reply, learn a mapping from the "sender" fields.
    std::optional<InternetDatagram> recv_frame(const EthernetFrame &frame);

    //! \brief Called periodically when time elapses
    void tick(const size_t ms_since_last_tick);
};

#endif  // SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
