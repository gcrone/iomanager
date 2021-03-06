/**
 * @file Sender.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef IOMANAGER_INCLUDE_IOMANAGER_SENDER_HPP_
#define IOMANAGER_INCLUDE_IOMANAGER_SENDER_HPP_

#include "iomanager/CommonIssues.hpp"
#include "iomanager/ConnectionId.hpp"
#include "iomanager/QueueRegistry.hpp"

#include "ipm/Sender.hpp"
#include "logging/Logging.hpp"
#include "networkmanager/NetworkManager.hpp"
#include "serialization/Serialization.hpp"

#include <memory>
#include <string>
#include <typeinfo>
#include <utility>

namespace dunedaq {

namespace iomanager {

// Typeless
class Sender : public utilities::NamedObject
{
public:
  using timeout_t = std::chrono::milliseconds;
  static constexpr timeout_t s_block = timeout_t::max();
  static constexpr timeout_t s_no_block = timeout_t::zero();

  explicit Sender(ConnectionId conn_id, ConnectionRef conn_ref)
    : utilities::NamedObject(conn_ref.name)
    , m_conn_id(conn_id)
    , m_conn_ref(conn_ref)
  {}
  virtual ~Sender() = default;

  ConnectionId const conn_id() const { return m_conn_id; }
  ConnectionRef const conn_ref() const { return m_conn_ref; }

protected:
  ConnectionId m_conn_id;
  ConnectionRef m_conn_ref;
};

// Interface
template<typename Datatype>
class SenderConcept : public Sender
{
public:
  explicit SenderConcept(ConnectionId conn_id, ConnectionRef conn_ref)
    : Sender(conn_id, conn_ref)
  {}
  virtual void send(Datatype&& data, Sender::timeout_t timeout, Topic_t topic = "") = 0;
  virtual bool try_send(Datatype&& data, Sender::timeout_t timeout, Topic_t topic = "") = 0;
};

// QImpl
template<typename Datatype>
class QueueSenderModel : public SenderConcept<Datatype>
{
public:
  explicit QueueSenderModel(ConnectionId conn_id, ConnectionRef conn_ref)
    : SenderConcept<Datatype>(conn_id, conn_ref)
  {
    TLOG() << "QueueSenderModel created with DT! Addr: " << static_cast<void*>(this);
    m_queue = QueueRegistry::get().get_queue<Datatype>(conn_id.uid);
    TLOG() << "QueueSenderModel m_queue=" << static_cast<void*>(m_queue.get());
    // get queue ref from queueregistry based on conn_id
  }

  QueueSenderModel(QueueSenderModel&& other)
    : SenderConcept<Datatype>(other.m_conn_id, other.m_conn_ref)
    , m_queue(other.m_queue)
  {}

  void send(Datatype&& data, Sender::timeout_t timeout, Topic_t topic = "") override
  {
    if (topic != "") {
      TLOG() << "Topics are invalid for queues! Check config!";
    }

    if (m_queue == nullptr)
      throw ConnectionInstanceNotFound(ERS_HERE, this->conn_id().uid);

    try {
      m_queue->push(std::move(data), timeout);
    } catch (QueueTimeoutExpired& ex) {
      throw TimeoutExpired(ERS_HERE, this->conn_id().uid, "push", timeout.count(), ex);
    }
  }

  bool try_send(Datatype&& data, Sender::timeout_t timeout, Topic_t topic = "") override
  {
    if (topic != "") {
      TLOG() << "Topics are invalid for queues! Check config!";
    }

    if (m_queue == nullptr) {
      ers::error(ConnectionInstanceNotFound(ERS_HERE, this->conn_id().uid));
      return false;
    }

    return m_queue->try_push(std::move(data), timeout);
  }

private:
  std::shared_ptr<Queue<Datatype>> m_queue;
};

// NImpl
template<typename Datatype>
class NetworkSenderModel : public SenderConcept<Datatype>
{
public:
  using SenderConcept<Datatype>::send;

  explicit NetworkSenderModel(ConnectionId conn_id, ConnectionRef conn_ref)
    : SenderConcept<Datatype>(conn_id, conn_ref)
  {
    TLOG() << "NetworkSenderModel created with DT! Addr: " << static_cast<void*>(this);
    // get network resources
    m_network_sender_ptr = networkmanager::NetworkManager::get().get_sender(conn_id.uid);
  }

  NetworkSenderModel(NetworkSenderModel&& other)
    : SenderConcept<Datatype>(other.m_conn_id, other.m_conn_ref)
    , m_network_sender_ptr(other.m_network_sender_ptr)
  {}

  void send(Datatype&& data, Sender::timeout_t timeout, Topic_t topic = "") override
  {
    try {
      write_network<Datatype>(data, timeout, topic);
    } catch (ipm::SendTimeoutExpired& ex) {
      throw TimeoutExpired(ERS_HERE, this->conn_id().uid, "send", timeout.count(), ex);
    }
  }

  bool try_send(Datatype&& data, Sender::timeout_t timeout, Topic_t topic = "") override
  {
    return try_write_network<Datatype>(data, timeout, topic);
  }

private:
  template<typename MessageType>
  typename std::enable_if<dunedaq::serialization::is_serializable<MessageType>::value, void>::type
  write_network(MessageType& message, Sender::timeout_t const& timeout, std::string const& topic = "")
  {
    if (m_network_sender_ptr == nullptr)
      throw ConnectionInstanceNotFound(ERS_HERE, this->conn_id().uid);

    auto serialized = dunedaq::serialization::serialize(message, dunedaq::serialization::kMsgPack);
    // TLOG() << "Serialized message for network sending: " << serialized.size() << ", this=" << (void*)this;
    std::lock_guard<std::mutex> lk(m_send_mutex);

    m_network_sender_ptr->send(serialized.data(), serialized.size(), timeout, topic);
  }

  template<typename MessageType>
  typename std::enable_if<!dunedaq::serialization::is_serializable<MessageType>::value, void>::type
  write_network(MessageType&, Sender::timeout_t const&, std::string const&)
  {
    throw NetworkMessageNotSerializable(ERS_HERE, typeid(MessageType).name());
  }

  template<typename MessageType>
  typename std::enable_if<dunedaq::serialization::is_serializable<MessageType>::value, bool>::type
      try_write_network(MessageType& message, Sender::timeout_t const& timeout, std::string const& topic = "")
  {
    if (m_network_sender_ptr == nullptr) {
      ers::error(ConnectionInstanceNotFound(ERS_HERE, this->conn_id().uid));
      return false;
    }

    auto serialized = dunedaq::serialization::serialize(message, dunedaq::serialization::kMsgPack);
    // TLOG() << "Serialized message for network sending: " << serialized.size() << ", this=" << (void*)this;
    std::lock_guard<std::mutex> lk(m_send_mutex);

    return m_network_sender_ptr->send(serialized.data(), serialized.size(), timeout, topic, true);
  }

  template<typename MessageType>
  typename std::enable_if<!dunedaq::serialization::is_serializable<MessageType>::value, bool>::type
      try_write_network(MessageType&, Sender::timeout_t const&, std::string const&)
  {
    ers::error(NetworkMessageNotSerializable(ERS_HERE, typeid(MessageType).name()));
    return false;
  }

  std::shared_ptr<ipm::Sender> m_network_sender_ptr;
  std::mutex m_send_mutex;
};

} // namespace iomanager
} // namespace dunedaq

#endif // IOMANAGER_INCLUDE_IOMANAGER_SENDER_HPP_
