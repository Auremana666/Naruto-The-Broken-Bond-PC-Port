#pragma once

#include "gpu/d3d/init/scalar_defaults.h"
#include "gpu/d3d/init/default_dispatch_table.h"

namespace gpu {
// A key selects an own service contract. It is never a callable guest address.
struct DefaultDispatchKey {
  bool indexed = false, getter = false;
  unsigned index = 0;
  bool operator==(const DefaultDispatchKey&) const = default;
};

class DefaultDispatchServices final {
 public:
  bool Initialize(uint64_t generation) {
    if (!generation || generation_ || generation <= last_generation_) return false;
    generation_ = last_generation_ = generation;
    return true;
  }
  bool PrepareInstall(uint64_t generation, const ScalarDefaultInput& input,
                      ScalarDefaultInput& output) const {
    if (!Active(generation)) return false;
    auto plan = input;
    for (unsigned i = 0; i < 101; ++i) {
      plan.Store(64 + 4 * i, kScalarDispatchRows[i][0]);
      plan.Store(548 + 4 * i, kScalarDispatchRows[i][1]);
    }
    for (unsigned i = 0; i < 20; ++i) {
      plan.Store(468 + 4 * i, kIndexedDispatchRows[i][0]);
      plan.Store(952 + 4 * i, kIndexedDispatchRows[i][1]);
    }
    output = plan;
    return true;
  }
  bool Resolve(uint64_t generation, bool indexed, bool getter, unsigned index,
               DefaultDispatchKey& output) const {
    if (!Active(generation) || index >= (indexed ? 20u : 101u)) return false;
    output = {indexed, getter, index};
    return true;
  }
  bool ReadScalar(uint64_t generation, const ScalarDefaultInput& cpu,
                  unsigned index, uint32_t& output) const {
    if (!Active(generation) || index >= 101) return false;
    uint32_t value = 0;
    const auto field = [&](unsigned at, unsigned shift, uint32_t mask) {
      return (cpu.Word(at) >> shift) & mask;
    };
    if (index < 10) { output = 0; return true; }
    // Two words each contain eight nibbles in source order.
    if (index >= 60 && index <= 75) {
      output = field(index < 68 ? 10540 : 10544, 4 * ((index - 60) % 8), 15);
      return true;
    }
    switch (index) {
      case 10: value = cpu.Word(11876); break;
      case 11: value = field(10548, 4, 7); break;
      case 12: value = field(10548, 2, 1); break;
      case 13: value = field(10568, 3, 255); break;
      case 14: value = field(10568, 0, 7); break;
      case 15: value = field(11852, 31, 1); break;
      case 16: value = field(11852, 30, 1); break;
      case 17: {
        std::array<uint32_t, 4> c{};
        for (unsigned i = 0; i < 4; ++i)
          if (!Color(cpu.Word(10464 + 4 * i), c[i])) return false;
        value = (c[2] & 255) | ((c[1] & 255) << 8) |
            ((c[0] & 255) << 16) | ((c[3] & 255) << 24);
        break;
      }
      case 18: value = field(11848, 0, 31); break;
      case 19: value = field(11848, 8, 31); break;
      case 20: value = field(11848, 5, 7); break;
      case 21: value = field(11848, 16, 31); break;
      case 22: value = field(11848, 24, 31); break;
      case 23: value = field(11848, 21, 7); break;
      case 24: value = field(10556, 3, 1); break;
      case 25: if (!Color(cpu.Word(10500), value)) return false; break;
      case 26: value = field(10556, 0, 7); break;
      case 27: value = cpu.Word(11880); break;
      case 28: value = field(10548, 7, 1); break;
      case 29: value = field(10548, 11, 7); break;
      case 30: value = field(10548, 17, 7); break;
      case 31: value = field(10548, 14, 7); break;
      case 32: value = field(10548, 8, 7); break;
      case 33: value = cpu.cpu[10499]; break;
      case 34: value = cpu.cpu[10498]; break;
      case 35: value = cpu.cpu[10497]; break;
      case 36: value = field(10548, 23, 7); break;
      case 37: value = field(10548, 29, 7); break;
      case 38: value = field(10548, 26, 7); break;
      case 39: value = field(10548, 20, 7); break;
      case 40: value = cpu.cpu[10495]; break;
      case 41: value = cpu.cpu[10494]; break;
      case 42: value = cpu.cpu[10493]; break;
      case 43: value = field(10564, 0, 63); break;
      case 44: if (!FloatBits(cpu.Word(11892), value)) return false; break;
      case 45: if (!FloatBits(cpu.Word(11896), value)) return false; break;
      case 46: value = cpu.Word(11884); break;
      case 47: if (!FloatBits(cpu.Word(11900), value)) return false; break;
      case 48: value = field(10568, 15, 1); break;
      case 49: value = cpu.Word(10752); break;
      case 50: value = cpu.Word(11856); break;
      case 51: {
        const float f = std::bit_cast<float>(cpu.Word(10832));
        if (!std::isfinite(f) || std::abs(f) > 1048576) return false;
        value = std::bit_cast<uint32_t>(f * 0.0625f); break;
      }
      case 52: if (!FloatBits(cpu.Word(10836), value)) return false; break;
      case 53: case 54: case 55: case 56: value = cpu.Word(11860 + 4 * (index - 53)); break;
      case 57: value = field(10616, 0, 3); break;
      case 58: if (!FloatBits(cpu.Word(10624), value)) return false; break;
      case 59: if (!FloatBits(cpu.Word(10620), value)) return false; break;
      case 76: value = field(10572, 0, 1); break;
      case 77: case 78: case 79: case 80: value = cpu.Word(12028 + 4 * (index - 77)); break;
      case 81: value = field(10688, 0, 1); break;
      case 82: value = field(10568, 21, 1); break;
      case 83: value = cpu.Word(10456); break;
      case 84: value = field(10556, 4, 1); break;
      case 85: value = cpu.cpu[10556]; break;
      case 86: value = cpu.Word(10700); break;
      case 87: value = cpu.Word(10692); break;
      case 88: value = cpu.Word(10704); break;
      case 89: value = cpu.Word(10696); break;
      case 90: value = field(10560, 3, 1); break;
      case 91: value = field(10560, 2, 1); break;
      case 92: value = field(10560, 5, 1); break;
      case 93: value = cpu.cpu[10562]; break;
      case 94: value = cpu.Word(13596); break;
      case 95: value = field(11852, 23, 127); break;
      case 96: value = field(11852, 20, 7); break;
      case 97: value = field(11852, 17, 7); break;
      case 98: value = field(10680, 10, 1); break;
      case 99: value = std::bit_cast<uint32_t>(float(uint32_t(cpu.cpu[10606]) << 8 | cpu.cpu[10607]) * 0.125f); break;
      case 100: value = field(23980, 0, 1); break;
      default: return false;
    }
    output = value;
    return true;
  }
  bool ReadIndexed(uint64_t generation, const ScalarDefaultInput& cpu,
                   unsigned index, unsigned slot, uint32_t& output) const {
    if (!Active(generation) || index >= 20 || slot >= 26) return false;
    const unsigned record = 1152 + 24 * slot;
    const auto field = [&](unsigned word, unsigned shift, uint32_t mask) {
      return (cpu.Word(record + 4 * word) >> shift) & mask;
    };
    uint32_t value = 0;
    switch (index) {
      case 0: value = field(0, 10, 7); break;
      case 1: value = field(0, 13, 7); break;
      case 2: value = field(0, 16, 7); break;
      case 3: value = field(5, 0, 3) ? 0xFFFFFFFF : 0; break;
      // rlwimi replaces bit 2 even on the ordinary path; it does not keep
      // the third bit of the packed field.
      case 4: value = field(4, 10, 1) ? 4 : field(3, 19, 3); break;
      case 5: value = field(4, 11, 1) ? 4 : field(3, 21, 3); break;
      case 6: value = field(3, 23, 3); break;
      case 7: value = std::bit_cast<uint32_t>(float(Signed(field(4, 12, 1023), 10)) * 0.03125f); break;
      case 8: value = cpu.cpu[11950 + slot]; break;
      case 9: value = cpu.cpu[11924 + slot]; break;
      case 10: case 11: case 12: value = (cpu.cpu[12002 + slot] >> (index - 10)) & 1; break;
      case 13: value = cpu.cpu[11976 + slot]; break;
      case 14: value = field(5, 3, 3); break;
      case 15: value = std::bit_cast<uint32_t>(-(float(Signed(field(5, 5, 15), 4)) * 0.125f)); break;
      case 16: value = uint32_t(Signed(field(4, 22, 31), 5)); break;
      case 17: value = uint32_t(Signed(field(4, 27, 31), 5)); break;
      case 18: value = field(5, 2, 1); break;
      case 19: value = field(1, 11, 1) ^ 1; break;
      default: return false;
    }
    output = value;
    return true;
  }
  bool Release(uint64_t generation) {
    if (!Active(generation)) return false;
    generation_ = 0; return true;
  }
  bool CanConsume() const { return false; }
 private:
  bool Active(uint64_t generation) const { return generation && generation == generation_; }
  static int32_t Signed(uint32_t value, unsigned bits) {
    return int32_t(value) - int32_t((value & (1u << (bits - 1))) ? 1u << bits : 0);
  }
  static bool FloatBits(uint32_t bits, uint32_t& output) {
    if (!std::isfinite(std::bit_cast<float>(bits))) return false;
    output = bits; return true;
  }
  static bool Color(uint32_t bits, uint32_t& output) {
    const float f = std::bit_cast<float>(bits);
    if (!std::isfinite(f) || std::abs(f) > 1048576) return false;
    const float rounded = float(std::fma(double(f), 255.0, 0.5));
    output = uint32_t(int32_t(rounded)); return true;
  }
  uint64_t generation_ = 0, last_generation_ = 0;
};
}  // namespace gpu
