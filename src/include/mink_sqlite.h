/*            _       _
 *  _ __ ___ (_)_ __ | | __
 * | '_ ` _ \| | '_ \| |/ /
 * | | | | | | | | | |   <
 * |_| |_| |_|_|_| |_|_|\_\
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef MINK_SQLITE_H
#define MINK_SQLITE_H 

#include <sqlite3.h>
#include <string>
#include <mink_utils.h>

namespace mink_db {
    using vpmap = mink_utils::PooledVPMap<uint32_t>;

    enum class QueryType {
        USER_AUTH,
        USER_ADD,
        USER_DEL,
        USER_CMD_DEL,
        USER_CMD_AUTH,
        USER_CMD_SPECIFIC_AUTH
    };

    class SqliteManager {
    public:
        SqliteManager() = default;
        explicit SqliteManager(const std::string &db_f);
        ~SqliteManager();
        SqliteManager(const SqliteManager &o) = delete;
        SqliteManager &operator=(const SqliteManager &o) = delete;

        bool cmd_auth(const int cmd_id, const std::string &u);
        bool cmd_specific_auth(const vpmap &vp, const std::string &u);
        bool user_auth(const std::string &u, const std::string &p);
        void connect(const std::string &db_f);

        // static constants
        static const char *SQL_USER_AUTH;
        static const char *SQL_USER_ADD;
        static const char *SQL_USER_DEL;
        static const char *SQL_USER_CMD_DEL;
        static const char *SQL_USER_CMD_AUTH;
        static const char *SQL_USER_CMD_SPECIFIC_AUTH;

    private:
        sqlite3 *db = nullptr;
    };
}

#endif /* ifndef MINK_SQLITE_H */
