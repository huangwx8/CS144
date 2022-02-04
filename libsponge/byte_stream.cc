#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : pipe(), _capacity(capacity), _nbytes_read(0), _nbytes_written(0), _end(false), _eof(false) {}

size_t ByteStream::write(const string &data) {
    size_t i = 0;
    for (; i < data.size() && remaining_capacity() > 0; i++, _nbytes_written++) {
        pipe.push_back(data[i]);
    }
    return i;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    string out;
    for (size_t i = 0; i < len && i < pipe.size(); i++) {
        out += pipe[i];
    }
    return out;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t i = 0;
    for (; i < len && !buffer_empty(); i++, _nbytes_read++) {
        pipe.pop_front();
    }
    if (input_ended() && buffer_empty())
        _eof = true;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string out = peek_output(len);
    pop_output(len);
    return out;
}

void ByteStream::end_input() {
    _end = true;
    if (buffer_empty())
        _eof = true;
}

bool ByteStream::input_ended() const { return _end; }

size_t ByteStream::buffer_size() const { return pipe.size(); }

bool ByteStream::buffer_empty() const { return pipe.empty(); }

bool ByteStream::eof() const { return _eof; }

size_t ByteStream::bytes_written() const { return _nbytes_written; }

size_t ByteStream::bytes_read() const { return _nbytes_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - pipe.size(); }
