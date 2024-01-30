// Copyright 2023 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <zenoh.h>

#include <cstring>
#include <mutex>
#include <optional>
#include <utility>

#include "rcpputils/scope_exit.hpp"
#include "rcutils/logging_macros.h"

#include "rmw/error_handling.h"

#include "rmw_data_types.hpp"

///=============================================================================
saved_msg_data::saved_msg_data(zc_owned_payload_t p, uint64_t recv_ts, const uint8_t pub_gid[16])
: payload(p), recv_timestamp(recv_ts)
{
  memcpy(publisher_gid, pub_gid, 16);
}

///=============================================================================
void rmw_publisher_data_t::event_set_callback(
  rmw_zenoh_event_type_t event_id,
  rmw_event_callback_t callback,
  const void * user_data)
{
  if (event_id > ZENOH_EVENT_ID_MAX) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "RMW Zenoh is not correctly configured to handle rmw_zenoh_event_type_t [%d].Report this bug.", event_id);
    return;
  }

  std::lock_guard<std::mutex> lock(user_callback_data_.mutex);

  // Set the user callback data
  user_callback_data_.event_callback[event_id] = callback;
  user_callback_data_.event_data[event_id] = user_data;

  if (callback && user_callback_data_.event_unread_count[event_id]) {
    // Push events happened before having assigned a callback
    callback(user_data, user_callback_data_.event_unread_count[event_id]);
    user_callback_data_.event_unread_count[event_id] = 0;
  }
  return;
}

///=============================================================================
void rmw_subscription_data_t::attach_condition(std::condition_variable * condition_variable)
{
  std::lock_guard<std::mutex> lock(condition_mutex_);
  condition_ = condition_variable;
}

///=============================================================================
void rmw_subscription_data_t::notify()
{
  std::lock_guard<std::mutex> lock(condition_mutex_);
  if (condition_ != nullptr) {
    condition_->notify_one();
  }
}

///=============================================================================
void rmw_subscription_data_t::detach_condition()
{
  std::lock_guard<std::mutex> lock(condition_mutex_);
  condition_ = nullptr;
}

///=============================================================================
bool rmw_subscription_data_t::message_queue_is_empty() const
{
  std::lock_guard<std::mutex> lock(message_queue_mutex_);
  return message_queue_.empty();
}

///=============================================================================
std::unique_ptr<saved_msg_data> rmw_subscription_data_t::pop_next_message()
{
  std::lock_guard<std::mutex> lock(message_queue_mutex_);

  if (message_queue_.empty()) {
    // This tells rcl that the check for a new message was done, but no messages have come in yet.
    return nullptr;
  }

  std::unique_ptr<saved_msg_data> msg_data = std::move(message_queue_.front());
  message_queue_.pop_front();

  return msg_data;
}

///=============================================================================
void rmw_subscription_data_t::add_new_message(
  std::unique_ptr<saved_msg_data> msg, const std::string & topic_name)
{
  std::lock_guard<std::mutex> lock(message_queue_mutex_);

  if (message_queue_.size() >= adapted_qos_profile.depth) {
    // Log warning if message is discarded due to hitting the queue depth
    RCUTILS_LOG_DEBUG_NAMED(
      "rmw_zenoh_cpp",
      "Message queue depth of %ld reached, discarding oldest message "
      "for subscription for %s",
      adapted_qos_profile.depth,
      topic_name.c_str());

    // If the adapted_qos_profile.depth is 0, the std::move command below will result
    // in UB and the z_drop will segfault. We explicitly set the depth to a minimum of 1
    // in rmw_create_subscription() but to be safe, we only attempt to discard from the
    // queue if it is non-empty.
    if (!message_queue_.empty()) {
      std::unique_ptr<saved_msg_data> old = std::move(message_queue_.front());
      z_drop(z_move(old->payload));
      message_queue_.pop_front();
    }
  }

  message_queue_.emplace_back(std::move(msg));

  // Trigger the user provided event callback if available.
  std::unique_lock<std::mutex> lock_event_mutex(user_callback_data_.mutex);
  if (user_callback_data_.callback != nullptr) {
    user_callback_data_.callback(user_callback_data_.user_data, 1);
  } else {
    ++user_callback_data_.unread_count;
  }
  lock_event_mutex.unlock();

  // Since we added new data, trigger the guard condition if it is available
  notify();
}

///=============================================================================
void rmw_subscription_data_t::set_on_new_message_callback(
  const void * user_data, rmw_event_callback_t callback)
{
  std::lock_guard<std::mutex> lock_mutex(user_callback_data_.mutex);

  if (callback) {
    // Push events arrived before setting the the executor callback.
    if (user_callback_data_.unread_count) {
      callback(user_data, user_callback_data_.unread_count);
      user_callback_data_.unread_count = 0;
    }
    user_callback_data_.user_data = user_data;
    user_callback_data_.callback = callback;
  } else {
    user_callback_data_.user_data = nullptr;
    user_callback_data_.callback = nullptr;
  }
}

