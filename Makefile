.PHONY: gif video server client

gif:
	uv run python record_hexapod_cuda.py

video:
	uv run python record_hexapod_cuda.py --output resources/hexapod/propulsion.mp4 --speed 5 --story

server:
	uv run uvicorn server.app:app --host 0.0.0.0 --port 9876 --workers 1 --ws-max-size 65536

client:
	uv run --extra client client.py --url ws://127.0.0.1:9876/ws

client-record:
	uv run --extra client client.py --url ws://127.0.0.1:9876/ws --record animation.mp4
