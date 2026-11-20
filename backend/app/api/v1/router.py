from fastapi import APIRouter

from app.api.v1 import captures, events, health, nodes

api_router = APIRouter()
api_router.include_router(health.router)
api_router.include_router(captures.router)
api_router.include_router(events.router)
api_router.include_router(nodes.router)
