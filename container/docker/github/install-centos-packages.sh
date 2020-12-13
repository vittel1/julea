#!/bin/bash

yum update -y
yum -y install \
        gcc \
        gcc-c++ \
        make \
        curl \
        unzip \
        python3

yum -y install git

yum clean all
rm -rf /var/cache/yum
