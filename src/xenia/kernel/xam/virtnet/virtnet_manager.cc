/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/virtnet/virtnet_manager.h"
#include <WinSock2.h>
#include <cstdio>
#include <ws2tcpip.h>
#include <cstring>
#include <sstream>

namespace xe {
namespace kernel {
#define MSGBOX(x)                                                 \
  {                                                               \
    std::wstringstream oss;                                       \
    oss << x;                                                     \
    LPCWSTR str = oss.str().c_str();                              \
    MessageBox(NULL, str, L"Msg Title", MB_OK | MB_ICONQUESTION); \
  }


void VirtNetManager::init() {
  // Start WSA
  WSADATA wsaData;
  int iResult;

  iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    MSGBOX(L"WSAStartup failed: " << iResult);
    return;
  }

  // Configure address hints
  struct addrinfo* result = NULL,* ptr = NULL, hints;
  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  // Resolve the server address and port
  iResult = getaddrinfo("127.0.0.1", "27015", &hints, &result);
  if (iResult != 0) {
    MSGBOX(L"Failed to get VirtNet address info: " << iResult);
    return;
  }

  // Attempt to connect to an address until one succeeds
  socket_ = (int)INVALID_SOCKET;
  for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
    // Create a SOCKET for connecting to server
    socket_ = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if (socket_ == INVALID_SOCKET) {
      MSGBOX("socket failed with error:" << WSAGetLastError());
      return;
    }

    // Connect to server.
    iResult = connect(socket_, ptr->ai_addr, (int)ptr->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
      closesocket(socket_);
      socket_ = (int)INVALID_SOCKET;
      continue;
    }
    break;
  }

  // Cleanup address lookup info
  freeaddrinfo(result);

  // Check we have connected
  if (socket_ == INVALID_SOCKET) {
    MSGBOX("Unable to connect to VirtNet!");
    return;
  }

  // Start threads
  read_thread_ = new std::thread([this] { handle_reads(); });
  write_thread_ = new std::thread([this] { handle_writes(); });

  MSGBOX("Connected to VirtNet");
}

void VirtNetManager::send_packet(VirtNetMessage msg) {
  std::unique_lock<std::mutex> lock(send_queue_mutex_);
  send_queue_.push(msg);

  lock.unlock();
  send_queue_cv_.notify_all();
}

VirtNetMessage VirtNetManager::send_packet_with_result(VirtNetMessage msg) {
  // Allocate response id
  uint32_t target_id = last_response_id_++;
  msg.set_is_response(false);
  msg.set_response_target(target_id);

  // Allocate response promise
  std::promise<VirtNetMessage> response_promise;
  response_map_[target_id] = &response_promise;

  // Send message
  send_packet(msg);

  // Await result
  VirtNetMessage response = response_promise.get_future().get();

  // Remove response promise
  response_map_.erase(target_id);

  return response;
}


VirtNetMessage VirtNetManager::read_packet_direct() {
  char sizeBuffer[4];

  int sizeResult = recv(socket_, sizeBuffer, 4, MSG_WAITALL);

  if (sizeResult != 4) {
    MSGBOX("VirtNet: Failed to recv packet header, only read " << sizeResult);
    return VirtNetMessage();
  }

  int size = (sizeBuffer[0] << 24) | (sizeBuffer[1] << 16) |
             (sizeBuffer[2] << 8) | sizeBuffer[3];

  char* buffer = new char[size];

  int readResult = recv(socket_, buffer, size, MSG_WAITALL);

  if (readResult != size) {
    MSGBOX("VirtNet: Failed to recv packet body, only read " << readResult <<
 " of " << size);
    return VirtNetMessage();
  }

  VirtNetMessage msg;
  msg.ParseFromArray(buffer, size);

  MSGBOX("VirtNet: Read packet");
  return msg;
}

void VirtNetManager::write_packet_direct(const VirtNetMessage& msg) const {
  MSGBOX("VirtNet: Sending packet");

  int size = msg.ByteSize();
  char* buffer = new char[size + 4];
  buffer[0] = (size >> 24) & 0xFF;
  buffer[1] = (size >> 16) & 0xFF;
  buffer[2] = (size >> 8) & 0xFF;
  buffer[3] = size & 0xFF;
  msg.SerializeToArray(buffer + 4, size);

  int result = send(socket_, buffer, size + 4, 0);

  if (result == SOCKET_ERROR) {
    MSGBOX(L"VirtNet: Failed to send: " << result);
  }
}

void VirtNetManager::handle_reads() {
  while (true) {
    VirtNetMessage msg = read_packet_direct();

    if (msg.is_response()) {
      response_map_[msg.response_target()]->set_value(msg);
    } else {
      MSGBOX("Non response messages are not handled yet :(");
    }
  }
}

void VirtNetManager::handle_writes() {
  std::unique_lock<std::mutex> lock(send_queue_mutex_);

  while (true) {
    send_queue_cv_.wait(lock, [this]
    {
      return send_queue_.size();
    });

    if (!send_queue_.empty()) {
      auto msg = std::move(send_queue_.front());
      send_queue_.pop();

      lock.unlock();

      write_packet_direct(msg);

      lock.lock();
    }
  }
}

} // namespace kernel
} // namespace xe
