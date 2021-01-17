FROM ubuntu:focal as build

RUN apt-get -y update
RUN DEBIAN_FRONTEND="noninteractive" apt-get -y install gcc g++ curl cmake libcurl4-openssl-dev libpqxx-6.4 libpqxx-dev

WORKDIR /build
RUN curl -s -O -L https://github.com/abedra/libvault/archive/0.36.0.tar.gz
RUN tar xzf 0.36.0.tar.gz

WORKDIR /build/libvault-0.36.0
RUN cmake -Bbuild -H. -DENABLE_TEST=OFF -DENABLE_INTEGRATION_TEST=OFF -DCMAKE_INSTALL_DIR=/usr/lib/x86_64-linux-gnu
RUN cmake --build build/

WORKDIR /build/libvault-0.36.0/build
RUN make -j install

WORKDIR /build/main
RUN mkdir lib
COPY Makefile .
COPY lib/json.hpp lib/json.hpp
COPY main.cpp .
RUN make main

FROM ubuntu:focal

RUN apt-get -y update
RUN apt-get -y install libpqxx-6.4 libcurl4

RUN groupadd user && useradd -g user user && mkdir -p /run
RUN chown -R user: /run
USER user

COPY --from=build /build/libvault-0.36.0/build/libvault.so.0.36.0 /usr/lib/x86_64-linux-gnu
COPY --from=build /build/libvault-0.36.0/build/libvault.so.0      /usr/lib/x86_64-linux-gnu
COPY --from=build /build/libvault-0.36.0/build/libvault.so        /usr/lib/x86_64-linux-gnu
COPY --from=build /build/main/main                                /run/
COPY config.json /run/

WORKDIR /run

CMD [ "./main" ]
