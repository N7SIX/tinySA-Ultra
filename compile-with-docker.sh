#!/bin/bash
# Usage: ./compile-with-docker.sh [TARGET]
# Example: ./compile-with-docker.sh F303

set -e

TARGET=${1:-F303}
IMAGE=tinysa-build

# Build the Docker image if it doesn't exist
if ! docker image inspect $IMAGE > /dev/null 2>&1; then
  docker build -t $IMAGE .
fi

docker run --rm -v "$PWD":/workspace -w /workspace $IMAGE bash -c "\
  git config --global --add safe.directory /workspace; \
  make clean TARGET=\"$TARGET\" && make TARGET=\"$TARGET\""
