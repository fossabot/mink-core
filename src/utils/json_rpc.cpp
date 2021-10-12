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

const json &json_rpc::JsonRpc::get_method() const {
    if (!verified_)
        throw std::invalid_argument("unverified");

    return data_.at(METHOD_);
}

const json &json_rpc::JsonRpc::get_params() const {
    if (!verified_)
        throw std::invalid_argument("unverified");

    return data_.at(PARAMS_);
}

static bool validate_id(const json &d){
    // id (optional)
    if (d.contains(json_rpc::JsonRpc::ID_)) {
        const json &j_id = d.at(json_rpc::JsonRpc::ID_);
        if (!(j_id.is_string() || j_id.is_number_integer()))
            throw std::invalid_argument("id != string | integer");

        return true;
    }
    return false;
}

int json_rpc::JsonRpc::get_mink_service_id() const {
    if (!mink_verified_)
        throw std::invalid_argument("MINK: unverified");

    // return id
    return (data_.at(PARAMS_).get<int>());
}

const std::string &json_rpc::JsonRpc::get_mink_dtype() const {
    if (!mink_verified_)
        throw std::invalid_argument("MINK: unverified");

    auto it = data_[PARAMS_].find(MINK_DTYPE_);
    return it.value();
}

int json_rpc::JsonRpc::get_id() const {
    if (!verified_)
        throw std::invalid_argument("unverified");

    // get ID (string or int)
    // this will throw in case of a missing ID field
    const json &j_id = data_.at(json_rpc::JsonRpc::ID_);
    if (j_id.is_string())
        return std::stoi(data_.at(ID_).get<std::string>());
    else
        return (data_.at(ID_).get<int>());
}

void json_rpc::JsonRpc::verify(bool check_mink){
    // method
    const json &j_method = data_.at(METHOD_);
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
    const json &j_params = data_.at(PARAMS_);
    if (!(j_params.is_array() || j_params.is_object()))
        throw std::invalid_argument("params != object | array");

    // id (optional)
    try {
        has_id_ = validate_id(data_);
    } catch (std::invalid_argument &e) {
        throw;
    }
    // rpc verified
    verified_ = true;

    // mink mandatory params
    // params cannot be an array
    if(check_mink){
        if (j_params.is_array())
            throw std::invalid_argument("MINK: params MUST be an object");

        // find service id (integer)
        auto it = j_params.find(MINK_SERVICE_ID_);
        if (it == j_params.end())
            throw std::invalid_argument("MINK: missing service id");
        if (!(*it).is_number_integer())
            throw std::invalid_argument("MINK: service id != integer");

        // find destination type
        it = j_params.find(MINK_DTYPE_);
        if (it == j_params.end())
            throw std::invalid_argument("MINK: missing destination type");
        if (!(*it).is_string())
            throw std::invalid_argument("MINK: destination type != string");

        // find destination id (optional)
        it = j_params.find(MINK_DID_);
        if (it != j_params.end() && !(*it).is_string())
            throw std::invalid_argument("MINK: destination id != string");

        // mink verified
        has_mink_service_ = true;
        has_mink_dtype_ = true;
        has_mink_did_ = true;
        mink_verified_ = true;
    }

    std::cout << data_.dump(4) << std::endl;
}
