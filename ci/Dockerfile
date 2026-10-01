FROM vitasdk/vitasdk@sha256:2e92c60b69cb06d3f20814f6c882e943d8efd80fc96e643e043803bc702fc4ac
COPY ci/install-host-tools.sh /tmp/install-host-tools.sh
RUN bash /tmp/install-host-tools.sh && rm /tmp/install-host-tools.sh
WORKDIR /source
