FROM docker.io/gcc
WORKDIR /usr/src/mcsv
COPY src/ /usr/src/mcsv/src/
COPY Makefile /usr/src/mcsv/
RUN make container-build

FROM docker.io/openjdk:8
WORKDIR /data
COPY --from=0 /usr/src/mcsv/mcsv /usr/local/bin/
COPY --from=0 /usr/src/mcsv/mcutil /usr/local/bin/
ENTRYPOINT ["mcsv"]
