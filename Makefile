.PHONY: gif video server client client-record core-build core-run devcontainer-build app-build app-run app-up app-stop app-restart

gif:
	uv run python -m lenia.scripts.record_hexapod_cuda

video:
	uv run python -m lenia.scripts.record_hexapod_cuda --output resources/hexapod/propulsion.mp4 --speed 5 --story

server:
	uvicorn lenia.server.app:app --host 0.0.0.0 --port 9876 --workers 1 --ws-max-size 65536

client:
	uv run --extra client python -m lenia.client.client --url ws://127.0.0.1:9876/ws

client-record:
	uv run --extra client python -m lenia.client.client --url ws://127.0.0.1:9876/ws --record animation.mp4


core-build:
	docker compose build lenia-core

core-run:
	docker compose run --rm lenia-core


devcontainer-build:
	docker compose build lenia-devcontainer


app-build:
	docker compose build lenia-app

app-run: app-build
	docker compose run --rm --service-ports lenia-app

app-up: app-build
	docker compose up -d lenia-app

app-stop:
	docker compose stop lenia-app

app-restart: app-stop app-up
