FROM python:3.11.15-trixie

RUN useradd -ms /bin/bash cram

RUN apt-get update && \
    apt-get install --yes --no-install-recommends yq

# Checkout and build CRAM
RUN git clone https://github.com/aiiie/cram.git \
    && cd cram/ \
    && pip install -r requirements.txt \
    && make install ||true

WORKDIR /home/cram/

ARG TARGET_DEVICE="generic"
ENV TARGET_DEVICE=$TARGET_DEVICE

ARG CRAM_REMOTE_COPY="scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
ENV TARGET_DEVICE=$CRAM_REMOTE_COPY

ARG TARGET_LAN_IP="192.168.1.1"
ENV TARGET_LAN_IP=$TARGET_LAN_IP

ARG PRPLOS_HASH

# Only clone prplOS CRAM tests for the given commit
RUN git clone -n --depth=1 --filter=blob:none --sparse https://gitlab.com/prpl-foundation/prplos/prplos.git \
    && cd prplos \
    && git sparse-checkout init --cone \
    && git sparse-checkout set .gitlab/tests/cram \
    && git fetch --depth=1 origin $PRPLOS_HASH \
    && git checkout $PRPLOS_HASH
    
ADD entrypoint.sh /
ENTRYPOINT ["/bin/bash", "/entrypoint.sh"]

