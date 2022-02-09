#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPSender::send_segment(TCPSegment seg) {
    if (seg.length_in_sequence_space() == 0) { // don't send empty packet
        return;
    }
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg); // push to sender buffer
    _unacknowledged_segments.push_back(seg); // start track
    if (_expire == UINT64_MAX)
        _expire = _ms_since_first_tick + _retransmission_timeout; // start timer
    _next_seqno += seg.length_in_sequence_space(); // update sender seqno
}

bool TCPSender::fully_acknowledged(const TCPSegment& segment, const WrappingInt32 ackno) {
    uint64_t end64 = unwrap(segment.header().seqno, _isn, _next_seqno) + segment.length_in_sequence_space();
    uint64_t ack64 = unwrap(ackno, _isn, _next_seqno);
    return ack64 >= end64;
}

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _retransmission_timeout(retx_timeout)
    , _stream(capacity)
    , _unacknowledged_segments() {}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _last_ackno; }

void TCPSender::fill_window() {
    TCPSegment seg;

    if (stream_in().eof() 
    && next_seqno_absolute() == stream_in().bytes_written() + 2) { // FIN SENT / FIN ACKED
        return;
    }

    if (_window_size > bytes_in_flight() && _next_seqno == 0) { // CLOSED
        seg.header().syn = true;
        send_segment(seg);
        return;
    }

    uint64_t window_size = _window_size == 0 ? 1 : _window_size;

    if (window_size > bytes_in_flight()) { 
        uint64_t fill_size;
        do {
            fill_size = window_size - bytes_in_flight();
            
            seg = TCPSegment();

            if (!_stream.buffer_empty()) { // read as more as possible
                seg.payload() = _stream.read(std::min(static_cast<size_t>(fill_size), TCPConfig::MAX_PAYLOAD_SIZE));
            }

            if (_stream.eof() && seg.length_in_sequence_space() < fill_size) { // mark fin
                seg.header().fin = true;
            }
            
            send_segment(seg);
        } while (window_size > bytes_in_flight() && !_stream.buffer_empty());
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    TCPSegment segment;
    uint64_t ack64 = unwrap(ackno, _isn, _next_seqno);
    
    if (_next_seqno == 0) { // CLOSED
        return;
    }
    else if (_next_seqno > 0 && _next_seqno == bytes_in_flight()) { // SYN SENT
        if (ack64 != 1) {
            return;
        }
    }
    else if ((next_seqno_absolute() > bytes_in_flight() && !stream_in().eof()) || 
            (stream_in().eof() && next_seqno_absolute() < stream_in().bytes_written() + 2)) { // SYN ACKED
        if (ack64 < _last_ackno) { // ignore old ack packet
            return;
        }
        if (ack64 == _last_ackno && window_size <= _window_size) { // ignore old ack packet
            return;
        }
        if (ack64 > _next_seqno) { // ignore exceed ack packet
            return;
        }
    }
    else if (stream_in().eof() 
    && next_seqno_absolute() == stream_in().bytes_written() + 2 
    && bytes_in_flight() > 0) { // FIN SENT
        if (ack64 < _last_ackno) { // ignore old ack packet
            return;
        }
        if (ack64 == _last_ackno && window_size <= _window_size) { // ignore old ack packet
            return;
        }
        if (ack64 > _next_seqno) { // ignore exceed ack packet
            return;
        }
    }
    else if (stream_in().eof()
    && next_seqno_absolute() == stream_in().bytes_written() + 2
    && bytes_in_flight() == 0) { // FIN ACKED
        return;
    }

    _last_ackno = ack64;

    _window_size = window_size;

    _retransmission_timeout = _initial_retransmission_timeout; // reset timeout
    
    _consecutive_retransmissions = 0; // reset retransmit counter
    
    // untrack acknowledged segments
    while (!_unacknowledged_segments.empty()
        && fully_acknowledged(_unacknowledged_segments[0], ackno)) {
        _unacknowledged_segments.pop_front();
    }
    if (_unacknowledged_segments.empty()) {
        _expire = UINT64_MAX; // stop timer
    }
    else {
        _expire = _ms_since_first_tick + _retransmission_timeout; // reset timer
    }

    // if (window_size > 0) { // fill the window again if new space has opened up
    //     fill_window();
    // }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _ms_since_first_tick += ms_since_last_tick;

    if (_expire > _ms_since_first_tick) { // not expired
        return;
    }

    if (!_unacknowledged_segments.empty()) {
        _segments_out.push(_unacknowledged_segments[0]); // retransmit earliest lost packet
        if (_window_size != 0) {
            _retransmission_timeout *= 2;  // double the timeout
            _consecutive_retransmissions++; // increment retransmit counter
        }
        _expire = _ms_since_first_tick + _retransmission_timeout; // reset timer
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
}
