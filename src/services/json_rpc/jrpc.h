/*            _       _
 *  _ __ ___ (_)_ __ | | __
 * | '_ ` _ \| | '_ \| |/ /
 * | | | | | | | | | |   <
 * |_| |_| |_|_|_| |_|_|\_\
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef JSON_RPC_H
#define JSON_RPC_H 

#include <config.h>
#include <sstream>
#include <atomic.h>
#include <daemon.h>
#ifdef ENABLE_CONFIGD
#include <mink_config.h>
#include <config_gdt.h>
#endif
#include <mink_plugin.h>
#include "ws_server.h"
#include "events.h"
#include <mink_utils.h>
#include <gdt_utils.h>
#include <gdt_stats.h>

// daemon name and description
constexpr const char *DAEMON_TYPE = "jrpcd";
constexpr const char *DAEMON_DESCRIPTION = "MINK JSON RPC daemon";
constexpr const char *DAEMON_CFG_NODE = "mink jrpc";

// types
using rtrd_lst_t = std::vector<std::string>;
using pmap_t = mink_utils::VariantParamMap<uint32_t>;

// json rpc payload (correlation)
struct JrpcPayload {
    mink_utils::Guid guid;
    std::shared_ptr<WsSession> cdata;
    int id;
};

// daemon descriptor definition
class JsonRpcdDescriptor : public mink::DaemonDescriptor {
public:
    // constructor
    JsonRpcdDescriptor(const char *_type, const char *_desc);
    // destructor
    ~JsonRpcdDescriptor() override;

    void process_args(int argc, char **argv) override;
    void print_help() override;
    void init_gdt();
    void init_wss(uint16_t port);
    void init();
    void terminate() override;

    // config daemons
    std::vector<std::string> rtrd_lst;
    // gdt session
    gdt::GDTSession *gdts = nullptr;
    // gdt client
    gdt::GDTClient *rtrd_gdtc = nullptr;
    // gdt service message manager
    gdt::ServiceMsgManager *gdtsmm = nullptr;
    // idt map
    gdt::ParamIdTypeMap idt_map;
    // GDT stats
    gdt::GDTStatsSession *gdt_stats = nullptr;
    // srvc msg handler
    EVSrvcMsgRX ev_srvcm_rx;
    // srvc msg tx handler
    EVSrvcMsgSent ev_srvcm_tx;
    // extra options
    pmap_t dparams;
    // ws port
    uint16_t ws_port;
    // local IP
    std::string local_ip;
    // correlation map
    mink_utils::CorrelationMap<JrpcPayload> cmap;
    // grpc payload pool
    //memory::Pool<JrpcPayload, true> cpool;

#ifdef ENABLE_CONFIGD
    int init_cfg(bool _proc_cfg) const;
    void process_cfg();

    // cfgd activity flag
    mink::Atomic<uint8_t> cfgd_active;
    // config
    config::Config *config = nullptr;
    // current cfg id
    std::string cfgd_id;
    // config auth user id
    config::UserId cfgd_uid;
    // config gdt client
    gdt::GDTClient *cfgd_gdtc = nullptr;
    // hbeat
    gdt::HeartbeatInfo *hbeat = nullptr;
#endif
};

#endif /* ifndef JSON_RPC_H */
