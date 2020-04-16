FROM ubuntu:19.04

ENV container=docker TERM=xterm LC_ALL=en_US LANGUAGE=en_US LANG=en_US.UTF-8
ENV DEBIAN_FRONTEND=noninteractive

# locale
RUN apt-get update -q > /dev/null && \
        apt-get install --no-install-recommends -yq apt-utils locales language-pack-en dialog \
        > /dev/null && \
        locale-gen $LANGUAGE $LANG

# sudo commmand
RUN apt-get -yq install sudo > /dev/null

# non-privileged user
RUN echo "nonprivuser ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
RUN useradd --no-log-init --home-dir /home/nonprivuser --create-home --shell /bin/bash -u 1000 \
        nonprivuser && adduser nonprivuser sudo
USER nonprivuser
WORKDIR /home/nonprivuser

# system packages
RUN sudo apt-get install --no-install-recommends -yq \
        git make gcc-arm-none-eabi libnewlib-arm-none-eabi \
        python3 python3-pip python3-setuptools python3-wheel \
        > /dev/null && \
        sudo apt-get clean -q

# python packages
RUN pip3 install \
        setuptools \
        Click intelhex PyYAML \
        colorama intervaltree lz4 numpy pycryptodome unicorn

