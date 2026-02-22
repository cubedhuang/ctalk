# ctalk

c chat

## protocol

`CHAT` is a magic prefix

### server message structure

```
CHAT<type><content>
```

- 2 types of server messages
  - `p`: **prompt** - show a prompt to the user (client uses `linenoise` to handle this)
    - invariant: multiple prompt messages will not be sent before the client sends a message back to the server
  - `m`: **message** - print content to the user

### user message structure

```
CHAT<length><content>
```

- `length` is the length of `content` in bytes, encoded as a 4-byte big-endian integer
- `content` is the message content, encoded as UTF-8
