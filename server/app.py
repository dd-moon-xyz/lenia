from fastapi import FastAPI

from server.routes import router

__all__ = ["app"]

app = FastAPI()
app.include_router(router)
