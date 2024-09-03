# Modem console FPE

## Overview

This example is designed for experimenting with a modem device, sending custom commands, and switching to PPP mode using the `esp-console` command line API. You can explore the list of supported commands by using the `help` command.

### Important Note

Before using any of the HTTP commands (`httpget`, `httppost`), ensure that the modem is set to `PPP` mode. You can do this using the `set_mode` command.

**Usage:**
```bash
set_mode PPP
```

## Custom Commands

This example implements several custom commands to demonstrate and test various functionalities:

**Usage:**

```bash
httpget <url>
```

- GET 500KB File from our server

```bash
httpget https://app-esp32-modem-test-8d3ed92fa208.herokuapp.com/download/file/364575
```

```bash
httppost <url>
```
- Upload a 100KB file:

```bash
httppost https://app-esp32-modem-test-8d3ed92fa208.herokuapp.com/upload /littlefs/file_100KB.txt
```

- Upload a 500KB file:

```bash
httppost https://app-esp32-modem-test-8d3ed92fa208.herokuapp.com/upload /littlefs/file_500KB.txt
```

- Upload a 1MB file:

```bash
httppost https://app-esp32-modem-test-8d3ed92fa208.herokuapp.com/upload /littlefs/file_1MB.txt
```

- Show littlefs folder system

```bash
show_littlefs_tree
```

## Why These Endpoints?

The reason we are using this specific Heroku server endpoint for HTTPS connections is that we have the corresponding cert.pem, which allows us to perform GET securely and POST requests to this server.



