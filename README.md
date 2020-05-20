Monitor class using 0MQ

Known issues (20.05.20):
- If two processes want to enter same object at the same time - they will.
- Can lead to deadlock if one process will receive too many messages at the same time.
