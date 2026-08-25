# Упаковка статического бинарника voterpool (собран хостом через ./build.sh)
# в минимальный distroless-образ. Сборка из корня репозитория:
#   docker build -f deploy/voterpool.Dockerfile -t voterpool:local .
FROM gcr.io/distroless/cc-debian12@sha256:e5d81ddde149641e2a9ba55be4545bc125c67de07508b03ba4c22e6eb0ded5aa

COPY build/voterpool /app/voterpool
COPY deploy/config/docker.yaml /etc/voterpool/config.yaml

WORKDIR /data
EXPOSE 8080

ENTRYPOINT ["/app/voterpool"]
CMD ["--config", "/etc/voterpool/config.yaml"]
