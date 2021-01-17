# Secrets Management: Dynamic Credentials

## Introduction

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
