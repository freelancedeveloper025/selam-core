RELEASE=9.1.0
docker build -t selamd:${RELEASE} -f Dockerfile.selamd --build-arg USER_ID=$(id -u) --build-arg GROUP_ID=$(id -g) .
