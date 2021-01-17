main.o: main.cpp
	g++ -std=c++17 -Wall -Werror -c lib/json.hpp main.cpp

main: main.o
	g++ main.o -o main -lvault -lcurl -lpqxx

.PHONY: clean
clean:
	rm -f *.o main

.PHONY: docker
docker:
	docker build -t dynamic-secrets .

.PHONY: docker-network
docker-network:
	docker network create dynamic-secrets

.PHONY: docker-run
docker-run: docker
	docker run --net dynamic-secrets --env-file .env dynamic-secrets

.PHONY: vault
vault:
	docker rm -f dynamic-secrets-vault
	docker run --net dynamic-secrets -p 8200:8200 --name dynamic-secrets-vault vault

.PHONY: vault-setup
vault-setup:
	vault/setup

.PHONY: postgres
postgres:
	docker rm -f dynamic-secrets-postgres
	docker run -e POSTGRES_USER=postgres -e POSTGRES_PASSWORD=postgres -e POSTGRES_DB=vault -p 5432:5432 --name dynamic-secrets-postgres --net dynamic-secrets postgres