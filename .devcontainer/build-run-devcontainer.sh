#!/bin/bash
set -e
BASH_SCRIPT=`readlink -f "$0"`
BASH_DIR=`dirname "$BASH_SCRIPT"`
# Run devcontainer locally without VSCode
# docker build -f Dockerfile . -t devcontainer-image:latest
docker run   -it --rm -v ${BASH_DIR}/..:/workspaces -u vscode -w /workspaces --name devcontainer mcr.microsoft.com/devcontainers/dotnet:6.0-bullseye bash
