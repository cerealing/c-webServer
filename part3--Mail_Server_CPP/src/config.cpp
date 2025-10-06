#include "config.h"
#include "jsmn.h"
#include "logger.h"

#include <charconv>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <vector>

namespace mail {

namespace {

std::string_view token_view(const std::string &json, const jsmntok_t &tok) {
    return std::string_view(json).substr(static_cast<std::size_t>(tok.start),
                                         static_cast<std::size_t>(tok.end - tok.start));
}

template <typename T>
T parse_number(std::string_view view, T fallback) {
    T value = fallback;
    if (!view.empty()) {
        const char *begin = view.data();
        const char *end = view.data() + view.size();
        auto [ptr, ec] = std::from_chars(begin, end, value);
        if (ec != std::errc{} || ptr != end) {
            return fallback;
        }
    }
    return value;
}

std::string to_string(std::string_view view) {
    return std::string(view.data(), view.size());
}

} // namespace

std::string ServerConfig::log_target() const {
    return log_path ? log_path->string() : std::string("-");
}

bool load_config(const std::filesystem::path &path, ServerConfig &cfg) {
    cfg = ServerConfig{};

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        LOGW("config: could not open %s, using defaults", path.string().c_str());
        return false;
    }

    std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (json.empty()) {
        return true;
    }

    std::vector<jsmntok_t> tokens(256);
    int token_count = -1;

    for (int attempt = 0; attempt < 5; ++attempt) {
        jsmn_parser parser;
        jsmn_init(&parser);
        token_count = jsmn_parse(&parser, json.c_str(), static_cast<unsigned int>(json.size()), tokens.data(), static_cast<unsigned int>(tokens.size()));
        if (token_count >= 0) {
            break;
        }
        tokens.resize(tokens.size() * 2);
    }

    if (token_count < 0) {
        LOGW("config: failed to parse %s, using defaults", path.string().c_str());
        return false;
    }

    for (int i = 1; i < token_count; ++i) {
        const jsmntok_t &key_tok = tokens[i];
        if (key_tok.type != JSMN_STRING) {
            continue;
        }

        std::string_view key = token_view(json, key_tok);

        if (key == "listen_address") {
            cfg.listen_address = to_string(token_view(json, tokens[++i]));
        } else if (key == "port") {
            cfg.port = static_cast<std::uint16_t>(parse_number(token_view(json, tokens[++i]), cfg.port));
        } else if (key == "max_connections") {
            cfg.max_connections = static_cast<std::size_t>(parse_number(token_view(json, tokens[++i]), cfg.max_connections));
        } else if (key == "thread_pool_size") {
            cfg.thread_pool_size = static_cast<std::size_t>(parse_number(token_view(json, tokens[++i]), cfg.thread_pool_size));
        } else if (key == "static_dir") {
            cfg.static_dir = std::filesystem::path(to_string(token_view(json, tokens[++i])));
        } else if (key == "template_dir") {
            cfg.template_dir = std::filesystem::path(to_string(token_view(json, tokens[++i])));
        } else if (key == "data_dir") {
            cfg.data_dir = std::filesystem::path(to_string(token_view(json, tokens[++i])));
        } else if (key == "log_path") {
            std::string value = to_string(token_view(json, tokens[++i]));
            if (value.empty() || value == "-") {
                cfg.log_path.reset();
            } else {
                cfg.log_path = std::filesystem::path(value);
            }
        } else if (key == "db_backend") {
            std::string value = to_string(token_view(json, tokens[++i]));
            cfg.backend = (value == "mysql") ? DbBackend::MySql : DbBackend::Stub;
        } else if (key == "session_secret") {
            cfg.session_secret = to_string(token_view(json, tokens[++i]));
        } else if (key == "mysql") {
            const int obj_index = ++i;
            const jsmntok_t &obj_tok = tokens[obj_index];
            int limit = obj_index + obj_tok.size * 2;
            for (int j = obj_index + 1; j <= limit && j + 1 < token_count; j += 2) {
                const jsmntok_t &mk = tokens[j];
                const jsmntok_t &mv = tokens[j + 1];
                if (mk.type != JSMN_STRING) {
                    continue;
                }
                std::string_view mysql_key = token_view(json, mk);
                std::string_view mysql_val = token_view(json, mv);

                if (mysql_key == "host") {
                    cfg.mysql.host = to_string(mysql_val);
                } else if (mysql_key == "port") {
                    cfg.mysql.port = static_cast<std::uint16_t>(parse_number(mysql_val, cfg.mysql.port));
                } else if (mysql_key == "user") {
                    cfg.mysql.user = to_string(mysql_val);
                } else if (mysql_key == "password") {
                    cfg.mysql.password = to_string(mysql_val);
                } else if (mysql_key == "database") {
                    cfg.mysql.database = to_string(mysql_val);
                } else if (mysql_key == "pool_size") {
                    cfg.mysql.pool_size = static_cast<std::size_t>(parse_number(mysql_val, cfg.mysql.pool_size));
                }
            }
            i = limit;
        } else {
            // Skip the corresponding value token
            ++i;
        }
    }

    return true;
}

} // namespace mail
