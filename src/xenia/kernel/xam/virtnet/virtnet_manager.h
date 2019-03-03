/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_VIRTNET_MANAGER_H_
#define XENIA_KERNEL_VIRTNET_MANAGER_H_
#include <thread>
#include <queue>

#include "xenia/kernel/xam/virtnet/virtnet_packets.pb.h"
#include <future>

namespace xe {
namespace kernel {

class VirtNetManager {
 public:
  void init();

  void send_packet(VirtNetMessage msg);

  VirtNetMessage send_packet_with_result(VirtNetMessage msg);

 private:
  int socket_ = 0;
  std::thread* read_thread_;
  std::thread* write_thread_;
  std::mutex send_queue_mutex_;
  std::condition_variable send_queue_cv_;
  std::queue<VirtNetMessage> send_queue_;
  std::map<uint32_t, std::promise<VirtNetMessage>*> response_map_;
  uint32_t last_response_id_ = 0;

  VirtNetMessage read_packet_direct();

  void write_packet_direct(const VirtNetMessage& msg) const;

  void handle_reads();

  void handle_writes();
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_VIRTNET_MANAGER_H_
