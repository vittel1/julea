#!/bin/bash

path=$1
echo "Build path for client is: $path"

. $path/scripts/environment.sh

#Set environment script to .bashrc. So when exec is called with /bin/bash variables are set
sed -i -e '$a. '"$path"'/scripts/environment.sh' ~/.bashrc

#MIT IP sollte es gehen
julea-config --user \
  --object-servers="julea-server:9876" --kv-servers="julea-server:9876" --db-servers="julea-server:9876" \
  --object-backend=posix --object-component=server --object-path="/tmp/julea-$(id -u)/posix" \
  --kv-backend=lmdb --kv-component=server --kv-path="/tmp/julea-$(id -u)/lmdb" \
  --db-backend=sqlite --db-component=server --db-path="/tmp/julea-$(id -u)/sqlite"


ps -aux && tail -f /dev/null
