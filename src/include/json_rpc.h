/*            _       _
 *  _ __ ___ (_)_ __ | | __
 * | '_ ` _ \| | '_ \| |/ /
 * | | | | | | | | | |   <
 * |_| |_| |_|_|_| |_|_|\_\
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef MINK_JSON_RPC_HNDLR_H

#include <nlohmann/json.hpp>

using json = nlohmann::basic_json<nlohmann::ordered_map>;

namespace json_rpc {

    class JsonRpc {
    public:
        JsonRpc() = default;
        explicit JsonRpc(const json &data);
        explicit JsonRpc(const json &&data) = delete;
        ~JsonRpc() = default;
        JsonRpc(const JsonRpc &o) = delete;
        JsonRpc &operator=(const JsonRpc &o) = delete;

        bool verify();
        static json gen_err(const int code, const std::string &msg);
        static json gen_err(const int code);

    private:
        // string constants
        static constexpr const char *JSON_RPC_  = "jsonrpc";
        static constexpr const char *VERSION_   = "2.0";
        static constexpr const char *METHOD_    = "method";
        static constexpr const char *PARAMS     = "params";
        static constexpr const char *ID         = "id";
        static constexpr const char *ERROR_     = "error";
        static constexpr const char *CODE_      = "code";
        static constexpr const char *MESSAGE_   = "message";
        // valid json rpc 2.0 message
        const json &data_;
    };

} // namespace json_rpc

#define MINK_JSON_RPC_HNDLR_H 
#endif /* ifndef MINK_JSON_RPC_HNDLR_H */
