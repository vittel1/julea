#!/bin/bash

echo "Path: $1"
echo "Executable: $2"

. /etc/julea/scripts/environment.sh
make -C $1
$1/$2

