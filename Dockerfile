FROM docker.io/gcc
WORKDIR /build
COPY src/ /build
RUN gcc -o mcsv -O2 -Wall -Wextra mcsv.c && \
    gcc -o mcutil -O2 -Wall -Wextra mcutil.c

FROM docker.io/debian:latest
WORKDIR /data
COPY --from=0 /build/mcsv /usr/local/bin/
COPY --from=0 /build/mcutil /usr/local/bin/
ENTRYPOINT ["mcsv"]