#!/usr/bin/env bash

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
source ${DIR}/set_container_vars.sh

IMAGE_NAME="${REGISTRY_REPO}:${IMAGE_TAG}"
CONTAINER_NAME="android_builder_soh"
PROJECT_PATH=$(realpath "${DIR}/../../")

ONLY_RELEASE_BUILD="false"
if [[ "$1" == "--release" ]] || [[ "$1" == "-r" ]]; then
    ONLY_RELEASE_BUILD="true"
fi

GRADLEW_BUILD_TYPE="build"
if [[ "$ONLY_RELEASE_BUILD" == "true" ]]; then
    GRADLEW_BUILD_TYPE="assembleRelease"
fi

echo ""
echo "-----------------------------------------------------------"
echo "-- Android Project Build                                 --"
echo "-----------------------------------------------------------"
echo "Building project '${PROJECT_PATH}'..."
echo ""

if [ ! -d "$PROJECT_PATH" ]; then
    echo "Error: Project path not found"
    echo ""
    exit 1
fi

docker run --network="host" --rm \
    --name $CONTAINER_NAME \
    -v "$PROJECT_PATH":/workspace \
    -w /workspace \
    $IMAGE_NAME \
    bash -c "cp -a local.properties Android/ && cd Android && ./gradlew --no-daemon ${GRADLEW_BUILD_TYPE}"

BUILD_RESULT=$?

echo ""
if [ $BUILD_RESULT -ne 0 ]; then
    echo "Build failed (error code ${BUILD_RESULT})"
    exit $BUILD_RESULT
else
    echo "Build success"
fi

exit 0
