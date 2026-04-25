#pragma once

#include <iostream>
#include <stdexcept>
#include <string>

#include "rclcpp/rclcpp.hpp"

struct QoSConfig
{
  static constexpr const char * kBestEffort = "BEST_EFFORT";
  static constexpr const char * kReliable = "RELIABLE";
  static constexpr const char * kKeepAll = "KEEP_ALL";
  static constexpr const char * kKeepLast = "KEEP_LAST";
  static constexpr const char * kVolatile = "VOLATILE";
  static constexpr const char * kTransientLocal = "TRANSIENT_LOCAL";

  std::string reliability = kReliable;
  std::string history = kKeepLast;
  std::size_t history_depth = 16;
  std::string durability = kVolatile;

  static void declare_parameters(rclcpp::Node & node)
  {
    node.declare_parameter("reliability", std::string{kReliable});
    node.declare_parameter("history", std::string{kKeepLast});
    node.declare_parameter("history_depth", static_cast<int64_t>(16));
    node.declare_parameter("durability", std::string{kVolatile});
  }

  static QoSConfig from_node(const rclcpp::Node & node)
  {
    QoSConfig config;
    config.reliability = node.get_parameter("reliability").as_string();
    config.history = node.get_parameter("history").as_string();
    config.history_depth = static_cast<std::size_t>(node.get_parameter("history_depth").as_int());
    config.durability = node.get_parameter("durability").as_string();
    return config;
  }

  rclcpp::QoS to_rclcpp_qos() const
  {
    rclcpp::QoS qos{rclcpp::KeepLast{history_depth}};

    if (reliability == kBestEffort) {
      qos.best_effort();
    } else if (reliability == kReliable) {
      qos.reliable();
    } else {
      throw std::runtime_error("reliability should be either BEST_EFFORT or RELIABLE");
    }

    if (history == kKeepAll) {
      qos.keep_all();
    } else if (history == kKeepLast) {
      qos.keep_last(history_depth);
    } else {
      throw std::runtime_error("history should be either KEEP_ALL or KEEP_LAST");
    }

    if (durability == kVolatile) {
      qos.durability_volatile();
    } else if (durability == kTransientLocal) {
      qos.transient_local();
    } else {
      throw std::runtime_error("durability should be either VOLATILE or TRANSIENT_LOCAL");
    }

    return qos;
  }

  void print() const
  {
    std::cout << "QoS: " << reliability << "," << durability << "," << history;
    if (history == kKeepLast) {
      std::cout << "(" << history_depth << ")";
    }
    std::cout << std::endl;
  }
};
