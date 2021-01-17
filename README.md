# Secrets Management: Dynamic Credentials

## Introduction

In the [previous post on secrets management](https://aaronbedra.com/post/injecting_secrets/), we explored a light lift into storing your secrets using a Hashicorp Vault instead of leaving them in the configuration file. While this approach solves the [secret sprawl](https://www.hashicorp.com/resources/what-is-secret-sprawl-why-is-it-harmful) problem, it does not address consistent rotation of credentials. Credential rotation for service accounts is often overlooked, and when performed can lead to downtime if performed infrequently. Starting from the assumption that your username and password will be dynamically generated and handed to your process when it starts creates a design that considers all credentials to be ephemeral and eliminates the need for credential rotation. This post will walk through setting up Vault to provide dynamically generated credentials as well as updating your program to use them.

The examples in this post can be referenced in their entirety at [https://github.com/abedra/dynamic_secrets_example](https://github.com/abedra/dynamic_secrets_example).

## Our Program

If you have not yet read the [previous post on secrets management](https://aaronbedra.com/post/injecting_secrets/), now would be a good time to do so. This post will skip the initial program explanation as it does not change for the examples that follow.

### Loading Configuration

Like our previous post, we will pickup our configuration from a JSON file on disk, however, this time we will skip the username and password entirely.

```json
{
  "database": {
    "host": "dynamic-secrets-postgres",
    "port": 5432,
    "database": "postgres",
    "secret_role": "example"
  }
}
```

In this iteration, we replace the username and password keys with a single `secret_role` key. This is the name of the role we will provide to vault to get credentials suitable for use in our program. To hold our configuration we will use a structure similar to the previous example, but we will add a place to hold our new configuration value:

```cpp
struct DatabaseConfig {
  int port;
  std::string host;
  std::string database;
  std::string secretRole;
  std::string username;
  std::string password;
};
```

We can deserialize the configuration like so:

```cpp
void from_json(const nlohmann::json &j, DatabaseConfig &databaseConfig) {
  j.at("port").get_to(databaseConfig.port);
  j.at("host").get_to(databaseConfig.host);
  j.at("database").get_to(databaseConfig.database);
  j.at("secret_role").get_to(databaseConfig.secretRole);
}
```

## Vault Setup

Setting up Vault for dynamic PostgreSQL secrets is a little more involved than our previous Key Value store setup. The following script will leave us in a place where we will be able to start requesting credentials by simply running our program:

```shell
#!/usr/bin/env bash

set -e

VAULT_VERSION=1.6.1

if [[ ! -f "bin/vault" ]]; then
    mkdir -p bin

    pushd bin

    curl -O -L https://releases.hashicorp.com/vault/$VAULT_VERSION/vault_"$VAULT_VERSION"_linux_amd64.zip
    unzip vault_"$VAULT_VERSION"_linux_amd64.zip
    rm vault_"$VAULT_VERSION"_linux_amd64.zip

    popd
fi

export VAULT_ADDR=http://127.0.0.1:8200
VAULT=bin/vault

$VAULT login
$VAULT policy write example vault/example.hcl
$VAULT auth enable approle
$VAULT write auth/approle/role/client policies="example"
ROLE_ID=$($VAULT read auth/approle/role/client/role-id | grep role_id | awk '{print $2}')
SECRET_ID=$($VAULT write -f auth/approle/role/client/secret-id | grep -m1 secret_id | awk '{print $2}')
$VAULT secrets enable database
$VAULT write database/config/postgresql    \
    plugin_name=postgresql-database-plugin \
    allowed_roles="example"                \
    username="postgres"                    \
    password="postgres"                    \
    connection_url="postgresql://{{username}}:{{password}}@dynamic-secrets-postgres:5432?sslmode=disable"
$VAULT write database/roles/example \
    db_name=postgresql              \
    default_ttl="1h"                \
    max_ttl="24h"                   \
    creation_statements="CREATE ROLE \"{{name}}\" WITH LOGIN PASSWORD '{{password}}' VALID UNTIL '{{expiration}}'; GRANT SELECT ON ALL TABLES IN SCHEMA public TO \"{{name}}\";" 

rm -f .env

echo "APPROLE_ROLE_ID=$ROLE_ID" >> .env
echo "APPROLE_SECRET_ID=$SECRET_ID" >> .env
```

This script enables the database secret backend and establishes a configuration for postgres using the `postgresql-database-plugin` inside Vault. Vault provides plugins for [most popular databases](https://www.vaultproject.io/docs/secrets/databases). The credentials supplied when creating the configuration must themselves contain enough privilege to allow user creation and privilege grants to any table required. Next, it creates a role that issues a command every time credentials are requested. The SQL statement in the `creation_statements` field is what runs during credential generation.

## Obtaining Credentials for Our Program

We will keep the `withSecrets` api from the previous post, but modify it to use the `Vault::Database` class.

```cpp
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
```

In this method we invoke `generateCredentials` to provide us with the required username and password. You will pretty quickly notice the ambiguity around having or not having a value from Vault is no longer present. If this call fails, there will be no way to connect to the database successfully. Our `main` method wraps all of the connection semantics in a `try`/`catch` block, so failure here will only result in libpqxx printing the result of the connection failure. Outside of an example, more care should be taken to address this failure earlier and either recover or exit.

## Putting it All Together

Let's take a look at the resulting `main` method:

```cpp
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
```

You won't notice any difference between this `main` and the `main` in the previous post. This is the result you should be shooting for when you design your program. The mechanism that provides your connection details should not change the interface you use to acquire them. This leaves you free to swap your implementation at will and not have to worry about consumers mishandling the information.

## Running the Example

This example is fully dockerized and uses three separate containers. One for PostgreSQL, one for Vault, and one for our program. The following commands will run the example.

In one terminal:

```shell
make docker-network
make postgres
```

In another terminal:
```shell
make vault
```

In a third terminal, copy the root token from the second terminal output after Vault has completed booting:
```shell
make vault-setup # paste the root token value when prompted
make docker-run
```

You will see the output `Connected` in the terminal if successful.

## Wrap-Up

This example demonstrates the move from static secrets to dynamically generated secrets issued on demand. While this step addresses additional risks associated with credential management such as storage, rotation, and identification, it by design ties your program directly to Vault and makes Vault availability a single point of failure for your program. You should carefully consider the design requirements of your program and infrastructure to determine if this solution is viable in your environment.
