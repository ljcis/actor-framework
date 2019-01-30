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

#pragma once

#include <functional>
#include <map>
#include <unordered_map>
#include <utility>

#include "caf/actor.hpp"
#include "caf/actor_control_block.hpp"
#include "caf/detail/shared_spinlock.hpp"
#include "caf/fwd.hpp"
#include "caf/node_id.hpp"

namespace caf {

/// Groups proxy instances by node ID.
class proxy_registry {
public:
  // -- member types -----------------------------------------------------------

  /// Generates proxy instances to remote nodes.
  using factory = strong_actor_ptr (*)(actor_system&, const node_id&, actor_id,
                                       actor);

  /// Maps actor IDs to proxy instances.
  using proxy_map = std::unordered_map<actor_id, strong_actor_ptr>;

  /// State per connected node.
  struct node_state {
    /// Multiplexes traffic to and from remote actors.
    actor endpoint;

    /// Stores all proxy instances for a remote node.
    proxy_map proxies;
  };

  /// Maps node IDs to proxy maps.
  using node_map = std::unordered_map<node_id, node_state>;

  // -- constructors, destructors, and assignment operators --------------------

  explicit proxy_registry(actor_system& sys);

  proxy_registry(const proxy_registry&) = delete;

  proxy_registry& operator=(const proxy_registry&) = delete;

  ~proxy_registry();

  // -- properties -------------------------------------------------------------

  void serialize(serializer& sink, const actor_addr& addr) const;

  void serialize(deserializer& source, actor_addr& addr);

  /// Writes an actor address to `sink` and adds the actor
  /// to the list of known actors for a later deserialization.
  void write(serializer* sink, const actor_addr& ptr) const;

  /// Reads an actor address from `source,` creating
  /// addresses for remote actors on the fly if needed.
  actor_addr read(deserializer* source);

  /// Returns the number of proxies for `node`.
  size_t count_proxies(const node_id& node) const;

  /// Returns the proxy instance identified by `node` and `aid`.
  strong_actor_ptr get(const node_id& node, actor_id aid) const;

  /// Returns the proxy instance identified by `node` and `aid`
  /// or creates a new (default) proxy instance.
  strong_actor_ptr get_or_put(const node_id& nid, actor_id aid);

  /// Returns all known proxies for `nid`.
  std::vector<strong_actor_ptr> get_all(const node_id& nid) const;

  /// Claims all proxies for `nid`. Future proxy instances are created by
  /// passing `endpoint` as the parent.
  /// @returns All proxies already created for `nid`.
  std::vector<strong_actor_ptr> claim(const node_id& nid, actor endpoint);

  /// Deletes all proxies for `node`.
  void erase(const node_id& nid);

  /// Deletes the proxy with id `aid` for `nid`.
  void erase(const node_id& nid, actor_id aid,
             error rsn = exit_reason::remote_link_unreachable);

  /// Queries whether there are any proxies left.
  bool empty() const;

  /// Deletes all proxies.
  void clear();

  /// Returns the hosting actor system.
  actor_system& system() {
    return system_;
  }

  /// Returns the hosting actor system.
  const actor_system& system() const {
    return system_;
  }

  /// Initializes the registry to produce proxy instances with given factory.
  /// @private
  void init(factory f);

private:
  void kill_proxy(strong_actor_ptr&, error);

  actor_system& system_;

  mutable detail::shared_spinlock mtx_;

  node_map nodes_;

  factory factory_;
};

} // namespace caf

