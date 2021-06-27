# Minecraft Supervisor
An init process for containers to help run Minecraft Java servers.

## What does this do?
This utility provides an entry-point for usage in containers running
Minecraft servers. Minecraft servers can be a little finnicky if run
directly inside containers as the init process. The program `mcsv` does three
main things: First, it listens for signals. Upon receiving INT, TERM, HUP, or
QUIT, it will send a stop command to the stdin of the server, ensuring a
graceful termination. Second, it also opens up a socket `/run/mcsv.sock`. This
socket is how you can use `mcutil` to send commands to your server without
having to directly attach to its stdin. This utility makes it easy to do
operation tasks directly from your container orchestrator. For example, using
`kubectl`:
```console
kubectl exec -it <minecraft-server-container> -- mcutil
```
This will open a write-only shell to send commands to the server. Multiple can
be open at the same time and not interfere with each other. `stdout` and
`stderr` of the server are the same as `mcsv`, so you can use the native
tooling of your orchestration system to view logs. (All `mcsv` messages go to
`stderr` and are start with `[[MCSV]]`). And third, it will properly clean
up after zombie processes as it serves the role of `init`.

***

The helper program `mcutil` can be used in two ways. By running it with no
arguments, it will receive commands from `stdin` until closed. Alternatively,
if given an argument, it will send that as a command to the server. Make sure
to quote the command if it is more than one word. (example: `mcutil "cmd arg1
arg2 ..."`).

## How to use this repository?
I recommend you clone this repo and build the container yourself. For
convenience, a pre-built image is provided at
`registry.gitlab.com/renilux/mcsv`. This image is based on
`docker.io/openjdk:8`, the binaries are in `/usr/local/bin`, has a default
working directory of `/data`, and entry-point `/usr/local/bin/mcsv`. This image
*does not* have a server jar in it.

The arguments provided to the program will be the command passed to `exec`. An
example usage:
```console
docker run -it --rm registry.gitlab.com/renilux/mcsv:latest java -jar server.jar --nogui
```
You might need to provide the `--nogui` option to prevent it from crashing.

***

The programs `mcsv` and `mcutil` are licensed under the MIT License.
