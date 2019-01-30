/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2019 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#pragma once

#include "caf/actor.hpp"
#include "caf/actor_proxy.hpp"
#include "caf/detail/shared_spinlock.hpp"
#include "caf/intrusive/drr_queue.hpp"
#include "caf/intrusive/fifo_inbox.hpp"
#include "caf/policy/normal_messages.hpp"
#include "caf/resumable.hpp"

namespace caf {
namespace io {

/// Implements a simple proxy that serializes incoming messages and forwards
/// them to a broker.
class basp_proxy : public actor_proxy, public resumable {
public:
  // -- member types -----------------------------------------------------------

  using forwarding_stack = std::vector<strong_actor_ptr>;

  struct mailbox_policy {
    using deficit_type = size_t;

    using mapped_type = mailbox_element;

    using unique_pointer = mailbox_element_ptr;

    using queue_type = intrusive::drr_queue<policy::normal_messages>;
  };

  using mailbox_type = intrusive::fifo_inbox<mailbox_policy>;

  // -- constructors, destructors, and assignment operators --------------------

  basp_proxy(actor_config& cfg, actor dest);

  ~basp_proxy() override;

  // -- virtual modifiers ------------------------------------------------------

  void enqueue(mailbox_element_ptr what, execution_unit* context) override;

  bool add_backlink(abstract_actor* x) override;

  bool remove_backlink(abstract_actor* x) override;

  void kill_proxy(execution_unit* ctx, error rsn) override;

  resume_result resume(execution_unit*, size_t) override;

private:
  void forward_msg(strong_actor_ptr sender, message_id mid, message msg,
                   const forwarding_stack* fwd = nullptr);

  mutable detail::shared_spinlock broker_mtx_;
  mailbox_type mailbox_;
  actor broker_;
};

} // namespace io
} // namespace caf
