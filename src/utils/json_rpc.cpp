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
#include <gdt.pb.enums_only.h>
#include <stdexcept>

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

void json_rpc::JsonRpc::verify(){
    // method
    const json &j_method = data_.at("method");
    if (!j_method.is_string())
        throw std::invalid_argument("method != string");
    // verify method
    auto it = gdt_grpc::SysagentCommandMap.cbegin();
    for (; it != gdt_grpc::SysagentCommandMap.cend(); ++it) {
        if (it->second == j_method)
            break;
    }
    if (it == gdt_grpc::SysagentCommandMap.cend())
        throw std::range_error("method not supported");

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


    std::cout << data_.dump(4) << std::endl;
}
