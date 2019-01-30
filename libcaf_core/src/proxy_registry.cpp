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

#include <utility>
#include <algorithm>

#include "caf/actor_addr.hpp"
#include "caf/actor_system.hpp"
#include "caf/deserializer.hpp"
#include "caf/locks.hpp"
#include "caf/node_id.hpp"
#include "caf/proxy_registry.hpp"
#include "caf/serializer.hpp"

#include "caf/logger.hpp"
#include "caf/actor_registry.hpp"

namespace caf {

namespace {

using exclusive_guard = unique_lock<detail::shared_spinlock>;

using shared_guard = shared_lock<detail::shared_spinlock>;

using upgrade_guard = upgrade_to_unique_lock<detail::shared_spinlock>;

strong_actor_ptr dummy_factory(actor_system&, const node_id&, actor_id, actor) {
  return nullptr;
}

} // namespace <anonymous>

proxy_registry::proxy_registry(actor_system& sys)
  : system_(sys),
    factory_(dummy_factory) {
  // nop
}

proxy_registry::~proxy_registry() {
  clear();
}

size_t proxy_registry::count_proxies(const node_id& node) const {
  shared_guard guard{mtx_};
  auto i = nodes_.find(node);
  return i != nodes_.end() ? i->second.proxies.size() : 0;
}

strong_actor_ptr proxy_registry::get(const node_id& node, actor_id aid) const {
  shared_guard guard{mtx_};
  auto i = nodes_.find(node);
  if (i == nodes_.end())
    return nullptr;
  auto& submap = i->second.proxies;
  auto j = submap.find(aid);
  if (j == submap.end())
    return nullptr;
  return j->second;
}

strong_actor_ptr proxy_registry::get_or_put(const node_id& nid, actor_id aid) {
  CAF_LOG_TRACE(CAF_ARG(nid) << CAF_ARG(aid));
  shared_guard guard{mtx_};
  auto i = nodes_.find(nid);
  if (i == nodes_.end()) {
    auto result = factory_(system_, nid, aid, actor{});
    proxy_map tmp;
    tmp.emplace(aid, result);
    upgrade_guard uguard{guard};
    auto op = nodes_.emplace(nid, node_state{actor{}, std::move(tmp)});
    if (!op.second) {
      // We got preempted, check whether there's already a proxy in the map.
      auto sub_op = op.first->second.proxies.emplace(aid, result);
      if (!sub_op.second)
        return sub_op.first->second;
    }
    return result;
  }
  auto& submap = i->second.proxies;
  auto j = submap.find(aid);
  if (j == submap.end()) {
    auto result = factory_(system_, nid, aid, i->second.endpoint);
    upgrade_guard uguard{guard};
    auto op = submap.emplace(aid, result);
    if (!op.second)
      return op.first->second;
    return result;
  }
  return j->second;
}

std::vector<strong_actor_ptr>
proxy_registry::get_all(const node_id& nid) const {
  // Make sure we have allocated at least some initial memory.
  std::vector<strong_actor_ptr> result;
  result.reserve(128);
  // Fetch all proxies in critical section.
  shared_guard guard{mtx_};
  auto i = nodes_.find(nid);
  if (i != nodes_.end())
    for (auto& kvp : i->second.proxies)
      result.emplace_back(kvp.second);
  return result;
}

std::vector<strong_actor_ptr> proxy_registry::claim(const node_id& nid,
                                                    actor endpoint) {
  // Make sure we have allocated at least some initial memory.
  std::vector<strong_actor_ptr> result;
  result.reserve(128);
  // Fetch all proxies in critical section.
  exclusive_guard guard{mtx_};
  auto& node = nodes_[nid];
  node.endpoint = std::move(endpoint);
  for (auto& kvp : node.proxies)
    result.emplace_back(kvp.second);
  return result;
}

bool proxy_registry::empty() const {
  shared_guard guard{mtx_};
  return nodes_.empty();
}

void proxy_registry::erase(const node_id& nid) {
  CAF_LOG_TRACE(CAF_ARG(nid));
  exclusive_guard guard{mtx_};
  auto i = nodes_.find(nid);
  if (i == nodes_.end())
    return;
  for (auto& kvp : i->second.proxies)
    kill_proxy(kvp.second, exit_reason::remote_link_unreachable);
  nodes_.erase(i);
}

void proxy_registry::erase(const node_id& nid, actor_id aid, error rsn) {
  CAF_LOG_TRACE(CAF_ARG(nid) << CAF_ARG(aid));
  exclusive_guard guard{mtx_};
  auto i = nodes_.find(nid);
  if (i != nodes_.end()) {
    auto& submap = i->second.proxies;
    auto j = submap.find(aid);
    if (j == submap.end())
      return;
    kill_proxy(j->second, std::move(rsn));
    submap.erase(j);
    if (submap.empty())
      nodes_.erase(i);
  }
}

void proxy_registry::clear() {
  exclusive_guard guard{mtx_};
  for (auto& kvp : nodes_)
    for (auto& sub_kvp : kvp.second.proxies)
      kill_proxy(sub_kvp.second, exit_reason::remote_link_unreachable);
  nodes_.clear();
}

void proxy_registry::kill_proxy(strong_actor_ptr& ptr, error) {
  if (!ptr)
    return;
  // TODO: implement me
  // auto pptr = static_cast<actor_proxy*>(actor_cast<abstract_actor*>(ptr));
  // pptr->kill_proxy(backend_.registry_context(), std::move(rsn));
}

void proxy_registry::init(factory f) {
  CAF_LOG_TRACE("");
  // Set factory without mutex, because this function must be called by the
  // actor system before any other member function.
  factory_ = f;
}

} // namespace caf
