/*            _       _
 *  _ __ ___ (_)_ __ | | __
 * | '_ ` _ \| | '_ \| |/ /
 * | | | | | | | | | |   <
 * |_| |_| |_|_|_| |_|_|\_\
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "jrpc.h"
#include "ws_server.h"
#include <daemon.h>
#include <atomic.h>
#include <gdt.pb.enums_only.h>

using data_vec_t = std::vector<uint8_t>;

#ifdef ENABLE_CONFIGD
EVHbeatMissed::EVHbeatMissed(mink::Atomic<uint8_t> *_activity_flag): activity_flag(_activity_flag) {}

void EVHbeatMissed::run(gdt::GDTCallbackArgs *args) {
    gdt::HeartbeatInfo *hi = args->get<gdt::HeartbeatInfo>(gdt::GDT_CB_INPUT_ARGS, 
                                                           gdt::GDT_CB_ARG_HBEAT_INFO);
    // set activity flag to false
    activity_flag->comp_swap(true, false);
    // stop heartbeat
    gdt::stop_heartbeat(hi);
    // display warning
    mink::CURRENT_DAEMON->log(
        mink::LLT_DEBUG,
        "GDT HBEAT not received, closing connection to [%s]...",
        hi->target_daemon_id);
}

void EVHbeatRecv::run(gdt::GDTCallbackArgs *args) {
    // do nothing
}

EVHbeatCleanup::EVHbeatCleanup(EVHbeatRecv *_recv, EVHbeatMissed *_missed) : missed(_missed),
                                                                             recv(_recv) {}

void EVHbeatCleanup::run(gdt::GDTCallbackArgs *args) {
    delete recv;
    delete missed;
    delete this;

    // get daemon pointer
    auto dd = static_cast<JsonRpcdDescriptor *>(mink::CURRENT_DAEMON);
    // init config until connected
    while (!mink::DaemonDescriptor::DAEMON_TERMINATED &&
           dd->init_cfg(false) != 0) {
        sleep(5);
    }
}
#endif

void EVSrvcMsgRecv::run(gdt::GDTCallbackArgs *args){
    std::cout << "EVSrvcMsgRecv::run" << std::endl;
    gdt::ServiceMessage* smsg = args->get<gdt::ServiceMessage>(gdt::GDT_CB_INPUT_ARGS, 
                                                               gdt::GDT_CB_ARGS_SRVC_MSG);
    auto dd = static_cast<JsonRpcdDescriptor*>(mink::CURRENT_DAEMON);
    auto gdt_stream = args->get<gdt::GDTStream>(gdt::GDT_CB_INPUT_ARGS, 
                                                gdt::GDT_CB_ARG_STREAM);
    // check for missing params
    if (smsg->missing_params) {
        // TODO stats
        return;
    }

    // check for incomplete msg
    if (!smsg->is_complete()) {
        // TODO stats
        return;
    }

    // check service id
    switch (smsg->get_service_id()) {
        case asn1::ServiceId::_sid_sysagent:
            break;

        default:
            // unsupported
            return;
    }

    std::cout << "Service ID found!!!" << std::endl;

    // check for source type
    const mink_utils::VariantParam *vp_src_type = smsg->vpget(asn1::ParameterType::_pt_mink_daemon_type);
    if (vp_src_type == nullptr) return;

    // check for source id
    const mink_utils::VariantParam *vp_src_id = smsg->vpget(asn1::ParameterType::_pt_mink_daemon_id);
    if (vp_src_id == nullptr) return;

    std::cout << "Source daemon found!!!" << (char *)*vp_src_type << ":" << (char *)*vp_src_id <<std::endl;
    // check for guid
    const mink_utils::VariantParam *vp_guid = smsg->vpget(asn1::ParameterType::_pt_mink_guid);
    if(!vp_guid) return;
    
    std::cout << "GUIDD found!!!" << std::endl;
    // correlate guid
    dd->cmap.lock();
    mink_utils::Guid guid;
    guid.set(static_cast<uint8_t *>((unsigned char *)*vp_guid));
    JrpcPayload *pld = dd->cmap.get(guid);
    if(!pld){
        dd->cmap.unlock();
        return;
    }
    // session pointer
    std::shared_ptr<WsSession> ws = pld->cdata;
    // update ts
    dd->cmap.update_ts(guid);
    dd->cmap.remove(guid);
    // unlock
    dd->cmap.unlock();


    std::cout << "GUIDD correlated!!!" << std::endl;

    // generate empty json rpc reply
    auto j = json_rpc::JsonRpc::gen_response(pld->id);
    j[json_rpc::JsonRpc::PARAMS_] = json::array();
    auto &j_params = j[json_rpc::JsonRpc::PARAMS_];

    // loop GDT params
    mink_utils::PooledVPMap<uint32_t>::it_t it = smsg->vpmap.get_begin();
    // loop param map
    for(; it != smsg->vpmap.get_end(); it++){
        // param name from ID
        auto itt = gdt_grpc::SysagentParamMap.find(it->first.key);
        const std::string pname = (itt != gdt_grpc::SysagentParamMap.cend() ? itt->second : "n/a");

        // pointer type is used for long params
        if(it->second.get_type() == mink_utils::DPT_POINTER){
            auto data = static_cast<data_vec_t *>((void *)it->second);
            try {
                std::string s(reinterpret_cast<char *>(data->data()),
                              data->size());
                // new json object
                auto o = json::object();
                o[pname] = s;
                o["idx"] = it->first.index;
                // new json rpc param object
                j_params.push_back(o);

            } catch (std::exception &e) {
                std::cout << e.what() << std::endl;

            }
            // cleanup
            delete data;
            continue;
        }

        // skip non-string params
        if(it->second.get_type() != mink_utils::DPT_STRING) continue;
        auto o = json::object();
        o[pname] = static_cast<char *>(it->second);
        o["idx"] = it->first.index;
        // new json rpc param object
        j_params.push_back(o);
    }
    // create json rpc reply
    std::string ws_rpl = j.dump();
    beast::flat_buffer &b = ws->get_buffer();
    std::size_t sz = net::buffer_copy(b.prepare(ws_rpl.size()), net::buffer(ws_rpl));
    
    // send json rpc reply
    b.commit(sz);
    ws->get_tcp_stream().async_write(b.data(), beast::bind_front_handler(&WsSession::on_write, ws)); 
}

void EVParamStreamLast::run(gdt::GDTCallbackArgs *args){
    gdt::ServiceMessage *smsg = args->get<gdt::ServiceMessage>(gdt::GDT_CB_INPUT_ARGS, 
                                                               gdt::GDT_CB_ARGS_SRVC_MSG);
    gdt::ServiceParam *sparam = args->get<gdt::ServiceParam>(gdt::GDT_CB_INPUT_ARGS,
                                                             gdt::GDT_CB_ARGS_SRVC_PARAM);

    // save data
    auto data = static_cast<data_vec_t *>(smsg->params.get_param(2));
    data->insert(data->end(), 
                 sparam->get_data(),
                 sparam->get_data() + sparam->get_data_size());
    smsg->vpmap.set_pointer(sparam->get_id(), data, sparam->get_index());
    smsg->params.remove_param(2);

}

void EVParamStreamNext::run(gdt::GDTCallbackArgs *args){
    gdt::ServiceMessage *smsg = args->get<gdt::ServiceMessage>(gdt::GDT_CB_INPUT_ARGS, 
                                                               gdt::GDT_CB_ARGS_SRVC_MSG);
    gdt::ServiceParam *sparam = args->get<gdt::ServiceParam>(gdt::GDT_CB_INPUT_ARGS,
                                                             gdt::GDT_CB_ARGS_SRVC_PARAM);

    // save data
    auto data = static_cast<data_vec_t *>(smsg->params.get_param(2));
    data->insert(data->end(), 
                 sparam->get_data(),
                 sparam->get_data() + sparam->get_data_size());

}

void EVParamStreamNew::run(gdt::GDTCallbackArgs *args){
    gdt::ServiceMessage *smsg = args->get<gdt::ServiceMessage>(gdt::GDT_CB_INPUT_ARGS, 
                                                               gdt::GDT_CB_ARGS_SRVC_MSG);
    gdt::ServiceParam *sparam = args->get<gdt::ServiceParam>(gdt::GDT_CB_INPUT_ARGS,
                                                             gdt::GDT_CB_ARGS_SRVC_PARAM);

    // set handlers
    sparam->set_callback(gdt::GDT_ET_SRVC_PARAM_STREAM_NEXT, &prm_strm_next);
    sparam->set_callback(gdt::GDT_ET_SRVC_PARAM_STREAM_END, &prm_strm_last);

    // save data
    auto data = new data_vec_t();
    data->insert(data->end(), 
                 sparam->get_data(), 
                 sparam->get_data() + sparam->get_data_size());
    smsg->params.set_param(2, data);
}

void EVSrvcMsgRX::run(gdt::GDTCallbackArgs *args){
    gdt::ServiceMessage *smsg = args->get<gdt::ServiceMessage>(gdt::GDT_CB_INPUT_ARGS, 
                                                               gdt::GDT_CB_ARGS_SRVC_MSG);
    // set handlers
    smsg->set_callback(gdt::GDT_ET_SRVC_MSG_COMPLETE, &msg_recv);
    smsg->set_callback(gdt::GDT_ET_SRVC_PARAM_STREAM_NEW, &prm_strm_new);
}

void EVSrvcMsgSent::run(gdt::GDTCallbackArgs *args){
    // get service message
    gdt::ServiceMessage *smsg = args->get<gdt::ServiceMessage>(gdt::GDT_CB_INPUT_ARGS, 
                                                               gdt::GDT_CB_ARGS_SRVC_MSG);
    // get extra user callback and free it
    auto usr_cb = static_cast<GDTCallbackMethod *>(smsg->params.get_param(3));
    delete usr_cb;

    // return service message to pool
    smsg->get_smsg_manager()->free_smsg(smsg);
    std::cout << "<<<< FREE!!!!<< " << std::endl;
}

void EVSrvcMsgErr::run(gdt::GDTCallbackArgs *args){
    // reserved
}


