#ifndef OBCX_INCLUDE_CORE_DB_MANAGER_HPP_
#define OBCX_INCLUDE_CORE_DB_MANAGER_HPP_

#include "common/config_snapshot.hpp"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace obcx::core {

using DbValue = std::variant<std::nullptr_t, std::int64_t, double, std::string>;
using DbParams = std::vector<DbValue>;
using DbRow = std::unordered_map<std::string, DbValue>;

class IDbConnection {
public:
  IDbConnection() = default;
  IDbConnection(const IDbConnection &) = delete;
  auto operator=(const IDbConnection &) -> IDbConnection & = delete;
  IDbConnection(IDbConnection &&) = delete;
  auto operator=(IDbConnection &&) -> IDbConnection & = delete;
  virtual ~IDbConnection() = default;

  virtual void execute(const std::string &sql, const DbParams &params = {}) = 0;
  [[nodiscard]] virtual auto query(const std::string &sql,
                                   const DbParams &params = {})
      -> std::vector<DbRow> = 0;
  virtual void run_write_task(std::function<void(IDbConnection &)> work) = 0;
  virtual void run_transaction_task(
      std::function<void(IDbConnection &)> work) = 0;
  virtual void with_migration_lock(
      const std::string &namespace_name,
      std::function<void(IDbConnection &)> work) = 0;
};

class IDbProvider {
public:
  IDbProvider() = default;
  IDbProvider(const IDbProvider &) = delete;
  auto operator=(const IDbProvider &) -> IDbProvider & = delete;
  IDbProvider(IDbProvider &&) = delete;
  auto operator=(IDbProvider &&) -> IDbProvider & = delete;
  virtual ~IDbProvider() = default;

  virtual void validate_config(const common::DbInstanceConfig &config) const {
    (void)config;
  }

  [[nodiscard]] virtual auto create_connection(
      const common::DbInstanceConfig &config)
      -> std::shared_ptr<IDbConnection> = 0;
};

class DbManager {
public:
  DbManager();

  [[nodiscard]] static auto shared_manager(
      std::vector<common::DbInstanceConfig> configs)
      -> std::shared_ptr<DbManager>;
  static void reset_shared_managers_for_tests();

  void register_provider(std::string type,
                         std::shared_ptr<IDbProvider> provider);

  void configure(const std::vector<common::DbInstanceConfig> &configs);

  [[nodiscard]] auto validate_configs(
      const std::vector<common::DbInstanceConfig> &configs) const
      -> std::vector<std::string>;

  [[nodiscard]] auto connection(const std::string &instance_name) const
      -> std::shared_ptr<IDbConnection>;

  template <typename Result, typename Work>
  auto run_read(const std::string &instance_name, Work &&work) const -> Result {
    return std::invoke(std::forward<Work>(work), *connection(instance_name));
  }

  template <typename Result, typename Work>
  auto run_write(const std::string &instance_name, Work &&work) const
      -> Result {
    auto db = connection(instance_name);
    std::exception_ptr exception;

    if constexpr (std::is_void_v<Result>) {
      db->run_write_task([&](IDbConnection &connection) {
        try {
          std::invoke(std::forward<Work>(work), connection);
        } catch (...) {
          exception = std::current_exception();
        }
      });
      if (exception) {
        std::rethrow_exception(exception);
      }
    } else {
      std::optional<Result> result;
      db->run_write_task([&](IDbConnection &connection) {
        try {
          result = std::invoke(std::forward<Work>(work), connection);
        } catch (...) {
          exception = std::current_exception();
        }
      });
      if (exception) {
        std::rethrow_exception(exception);
      }
      return std::move(*result);
    }
  }

  template <typename Result, typename Work>
  auto run_transaction(const std::string &instance_name, Work &&work) const
      -> Result {
    auto db = connection(instance_name);
    if constexpr (std::is_void_v<Result>) {
      db->run_transaction_task([&](IDbConnection &connection) {
        std::invoke(std::forward<Work>(work), connection);
      });
    } else {
      std::optional<Result> result;
      db->run_transaction_task([&](IDbConnection &connection) {
        result = std::invoke(std::forward<Work>(work), connection);
      });
      return std::move(*result);
    }
  }

  void with_migration_lock(const std::string &instance_name,
                           const std::string &namespace_name,
                           std::function<void(IDbConnection &)> work) const;

private:
  std::unordered_map<std::string, std::shared_ptr<IDbProvider>> providers_;
  std::unordered_map<std::string, std::shared_ptr<IDbConnection>> instances_;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_DB_MANAGER_HPP_
