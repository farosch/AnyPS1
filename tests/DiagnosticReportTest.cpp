#include "diagnostics/DiagnosticReport.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
    try {
        anyps1::MipsExecutablePlan plan;
        plan.load_address = 0x80010000u;
        plan.initial_pc = 0x80010000u;
        plan.initial_gp = 0u;
        plan.initial_sp = 0x801FF000u;
        plan.file_size = 1024u;
        plan.reachable_count = 200u;
        plan.supported_count = 200u;
        plan.call_edges = 12u;
        plan.leaders.insert(0x80010000u);
        plan.leader_chunks.push_back({0x80010000u});

        anyps1::DiagnosticReport report{
            anyps1::TargetPlatform::Windows,
            plan,
            {60u, 60u, 60u, 44100u},
        };
        const std::string summary =
            anyps1::format_diagnostic_report(report, "summary");
        if (summary.find("AnyPS1 Native Executable") == std::string::npos ||
            summary.find("Coverage") == std::string::npos ||
            summary.find("No blocking instruction gap") == std::string::npos) {
            throw std::runtime_error("summary diagnostic format is incomplete");
        }
        const std::string verbose =
            anyps1::format_diagnostic_report(report, "verbose");
        if (verbose.find("Frames") == std::string::npos ||
            verbose.find("Audio samples") == std::string::npos) {
            throw std::runtime_error("verbose diagnostic format is incomplete");
        }
        const std::string json =
            anyps1::format_diagnostic_report(report, "json");
        if (json.find("\"target\":\"windows\"") == std::string::npos ||
            json.find("\"frames\":60") == std::string::npos ||
            json[0] != '{' || json.back() != '}') {
            throw std::runtime_error("JSON diagnostic format is invalid");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Diagnostic report test failed: "
                  << error.what() << "\n";
        return 1;
    }
}
