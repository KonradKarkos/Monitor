Monitor class using 0MQ REQ/REP sockets.

Monitor will use 127.0.0.1 IP address and claim ports x and x+1 for communication.

MonitorTest project is exact copy of Monitor but it uses arguments passed by user. Used for testing multiple instances while debugging Monitor instance.

Known issues (20.05.20):
- If two processes want to enter same object at the same time - they will.
- Can lead to deadlock if one process will receive too many messages at the same time.
- Enter works as Try_Enter.
