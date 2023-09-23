FROM mcr.microsoft.com/devcontainers/base:alpine-3.18

RUN \
   apk --no-cache add build-base linux-headers cmake git && \
   apk --no-cache --repository http://dl-cdn.alpinelinux.org/alpine/v3.16/community add fuse3-dev flatbuffers-dev