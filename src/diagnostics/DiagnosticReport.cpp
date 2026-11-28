#include "DiagnosticReport.h"

#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace anyps1 {

std::string get_diagnostic_mode()
{
#ifdef _WIN32
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, "ANYPS1_DIAGNOSTICS") != 0 ||
        value == nullptr) {
        return "summary";
    }
    std::string mode(value);
    std::free(value);
    return mode.empty() ? "summary" : mode;
#else
    const char* value = std::getenv("ANYPS1_DIAGNOSTICS");
    return value == nullptr || value[0] == '\0' ? "summary" : value;
#endif
}

std::string format_diagnostic_report(
    const DiagnosticReport& report,
    const std::string& mode)
{
    const MipsExecutablePlan& plan = report.plan;
    const std::uint64_t coverage =
        plan.reachable_count == 0
            ? 0
            : static_cast<std::uint64_t>(
                  (plan.supported_count * 1000u) / plan.reachable_count);
    const std::string target =
        report.target == TargetPlatform::Windows ? "windows" : "linux";
    const std::string result =
        plan.unsupported_count == 0
            ? "No blocking instruction gap detected"
            : "Blocking instruction gap detected";

    if (mode == "json") {
        std::ostringstream json;
        json << "{"
             << "\"target\":\"" << target << "\","
             << "\"loadAddress\":\"0x" << std::hex << std::uppercase
             << std::setw(8) << std::setfill('0') << plan.load_address << "\","
             << "\"entryPoint\":\"0x" << std::setw(8) << plan.initial_pc << "\","
             << "\"initialGp\":\"0x" << std::setw(8) << plan.initial_gp << "\","
             << "\"initialSp\":\"0x" << std::setw(8) << plan.initial_sp << "\","
             << "\"payloadSize\":" << std::dec << plan.file_size << ","
             << "\"reachableWords\":" << plan.reachable_count << ","
             << "\"supportedWords\":" << plan.supported_count << ","
             << "\"unsupportedInstructions\":" << plan.unsupported_count << ","
             << "\"embeddedDataWords\":" << plan.embedded_data_count << ","
             << "\"unknownRegionWords\":" << plan.unknown_region_count << ","
             << "\"callEdges\":" << plan.call_edges << ","
             << "\"unresolvedTransfers\":" << plan.unresolved_indirect_transfers << ","
             << "\"leaders\":" << plan.leaders.size() << ","
             << "\"chunks\":" << plan.leader_chunks.size() << ","
             << "\"coveragePermille\":" << coverage << ","
             << "\"frames\":" << report.runtime.frame_count << ","
             << "\"presentedFrames\":" << report.runtime.presented_frames << ","
             << "\"vblanks\":" << report.runtime.vblank_count << ","
             << "\"queuedAudioSamples\":" << report.runtime.queued_audio_samples << ","
             << "\"result\":\"" << result << "\"}";
        return json.str();
    }

    std::ostringstream text;
    text << "AnyPS1 Native Executable\n"
         << "========================\n"
         << "Target          " << target << "/amd64\n"
         << "Load address    0x" << std::hex << std::uppercase
         << std::setfill('0') << std::setw(8) << plan.load_address << "\n"
         << "Entry point     0x" << std::setw(8) << plan.initial_pc << "\n"
         << "Initial GP/SP   0x" << std::setw(8) << plan.initial_gp
         << " / 0x" << std::setw(8) << plan.initial_sp << "\n"
         << "\nLowering\n"
         << "--------\n"
         << "Payload         " << std::dec << plan.file_size << " bytes\n"
         << "Reachable       " << plan.reachable_count << " words\n"
         << "Supported       " << plan.supported_count << " words\n"
         << "Unsupported     " << plan.unsupported_count << "\n"
         << "Embedded data   " << plan.embedded_data_count << " words\n"
         << "Unknown regions " << plan.unknown_region_count << " words\n"
         << "Coverage        " << std::fixed << std::setprecision(1)
         << (coverage / 10.0) << "%\n"
         << "Leaders/chunks  " << plan.leaders.size() << " / "
         << plan.leader_chunks.size() << "\n"
         << "Call edges      " << plan.call_edges << "\n"
         << "Unresolved      " << plan.unresolved_indirect_transfers << "\n";

    if (mode == "verbose") {
        text << "\nRuntime\n"
             << "-------\n"
             << "Host loop       " << (plan.native_host_loop_required ? "enabled" : "not required") << "\n"
             << "SDL input       enabled\n"
             << "SDL audio       enabled\n"
             << "Vulkan/video    enabled\n"
             << "DMA/CD-ROM/SPU  enabled\n"
             << "Frames          " << report.runtime.frame_count << "\n"
             << "Presented       " << report.runtime.presented_frames << "\n"
             << "VBlanks         " << report.runtime.vblank_count << "\n"
             << "Audio samples   " << report.runtime.queued_audio_samples << "\n";
    }

    text << "\nResult\n"
         << "------\n"
         << result << "\n";
    return text.str();
}

}
