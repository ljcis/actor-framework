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

#define CAF_SUITE proxy_registry

#include "caf/proxy_registry.hpp"

#include "caf/test/dsl.hpp"

#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"

using namespace caf;

namespace {

size_t testee_instances_created = 0;

size_t testee_instances_destroyed = 0;

class testee_actor : public event_based_actor {
public:
  testee_actor(actor_config& cfg, actor parent)
    : event_based_actor(cfg),
      parent_(std::move(parent)) {
    ++testee_instances_created;
  }

  ~testee_actor() {
    ++testee_instances_destroyed;
  }

  const char* name() const override {
    return "testee";
  }

  const actor& parent() const {
    return parent_;
  }

private:
  actor parent_;
};

strong_actor_ptr test_factory(actor_system& sys_, const node_id&, actor_id,
                              actor parent) {
  auto hdl = sys_.spawn<testee_actor, lazy_init + hidden>(std::move(parent));
  return actor_cast<strong_actor_ptr>(std::move(hdl));
}

struct fixture : test_coordinator_fixture<> {
  fixture()
    : proxies(sys.proxies()),
      mars(1, "1111111111111111111111111111111111111111"),
      mercury(2, "222222222222222222222222222222222222222222") {
    proxies.init(test_factory);
    testee_instances_created = 0;
    testee_instances_destroyed = 0;
  }

  proxy_registry& proxies;
  node_id mars;
  node_id mercury;
};

} // namespace <anonymous>

CAF_TEST_FIXTURE_SCOPE(proxy_registry_tests, fixture)

CAF_TEST(empty registry) {
  CAF_CHECK_EQUAL(proxies.empty(), true);
  CAF_CHECK_EQUAL(proxies.count_proxies(mars), 0u);
  CAF_CHECK_EQUAL(proxies.count_proxies(mercury), 0u);
  CAF_CHECK_EQUAL(proxies.get(mars, 1), nullptr);
  CAF_CHECK_EQUAL(proxies.get(mercury, 2), nullptr);
  CAF_CHECK_EQUAL(proxies.get_all(mars).size(), 0u);
  CAF_CHECK_EQUAL(proxies.get_all(mercury).size(), 0u);
  // Check again to make sure no getter silently inserts nodes.
  CAF_CHECK_EQUAL(proxies.empty(), true);
  CAF_CHECK_EQUAL(testee_instances_created, 0u);
}

CAF_TEST(get_or_put) {
  auto ptr = proxies.get_or_put(mars, 1);
  CAF_CHECK_EQUAL(proxies.get(mars, 1), ptr);
  CAF_CHECK_EQUAL(proxies.get_or_put(mars, 1), ptr);
  CAF_CHECK_EQUAL(proxies.get(mercury, 1), nullptr);
  ptr = nullptr;
  proxies.erase(mars, 1);
  CAF_CHECK_EQUAL(proxies.empty(), true);
  CAF_CHECK_EQUAL(testee_instances_created, 1u);
  CAF_CHECK_EQUAL(testee_instances_destroyed, 1u);
}

CAF_TEST(deserialization) {
  CAF_MESSAGE("write actor ID and node ID into buffer");
  std::vector<char> buf;
  { // Lifetime scope of sink.
    binary_serializer sink{sys, buf};
    if (auto err = sink(actor_id{1}, mars))
      CAF_FAIL("sink returned an error: " << sys.render(err));
  }
  CAF_MESSAGE("deserialize buffer as actor handle");
  actor hdl;
  { // Lifetime scope of source.
    binary_deserializer source{sys, buf};
    if (auto err = source(hdl))
      CAF_FAIL("source returned an error: " << sys.render(err));
    if (hdl == nullptr)
      CAF_FAIL("source produced an nullptr handle");
  }
  CAF_MESSAGE("check registry for deserialized handle");
  CAF_CHECK_EQUAL(testee_instances_created, 1u);
  CAF_CHECK_EQUAL(proxies.count_proxies(mars), 1u);
  CAF_CHECK_EQUAL(hdl, proxies.get(mars, 1));
}

CAF_TEST(claiming nodes) {
  auto dummy = sys.spawn([] {});
  auto ptr1 = proxies.get_or_put(mars, 1);
  CAF_CHECK_EQUAL(proxies.claim(mars, dummy),
                  std::vector<decltype(ptr1)>{ptr1});
  auto ptr2 = proxies.get_or_put(mars, 2);
  CAF_CHECK_EQUAL(deref<testee_actor>(ptr1).parent(), nullptr);
  CAF_CHECK_EQUAL(deref<testee_actor>(ptr2).parent(), dummy);
}

CAF_TEST_FIXTURE_SCOPE_END()
