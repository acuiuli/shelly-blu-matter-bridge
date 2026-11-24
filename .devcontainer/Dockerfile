# The official Matter image supplies the released Matter SDK and host tools.
# IDF is advanced to its latest stable release, with its own pinned submodules.
ARG BASE_IMAGE=espressif/esp-matter@sha256:a55d497c9b53e5a65789360e47ffb5cc3622a386e7b5bb76d2129578dc33c4e6
FROM ${BASE_IMAGE}
ARG ESP_IDF_REV=051e4c4eb20346e043cdd47bc388fde75ff8af63
USER root
RUN git -C /opt/espressif/esp-idf fetch --depth 1 origin "${ESP_IDF_REV}" \
    && git -C /opt/espressif/esp-idf checkout --detach "${ESP_IDF_REV}" \
    && git -C /opt/espressif/esp-idf submodule update --init --recursive --depth 1 \
    && /usr/bin/python3 /opt/espressif/esp-idf/tools/idf_tools.py install --targets=esp32s3,esp32h2
COPY python-tools.lock /tmp/bridge-python-tools.lock
RUN PIP_CONSTRAINT=/tmp/bridge-python-tools.lock \
      /usr/bin/python3 /opt/espressif/esp-idf/tools/idf_tools.py install-python-env \
    && bash -c 'source /opt/espressif/esp-idf/export.sh && PIP_CONSTRAINT=/tmp/bridge-python-tools.lock python -m pip install -r /opt/espressif/esp-matter/requirements.txt -r /tmp/bridge-python-tools.lock'
LABEL org.opencontainers.image.description="Shelly BLU bridge development toolchain: ESP-Matter 1.6.1 and ESP-IDF 6.1.1"
COPY patches /tmp/bridge-patches
RUN git -C /opt/espressif/esp-matter apply --check /tmp/bridge-patches/esp-matter-const-log-tag.patch \
    && git -C /opt/espressif/esp-matter apply /tmp/bridge-patches/esp-matter-const-log-tag.patch
