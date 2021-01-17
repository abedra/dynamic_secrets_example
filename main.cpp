#include <filesystem>
#include <fstream>
#include <iostream>
#include <pqxx/pqxx>

#include "VaultClient.h"
#include "lib/json.hpp"

struct DatabaseConfig {
  int port;
  std::string host;
  std::string database;
  std::string secretRole;
  std::string username;
  std::string password;

  DatabaseConfig withSecrets(const Vault::Client &vaultClient) {
    Vault::Database postgresAdmin{vaultClient};
    auto secretResponse = postgresAdmin.generateCredentials(Vault::Path{this->secretRole});
    if (secretResponse) {
      auto credentials = nlohmann::json::parse(secretResponse.value())["data"];
      this->username = credentials["username"];
      this->password = credentials["password"];

      return *this;
    } else {
      return *this;
    }
  }

  std::string connectionString() {
    std::stringstream ss;
    ss << "host=" << host << " "
       << "port=" << port << " "
       << "user=" << username << " "
       << "password=" << password << " "
       << "dbname=" << database;

    return ss.str();
  }
};

void from_json(const nlohmann::json &j, DatabaseConfig &databaseConfig) {
  j.at("port").get_to(databaseConfig.port);
  j.at("host").get_to(databaseConfig.host);
  j.at("database").get_to(databaseConfig.database);
  j.at("secret_role").get_to(databaseConfig.secretRole);
}

DatabaseConfig getDatabaseConfiguration(const std::filesystem::path &path) {
  std::ifstream inputStream(path.generic_string());
  std::string raw(std::istreambuf_iterator<char>{inputStream}, {});

  return nlohmann::json::parse(raw)["database"];
}

Vault::Client getVaultClient() {
  char *roleId = std::getenv("APPROLE_ROLE_ID");
  char *secretId = std::getenv("APPROLE_SECRET_ID");

  if (!roleId && !secretId) {
    std::cout << "APPROLE_ROLE_ID and APPROLE_SECRET_ID environment variables must be set" << std::endl;
    exit(-1);
  }

  Vault::AppRoleStrategy appRoleStrategy{Vault::RoleId{roleId}, Vault::SecretId{secretId}};
  Vault::Config config = Vault::ConfigBuilder()
                             .withHost(Vault::Host{"dynamic-secrets-vault"})
                             .withTlsEnabled(false)
                             .build();

  return Vault::Client{config, appRoleStrategy};
}

int main(void) {
  std::filesystem::path configPath{"config.json"};
  Vault::Client vaultClient = getVaultClient();

  if (vaultClient.is_authenticated()) {
    try {
      DatabaseConfig databaseConfig = getDatabaseConfiguration(configPath).withSecrets(vaultClient);
      pqxx::connection databaseConnection{databaseConfig.connectionString()};

      databaseConnection.is_open() 
        ? std::cout << "Connected" << std::endl 
        : std::cout << "Could not connect" << std::endl;
    } catch (const std::exception &e) {
      std::cout << e.what() << std::endl;
    }
  } else {
    std::cout << "Unable to authenticate to Vault" << std::endl;
  }
}