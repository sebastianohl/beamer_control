ARG VARIANT="buster"
FROM mcr.microsoft.com/vscode/devcontainers/base:0-${VARIANT}

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y git python3-pip gcc git wget make libncurses-dev flex bison gperf python python-serial wget cmake screen

RUN export DEBIAN_FRONTEND=noninteractive && apt-get remove --purge -y python python2.7 && apt-get autoremove --purge -y
RUN ln -s /usr/bin/python3 /usr/bin/python
RUN ln -s /usr/bin/pip3 /usr/bin/pip

RUN mkdir -p /esp && cd esp \
    && wget -O - https://dl.espressif.com/dl/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz | tar xvz
RUN cd /esp && git clone --recursive https://github.com/espressif/ESP8266_RTOS_SDK.git
RUN pip3 install -r /esp/ESP8266_RTOS_SDK/requirements.txt

