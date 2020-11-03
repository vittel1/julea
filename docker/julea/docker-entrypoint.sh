#!/bin/bash

. /julea/scripts/environment.sh

julea-config --user \
  --object-servers="localhost:9876" --kv-servers="localhost:9876" --db-servers="localhost:9876" \
  --object-backend=posix --object-component=server --object-path="/tmp/julea-$(id -u)/posix" \
  --kv-backend=lmdb --kv-component=server --kv-path="/tmp/julea-$(id -u)/lmdb" \
  --db-backend=sqlite --db-component=server --db-path="/tmp/julea-$(id -u)/sqlite"

julea-server --daemon --host "localhost" --port "9876"
