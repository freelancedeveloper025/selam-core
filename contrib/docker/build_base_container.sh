RELEASE=20.04
curl -so selam-deb-key.gpg https://deb.selam.io/pub.gpg
docker build -t selam-ubuntu:${RELEASE} -f Dockerfile.selam-ubuntu .
rm selam-deb-key.gpg
