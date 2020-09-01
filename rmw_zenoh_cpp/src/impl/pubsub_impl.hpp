#ifndef RMW_ZENOH_CPP__ZENOH_PUBSUB_HPP_
#define RMW_ZENOH_CPP__ZENOH_PUBSUB_HPP_

#include <unordered_map>
#include <utility>
#include <string>
#include <vector>

#include "rmw/rmw.h"
#include "rmw_zenoh_cpp/TypeSupport.hpp"

extern "C"
{
  #include "zenoh/zenoh-ffi.h"
}

struct rmw_node_impl_t
{

};

struct rmw_publisher_data_t
{
  const void * type_support_impl_;
  const char * typesupport_identifier_;

  rmw_zenoh_cpp::TypeSupport * type_support_;

  size_t zn_topic_id_;
  ZNSession * zn_session_;

  const rmw_node_t * node_;
};

// Functionally a struct. But with a method for handling incoming Zenoh messages
struct rmw_subscription_data_t
{
  static void zn_sub_callback(const zn_sample * sample);

  // Map of Zenoh topic key expression to latest serialized ROS messages
  static std::unordered_map<std::string, std::vector<unsigned char> > zn_messages_;

  const void * type_support_impl_;
  const char * typesupport_identifier_;

  rmw_zenoh_cpp::TypeSupport * type_support_;
  const rmw_node_t * node_;

  ZNSession * zn_session_;
  ZNSubscriber * zn_subscriber_;
};

#endif // RMW_ZENOH_CPP__ZENOH_PUBSUB_HPP_
