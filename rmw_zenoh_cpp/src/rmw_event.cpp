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

#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/types.h"

#include "detail/event.hpp"
#include "detail/identifier.hpp"
#include "detail/rmw_data_types.hpp"

extern "C"
{
///==============================================================================
/// Initialize a rmw publisher event
rmw_ret_t
rmw_publisher_event_init(
  rmw_event_t * rmw_event,
  const rmw_publisher_t * publisher,
  rmw_event_type_t event_type)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(rmw_event, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(publisher, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(publisher->implementation_identifier, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(publisher->data, RMW_RET_INVALID_ARGUMENT);
  rmw_publisher_data_t * pub_data = static_cast<rmw_publisher_data_t *>(publisher->data);
  RMW_CHECK_ARGUMENT_FOR_NULL(pub_data, RMW_RET_INVALID_ARGUMENT);

  if (publisher->implementation_identifier != rmw_zenoh_identifier) {
    RMW_SET_ERROR_MSG("Publisher implementation identifier not from this implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }

  if (event_map.count(event_type) != 1) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "provided event_type %d is not supported by rmw_zenoh_cpp", event_type);
    return RMW_RET_UNSUPPORTED;
  }

  rmw_event->implementation_identifier = publisher->implementation_identifier;
  rmw_event->data = pub_data;
  rmw_event->event_type = event_type;

  return RMW_RET_OK;
}

///==============================================================================
/// Take an event from the event handle.
rmw_ret_t
rmw_subscription_event_init(
  rmw_event_t * rmw_event,
  const rmw_subscription_t * subscription,
  rmw_event_type_t event_type)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(rmw_event, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(subscription, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(subscription->implementation_identifier, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(subscription->data, RMW_RET_INVALID_ARGUMENT);
  rmw_subscription_data_t * sub_data = static_cast<rmw_subscription_data_t *>(subscription->data);
  RMW_CHECK_ARGUMENT_FOR_NULL(sub_data, RMW_RET_INVALID_ARGUMENT);

  if (subscription->implementation_identifier != rmw_zenoh_identifier) {
    RMW_SET_ERROR_MSG(
      "Subscription implementation identifier not from this implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }

  if (event_map.count(event_type) != 1) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "provided event_type %d is not supported by rmw_zenoh_cpp", event_type);
    return RMW_RET_UNSUPPORTED;
  }

  rmw_event->implementation_identifier = subscription->implementation_identifier;
  rmw_event->data = sub_data;
  rmw_event->event_type = event_type;

  return RMW_RET_OK;
}

//==============================================================================
/// Set the callback function for the event.
rmw_ret_t
rmw_event_set_callback(
  rmw_event_t * rmw_event,
  rmw_event_callback_t callback,
  const void * user_data)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(rmw_event, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(rmw_event->data, RMW_RET_INVALID_ARGUMENT);

  auto zenoh_event_it = event_map.find(rmw_event->event_type);
  if (zenoh_event_it == event_map.end()) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("RMW Zenoh does not support event [%d]", rmw_event->event_type);
    return RMW_RET_ERROR;
  }

  switch (zenoh_event_it->second) {
    case ZENOH_EVENT_REQUESTED_QOS_INCOMPATIBLE: {
      rmw_subscription_data_t * sub_data = static_cast<rmw_subscription_data_t *>(rmw_event->data);
      RMW_CHECK_ARGUMENT_FOR_NULL(sub_data, RMW_RET_INVALID_ARGUMENT);
      sub_data->event_set_callback(
        zenoh_event_it->second,
        callback,
        user_data);
      break;
    }
    case ZENOH_EVENT_OFFERED_QOS_INCOMPATIBLE: {
      rmw_publisher_data_t * pub_data = static_cast<rmw_publisher_data_t *>(rmw_event->data);
      RMW_CHECK_ARGUMENT_FOR_NULL(pub_data, RMW_RET_INVALID_ARGUMENT);
      pub_data->event_set_callback(
        zenoh_event_it->second,
        callback,
        user_data);
      break;
    }
    default: {
      return RMW_RET_INVALID_ARGUMENT;
    }
  }

  return RMW_RET_OK;
}

///==============================================================================
rmw_ret_t
rmw_take_event(
  const rmw_event_t * event_handle,
  void * event_info,
  bool * taken)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(event_handle, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(event_info, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(taken, RMW_RET_INVALID_ARGUMENT);

  *taken = false;

  if (event_handle->implementation_identifier != rmw_zenoh_identifier) {
    RMW_SET_ERROR_MSG(
      "Event implementation identifier not from this implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }

  auto zenoh_event_it = event_map.find(event_handle->event_type);
  if (zenoh_event_it == event_map.end()) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("RMW Zenoh does not support event [%d]", event_handle->event_type);
    return RMW_RET_ERROR;
  }

  switch (zenoh_event_it->second) {
    case ZENOH_EVENT_REQUESTED_QOS_INCOMPATIBLE: {
      rmw_subscription_data_t * sub_data = static_cast<rmw_subscription_data_t *>(event_handle->data);
      RMW_CHECK_ARGUMENT_FOR_NULL(sub_data, RMW_RET_INVALID_ARGUMENT);
      auto ei = static_cast<rmw_requested_qos_incompatible_event_status_t *>(event_info);
      ei->total_count = 0;
      ei->total_count_change = 0;
      *taken = true;
      return RMW_RET_OK;
    }
    case ZENOH_EVENT_OFFERED_QOS_INCOMPATIBLE: {
      rmw_publisher_data_t * pub_data = static_cast<rmw_publisher_data_t *>(event_handle->data);
      auto ei = static_cast<rmw_offered_qos_incompatible_event_status_t *>(event_info);
      ei->total_count = 0;
      ei->total_count_change = 0;
      *taken = true;
      return RMW_RET_OK;
    }
    default: {
      return RMW_RET_INVALID_ARGUMENT;
    }
  }

  return RMW_RET_ERROR;
}
}  // extern "C"
