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
#include <routing.h>

RoutingdDescriptor::RoutingdDescriptor(const char *_type, 
                                       const char *_desc) : mink::DaemonDescriptor(_type, nullptr, _desc),
                                                            gdts(nullptr),
                                                            gdt_stats(nullptr),
                                                            gdt_port(0) {
#ifdef ENABLE_CONFIGD
    config = new config::Config();
    memset(cfgd_id, 0, sizeof(cfgd_id));

    // set daemon params
    set_param(0, config);
#endif
    // default extra param values
    // --gdt-streams
    extra_params.set_int(0, 1000);
    // --gdt-stimeout
    extra_params.set_int(1, 5);
}

RoutingdDescriptor::~RoutingdDescriptor() {
#ifdef ENABLE_CONFIGD
    // free routing deamons address strings
    std::all_of(config_daemons.cbegin(), config_daemons.cend(),
                [](std::string *cd) {
                    delete cd;
                    return true;
                });
#endif
}

void RoutingdDescriptor::process_args(int argc, char **argv) {
    std::regex addr_regex("(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}):(\\d+)");
    int option_index = 0;
    struct option long_options[] = {{"gdt-streams", required_argument, 0, 0},
                                    {"gdt-stimeout", required_argument, 0, 0},
                                    {0, 0, 0, 0}};

    if (argc < 5) {
        print_help();
        exit(EXIT_FAILURE);
    } else {
        int opt;
        while ((opt = getopt_long(argc, argv, "?p:c:i:D", long_options,
                                  &option_index)) != -1) {
            switch (opt) {
            // long options
            case 0:
                if (long_options[option_index].flag != 0)
                    break;
                switch (option_index) {
                // gdt-streams
                case 0:
                    extra_params.set_int(0, atoi(optarg));
                    break;

                // gdt-stimeout
                case 1:
                    extra_params.set_int(1, atoi(optarg));
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

            // config daemon address
            case 'c':
#ifdef ENABLE_CONFIGD
                // check pattern (ipv4:port)
                // check if valid
                if (!std::regex_match(optarg, addr_regex)) {
                    std::cout << "ERROR: Invalid daemon address format '"
                              << optarg << "'!" << std::endl;
                    exit(EXIT_FAILURE);

                } else {
                    config_daemons.push_back(new std::string(optarg));
                }
#endif
                break;

            // gdt port
            case 'p':
                gdt_port = atoi(optarg);
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
        if (strnlen(get_daemon_id(), 15) == 0) {
            std::cout << "ERROR: Daemon id not defined!" << std::endl;
            exit(EXIT_FAILURE);
        }

        // port
        if (gdt_port == 0) {
            std::cout << "ERROR: GDT IN port not defined!" << std::endl;
            exit(EXIT_FAILURE);
        }
    }
}

void RoutingdDescriptor::print_help() {
    std::cout << daemon_type << " - " << daemon_description << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << " -?\thelp" << std::endl;
    std::cout << " -i\tunique daemon id" << std::endl;
#ifdef ENABLE_CONFIGD
    std::cout << " -c\tconfig daemon address (ipv4:port)" << std::endl;
#endif
    std::cout << " -p\tGDT inbound port" << std::endl;
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

void RoutingdDescriptor::init() {
    // init gdt
    init_gdt();
#ifdef ENABLE_CONFIGD
    // init config
    if (init_config() != 0) {
        // log
        mink::CURRENT_DAEMON->log(mink::LLT_INFO,
            "Cannot find any valid config daemon connection for node [%s], "
            "using automatic configuration...",
            get_daemon_id());
        // not exiting since routingd is allowed to run without configd
        // connection
    }
#endif
    // accept connections (server mode)
    gdts->start_server(nullptr, gdt_port);

    // connect stats with routing
    gdt::GDTClient *gdtc = gdt_stats->get_gdt_session()
                                    ->connect("127.0.0.1", 
                                              gdt_port, 
                                              16, 
                                              nullptr, 
                                              0);
    if (gdtc != nullptr)
        gdt_stats->setup_client(gdtc);
}

#ifdef ENABLE_CONFIGD
void RoutingdDescriptor::process_config() {
    // create root node string
    std::string root_node_str(DAEMON_CFG_NODE);
    root_node_str.append(" ");
    root_node_str.append(daemon_id);

    // get node
    config::ConfigItem *root = (*config->get_definition_root())(root_node_str.c_str());
    config::ConfigItem *tmp_node = nullptr;

    // check if configuration exists
    if (root == nullptr) {
        mink::CURRENT_DAEMON->log(mink::LLT_INFO,
                                  "Configuration for node [%s] does not exist, "
                                  "using automatic routing...",
                                  get_daemon_id());
        return;
    }

    // process configuration
    mink::CURRENT_DAEMON->log(mink::LLT_DEBUG,
                              "Configuration for node [%s] successfully received, processing...",
                              get_daemon_id());

    // asp list
    if ((*root)("destinations") == nullptr)
        mink::CURRENT_DAEMON->log(mink::LLT_WARNING,
                                  "Missing destination configuration node set for node [%s]!",
                                  get_daemon_id());
    else {
        tmp_node = (*root)("destinations");
        // setup config on chage events
        tmp_node->set_on_change_handler(&wrr_mod_handler, true);
        // check all nodes
        config::ConfigItem *dest_node_type = nullptr;
        for (unsigned int i = 0; i < tmp_node->children.size(); i++) {
            dest_node_type = tmp_node->children[i];
            mink::CURRENT_DAEMON->log(mink::LLT_DEBUG,
                                      "Processing configuration for "
                                      "destination type [%s] for node [%s]...",
                                      dest_node_type->name.c_str(), get_daemon_id());

            if ((*dest_node_type)("nodes") == nullptr) {
                mink::CURRENT_DAEMON->log(mink::LLT_WARNING,
                                          "Missing destination [%s] nodes configuration node set for "
                                          "node [%s]!",
                                          dest_node_type->name.c_str(), 
                                          get_daemon_id());
                continue;
            }

            // nodes
            config::ConfigItem *nodes = (*dest_node_type)("nodes");

            // process nodes
            config::ConfigItem *dest_node = nullptr;
            mink_utils::PooledVPMap<uint32_t> tmp_params;
            for (unsigned int j = 0; j < nodes->children.size(); j++) {
                dest_node = nodes->children[j];
                mink::CURRENT_DAEMON->log(mink::LLT_DEBUG,
                                          "Adding node [%s] to [%s] routing table with weight "
                                          "[%d]...",
                                          dest_node->name.c_str(), 
                                          dest_node_type->name.c_str(),
                                          dest_node->to_int("weight"), get_daemon_id());

                // set weight data
                tmp_params.set_int(0, dest_node->to_int("weight", 1));
                // add to routing handler
                gdts->get_routing_handler()->add_node(nullptr, 
                                                      dest_node_type->name.c_str(), 
                                                      dest_node->name.c_str(),
                                                      &tmp_params);
            }
        }
    }
}

int RoutingdDescriptor::init_config(bool _process_config) {
    // log
    mink::CURRENT_DAEMON->log(
        mink::LLT_DEBUG, "Starting config daemon registration procedure...");
    // loop routing daemons

    for (unsigned int i = 0; i < gdts->get_client_count(); i++) {
        // get client
        gdt::GDTClient *gdt_client = gdts->get_client(i);
        // null check
        if (gdt_client != nullptr && gdt_client->is_registered()) {
            // check only OUTBOUND
            if (gdt_client->direction != gdt::GDT_CD_OUTBOUND)
                continue;
            // log
            mink::CURRENT_DAEMON->log(mink::LLT_DEBUG,
                                      "Connection to remote daemon established, L3 address = "
                                      "[%s:%d], GDT address = [%s:%s]",
                                      gdt_client->get_end_point_address(),
                                      gdt_client->get_end_point_port(),
                                      gdt_client->get_end_point_daemon_type(),
                                      gdt_client->get_end_point_daemon_id());
            // check for active configd
            if (!cfgd_active.get()) {
                // user login
                if (config::user_login(config, 
                                       gdt_client, nullptr,
                                       (char *)cfgd_id, 
                                       &cfgd_uid) == 0) {
                    if (strnlen((char *)cfgd_id, sizeof(cfgd_id) - 1) > 0) {
                        // log
                        mink::CURRENT_DAEMON->log(mink::LLT_DEBUG,
                                                  "User [%s] successfully authenticated with config "
                                                  "daemon [%s]",
                                                  cfgd_uid.user_id, 
                                                  cfgd_id);
                        // notification request
                        if (config::notification_request(config, 
                                                         gdt_client, 
                                                         DAEMON_CFG_NODE, 
                                                         nullptr,
                                                         (char *)cfgd_id, 
                                                         &cfgd_uid, 
                                                         nullptr) == 0) {

                            // create hbeat events
                            auto hb_recv = new HbeatRecv();
                            auto hb_missed = new HbeatMissed(&cfgd_active);
                            auto hb_cleanup = new HbeatCleanup(hb_recv, hb_missed);

                            // init hbeat
                            hbeat = gdt::init_heartbeat("config_daemon", 
                                                        (char *)cfgd_id, 
                                                        gdt_client, 
                                                        5,
                                                        hb_recv, 
                                                        hb_missed, 
                                                        hb_cleanup);
                            if (hbeat != nullptr) {
                                cfgd_active.comp_swap(false, true);
                                // log
                                mink::CURRENT_DAEMON->log(mink::LLT_DEBUG,
                                                          "Starting GDT HBEAT for config daemon "
                                                          "[%s], L3 address = [%s:%d]",
                                                          cfgd_id,
                                                          gdt_client->get_end_point_address(),
                                                          gdt_client->get_end_point_port());

                                // free event memory on error
                            } else {
                                delete hb_recv;
                                delete hb_missed;
                                delete hb_cleanup;
                            }

                            // log
                            mink::CURRENT_DAEMON->log(mink::LLT_DEBUG,
                                                      "Registering notification request for node "
                                                      "path [%s] with config daemon [%s]",
                                                      DAEMON_CFG_NODE, 
                                                      cfgd_id);
                            // process config
                            if (_process_config)
                                process_config();

                            // stop if config daemon connected
                            // ok
                            return 0;

                        } else {
                            // log
                            mink::CURRENT_DAEMON->log(mink::LLT_ERROR,
                                                      "Error while requesting notifications from "
                                                      "config daemon [%s]!",
                                                      cfgd_id);
                        }

                    } else {
                        // log
                        mink::CURRENT_DAEMON->log(mink::LLT_ERROR,
                                                  "Error while trying to find config daemon id via "
                                                  "GDT connection, L3 address = [%s:%d], GDT address "
                                                  "= [%s:%s]",
                                                  gdt_client->get_end_point_address(),
                                                  gdt_client->get_end_point_port(),
                                                  gdt_client->get_end_point_daemon_type(),
                                                  gdt_client->get_end_point_daemon_id());
                    }

                } else {
                    // log
                    mink::CURRENT_DAEMON->log(mink::LLT_ERROR,
                                              "Error while trying to authenticate user [%s] with "
                                              "config daemon [%s:%d]!",
                                              cfgd_uid.user_id, gdt_client->get_end_point_address(),
                                              gdt_client->get_end_point_port());
                }
            }

        } else {
            // log
            mink::CURRENT_DAEMON->log(mink::LLT_ERROR,
                                      "Error while connecting to config daemon [%s:%d]!",
                                      (gdt_client ? gdt_client->get_end_point_address() : ""),
                                      (gdt_client ? gdt_client->get_end_point_port() : 0));
        }
    }

    // err
    return 5;
}
#endif

void RoutingdDescriptor::init_gdt() {
    // start GDT session
    gdts = gdt::init_session(get_daemon_type(), 
                             get_daemon_id(), 
                             (int)*extra_params.get_param(0),
                             (int)*extra_params.get_param(1), 
                             true, 
                             (int)*extra_params.get_param(1));

    // set routing algorighm
    gdts->set_routing_algo(gdt::GDT_RA_WRR);
    // set gdts pointer
#ifdef ENABLE_CONFIGD
    wrr_mod_handler.gdts = gdts;
#endif
    // gdt stats
    gdt_stats = new gdt::GDTStatsSession(5, gdts);
    // start stats
    gdt_stats->start();

    // init gdt stats
    gdt_stats->init_gdt_session_stats(gdts);

    // set params
    set_param(2, gdts);

    // connect to config daemons
    std::smatch regex_groups;
    std::regex addr_regex("(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}):(\\d+)");

#ifdef ENABLE_CONFIGD
    // loop config daemons
    std::all_of(config_daemons.cbegin(), config_daemons.cend(),
                [this, &regex_groups, &addr_regex](const std::string *cd) {
                    // separate IP and PORT
                    std::regex_search(*cd, regex_groups, addr_regex);
                    // connect to config daemon
                    gdts->connect(regex_groups[1].str().c_str(),
                                  atoi(regex_groups[2].str().c_str()), 
                                  16,
                                  nullptr, 
                                  0);

                    return true;
                });
#endif
}

void RoutingdDescriptor::terminate() {
    // stop server
    gdts->stop_server();
    // stop stats
    gdt_stats->stop();
    // destroy session, free memory
    gdt::destroy_session(gdts);
#ifdef ENABLE_CONFIGD
    // deallocate config memory
    if (config->get_definition_root() != nullptr)
        delete config->get_definition_root();
    // free config
    delete config;
#endif
    // gdt stats
    delete gdt_stats;
}