///=============================================================================
void rmw_subscription_data_t::event_set_callback(
  rmw_zenoh_event_type_t event_id,
  rmw_event_callback_t callback,
  const void * user_data)
{
  if (event_id > ZENOH_EVENT_ID_MAX) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "RMW Zenoh is not correctly configured to handle rmw_zenoh_event_type_t [%d].Report this bug.", event_id);
    return;
  }

  std::lock_guard<std::mutex> lock(user_callback_data_.mutex);

  // Set the user callback data
  user_callback_data_.event_callback[event_id] = callback;
  user_callback_data_.event_data[event_id] = user_data;

  if (callback && user_callback_data_.event_unread_count[event_id]) {
    // Push events happened before having assigned a callback
    callback(user_data, user_callback_data_.event_unread_count[event_id]);
    user_callback_data_.event_unread_count[event_id] = 0;
  }
  return;
}

///=============================================================================
bool rmw_service_data_t::query_queue_is_empty() const
{
  std::lock_guard<std::mutex> lock(query_queue_mutex_);
  return query_queue_.empty();
}

///=============================================================================
void rmw_service_data_t::attach_condition(std::condition_variable * condition_variable)
{
  std::lock_guard<std::mutex> lock(condition_mutex_);
  condition_ = condition_variable;
}

///=============================================================================
void rmw_service_data_t::detach_condition()
{
  std::lock_guard<std::mutex> lock(condition_mutex_);
  condition_ = nullptr;
}

///=============================================================================
std::unique_ptr<ZenohQuery> rmw_service_data_t::pop_next_query()
{
  std::lock_guard<std::mutex> lock(query_queue_mutex_);
  if (query_queue_.empty()) {
    return nullptr;
  }

  std::unique_ptr<ZenohQuery> query = std::move(query_queue_.front());
  query_queue_.pop_front();

  return query;
}

///=============================================================================
void rmw_service_data_t::notify()
{
  std::lock_guard<std::mutex> lock(condition_mutex_);
  if (condition_ != nullptr) {
    condition_->notify_one();
  }
}

///=============================================================================
void rmw_service_data_t::add_new_query(std::unique_ptr<ZenohQuery> query)
{
  std::lock_guard<std::mutex> lock(query_queue_mutex_);
  query_queue_.emplace_back(std::move(query));

  // Trigger the user provided event callback if available.
  std::unique_lock<std::mutex> lock_event_mutex(user_callback_data_.mutex);
  if (user_callback_data_.callback != nullptr) {
    user_callback_data_.callback(user_callback_data_.user_data, 1);
  } else {
    ++user_callback_data_.unread_count;
  }
  lock_event_mutex.unlock();

  // Since we added new data, trigger the guard condition if it is available
  notify();
}

///=============================================================================
bool rmw_service_data_t::add_to_query_map(
  int64_t sequence_number, std::unique_ptr<ZenohQuery> query)
{
  std::lock_guard<std::mutex> lock(sequence_to_query_map_mutex_);
  if (sequence_to_query_map_.find(sequence_number) != sequence_to_query_map_.end()) {
    return false;
  }
  sequence_to_query_map_.emplace(
    std::pair(sequence_number, std::move(query)));

  return true;
}

///=============================================================================
std::unique_ptr<ZenohQuery> rmw_service_data_t::take_from_query_map(int64_t sequence_number)
{
  std::lock_guard<std::mutex> lock(sequence_to_query_map_mutex_);
  auto query_it = sequence_to_query_map_.find(sequence_number);
  if (query_it == sequence_to_query_map_.end()) {
    return nullptr;
  }

  std::unique_ptr<ZenohQuery> query = std::move(query_it->second);
  sequence_to_query_map_.erase(query_it);

  return query;
}

///=============================================================================
void rmw_service_data_t::set_on_new_request_callback(
  const void * user_data, rmw_event_callback_t callback)
{
  std::lock_guard<std::mutex> lock_mutex(user_callback_data_.mutex);

  if (callback) {
    // Push events arrived before setting the the executor callback.
    if (user_callback_data_.unread_count) {
      callback(user_data, user_callback_data_.unread_count);
      user_callback_data_.unread_count = 0;
    }
    user_callback_data_.user_data = user_data;
    user_callback_data_.callback = callback;
  } else {
    user_callback_data_.user_data = nullptr;
    user_callback_data_.callback = nullptr;
  }
}

///=============================================================================
void rmw_client_data_t::notify()
{
  std::lock_guard<std::mutex> lock(condition_mutex_);
  if (condition_ != nullptr) {
    condition_->notify_one();
  }
}

///=============================================================================
void rmw_client_data_t::add_new_reply(std::unique_ptr<ZenohReply> reply)
{
  std::lock_guard<std::mutex> lock(reply_queue_mutex_);
  reply_queue_.emplace_back(std::move(reply));

  // Trigger the user provided event callback if available.
  std::unique_lock<std::mutex> lock_event_mutex(user_callback_data_.mutex);
  if (user_callback_data_.callback != nullptr) {
    user_callback_data_.callback(user_callback_data_.user_data, 1);
  } else {
    ++user_callback_data_.unread_count;
  }
  lock_event_mutex.unlock();

  notify();
}

