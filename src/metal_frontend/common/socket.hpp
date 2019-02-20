#pragma once

#include <sys/socket.h>

#include <metal_frontend/messages/message_header.hpp>
#include <Messages.pb.h>

#define ASSIGN_MESSAGE_TYPE(Type, Message) \
  template<> \
  void Socket::send_message<Type>(Message &message) { \
    _send_message(Type, message); \
  }

namespace metal {

class Socket {
 public:
  explicit Socket(int fd) : _fd(fd) {}

  template<message_type TType, typename TMessage>
  void send_message(TMessage &message);

 protected:
  template<typename TMessage>
  void _send_message(message_type type, TMessage &message);

  int _fd;
};

template<typename TMessage>
void Socket::_send_message(message_type type, TMessage &message) {
  char buf[message.ByteSize()];
  message.SerializeToArray(buf, message.ByteSize());

  MessageHeader header(type, message.ByteSize());
  header.sendHeader(_fd);
  send(_fd, buf, message.ByteSize(), 0);
}

ASSIGN_MESSAGE_TYPE(message_type::SERVER_ACCEPT_AGENT, ServerAcceptAgent)

} // namespace metal
