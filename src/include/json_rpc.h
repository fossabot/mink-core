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
#define MINK_JSON_RPC_HNDLR_H 

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

        void verify(bool check_mink = false);
        const json &get_method() const; 
        const json &get_params() const; 
        int get_id() const; 
        int get_mink_service_id() const; 
        const std::string &get_mink_dtype() const; 
        
        static json gen_err(const int code, const std::string &msg);
        static json gen_err(const int code);

        // string constants
        static constexpr const char *JSON_RPC_          = "jsonrpc";
        static constexpr const char *VERSION_           = "2.0";
        static constexpr const char *METHOD_            = "method";
        static constexpr const char *PARAMS_            = "params";
        static constexpr const char *ID_                = "id";
        static constexpr const char *ERROR_             = "error";
        static constexpr const char *CODE_              = "code";
        static constexpr const char *MESSAGE_           = "message";
        // mink string constants
        static constexpr const char *MINK_SERVICE_ID_   = "MINK_SERVICE_ID";
        static constexpr const char *MINK_DTYPE_        = "MINK_DTYPE";
        static constexpr const char *MINK_DID_          = "MINK_DID";
 
    private:
        // valid json rpc 2.0 message
        const json &data_;
        // verified
        bool verified_ = false;
        bool has_id_ = false;
        // mink params
        bool mink_verified_ = false;
        bool has_mink_service_ = false;
        bool has_mink_dtype_ = false;
        bool has_mink_did_ = false;
    };

} // namespace json_rpc

#endif /* ifndef MINK_JSON_RPC_HNDLR_H */