///=============================================================================
bool rmw_client_data_t::reply_queue_is_empty() const
{
  std::lock_guard<std::mutex> lock(reply_queue_mutex_);

  return reply_queue_.empty();
}

///=============================================================================
void rmw_client_data_t::attach_condition(std::condition_variable * condition_variable)
{
  std::lock_guard<std::mutex> lock(condition_mutex_);
  condition_ = condition_variable;
}

///=============================================================================
void rmw_client_data_t::detach_condition()
{
  std::lock_guard<std::mutex> lock(condition_mutex_);
  condition_ = nullptr;
}

///=============================================================================
std::unique_ptr<ZenohReply> rmw_client_data_t::pop_next_reply()
{
  std::lock_guard<std::mutex> lock(reply_queue_mutex_);

  if (reply_queue_.empty()) {
    return nullptr;
  }

  std::unique_ptr<ZenohReply> latest_reply = std::move(reply_queue_.front());
  reply_queue_.pop_front();

  return latest_reply;
}

///=============================================================================
void rmw_client_data_t::set_on_new_response_callback(
  const void * user_data, rmw_event_callback_t callback)
{
  std::lock_guard<std::mutex> lock_mutex(user_callback_data_.mutex);

  if (callback) {
    // Push events arrived before setting the the executor callback.
    if (user_callback_data_.unread_count) {
      callback(user_data, user_callback_data_.unread_count);
      user_callback_data_.unread_count = 0;
    }
    user_callback_data_.user_data = user_data;
    user_callback_data_.callback = callback;
  } else {
    user_callback_data_.user_data = nullptr;
    user_callback_data_.callback = nullptr;
  }
}

//==============================================================================
void sub_data_handler(
  const z_sample_t * sample,
  void * data)
{
  z_owned_str_t keystr = z_keyexpr_to_string(sample->keyexpr);
  auto drop_keystr = rcpputils::make_scope_exit(
    [&keystr]() {
      z_drop(z_move(keystr));
    });

  auto sub_data = static_cast<rmw_subscription_data_t *>(data);
  if (sub_data == nullptr) {
    RCUTILS_LOG_ERROR_NAMED(
      "rmw_zenoh_cpp",
      "Unable to obtain rmw_subscription_data_t from data for "
      "subscription for %s",
      z_loan(keystr)
    );
    return;
  }

  sub_data->add_new_message(
    std::make_unique<saved_msg_data>(
      zc_sample_payload_rcinc(sample),
      sample->timestamp.time, sample->timestamp.id.id), z_loan(keystr));
}

///=============================================================================
ZenohQuery::ZenohQuery(const z_query_t * query)
{
  query_ = z_query_clone(query);
}

///=============================================================================
ZenohQuery::~ZenohQuery()
{
  z_drop(z_move(query_));
}

///=============================================================================
const z_query_t ZenohQuery::get_query() const
{
  return z_query_loan(&query_);
}

//==============================================================================
void service_data_handler(const z_query_t * query, void * data)
{
  z_owned_str_t keystr = z_keyexpr_to_string(z_query_keyexpr(query));
  auto drop_keystr = rcpputils::make_scope_exit(
    [&keystr]() {
      z_drop(z_move(keystr));
    });

  rmw_service_data_t * service_data = static_cast<rmw_service_data_t *>(data);
  if (service_data == nullptr) {
    RCUTILS_LOG_ERROR_NAMED(
      "rmw_zenoh_cpp",
      "Unable to obtain rmw_service_data_t from data for "
      "service for %s",
      z_loan(keystr)
    );
    return;
  }

  service_data->add_new_query(std::make_unique<ZenohQuery>(query));
}

///=============================================================================
ZenohReply::ZenohReply(const z_owned_reply_t * reply)
{
  reply_ = *reply;
}

///=============================================================================
ZenohReply::~ZenohReply()
{
  z_reply_drop(z_move(reply_));
}

///=============================================================================
std::optional<z_sample_t> ZenohReply::get_sample() const
{
  if (z_reply_is_ok(&reply_)) {
    return z_reply_ok(&reply_);
  }

  return std::nullopt;
}

///=============================================================================
size_t rmw_client_data_t::get_next_sequence_number()
{
  std::lock_guard<std::mutex> lock(sequence_number_mutex);
  return sequence_number++;
}

//==============================================================================
void client_data_handler(z_owned_reply_t * reply, void * data)
{
  auto client_data = static_cast<rmw_client_data_t *>(data);
  if (client_data == nullptr) {
    RCUTILS_LOG_ERROR_NAMED(
      "rmw_zenoh_cpp",
      "Unable to obtain client_data_t "
    );
    return;
  }
  if (!z_reply_check(reply)) {
    RCUTILS_LOG_ERROR_NAMED(
      "rmw_zenoh_cpp",
      "z_reply_check returned False"
    );
    return;
  }
  if (!z_reply_is_ok(reply)) {
    RCUTILS_LOG_ERROR_NAMED(
      "rmw_zenoh_cpp",
      "z_reply_is_ok returned False"
    );
    return;
  }

  client_data->add_new_reply(std::make_unique<ZenohReply>(reply));
  // Since we took ownership of the reply, null it out here
  *reply = z_reply_null();
}
