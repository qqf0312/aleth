# Hyra：Building Instructions

We use **CMake** as the building system.

## Step 1: Configure the Build

First, open a terminal and navigate to the root directory of the project:

```bash
cd aleth
```

Then configure the build plan:

```bash
cmake -S . -B build
```

## Step 2: Build the Project

Run the following command to compile:

```bash
cmake --build build -j
```

This will compile and link the main library and several executables.

## Step 3: Run the Program

The generated executable is located at:

```
./build/aleth/aleth
```

The following lists only the options used by Hyra in this guide 

| Option           | Syntax                      | What it does                                                 |
| ---------------- | --------------------------- | ------------------------------------------------------------ |
| `--config`       | `--config <file>`           | Path to the specialized chain/node JSON config.              |
| `--data-dir`     | `--data-dir <path>`         | Base directory for runtime data (keystore, logs, etc.).      |
| `--db-path`      | `--db-path <path>`          | Database directory for chain/state storage.                  |
| `--network-id`   | `--network-id <n>`          | Connect only to peers with this network ID (chain separation). |
| `--no-discovery` | `--no-discovery`            | Disable node discovery; implies no auto-bootstrapping.       |
| `--listen-ip`    | `--listen-ip <ip>(:<port>)` | Bind IP for inbound P2P connections.                         |
| `--listen`       | `--listen <port>`           | P2P TCP listening port (default 30303).                      |
| `--peerset`      | `--peerset <list>`          | Comma-separated fixed peers. Each element is `type:enode://<pubkey>@<ip>:<port>`. Supported `type`: `required` (always keep connected), `default`. |

Before running the program, create a file named `config.ini` inside the `build/aleth` directory.

### Example `config.ini`:

```ini
# ============================================================
# Hyra Configuration Template
# ============================================================
# This file defines the parameters for EC settings, workload,
# readback, and participating nodes.
# ============================================================

[ec]
# Number of EC nodes in the system
nodes = 4

# Fault tolerance threshold (e.g. f = 2 means tolerates 2 failures)
f = 2

# Encoding level (affects redundancy or erasure coding depth)
level = 2


[workload]
# Number of blocks to generate during synthetic load
blocks = 2000

# Number of accounts per block
accounts_per_block = 1000

# Zipfian skew factor for account access distribution (0 = uniform)
skew = 0.0

# Initial balance for each account
balance_start = 1

# Total logical account space size
account_space = 1000000


[readback]
# Enable readback verification (1 = enabled, 0 = disabled)
enable = 1

# Limit on number of accounts to read back
limit = 20000

[nodes]
# Define participating node identifiers (node0, node1, node2, ...)
# Each nodeX corresponds to node's unique public key (its enode ID).
node0 = nodeA
node1 = nodeB
node2 = nodeC
node3 = nodeD
```

> You can modify the values as needed. The program will read `config.ini` from the current working directory.

## Example Command

```bash
./aleth \
  --config ~/node0/config.json \
  --data-dir ~/node0/data \
  --db-path  ~/node0/db \
  --network-id 12345 \
  --no-discovery \
  --listen-ip 127.0.0.1 \
  --listen 30303 \
  --peerset required:enode://<nodeID>@ip:listen-port
```

**Note:** `--peerset` must include **all nodes** you intend to interact with (e.g., `nodeA`–`nodeD` from the `config.ini` example).