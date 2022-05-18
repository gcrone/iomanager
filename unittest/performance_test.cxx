/**
 * @file performance_test.cxx Performance Unit Tests
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "iomanager/IOManager.hpp"

#include "serialization/Serialization.hpp"

#define BOOST_TEST_MODULE performance_test // NOLINT

#include "boost/test/unit_test.hpp"

#include <atomic>
#include <functional>
#include <future>
#include <string>
#include <vector>

using namespace dunedaq::iomanager;

namespace dunedaq {
struct data_t
{
  std::vector<uint8_t> d; // NOLINT(build/unsigned)
  data_t() = default;
  data_t(unsigned int size, uint8_t c) // NOLINT(build/unsigned)
    : d(size, c)
  {
    ;
  }
  DUNE_DAQ_SERIALIZE(data_t, d);
};
DUNE_DAQ_SERIALIZABLE(data_t);
} // namespace dunedaq

BOOST_AUTO_TEST_SUITE(performance_test)

struct ConfigurationTestFixture
{
  ConfigurationTestFixture()
  {
    dunedaq::iomanager::ConnectionIds_t connections;
    connections.emplace_back(ConnectionId{ "test_queue", ServiceType::kQueue, "data_t", "queue://FollySPSC:50" });
    connections.emplace_back(ConnectionId{ "test_connection_s", ServiceType::kNetSender, "data_t", "inproc://foo" });
    connections.emplace_back(ConnectionId{ "test_connection_r", ServiceType::kNetReceiver, "data_t", "inproc://foo" });
    IOManager::get()->configure(connections);
    conn_ref_s = ConnectionRef{ "network_s", "test_connection_s", Direction::kOutput};
    conn_ref_r = ConnectionRef{ "network_r", "test_connection_r", Direction::kInput };
    queue_ref = ConnectionRef{ "queue", "test_queue" };
  }
  ~ConfigurationTestFixture() { IOManager::get()->reset(); }

  ConfigurationTestFixture(ConfigurationTestFixture const&) = default;
  ConfigurationTestFixture(ConfigurationTestFixture&&) = default;
  ConfigurationTestFixture& operator=(ConfigurationTestFixture const&) = default;
  ConfigurationTestFixture& operator=(ConfigurationTestFixture&&) = default;

  dunedaq::iomanager::ConnectionRef conn_ref_s;
  dunedaq::iomanager::ConnectionRef conn_ref_r;
  dunedaq::iomanager::ConnectionRef queue_ref;
  const size_t n_sends = 10000;
  const size_t message_size = 55680;
};

BOOST_FIXTURE_TEST_CASE(CallbackRegistrationNetwork, ConfigurationTestFixture)
{
  std::atomic<unsigned int> received_count = 0;
  std::function<void(dunedaq::data_t)> callback = [&](dunedaq::data_t) { ++received_count; }; // NOLINT

  IOManager::get()->add_callback<dunedaq::data_t>(conn_ref_r, callback);
  auto net_sender = IOManager::get()->get_sender<dunedaq::data_t>(conn_ref_s);
  auto start_time = std::chrono::steady_clock::now();
  for (unsigned int i = 0; i < n_sends; ++i) {
    dunedaq::data_t temp(message_size, i % 200);
    net_sender->send(std::move(temp), Sender::s_no_block);
  }
  BOOST_TEST_MESSAGE("Messages sent, waiting for receives");
  while (received_count < n_sends) {
    usleep(1000);
  }

  IOManager::get()->remove_callback<dunedaq::data_t>(conn_ref_r);
  auto stop_time = std::chrono::steady_clock::now();

  auto time = std::chrono::duration_cast<std::chrono::microseconds>(stop_time - start_time).count();
  double rate = received_count.load() / static_cast<double>(time) * 1e6; // Hz

  BOOST_CHECK(rate > 0.);
  BOOST_TEST_MESSAGE("network callback rate " << rate << " Hz");
}

BOOST_FIXTURE_TEST_CASE(CallbackRegistrationQueue, ConfigurationTestFixture)
{
  std::atomic<unsigned int> received_count = 0;
  std::function<void(dunedaq::data_t)> callback = [&](dunedaq::data_t) { ++received_count; }; // NOLINT

  IOManager::get()->add_callback<dunedaq::data_t>(queue_ref, callback);
  auto queue_sender = IOManager::get()->get_sender<dunedaq::data_t>(queue_ref);
  auto start_time = std::chrono::steady_clock::now();
  for (unsigned int i = 0; i < n_sends; ++i) {
    dunedaq::data_t temp(message_size, i % 200);
    queue_sender->send(std::move(temp), std::chrono::milliseconds(1000));
  }
  BOOST_TEST_MESSAGE("Messages sent, waiting for receives");
  while (received_count < n_sends) {
    usleep(1000);
  }
  IOManager::get()->remove_callback<dunedaq::data_t>(queue_ref);
  auto stop_time = std::chrono::steady_clock::now();

  auto time = std::chrono::duration_cast<std::chrono::microseconds>(stop_time - start_time).count();
  double rate = received_count.load() / static_cast<double>(time) * 1e6; // Hz

  BOOST_CHECK(rate > 0.);
  BOOST_TEST_MESSAGE("queue callback rate " << rate << " Hz");
}

BOOST_FIXTURE_TEST_CASE(DirectReadNetwork, ConfigurationTestFixture)
{
  std::atomic<unsigned int> received_count = 0;
  unsigned int total_send = n_sends;
  std::function<void()> recv_func = [&]() {
    do {
      auto mess = IOManager::get()->get_receiver<dunedaq::data_t>(conn_ref_r)->receive(std::chrono::milliseconds(10));
      ++received_count;
    } while (received_count.load() < total_send);
  };

  auto net_sender = IOManager::get()->get_sender<dunedaq::data_t>(conn_ref_s);
  auto rcv_ftr = std::async(std::launch::async, recv_func);

  auto start_time = std::chrono::steady_clock::now();
  for (unsigned int i = 0; i < total_send; ++i) {
    dunedaq::data_t temp(message_size, i % 200);
    net_sender->send(std::move(temp), dunedaq::iomanager::Sender::s_no_block);
  }
  BOOST_TEST_MESSAGE("Messages sent, waiting for receives");
  rcv_ftr.get();
  auto stop_time = std::chrono::steady_clock::now();

  auto time = std::chrono::duration_cast<std::chrono::microseconds>(stop_time - start_time).count();
  double rate = received_count.load() / static_cast<double>(time) * 1e6; // Hz

  BOOST_CHECK(rate > 0.);
  BOOST_TEST_MESSAGE("network read rate " << rate << " Hz");
}

BOOST_FIXTURE_TEST_CASE(DirectReadQueue, ConfigurationTestFixture)
{
  std::atomic<unsigned int> received_count = 0;
  unsigned int total_send = n_sends;
  std::function<void()> recv_func = [&]() {
    do {
      auto mess = IOManager::get()->get_receiver<dunedaq::data_t>(queue_ref)->receive(std::chrono::milliseconds(10));
      ++received_count;
    } while (received_count.load() < total_send);
  };

  auto queue_sender = IOManager::get()->get_sender<dunedaq::data_t>(queue_ref);
  auto rcv_ftr = std::async(std::launch::async, recv_func);

  auto start_time = std::chrono::steady_clock::now();
  for (unsigned int i = 0; i < total_send; ++i) {
    dunedaq::data_t temp(message_size, i % 200);
    queue_sender->send(std::move(temp), std::chrono::milliseconds(10));
  }
  BOOST_TEST_MESSAGE("Messages sent, waiting for receives");
  rcv_ftr.get();
  auto stop_time = std::chrono::steady_clock::now();

  auto time = std::chrono::duration_cast<std::chrono::microseconds>(stop_time - start_time).count();
  double rate = received_count.load() / static_cast<double>(time) * 1e6; // Hz

  BOOST_CHECK(rate > 0.);
  BOOST_TEST_MESSAGE("queue read rate " << rate << " Hz");
}

BOOST_AUTO_TEST_SUITE_END()
