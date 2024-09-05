FROM python:3.12.5-slim-bullseye

RUN apt update && apt install -y curl && curl -sSL https://install.python-poetry.org | python3 -
ENV PATH=$PATH:/root/.local/bin

WORKDIR /src
ADD . /src

RUN poetry install
