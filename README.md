
# Termtunnel [![Termtunnel](https://github.com/beordle/termtunnel/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/beordle/termtunnel/actions/workflows/build.yml)
Termtunnel is a tool allow you create an tunnel via multiple hops or fight against intranet isolation in a very simple way, Similar to lrzsz. 

## Working principle

As you can imagine, we use the method of tapping the string to upload a message to remote in Terminal, and then get a message back from remote in the Terminal, and in this way, we get a point-to-point transmission channel.

```mermaid
flowchart LR
    local(temrtunnel local) <--> ssh
    ssh <--> sshd
    sshd <--> bash
    bash <--> remote(termtunnel remote)
```

Simply put, Termtunnel use pty to control local application, write data to its stdin, read data from its stdout. and then the local appliction stdin and stdout be link with remote termtunnel.


## Quick start

**You must ensure that the Termtunnel binary exists on both the local and remote.**

Please use termtunnel locally to open any terminal application such as ssh, bash, etc.

`termtunnel ssh root@111.111.111.111`

After running, the terminal output is the same as without the `termtunnel` prefix, and you can keep your normal usage habits.

When a tunnel needs to be established, just run command `termtunnel -a` on that remote host.

In remote Termtunnel console, you are allowed to download files up and down, or create socks5 proxy.

For example, you can execute `termtunnel ssh root@19.95.02.23` locally, then start `/tmp/termtunnel -a` on the ssh host to enter the interactive console, and then you can type `upload` to upload local file to remote or create a port forward. supported commands are listed in the (REPL Command)[#repl-command] section.

## Download
   See [Github Releases](https://github.com/beordle/termtunnel/releases)

## Build from Source
```
cmake .
make
```


## REPL Command
   > fff


## FAQ

1. **Can I make the whole process unattended？** To reduce user intervention, you can try to use UNIX expect tool.
2. **How to use it with tmux？** Out of the box. Designed with tmux in mind. But because of the implementation of tmux, the speed is very limited. If you want to improve the speed, you need to modify the source code and recompile tmux

## License
This application is free software; you can redistribute it and/or modify it under the terms of the MIT license. See LICENSE file for details.
