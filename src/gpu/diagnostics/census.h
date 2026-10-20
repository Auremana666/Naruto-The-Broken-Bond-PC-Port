#pragma once

// Guard census of the D3D boundary (census.cpp).
#include <cstdint>
#include <string>

struct PPCContext;

namespace gpu {
// Guard census (REX_NATIVE_GUARD_CENSUS=1, diagnosis only, with the
// measurement mode). A draw guard after the original commit records its detail
// and skips the own draw instead of stopping: the title state is the one the
// original commit left over the discard, only the own image misses the draw.
// Up to kCensusRecords records per reason and kCensusReasons reasons; StopInOrder
// records the counts. Unset: the guards stop.
struct CensusSkip {};
constexpr uint64_t kCensusReasons = 64, kCensusRecords = 3, kCensusReadEvery = 150, kCensusReads = 12;  // 150 (5 s).
int GuardCensus();
// An uncovered draw is skipped and counted instead of stopping the
// title: in the guard census, and in the product (without the diagnostic
// instruments). With the instruments and no census (the regression boots)
// the guards stop: those boots prove that nothing was skipped. Without the
// census there are no front-buffer reads.
bool SkipUncoveredDraws();
// The occurrence of this reason (from 1), or 0 past the reason cap.
uint64_t NoteCensus(const std::string& reason);
// An uncovered operation outside the draw (tiling, resolve, Swap, query, D3D
// entries) is reduced to what the boundary covers, or skipped, and counted
// instead of stopping the title: the parameters the boundary does not cover
// are not read and the entries the game does not need are stubs. That holds
// with SkipUncoveredDraws (the census, or the product); otherwise (the
// regression boots) it stops in order (86). The first kCensusRecords
// occurrences of a reason are recorded (native-d3d-operation-skip).
// NoteUncoveredOperation: false when the caller must stop (the threads
// without a guest context); UncoveredOperation stops itself.
bool NoteUncoveredOperation(const std::string& reason, uint32_t lr = 0);
void UncoveredOperation(PPCContext& ctx, const std::string& reason);
// Front-buffer readback of the partial image every kCensusReadEvery Swaps
// after the first skip, kCensusReads times.
bool CensusFrontRead(uint64_t n);
std::string GuardCensusSummary();
}  // namespace gpu
