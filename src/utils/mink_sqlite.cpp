/*            _       _
 *  _ __ ___ (_)_ __ | | __
 * | '_ ` _ \| | '_ \| |/ /
 * | | | | | | | | | |   <
 * |_| |_| |_|_|_| |_|_|\_\
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <mink_sqlite.h>
#include <iostream>
#include <string.h>

// alias
using msqlm = mink_db::SqliteManager;

// sql statements
// authenticate user
const char *msqlm::SQL_USER_AUTH =
    "SELECT count(a.id) "
    "FROM user a "
    "WHERE a.username = ? AND "
    "password = ?";

// add new user
const char *msqlm::SQL_USER_ADD =
    "INSERT INTO user(username, password) "
    "VALUES(?, ?)";

// delete user
const char *msqlm::SQL_USER_DEL = 
    "DELETE FROM user "
    "WHERE username = ?";

// delete user <-> cmd relation
const char *msqlm::SQL_USER_CMD_DEL = 
    "DELETE FROM user_action "
    "WHERE user_id = ?";

// authenticate user action
const char *msqlm::SQL_USER_CMD_AUTH =
    "SELECT b.id "
    "FROM user_action a, user b "
    "WHERE a.user_id = b.id AND "
    "a.cmd_id = ? AND "
    "b.username = ?";

// authenticate action specific methods
const char *msqlm::SQL_USER_CMD_SPECIFIC_AUTH ="";


msqlm::SqliteManager(const std::string &db_f) { 
    connect(db_f);
}

msqlm::~SqliteManager(){
    if (db) sqlite3_close(db);
}

bool msqlm::cmd_specific_auth(const vpmap &vp, const std::string &u){
    if (!db)
        throw std::invalid_argument("invalid db connection");


    return true;
} 

bool msqlm::cmd_auth(const int cmd_id, const std::string &u){
    if (!db)
        throw std::invalid_argument("invalid db connection");

    // prepare statement
    sqlite3_stmt *stmt = nullptr;
    int r = sqlite3_prepare_v2(db, 
                               SQL_USER_CMD_AUTH, 
                               -1, 
                               &stmt,
                               nullptr);
    if (r != SQLITE_OK)
        throw std::invalid_argument("sql:cannot prepare statement");

    // cmd id
    if (sqlite3_bind_int(stmt, 1, cmd_id))
        throw std::invalid_argument("sql:cannot bind cmd id");

    // username
    if (sqlite3_bind_text(stmt, 2, u.c_str(), u.size(), SQLITE_STATIC))
        throw std::invalid_argument("sql:cannot bind username");

    // step
    bool res = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        res = sqlite3_column_int(stmt, 0) == 1;

    // cleanup
    if(sqlite3_clear_bindings(stmt))
        throw std::invalid_argument("sql:cannot clear bindings");
    if(sqlite3_reset(stmt))
        throw std::invalid_argument("sql:cannot reset statement");
    if(sqlite3_finalize(stmt))
        throw std::invalid_argument("sql:cannot finalize statement");

    // default auth value
    return res;
} 

bool msqlm::user_auth(const std::string &u, const std::string &p){
    if (!db)
        throw std::invalid_argument("invalid db connection");

    // prepare statement
    sqlite3_stmt *stmt = nullptr;
    int r = sqlite3_prepare_v2(db, 
                               SQL_USER_AUTH, 
                               -1, 
                               &stmt,
                               nullptr);
    if (r != SQLITE_OK)
        throw std::invalid_argument("sql:cannot prepare statement");

    // username
    if (sqlite3_bind_text(stmt, 1, u.c_str(), u.size(), SQLITE_STATIC))
        throw std::invalid_argument("sql:cannot bind username");

    // pwd
    if (sqlite3_bind_text(stmt, 2, p.c_str(), p.size(), SQLITE_STATIC))
        throw std::invalid_argument("sql:cannot bind password");

    // step
    bool res = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        res = sqlite3_column_int(stmt, 0) == 1;

    // cleanup
    if(sqlite3_clear_bindings(stmt))
        throw std::invalid_argument("sql:cannot clear bindings");
    if(sqlite3_reset(stmt))
        throw std::invalid_argument("sql:cannot reset statement");
    if(sqlite3_finalize(stmt))
        throw std::invalid_argument("sql:cannot finalize statement");

    // default auth value
    return res;
}

void msqlm::connect(const std::string &db_f){
    std::cout << "Connecting to [db]..." << std::endl;
    int r = sqlite3_open_v2(db_f.c_str(), 
                            &db, 
                            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, 
                            nullptr);
    if (r)
        throw std::invalid_argument("cannot open database file");

    std::cout << "Connected to [db]..." << std::endl;
}
