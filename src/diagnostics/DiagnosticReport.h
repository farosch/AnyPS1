#pragma once

#include "lowering/MipsLowerer.h"
#include "relinker/Relinker.h"

#include <cstdint>
#include <string>

namespace anyps1 {

struct RuntimeCounters {
    std::uint64_t frame_count = 0;
    std::uint64_t presented_frames = 0;
    std::uint64_t vblank_count = 0;
    std::uint64_t queued_audio_samples = 0;
};

struct DiagnosticReport {
    TargetPlatform target;
    const MipsExecutablePlan& plan;
    RuntimeCounters runtime;
};

std::string format_diagnostic_report(
    const DiagnosticReport& report,
    const std::string& mode);
std::string get_diagnostic_mode();

}
