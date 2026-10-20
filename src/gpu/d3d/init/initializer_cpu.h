#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// CPU-only contract of 821B7570. This is not native pipeline state and must
// never be consumed as GPU registers, descriptors, commands or completion.
// Capture/commit/restore authority belongs to the retained session, not here.
namespace gpu {
enum class InitializerCpuRule { ClearLowTwo, SetLowTwoToOne, Constant, Or };
enum class InitializerCpuPurpose { Dirty, Tracking24, Tracking8, Marker, Scalar };
struct InitializerCpuField {
  uint32_t offset;
  InitializerCpuRule rule;
  uint32_t argument;
  InitializerCpuPurpose purpose;
};
inline constexpr size_t kInitializerCpuWords = 70;
inline constexpr size_t kInitializerCpuBytes = kInitializerCpuWords * 4;

constexpr std::array<InitializerCpuField, kInitializerCpuWords> InitializerCpuLayout() {
  std::array<InitializerCpuField, kInitializerCpuWords> fields{};
  size_t n = 0;
  for (uint32_t offset = 0; offset < 40; offset += 4)
    fields[n++] = {offset, InitializerCpuRule::Constant, UINT32_MAX, InitializerCpuPurpose::Dirty};
  for (uint32_t i = 0; i < 26; ++i)
    fields[n++] = {1152 + 24 * i, InitializerCpuRule::ClearLowTwo, 0, InitializerCpuPurpose::Tracking24};
  for (uint32_t i = 0; i < 18; ++i)
    fields[n++] = {1776 + 8 * i, InitializerCpuRule::SetLowTwoToOne, 0, InitializerCpuPurpose::Tracking8};
  // clrldi r30,r11,32 after li r11,-1 gives 00000000FFFFFFFF.
  fields[n++] = {11824, InitializerCpuRule::Constant, 0, InitializerCpuPurpose::Marker};
  fields[n++] = {11828, InitializerCpuRule::Constant, UINT32_MAX, InitializerCpuPurpose::Marker};
  constexpr std::array<std::array<uint32_t, 2>, 12> constants{{
      {10428, 0x20002000}, {10444, 0x00FFFFFF}, {10580, 4}, {10604, 8},
      {10628, 14}, {10688, 4}, {10708, 0x000FF000}, {10712, 0x000FF100},
      {10768, 14}, {10772, 16}, {10824, 2}, {10916, 14}}};
  for (const auto& field : constants)
    fields[n++] = {field[0], InitializerCpuRule::Constant, field[1], InitializerCpuPurpose::Scalar};
  fields[n++] = {10564, InitializerCpuRule::Or, 0x00080000, InitializerCpuPurpose::Scalar};
  fields[n++] = {10568, InitializerCpuRule::Or, 0x00010000, InitializerCpuPurpose::Scalar};
  return fields;
}

constexpr uint32_t ProjectInitializerCpu(const InitializerCpuField& field, uint32_t before) {
  switch (field.rule) {
    case InitializerCpuRule::ClearLowTwo: return before & 0xFFFFFFFCu;
    case InitializerCpuRule::SetLowTwoToOne: return (before & 0xFFFFFFFCu) | 1u;
    case InitializerCpuRule::Constant: return field.argument;
    case InitializerCpuRule::Or: return before | field.argument;
  }
  return before;
}

constexpr std::array<uint8_t, 4> InitializerCpuBigEndian(uint32_t value) {
  return {uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)};
}

// Exact noncontiguous word images for a future transaction. The original
// before image remains distinct even when a projected value is unchanged.
// Construction neither reads nor writes guest memory and grants no lifetime.
struct InitializerCpuImage {
  std::array<std::array<uint8_t, 4>, kInitializerCpuWords> before{};
  std::array<std::array<uint8_t, 4>, kInitializerCpuWords> projected{};
};
constexpr InitializerCpuImage ProjectInitializerCpuImage(
    const std::array<uint32_t, kInitializerCpuWords>& before) {
  InitializerCpuImage image{};
  const auto fields = InitializerCpuLayout();
  for (size_t i = 0; i < fields.size(); ++i) {
    image.before[i] = InitializerCpuBigEndian(before[i]);
    image.projected[i] = InitializerCpuBigEndian(ProjectInitializerCpu(fields[i], before[i]));
  }
  return image;
}
}  // namespace gpu
