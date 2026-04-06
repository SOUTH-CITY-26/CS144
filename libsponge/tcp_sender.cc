#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) 
    , _retransmission_timeout(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    // 已发送FIN，不能再发送新字节
    if (_fin_sent) {
        return;
    }
    // 如果窗口大小为0，发送空间为1
    size_t sending_space = _ackno + (_window_size != 0 ? _window_size : 1) - _next_seqno;
    // 有发送空间且未发送FIN
    while (sending_space > 0 && !_fin_sent) {
        TCPSegment seg;
        TCPHeader &header = seg.header();
        if (_next_seqno == 0) { // 发送第一个字节，设置SYN
            header.syn = true;
            --sending_space;
        }
        header.seqno = wrap(_next_seqno, _isn);
        Buffer &buffer = seg.payload();
        buffer = stream_in().read(min(sending_space, TCPConfig::MAX_PAYLOAD_SIZE));
        // 如果添加FIN会超过窗口大小，就不添加
        sending_space -= buffer.size();
        if (stream_in().eof() && sending_space > 0) {
            header.fin = true;
            --sending_space;
            _fin_sent = true;
        }

        size_t len = seg.length_in_sequence_space();
        if (len == 0) {
            return;
        }

        segments_out().emplace(seg);
        if (_timer_running == false) {
            _timer = 0;
            _timer_running = true;
        }
        _segments_outstanding.emplace(seg);
        _next_seqno += len;
        _bytes_in_flight += len;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
//! \returns `false` if the ackno appears invalid (acknowledges something the TCPSender hasn't sent yet)
bool TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    _ackno = unwrap(ackno, _isn, _next_seqno);
    if (_ackno > _next_seqno) {
        return false;           // 还未发送到这个ackno
    }
    _window_size = window_size;

    bool has_new = false;       // 标识是否有新的ack被接收
    while (!_segments_outstanding.empty()) {
        TCPSegment seg = _segments_outstanding.front();
        size_t len = seg.length_in_sequence_space();
        uint64_t seqno = unwrap(seg.header().seqno, _isn, _next_seqno);
        // 当前报文还未被完全ack，不能从未确认队列中移除
        if (seqno + len > _ackno) {
            break;
        }
        _segments_outstanding.pop();
        _bytes_in_flight -= len;
        has_new = true;
    }
    fill_window();
    if (has_new) {
        _retransmission_timeout = _initial_retransmission_timeout;
        if (!_segments_outstanding.empty()) {
            _timer = 0;
            _timer_running = true;
        } else {
            _timer_running = false;
        }
        _consec_retrans = 0;
    }
    return true;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_timer_running) {
        return;
    }

    _timer += ms_since_last_tick;

    if (_timer >= _retransmission_timeout) {
        segments_out().push(_segments_outstanding.front());

        if (_window_size != 0) {
            _consec_retrans++;
            _retransmission_timeout *= 2;
        }

        _timer = 0;
        _timer_running = true;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consec_retrans; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    segments_out().emplace(seg);
}
