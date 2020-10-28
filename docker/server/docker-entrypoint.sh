#!/bin/bash

. /etc/julea/scripts/environment.sh

#Set environment script to .bashrc. So when exec is called with /bin/bash variables are set
sed -i -e '$a. /etc/julea/scripts/environment.sh' ~/.bashrc

julea-config --user \
  --object-servers="localhost:9876" --kv-servers="localhost:9876" --db-servers="localhost:9876" \
  --object-backend=posix --object-component=server --object-path="/tmp/julea-$(id -u)/posix" \
  --kv-backend=lmdb --kv-component=server --kv-path="/tmp/julea-$(id -u)/lmdb" \
  --db-backend=sqlite --db-component=server --db-path="/tmp/julea-$(id -u)/sqlite"

#Tail Command is needed otherwise the container is not running after the setup.sh is loaded
#/etc/julea/scripts/setup.sh start && tail -f /dev/null
julea-server --host "localhost" --port "9876" && tail -f /dev/null
