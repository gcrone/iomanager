/**
 * @file IOManager_test.cxx IOManager Unit Tests
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "iomanager/IOManager.hpp"

#include "appfwk/QueueRegistry.hpp"
#include "appfwk/app/Nljs.hpp"
#include "networkmanager/NetworkManager.hpp"
#include "networkmanager/nwmgr/Structs.hpp"
#include "serialization/Serialization.hpp"

#define BOOST_TEST_MODULE IOManager_test // NOLINT

#include "boost/test/unit_test.hpp"

#include <string>
#include <vector>

using namespace dunedaq::iomanager;

namespace dunedaq {
namespace iomanager {
struct Data
{
  int d1;
  double d2;
  std::string d3;

  Data() = default;
  Data(int i, double d, std::string s)
    : d1(i)
    , d2(d)
    , d3(s)
  {}
  virtual ~Data() = default;
  Data(Data const&) = default;
  Data& operator=(Data const&) = default;
  Data(Data&&) = default;
  Data& operator=(Data&&) = default;

  DUNE_DAQ_SERIALIZE(Data, d1, d2, d3);
};

struct NonCopyableData
{
  int d1;
  double d2;
  std::string d3;

  NonCopyableData() = default;
  NonCopyableData(int i, double d, std::string s)
    : d1(i)
    , d2(d)
    , d3(s)
  {}
  virtual ~NonCopyableData() = default;
  NonCopyableData(NonCopyableData const&) = delete;
  NonCopyableData& operator=(NonCopyableData const&) = delete;
  NonCopyableData(NonCopyableData&&) = default;
  NonCopyableData& operator=(NonCopyableData&&) = default;

  DUNE_DAQ_SERIALIZE(NonCopyableData, d1, d2, d3);
};

struct NonSerializableData
{
  int d1;
  double d2;
  std::string d3;

  NonSerializableData() = default;
  NonSerializableData(int i, double d, std::string s)
    : d1(i)
    , d2(d)
    , d3(s)
  {}
  virtual ~NonSerializableData() = default;
  NonSerializableData(NonSerializableData const&) = default;
  NonSerializableData& operator=(NonSerializableData const&) = default;
  NonSerializableData(NonSerializableData&&) = default;
  NonSerializableData& operator=(NonSerializableData&&) = default;
};

struct NonSerializableNonCopyable
{
  int d1;
  double d2;
  std::string d3;

  NonSerializableNonCopyable() = default;
  NonSerializableNonCopyable(int i, double d, std::string s)
    : d1(i)
    , d2(d)
    , d3(s)
  {}
  virtual ~NonSerializableNonCopyable() = default;
  NonSerializableNonCopyable(NonSerializableNonCopyable const&) = delete;
  NonSerializableNonCopyable& operator=(NonSerializableNonCopyable const&) = delete;
  NonSerializableNonCopyable(NonSerializableNonCopyable&&) = default;
  NonSerializableNonCopyable& operator=(NonSerializableNonCopyable&&) = default;
};

} // namespace iomanager
} // namespace dunedaq

BOOST_AUTO_TEST_SUITE(IOManager_test)

struct ConfigurationTestFixture
{
  ConfigurationTestFixture()
  {
    dunedaq::networkmanager::nwmgr::Connections nwCfg;
    dunedaq::networkmanager::nwmgr::Connection testConn;
    testConn.name = "test_connection";
    testConn.address = "inproc://foo";
    nwCfg.push_back(testConn);
    dunedaq::networkmanager::NetworkManager::get().configure(nwCfg);

    std::map<std::string, dunedaq::appfwk::QueueConfig> config_map;
    dunedaq::appfwk::QueueConfig qspec;
    qspec.kind = dunedaq::appfwk::QueueConfig::queue_kind::kStdDeQueue;
    qspec.capacity = 10;
    config_map["test_queue"] = qspec;
    dunedaq::appfwk::QueueRegistry::get().configure(config_map);
  }
  ~ConfigurationTestFixture()
  {
    dunedaq::networkmanager::NetworkManager::get().reset();
    dunedaq::appfwk::QueueRegistry::get().reset();
  }

  ConfigurationTestFixture(ConfigurationTestFixture const&) = default;
  ConfigurationTestFixture(ConfigurationTestFixture&&) = default;
  ConfigurationTestFixture& operator=(ConfigurationTestFixture const&) = default;
  ConfigurationTestFixture& operator=(ConfigurationTestFixture&&) = default;
};

BOOST_AUTO_TEST_CASE(CopyAndMoveSemantics)
{
  BOOST_REQUIRE(!std::is_copy_constructible_v<IOManager>);
  BOOST_REQUIRE(!std::is_copy_assignable_v<IOManager>);
  BOOST_REQUIRE(!std::is_move_constructible_v<IOManager>);
  BOOST_REQUIRE(!std::is_move_assignable_v<IOManager>);
}

BOOST_FIXTURE_TEST_CASE(SimpleSendReceive, ConfigurationTestFixture)
{
  IOManager iom;
  ConnectionID test_conn{ "network", "test_connection", "" };
  ConnectionID test_queue{ "queue", "test_queue", "" };
  auto net_sender = iom.get_sender<Data>(test_conn);
  auto net_receiver = iom.get_receiver<Data>(test_conn);
  auto q_sender = iom.get_sender<Data>(test_queue);
  auto q_receiver = iom.get_receiver<Data>(test_queue);

  Data sent_nw(56, 26.5, "test1");
  Data sent_q(57, 27.5, "test2");
  net_sender->send(sent_nw, dunedaq::iomanager::Sender::s_no_block);

  auto ret = net_receiver->receive(std::chrono::milliseconds(10));
  BOOST_CHECK_EQUAL(ret.d1, 56);
  BOOST_CHECK_EQUAL(ret.d2, 26.5);
  BOOST_CHECK_EQUAL(ret.d3, "test1");

  q_sender->send(sent_q, std::chrono::milliseconds(10));

  ret = q_receiver->receive(std::chrono::milliseconds(10));
  BOOST_CHECK_EQUAL(ret.d1, 57);
  BOOST_CHECK_EQUAL(ret.d2, 27.5);
  BOOST_CHECK_EQUAL(ret.d3, "test2");
}

BOOST_FIXTURE_TEST_CASE(NonSerializableSendReceive, ConfigurationTestFixture)
{
  IOManager iom;
  ConnectionID test_conn{ "network", "test_connection", "" };
  ConnectionID test_queue{ "queue", "test_queue", "" };
  auto net_sender = iom.get_sender<NonSerializableData>(test_conn);
  auto net_receiver = iom.get_receiver<NonSerializableData>(test_conn);
  auto q_sender = iom.get_sender<NonSerializableData>(test_queue);
  auto q_receiver = iom.get_receiver<NonSerializableData>(test_queue);

  NonSerializableData sent_nw(56, 26.5, "test1");
  NonSerializableData sent_q(57, 27.5, "test2");
  net_sender->send(sent_nw, dunedaq::iomanager::Sender::s_no_block);

  auto ret = net_receiver->receive(std::chrono::milliseconds(10));
  BOOST_CHECK_EQUAL(ret.d1, 0);
  BOOST_CHECK_EQUAL(ret.d2, 0);
  BOOST_CHECK_EQUAL(ret.d3, "");

  q_sender->send(sent_q, std::chrono::milliseconds(10));

  ret = q_receiver->receive(std::chrono::milliseconds(10));
  BOOST_CHECK_EQUAL(ret.d1, 57);
  BOOST_CHECK_EQUAL(ret.d2, 27.5);
  BOOST_CHECK_EQUAL(ret.d3, "test2");
}

BOOST_FIXTURE_TEST_CASE(NonCopyableSendReceive, ConfigurationTestFixture)
{
  IOManager iom;
  ConnectionID test_conn{ "network", "test_connection", "" };
  ConnectionID test_queue{ "queue", "test_queue", "" };
  auto net_sender = iom.get_sender<NonCopyableData>(test_conn);
  auto net_receiver = iom.get_receiver<NonCopyableData>(test_conn);
  auto q_sender = iom.get_sender<NonCopyableData>(test_queue);
  auto q_receiver = iom.get_receiver<NonCopyableData>(test_queue);

  NonCopyableData sent_nw(56, 26.5, "test1");
  NonCopyableData sent_q(57, 27.5, "test2");
  net_sender->send(sent_nw, dunedaq::iomanager::Sender::s_no_block);

  auto ret = net_receiver->receive(std::chrono::milliseconds(10));
  BOOST_CHECK_EQUAL(ret.d1, 56);
  BOOST_CHECK_EQUAL(ret.d2, 26.5);
  BOOST_CHECK_EQUAL(ret.d3, "test1");

  q_sender->send(sent_q, std::chrono::milliseconds(10));

  ret = q_receiver->receive(std::chrono::milliseconds(10));
  BOOST_CHECK_EQUAL(ret.d1, 57);
  BOOST_CHECK_EQUAL(ret.d2, 27.5);
  BOOST_CHECK_EQUAL(ret.d3, "test2");
}

BOOST_FIXTURE_TEST_CASE(NonSerializableNonCopyableSendReceive, ConfigurationTestFixture)
{
  IOManager iom;
  ConnectionID test_conn{ "network", "test_connection", "" };
  ConnectionID test_queue{ "queue", "test_queue", "" };
  auto net_sender = iom.get_sender<NonSerializableNonCopyable>(test_conn);
  auto net_receiver = iom.get_receiver<NonSerializableNonCopyable>(test_conn);
  auto q_sender = iom.get_sender<NonSerializableNonCopyable>(test_queue);
  auto q_receiver = iom.get_receiver<NonSerializableNonCopyable>(test_queue);

  NonSerializableNonCopyable sent_nw(56, 26.5, "test1");
  NonSerializableNonCopyable sent_q(57, 27.5, "test2");
  net_sender->send(sent_nw, dunedaq::iomanager::Sender::s_no_block);

  auto ret = net_receiver->receive(std::chrono::milliseconds(10));
  BOOST_CHECK_EQUAL(ret.d1, 0);
  BOOST_CHECK_EQUAL(ret.d2, 0);
  BOOST_CHECK_EQUAL(ret.d3, "");

  q_sender->send(sent_q, std::chrono::milliseconds(10));

  ret = q_receiver->receive(std::chrono::milliseconds(10));
  BOOST_CHECK_EQUAL(ret.d1, 57);
  BOOST_CHECK_EQUAL(ret.d2, 27.5);
  BOOST_CHECK_EQUAL(ret.d3, "test2");
}

BOOST_FIXTURE_TEST_CASE(CallbackRegistration, ConfigurationTestFixture)
{
  IOManager iom;
  ConnectionID test_conn{ "network", "test_connection", "" };
  ConnectionID test_queue{ "queue", "test_queue", "" };
  auto net_sender = iom.get_sender<Data>(test_conn);
  auto q_sender = iom.get_sender<Data>(test_queue);

  Data sent_data_nw(56, 26.5, "test1");
  Data sent_data_q(57, 27.5, "test2");
  Data recv_data;
  std::atomic<bool> has_received_data = false;

  std::function<void(Data&)> callback = [&](Data& d) {
    has_received_data = true;
    recv_data = std::move(d);
  };

  iom.add_callback<Data>(test_conn, callback);
  iom.add_callback<Data>(test_queue, callback);

  usleep(1000);

  net_sender->send(sent_data_nw, dunedaq::iomanager::Sender::s_no_block);

  while (!has_received_data.load())
    usleep(1000);

  BOOST_CHECK_EQUAL(recv_data.d1, 56);
  BOOST_CHECK_EQUAL(recv_data.d2, 26.5);
  BOOST_CHECK_EQUAL(recv_data.d3, "test1");

  has_received_data = false;
  q_sender->send(sent_data_q, std::chrono::milliseconds(10));

  while (!has_received_data.load())
    usleep(1000);

  BOOST_CHECK_EQUAL(recv_data.d1, 57);
  BOOST_CHECK_EQUAL(recv_data.d2, 27.5);
  BOOST_CHECK_EQUAL(recv_data.d3, "test2");

  iom.remove_callback<Data>(test_conn);
  iom.remove_callback<Data>(test_queue);
}

BOOST_FIXTURE_TEST_CASE(NonCopyableCallbackRegistration, ConfigurationTestFixture)
{

  IOManager iom;
  ConnectionID test_conn{ "network", "test_connection", "" };
  ConnectionID test_queue{ "queue", "test_queue", "" };
  auto net_sender = iom.get_sender<NonCopyableData>(test_conn);
  auto q_sender = iom.get_sender<NonCopyableData>(test_queue);

  NonCopyableData sent_data_nw(56, 26.5, "test1");
  NonCopyableData sent_data_q(57, 27.5, "test2");
  NonCopyableData recv_data;
  std::atomic<bool> has_received_data = false;

  std::function<void(NonCopyableData&)> callback = [&](NonCopyableData& d) {
    has_received_data = true;
    recv_data = std::move(d);
  };

  iom.add_callback<NonCopyableData>(test_conn, callback);
  iom.add_callback<NonCopyableData>(test_queue, callback);

  usleep(1000);

  net_sender->send(sent_data_nw, dunedaq::iomanager::Sender::s_no_block);

  while (!has_received_data.load())
    usleep(1000);

  BOOST_CHECK_EQUAL(recv_data.d1, 56);
  BOOST_CHECK_EQUAL(recv_data.d2, 26.5);
  BOOST_CHECK_EQUAL(recv_data.d3, "test1");

  has_received_data = false;
  q_sender->send(sent_data_q, std::chrono::milliseconds(10));

  while (!has_received_data.load())
    usleep(1000);

  BOOST_CHECK_EQUAL(recv_data.d1, 57);
  BOOST_CHECK_EQUAL(recv_data.d2, 27.5);
  BOOST_CHECK_EQUAL(recv_data.d3, "test2");

  iom.remove_callback<NonCopyableData>(test_conn);
  iom.remove_callback<NonCopyableData>(test_queue);
}

BOOST_FIXTURE_TEST_CASE(NonSerializableCallbackRegistration, ConfigurationTestFixture)
{

  IOManager iom;
  ConnectionID test_conn{ "network", "test_connection", "" };
  ConnectionID test_queue{ "queue", "test_queue", "" };
  auto net_sender = iom.get_sender<NonSerializableData>(test_conn);
  auto q_sender = iom.get_sender<NonSerializableData>(test_queue);

  NonSerializableData sent_data_nw(56, 26.5, "test1");
  NonSerializableData sent_data_q(57, 27.5, "test2");
  NonSerializableData recv_data;
  std::atomic<bool> has_received_data = false;

  std::function<void(NonSerializableData&)> callback = [&](NonSerializableData& d) {
    has_received_data = true;
    recv_data = std::move(d);
  };

  iom.add_callback<NonSerializableData>(test_conn, callback);
  iom.add_callback<NonSerializableData>(test_queue, callback);

  usleep(1000);

  net_sender->send(sent_data_nw, dunedaq::iomanager::Sender::s_no_block);

  while (!has_received_data.load())
    usleep(1000);

  BOOST_CHECK_EQUAL(recv_data.d1, 0);
  BOOST_CHECK_EQUAL(recv_data.d2, 0.0);
  BOOST_CHECK_EQUAL(recv_data.d3, "");

  // Have to stop the callback from endlessly setting recv_data to default-constructed object
  iom.remove_callback<NonSerializableData>(test_conn);
  has_received_data = false;
  q_sender->send(sent_data_q, std::chrono::milliseconds(10));

  while (!has_received_data.load())
    usleep(1000);

  BOOST_CHECK_EQUAL(recv_data.d1, 57);
  BOOST_CHECK_EQUAL(recv_data.d2, 27.5);
  BOOST_CHECK_EQUAL(recv_data.d3, "test2");

  iom.remove_callback<NonSerializableData>(test_queue);
}

BOOST_FIXTURE_TEST_CASE(NonSerializableNonCopyableCallbackRegistration, ConfigurationTestFixture)
{
  IOManager iom;
  ConnectionID test_conn{ "network", "test_connection", "" };
  ConnectionID test_queue{ "queue", "test_queue", "" };
  auto net_sender = iom.get_sender<NonSerializableNonCopyable>(test_conn);
  auto q_sender = iom.get_sender<NonSerializableNonCopyable>(test_queue);

  NonSerializableNonCopyable sent_data_nw(56, 26.5, "test1");
  NonSerializableNonCopyable sent_data_q(57, 27.5, "test2");
  NonSerializableNonCopyable recv_data;
  std::atomic<bool> has_received_data = false;

  std::function<void(NonSerializableNonCopyable&)> callback = [&](NonSerializableNonCopyable& d) {
    has_received_data = true;
    recv_data = std::move(d);
  };

  iom.add_callback<NonSerializableNonCopyable>(test_conn, callback);
  iom.add_callback<NonSerializableNonCopyable>(test_queue, callback);

  usleep(1000);

  net_sender->send(sent_data_nw, dunedaq::iomanager::Sender::s_no_block);

  while (!has_received_data.load())
    usleep(1000);

  BOOST_CHECK_EQUAL(recv_data.d1, 0);
  BOOST_CHECK_EQUAL(recv_data.d2, 0.0);
  BOOST_CHECK_EQUAL(recv_data.d3, "");

  // Have to stop the callback from endlessly setting recv_data to default-constructed object
  iom.remove_callback<NonSerializableNonCopyable>(test_conn);
  has_received_data = false;
  q_sender->send(sent_data_q, std::chrono::milliseconds(10));

  while (!has_received_data.load())
    usleep(1000);

  BOOST_CHECK_EQUAL(recv_data.d1, 57);
  BOOST_CHECK_EQUAL(recv_data.d2, 27.5);
  BOOST_CHECK_EQUAL(recv_data.d3, "test2");

  iom.remove_callback<NonSerializableNonCopyable>(test_queue);
}

BOOST_AUTO_TEST_SUITE_END()