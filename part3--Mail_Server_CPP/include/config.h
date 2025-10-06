#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace mail {

enum class DbBackend {
    Stub,
    MySql
};

struct MysqlConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{3306};
    std::string user{"root"};
    std::string password{"123456789"};
    std::string database{"mail_app"};
    std::size_t pool_size{10};
};

struct ServerConfig {
    std::string listen_address{"0.0.0.0"};
    std::uint16_t port{8085};
    std::size_t max_connections{64};
    std::size_t thread_pool_size{8};
    std::filesystem::path static_dir{"static"};
    std::filesystem::path template_dir{"templates"};
    std::filesystem::path data_dir{"data"};
    std::optional<std::filesystem::path> log_path{}; // std::nullopt -> stderr
    DbBackend backend{DbBackend::Stub};
    MysqlConfig mysql{};
    std::string session_secret{"change-me"};

    bool log_to_stderr() const noexcept { return !log_path.has_value(); }
    std::string log_target() const;
};

bool load_config(const std::filesystem::path &path, ServerConfig &cfg);

} // namespace mail

#endif // CONFIG_H
