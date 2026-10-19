// Which player the keyboard controls (PC Options > Controls > Keyboard Player).
//
//   auto  keyboard and the first controller both control player 1, the second controller is
//         player 2 (the SDK's default)
//   1     keyboard is player 1, controllers are players 2, 3 and 4
//   2     the first controller is player 1, the keyboard player 2, other controllers 3 and 4
//
// With no controller connected the keyboard is always player 1, so the menus stay usable.

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <rex/input/device_assignment.h>

class PlayerAssignment final : public rex::input::DeviceAssignment {
 public:
  void OnDevicesChanged(const std::vector<rex::input::DeviceInfo>& devices) override;
  void DevicesForUser(uint32_t user_index, std::vector<rex::input::DeviceId>& out) const override;

  // Guest user (player slot) a device feeds, or -1 if it is unknown.
  int UserForDevice(rex::input::DeviceId id) const;
  // 0 for the first connected controller, 1 for the second, ...; -1 if not a controller.
  int ControllerNumber(rex::input::DeviceId id) const;

  // The assignment the app installed, or nullptr before that.
  static const PlayerAssignment* Current();

 private:
  uint32_t UserFor(const rex::input::DeviceInfo& device) const;  // mutex_ held

  mutable std::mutex mutex_;
  std::vector<rex::input::DeviceInfo> devices_;
  mutable std::string logged_mode_;
};
