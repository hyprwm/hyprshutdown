#include "helpers/Asserts.hpp"
#include "ui/UI.hpp"
#include "state/AppState.hpp"

#include <hyprutils/cli/ArgumentParser.hpp>

#include <print>

int main(int argc, const char** argv, const char** envp) {
    Hyprutils::CLI::CArgumentParser parser({argv, sc<size_t>(argc)});

    ASSERT(parser.registerBoolOption("dry-run", "", "Do not exit apps, only show UI"));
    ASSERT(parser.registerBoolOption("no-exit", "", "Do not exit hyprland once apps close"));
    ASSERT(parser.registerStringOption("top-label", "t", "Set the text appearing on top (set to \"Shutting down...\" by default)"));
    ASSERT(parser.registerStringOption("post-cmd", "p", "Set a command ran after all apps and Hyprland shut down"));
    ASSERT(parser.registerBoolOption("verbose", "", "Enable more logging"));
    ASSERT(parser.registerBoolOption("help", "h", "Show the help menu"));

    if (const auto ret = parser.parse(); !ret) {
        g_logger->log(LOG_ERR, "Failed parsing arguments: {}", ret.error());
        return 1;
    }

    if (parser.getBool("help").value_or(false)) {
        std::println("{}", parser.getDescription(std::format("hyprshutdown v{}", HYPRSHUTDOWN_VERSION)));
        return 0;
    }

    if (parser.getBool("verbose").value_or(false))
        g_logger->setLogLevel(LOG_TRACE);

    if (parser.getBool("dry-run").value_or(false))
        State::state()->m_dryRun = true;

    const auto HIS = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!HIS || HIS[0] == '\0') {
        g_logger->log(LOG_ERR, "Cannot run under a non-hyprland environment");
        return 1;
    }

    if (!State::state()->init()) {
        g_logger->log(LOG_ERR, "Failed to init state");
        return 1;
    }

    g_ui                  = makeUnique<CUI>();
    g_ui->m_noExit        = parser.getBool("no-exit").value_or(false);
    g_ui->m_shutdownLabel = parser.getString("top-label").value_or("Shutting down...");
    g_ui->m_postExitCmd   = parser.getString("post-cmd");
    g_ui->run();

    return 0;
}