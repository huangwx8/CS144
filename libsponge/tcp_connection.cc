#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPConnection::send_control_segment(ETCPControlType Type) {
    _sender.send_empty_segment();
    TCPSegment seg = std::move(_sender.segments_out().front());
    _sender.segments_out().pop();

    switch (Type) {
    case ETCPControlType::Synchronous:
        seg.header().syn = true;
        break;
    case ETCPControlType::Acknowledgement:
        seg.header().ack = true;
        seg.header().ackno = _receiver.ackno().value();
        seg.header().win = _receiver.window_size();
        break;
    case ETCPControlType::KeepAlive:
        break;
    case ETCPControlType::Reset:
        seg.header().rst = true;
        break;
    default:
        break;
    }

    _segments_out.push(seg);
}

void TCPConnection::send_queued_segments() {
    while (!_sender.segments_out().empty()) {
        auto seg = std::move(_sender.segments_out().front());
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
        _segments_out.push(std::move(seg));
    }
}

bool TCPConnection::local_ended() const {
    return
    // Prereq #1 The inbound stream has been fully assembled and has ended.
    (_receiver.stream_out().input_ended() && _receiver.unassembled_bytes() == 0)
    // Prereq #2 The outbound stream has been ended by the local application and fully sent (including
    // the fact that it ended, i.e. a segment with fin ) to the remote peer.
    && (_sender.stream_in().eof()) 
    // Prereq #3 The outbound stream has been fully acknowledged by the remote peer.
    && (_sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2 && bytes_in_flight() == 0);
}

void TCPConnection::shutdown() {
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
}

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const {
    if (!_receiver.ackno().has_value()) { // haven't received any segment
        return 0;
    }
    return _ms_since_first_tick - _ms_last_segment_received;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    _ms_last_segment_received = _ms_since_first_tick;

    if (seg.header().rst) { // reset, unclean close
        shutdown();
        return;
    }

    if (!_receiver.ackno().has_value() && !seg.header().syn) { // listen
        return; // refuse non syn segment
    }

    if (_receiver.ackno().has_value() 
    && (seg.length_in_sequence_space() == 0)
    && seg.header().seqno == _receiver.ackno().value() - 1) { // keep-alive 
        send_control_segment(ETCPControlType::KeepAlive);
        return;
    }
    
    // notify receiver
    _receiver.segment_received(seg);

    // fin recv before outgoing stream eof, don't linger
    if (_receiver.stream_out().input_ended() && !_sender.stream_in().eof()) {
        _linger_after_streams_finish = false;
    }

    // notify sender if ack flag is set
    _sender.ack_received(seg.header().ackno, seg.header().win);
    _sender.fill_window(); // try fill window

    if (!_sender.segments_out().empty()) { // send data, it will carry an ack
        send_queued_segments();
    }
    else if (seg.length_in_sequence_space() > 0) { // send a pure ack segment if no hitchhike 
        send_control_segment(ETCPControlType::Acknowledgement);
    }
}

bool TCPConnection::active() const {
    // shutdown
    if (_sender.stream_in().error() && _receiver.stream_out().error()) {
        return false;
    }
    if (local_ended())
    {
        if (_linger_after_streams_finish) {
            return true;
        }
        else {
            return false;
        }
    }
    return true;
}

size_t TCPConnection::write(const string &data) {
    size_t size = _sender.stream_in().write(data);
    _sender.fill_window();
    send_queued_segments();
    return size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _ms_since_first_tick += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        send_control_segment(ETCPControlType::Reset);
        shutdown();
        return;
    }
    
    send_queued_segments();

    if (local_ended() && _linger_after_streams_finish) {
        if (time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {
            _linger_after_streams_finish = false;
        }
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window(); // fin
    send_queued_segments();
}

void TCPConnection::connect() {
    _sender.fill_window(); // default window size is 1, sender will generate exactly a syn to the peer
    send_queued_segments();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            send_control_segment(ETCPControlType::Reset);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
