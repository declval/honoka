#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <print>
#include <sqlite3.h>
#include <stdexcept>
#include <string>

#define requirenargs(n)                                                        \
  if (argc != n) {                                                             \
    usage();                                                                   \
    return 1;                                                                  \
  }

#define sizetint(s)                                                            \
  s > static_cast<size_t>(std::numeric_limits<int>::max())                     \
      ? throw std::overflow_error("size_t value too big for int")              \
      : static_cast<int>(s)

using namespace std::literals;

const auto DAY = 24h;

const std::array<std::chrono::duration<long long>, 8> INTERVALS = {
    0 * DAY, 1 * DAY, 2 * DAY, 4 * DAY, 8 * DAY, 16 * DAY, 32 * DAY, 64 * DAY};

const auto PROGRAM = "honoka"s;

const auto DATABASE = std::filesystem::path(getenv("HOME")) / ".local"s /
                      "share"s / PROGRAM / "data.db"s;

class Application {
public:
  Application() : db_(nullptr) {
    std::filesystem::create_directories(DATABASE.parent_path());

    try {
      if (int rc = sqlite3_open(DATABASE.c_str(), &db_); rc) {
        throw std::runtime_error("Can't open database");
      }

      create();
    } catch (...) {
      sqlite3_close(db_);
      throw;
    }
  }

  ~Application() { sqlite3_close(db_); }

  auto add(const std::string &front, const std::string &back) -> void {
    const auto query = "INSERT INTO cards (front, back) VALUES (?, ?)"s;

    sqlite3_stmt *stmt = nullptr;

    sqlite3_prepare_v2(db_, query.c_str(), sizetint(query.size()), &stmt,
                       nullptr);

    sqlite3_bind_text(stmt, 1, front.c_str(), sizetint(front.size()),
                      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, back.c_str(), sizetint(back.size()),
                      SQLITE_STATIC);

    if (int rc = sqlite3_step(stmt); rc != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      throw std::runtime_error("Can't insert into table");
    }

    sqlite3_finalize(stmt);
  }

  auto list() -> void {
    const auto query =
        "SELECT front, interval, unixepoch(updated_at) FROM cards"s;

    sqlite3_stmt *stmt = nullptr;

    sqlite3_prepare_v2(db_, query.c_str(), sizetint(query.size()), &stmt,
                       nullptr);

    int rc = 0;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      const std::string front = (const char *)sqlite3_column_text(stmt, 0);
      int interval = sqlite3_column_int(stmt, 1);
      long long updated_at = sqlite3_column_int64(stmt, 2);

      if (needs_review(interval, updated_at)) {
        std::println("{}", front);
      }
    }

    if (rc != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      throw std::runtime_error("Can't select from table");
    }

    sqlite3_finalize(stmt);
  }

  auto next() -> void {
    const auto query =
        "SELECT front, back, interval, unixepoch(updated_at) FROM cards"s;

    sqlite3_stmt *stmt = nullptr;

    sqlite3_prepare_v2(db_, query.c_str(), sizetint(query.size()), &stmt,
                       nullptr);

    int rc = 0;

    std::string front;
    std::string back;
    int interval = 0;
    long long updated_at = 0;

    bool success = false;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      if (!success) {
        front = (const char *)sqlite3_column_text(stmt, 0);
        back = (const char *)sqlite3_column_text(stmt, 1);
        interval = sqlite3_column_int(stmt, 2);
        updated_at = sqlite3_column_int64(stmt, 3);

        if (needs_review(interval, updated_at)) {
          success = true;
        }
      }
    }

    if (rc != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      throw std::runtime_error("Can't select from table");
    }

    sqlite3_finalize(stmt);

    if (!success) {
      return;
    }

    std::string reply;

    std::print("{}", front);
    std::getline(std::cin, reply);
    std::println("{}", back);
    std::print("Ok? (Y/n) ");
    std::getline(std::cin, reply);

    if (reply == ""s || reply == "Y"s || reply == "y"s) {
      interval = sizetint(std::min(size_t(interval) + 1, INTERVALS.size() - 1));
    } else {
      interval = 1;
    }

    update(interval, front);
  }

  auto remove(const std::string &front) -> void {
    const auto query = "DELETE FROM cards WHERE front = ?"s;

    sqlite3_stmt *stmt = nullptr;

    sqlite3_prepare_v2(db_, query.c_str(), sizetint(query.size()), &stmt,
                       nullptr);

    sqlite3_bind_text(stmt, 1, front.c_str(), sizetint(front.size()),
                      SQLITE_STATIC);

    if (int rc = sqlite3_step(stmt); rc != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      throw std::runtime_error("Can't delete from table");
    }

    sqlite3_finalize(stmt);
  }

private:
  auto create() -> void {
    const auto query =
        "CREATE TABLE IF NOT EXISTS cards (front TEXT PRIMARY KEY, back TEXT NOT NULL, interval INTEGER NOT NULL DEFAULT 0, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP)"s;

    sqlite3_stmt *stmt = nullptr;

    sqlite3_prepare_v2(db_, query.c_str(), sizetint(query.size()), &stmt,
                       nullptr);

    if (int rc = sqlite3_step(stmt); rc != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      throw std::runtime_error("Can't create table");
    }

    sqlite3_finalize(stmt);
  }

  auto needs_review(int interval, long long updated_at) -> bool {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();

    auto review_at =
        updated_at +
        std::chrono::duration_cast<std::chrono::seconds>(INTERVALS[interval])
            .count();

    return now >= review_at;
  }

  auto update(int interval, const std::string &front) -> void {
    const auto query =
        "UPDATE cards SET interval = ?, updated_at = CURRENT_TIMESTAMP WHERE front = ?"s;

    sqlite3_stmt *stmt = nullptr;

    sqlite3_prepare_v2(db_, query.c_str(), sizetint(query.size()), &stmt,
                       nullptr);

    sqlite3_bind_int(stmt, 1, interval);
    sqlite3_bind_text(stmt, 2, front.c_str(), sizetint(front.size()),
                      SQLITE_STATIC);

    if (int rc = sqlite3_step(stmt); rc != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      throw std::runtime_error("Can't update table");
    }

    sqlite3_finalize(stmt);
  }

  sqlite3 *db_;
};

static auto usage() -> void {
  std::println(stderr, "Usage: {} [add <front> <back> | list | remove <front>]",
               PROGRAM);
}

auto main(int argc, char *argv[]) -> int {
  try {
    Application application;

    if (argc == 1) {
      application.next();
      return 0;
    }

    const std::string command = argv[1];

    if (command == "add"s) {
      requirenargs(4);

      const std::string front = argv[2];
      const std::string back = argv[3];

      application.add(front, back);
    } else if (command == "list"s) {
      requirenargs(2);

      application.list();
    } else if (command == "remove"s) {
      requirenargs(3);

      const std::string front = argv[2];

      application.remove(front);
    } else {
      usage();
      return 1;
    }
  } catch (const std::exception &e) {
    std::println(stderr, "{}", e.what());
    return 1;
  }
}
