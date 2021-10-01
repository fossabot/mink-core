/*            _       _
 *  _ __ ___ (_)_ __ | | __
 * | '_ ` _ \| | '_ \| |/ /
 * | | | | | | | | | |   <
 * |_| |_| |_|_|_| |_|_|\_\
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <iostream>
#include <json_rpc.h>


json_rpc::JsonRpc::JsonRpc(const json &data) : data_(data){

}

json json_rpc::JsonRpc::gen_err(const int code, const std::string &msg){
    json j;
    j[JSON_RPC_] = const_cast<char*>(VERSION_);
    j[ERROR_][CODE_] = code;
    j[ERROR_][MESSAGE_] = msg;
    return j;
}

json json_rpc::JsonRpc::gen_err(const int code){
    json j;
    j[JSON_RPC_] = const_cast<char*>(VERSION_);
    j[ERROR_][CODE_] = code;
    return j;

}

bool json_rpc::JsonRpc::verify(){
    try {
        // method
        const json &j_method = data_.at("method");
        if (!j_method.is_string())
            throw std::invalid_argument("method != string");

        // params
        const json &j_params = data_.at("params");
        if (!(j_params.is_array() || j_params.is_object()))
            throw std::invalid_argument("params != object | array");

        // id (optional)
        if (data_.contains("id")) {
            const json &j_id = data_.at("id");
            if (!(j_id.is_string() || j_id.is_number_integer()))
                throw std::invalid_argument("id != string | integer");
        }

    } catch (std::exception &e) {
        throw e;
    }

    std::cout << data_.dump(4) << std::endl;
    return true;
}
