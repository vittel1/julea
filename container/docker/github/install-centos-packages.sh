#!/bin/bash

dnf -y upgrade-minimal
dnf -y install --setopt=install_weak_deps=False \
        build-essential \
        curl \
        unzip \
        python3

dnf -y install git

dnf clean all 
rm -rf /var/cache/yum
