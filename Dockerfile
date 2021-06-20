FROM alpine:3.13 AS buildbase

RUN apk add --update --no-cache clang llvm lld cmake make
RUN apk add --update --no-cache build-base flex bison


FROM alpine:3.13 AS runbase

RUN apk add --update --no-cache clang llvm lld cmake make

# Download and install glibc
RUN GLIBC_VERSION="2.33-r0" && \
    apk add --update --no-cache curl && \
    curl -Lo /etc/apk/keys/sgerrand.rsa.pub https://alpine-pkgs.sgerrand.com/sgerrand.rsa.pub && \
    curl -Lo glibc.apk "https://github.com/sgerrand/alpine-pkg-glibc/releases/download/${GLIBC_VERSION}/glibc-${GLIBC_VERSION}.apk" && \
    curl -Lo glibc-bin.apk "https://github.com/sgerrand/alpine-pkg-glibc/releases/download/${GLIBC_VERSION}/glibc-bin-${GLIBC_VERSION}.apk" && \
    apk add glibc-bin.apk glibc.apk && \
    /usr/glibc-compat/sbin/ldconfig /lib /usr/glibc-compat/lib && \
    echo 'hosts: files mdns4_minimal [NOTFOUND=return] dns mdns4' >> /etc/nsswitch.conf && \
    apk del curl && \
    rm -rf glibc.apk glibc-bin.apk



RUN set -ex && \
        apk --no-cache --update add \
        # basic packages
                bash bash-completion coreutils file grep openssl openssh nano sudo tar xz \
        # debug tools
                binutils gdb musl-dbg strace \
        # docs and man
                bash-doc man-db man-pages less less-doc \
        # GUI fonts
                font-noto \
        # user utils
                shadow \
        # Hunter likey
                tree vim gedit filezilla geany gnome-desktop unzip build-base cmake clang bison flex lld git llvm 

RUN set -ex && \
        apk --no-cache --update add \
        # C++ build tools
                cmake g++ git linux-headers libpthread-stubs make

RUN set -ex && \
        apk --no-cache --update add \
        # Java tools
                gradle openjdk8

# Create a new user with no password
ENV USERNAME hunter
RUN set -ex && \
        useradd --create-home --key MAIL_DIR=/dev/null --shell /bin/bash $USERNAME && \
        passwd -d $USERNAME

# Set timezone (default = UTC)
ENV TZ US/Eastern
RUN set -ex && \
        apk add tzdata && \
        cp /usr/share/zoneinfo/$TZ /etc/localtime && \
        echo "$TZ" > /etc/timezone && \
        date

# Set additional environment variables
ENV JAVA_HOME /usr/lib/jvm/java-1.8-openjdk
ENV JDK_HOME  /usr/lib/jvm/java-1.8-openjdk
ENV JAVA_EXE  /usr/lib/jvm/java-1.8-openjdk/bin/java


RUN echo "user ALL=(root) NOPASSWD:ALL" > /etc/sudoers.d/user && \
    chmod 0440 /etc/sudoers.d/user

RUN su hunter -c "mkdir /home/hunter/workspace/"
RUN su hunter -c "cd /home/hunter/workspace && git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/XboxDev/nxdk.git"
RUN su hunter -c "cd /home/hunter/workspace/nxdk/ && make -C samples/sdl/"
RUN su hunter -c "cd /home/hunter/workspace/nxdk/samples && git clone https://github.com/huntergdavis/jc_reborn.git"

