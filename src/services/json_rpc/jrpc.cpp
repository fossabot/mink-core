/*            _       _
 *  _ __ ___ (_)_ __ | | __
 * | '_ ` _ \| | '_ \| |/ /
 * | | | | | | | | | |   <
 * |_| |_| |_|_|_| |_|_|\_\
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <getopt.h>
#include <regex>
#include <iostream>
#include "jrpc.h"
#include <json_rpc.h>
#include <boost/asio/signal_set.hpp>

JsonRpcdDescriptor::JsonRpcdDescriptor(const char *_type, 
                                       const char *_desc)
    : mink::DaemonDescriptor(_type, nullptr, _desc) {

#ifdef ENABLE_CONFIGD
    config = new config::Config();
    // set daemon params
    set_param(0, config);
#endif
    // default extra param values
    // --gdt-streams
    dparams.set_int(0, 1000);
    // --gdt-stimeout
    dparams.set_int(1, 5);
    // --gdt-smsg-pool
    dparams.set_int(2, 1000);
    // --gdt-sparam-pool
    dparams.set_int(3, 5000);
}

JsonRpcdDescriptor::~JsonRpcdDescriptor(){
    delete gdtsmm;
}

void JsonRpcdDescriptor::process_args(int argc, char **argv){
    std::regex addr_regex("(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}):(\\d+)");
    int opt;
    int option_index = 0;
    struct option long_options[] = {{"gdt-streams", required_argument, 0, 0},
                                    {"gdt-stimeout", required_argument, 0, 0},
                                    {0, 0, 0, 0}};

    if (argc < 5) {
        print_help();
        exit(EXIT_FAILURE);
    }

    while ((opt = getopt_long(argc, argv, "?c:i:w:D", long_options,
                              &option_index)) != -1) {
        switch (opt) {
        // long options
        case 0:
            if (long_options[option_index].flag != 0)
                break;
            switch (option_index) {
            // gdt-streams
            case 0:
                dparams.set_int(0, atoi(optarg));
                break;

            // gdt-stimeout
            case 1:
                dparams.set_int(1, atoi(optarg));
                break;

            default:
                break;
            }
            break;

        // help
        case '?':
            print_help();
            exit(EXIT_FAILURE);

        // daemon id
        case 'i':
            if (set_daemon_id(optarg) > 0) {
                std::cout << "ERROR: Maximum size of daemon id string is "
                             "15 characters!"
                          << std::endl;
                exit(EXIT_FAILURE);
            }
            break;

        // router daemon address
        case 'c':
            // check pattern (ipv4:port)
            // check if valid
            if (!std::regex_match(optarg, addr_regex)) {
                std::cout << "ERROR: Invalid daemon address format '"
                          << optarg << "'!" << std::endl;
                exit(EXIT_FAILURE);

            } else {
                rtrd_lst.push_back(std::string(optarg));
            }
            break;

        // ws port
        case 'w':
            if (atoi(optarg) <= 0) {
                std::cout << "ERROR: Invalid ws port!" << std::endl;
                exit(EXIT_FAILURE);
            }
            ws_port = atoi(optarg);
            break;

        // debug mode
        case 'D':
            set_log_level(mink::LLT_DEBUG);
            break;

        default:
            break;
        }
    }

    // check mandatory id
    if (strlen(get_daemon_id()) == 0) {
        std::cout << "ERROR: Daemon id not defined!" << std::endl;
        exit(EXIT_FAILURE);
    }

}

void JsonRpcdDescriptor::print_help(){
    std::cout << daemon_type << " - " << daemon_description << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << " -?\thelp" << std::endl;
    std::cout << " -i\tunique daemon id" << std::endl;
    std::cout << " -c\trouter daemon address (ipv4:port)" << std::endl;
    std::cout << " -w\tWebSocket server port" << std::endl;
    std::cout << " -D\tstart in debug mode" << std::endl;
    std::cout << std::endl;
    std::cout << "GDT Options:" << std::endl;
    std::cout << "=============" << std::endl;
    std::cout << " --gdt-streams\t\tGDT Session stream pool\t\t(default = 1000)"
              << std::endl;
    std::cout
        << " --gdt-stimeout\tGDT Stream timeout in seconds\t\t(default = 5)"
        << std::endl;
}

void JsonRpcdDescriptor::init_gdt(){
    // service message manager
    gdtsmm = new gdt::ServiceMsgManager(&idt_map, 
                                        nullptr, 
                                        nullptr,
                                        dparams.get_pval<int>(2),
                                        dparams.get_pval<int>(3));

    // set daemon params
#ifdef ENABLE_CONFIGD
    set_param(0, config);
#endif
    set_param(1, gdtsmm);

    // set service message handlers
    gdtsmm->set_new_msg_handler(&ev_srvcm_rx);
    gdtsmm->set_msg_err_handler(&ev_srvcm_rx.msg_err);

    // start GDT session
    gdts = gdt::init_session(get_daemon_type(), 
                             get_daemon_id(),
                             dparams.get_pval<int>(0),
                             dparams.get_pval<int>(1), 
                             false,
                             dparams.get_pval<int>(1));

    // connect to routing daemons
    std::smatch regex_groups;
    std::regex addr_regex("(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}):(\\d+)");


    // loop routing daemons
    for (size_t i = 0; i < rtrd_lst.size(); i++) {
        // separate IP and PORT
        if (!std::regex_match(rtrd_lst[i], regex_groups, addr_regex))
            continue;
        // connect to routing daemon
        gdt::GDTClient *gdtc = gdts->connect(regex_groups[1].str().c_str(),
                                             atoi(regex_groups[2].str().c_str()), 
                                             16, 
                                             nullptr, 
                                             0);

        // setup client for service messages
        if (gdtc!= nullptr) {
            // setup service message event handlers
            gdtsmm->setup_client(gdtc);
        }
    }

}

static void handler(const boost::system::error_code  error, int signum){
    std::cout << "==== SIGNAL " << std::endl;
}

void JsonRpcdDescriptor::init_wss(uint16_t port){
    auto const addr = net::ip::make_address("0.0.0.0");
    auto const th_nr = 1;

    // The io_context is required for all I/O
    net::io_context ioc{th_nr};

    // Create and launch a listening port
    std::make_shared<WsListener>(ioc, tcp::endpoint{addr, port})->run();
    // Run the I/O service on the requested number of threads
    //std::vector<std::thread> v;
    //v.reserve(th_nr);
    //for (auto i = th_nr- 1; i > 0; --i)
    //    v.emplace_back([&ioc] { ioc.run(); });

    // Construct a signal set registered for process termination.
    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);

    // Start an asynchronous wait for one of the signals to occur.
    signals.async_wait([&ioc](const boost::system::error_code error, int signum) {
        std::cout << "==== SIGNAL " << std::endl;
        ioc.stop();
        DaemonDescriptor::DAEMON_TERMINATED = true;
    });

    ioc.run();
    std::cout << "DONE" << std::endl;
}

#ifdef ENABLE_CONFIGD
int JsonRpcdDescriptor::init_cfg(bool _proc_cfg) const {
    // reserved
    return 0;
}
#endif

void JsonRpcdDescriptor::init(){
#ifdef ENABLE_CONFIGD
    init_cfg(true);
#endif
    init_gdt();
    init_wss(ws_port);
}


#ifdef ENABLE_CONFIGD
void JsonRpcdDescriptor::process_cfg(){
    // reserved
}
#endif

void JsonRpcdDescriptor::terminate(){
    //gdts->stop_server();
    // stop stats
    //gdt_stats->stop();
    // destroy session, free memory
    gdt::destroy_session(gdts);
#ifdef ENABLE_CONFIGD
    // deallocate config memory
    if (config->get_definition_root() != nullptr)
        delete config->get_definition_root();
    // free config
    delete config;
#endif
}

