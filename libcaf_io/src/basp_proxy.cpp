/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2018 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include "caf/io/basp_proxy.hpp"

#include <utility>

#include "caf/actor_system.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/detail/sync_request_bouncer.hpp"
#include "caf/io/basp/header.hpp"
#include "caf/locks.hpp"
#include "caf/logger.hpp"
#include "caf/mailbox_element.hpp"
#include "caf/scheduler/abstract_coordinator.hpp"
#include "caf/send.hpp"

namespace caf {
namespace io {

basp_proxy::basp_proxy(actor_config& cfg, actor dest)
  : actor_proxy(cfg),
    mailbox_(unit),
    broker_(std::move(dest)) {
  // nop
}

basp_proxy::~basp_proxy() {
  anon_send(broker_, make_message(delete_atom::value, node(), id()));
}

void basp_proxy::enqueue(mailbox_element_ptr ptr,
                                     execution_unit* eu) {
  CAF_ASSERT(ptr != nullptr);
  CAF_LOG_TRACE(CAF_ARG(*ptr));
  CAF_LOG_SEND_EVENT(ptr);
  auto mid = ptr->mid;
  auto sender = ptr->sender;
  switch (mailbox_.push_back(std::move(ptr))) {
    case intrusive::inbox_result::unblocked_reader: {
      CAF_LOG_ACCEPT_EVENT(true);
      // add a reference count to this actor and re-schedule it
      intrusive_ptr_add_ref(ctrl());
      if (eu != nullptr)
        eu->exec_later(this);
      else
        home_system().scheduler().enqueue(this);
      break;
    }
    case intrusive::inbox_result::queue_closed: {
      CAF_LOG_REJECT_EVENT();
      if (mid.is_request()) {
        detail::sync_request_bouncer f{exit_reason()};
        f(sender, mid);
      }
      break;
    }
    case intrusive::inbox_result::success:
      // enqueued to a running actors' mailbox; nothing to do
      CAF_LOG_ACCEPT_EVENT(false);
      break;
  }
}

bool basp_proxy::add_backlink(abstract_actor* x) {
  if (monitorable_actor::add_backlink(x)) {
    forward_msg(ctrl(), make_message_id(),
                make_message(link_atom::value, x->ctrl()));
    return true;
  }
  return false;
}

bool basp_proxy::remove_backlink(abstract_actor* x) {
  if (monitorable_actor::remove_backlink(x)) {
    forward_msg(ctrl(), make_message_id(),
                make_message(unlink_atom::value, x->ctrl()));
    return true;
  }
  return false;
}

void basp_proxy::kill_proxy(execution_unit* ctx, error rsn) {
  CAF_ASSERT(ctx != nullptr);
  actor tmp;
  { // lifetime scope of guard
    std::unique_lock<detail::shared_spinlock> guard(broker_mtx_);
    broker_.swap(tmp); // manually break cycle
  }
  cleanup(std::move(rsn), ctx);
}

resumable::resume_result basp_proxy::resume(execution_unit* ctx,
                                            size_t max_throughput) {
  CAF_PUSH_AID(id());
  CAF_LOG_TRACE(CAF_ARG(max_throughput));
  size_t handled_msgs = 0;
  auto f = [&](mailbox_element& element) -> intrusive::task_result {
    CAF_ASSERT(broker_ != nullptr);
    if (element.content().match_elements<basp::header, std::vector<char>>()) {
    } else {
      basp::header hdr{basp::message_type::dispatch_message,
                       0,
                       0,
                       element.mid.integer_value(),
                       element.sender ? element.sender->node()
                                      : home_system().node(),
                       node(),
                       element.sender ? element.sender->id() : 0,
                       id()};
      std::vector<char> buf;
      binary_serializer sink{home_system(), buf};
      if (auto err = sink(element.stages, element.content())) {
        // TODO: error reporting
        return intrusive::task_result::stop_all;
      }
      hdr.payload_len = static_cast<uint32_t>(buf.size());
      broker_->eq_impl(make_message_id(), ctrl(), ctx, hdr, std::move(buf));
    }
    return ++handled_msgs < max_throughput ? intrusive::task_result::resume
                                           : intrusive::task_result::stop_all;
  };
  // Timeout for calling `advance_streams`.
  while (handled_msgs < max_throughput) {
    CAF_LOG_DEBUG("start new DRR round");
    // TODO: maybe replace '3' with configurable / adaptive value?
    // Dispatch on the different message categories in our mailbox.
    if (!mailbox_.new_round(3, f).consumed_items)
      if (mailbox_.try_block())
        return resumable::awaiting_message;
    // Check whether the visitor left the actor without behavior.
    if (broker_ == nullptr)
      return resumable::done;
  }
  CAF_LOG_DEBUG("max throughput reached");
  // Time's up.
  return mailbox_.try_block() ? resumable::awaiting_message
                              : resumable::resume_later;
}

void basp_proxy::forward_msg(strong_actor_ptr sender,
                                         message_id mid, message msg,
                                         const forwarding_stack* fwd) {
  CAF_LOG_TRACE(CAF_ARG(id()) << CAF_ARG(sender)
                << CAF_ARG(mid) << CAF_ARG(msg));
  if (msg.match_elements<exit_msg>())
    unlink_from(msg.get_as<exit_msg>(0).source);
  forwarding_stack tmp;
  shared_lock<detail::shared_spinlock> guard(broker_mtx_);
  if (broker_)
    broker_->enqueue(nullptr, make_message_id(),
                     make_message(forward_atom::value, std::move(sender),
                                  fwd != nullptr ? *fwd : tmp,
                                  strong_actor_ptr{ctrl()}, mid,
                                  std::move(msg)),
                     nullptr);
}

} // namespace io
} // namespace caf
