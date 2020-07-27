# Monitor class using 0MQ REQ/REP sockets.

Monitor will use 127.0.0.1 IP address and claim ports x and x+1 for communication.

MonitorTest project is exact copy of Monitor but it uses arguments passed by user. Used for testing multiple instances while debugging Monitor instance.

Delete/clear "ports.txt" after each usage. If not, it will lead to misbehaviour due to sending messages to not yet created sockets and waiting till timeout.

Known issues (27.07.20):
- ~~If two processes want to enter same object at the same time - they will.~~
- ~~Can lead to deadlock if one process receives too many messages at the same time.~~
- ~~Enter works as Try_Enter.~~
- Cout messages can misrepresent actual state of process due to multithreading.
- ~~If two processes will start entering the same object at the same time it will most probably result in deadlock - better to use try_enter.~~
- ~~Saving processes ports into one file and constant checking them can lead to misbehaviour (some processes will not get all ports or any ports).~~
