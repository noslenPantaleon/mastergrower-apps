"""Errores con el formato del Contrato §5.1: {"code", "message", "detail"}."""

from typing import Any

from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from starlette.exceptions import HTTPException as StarletteHTTPException

_CODES = {
    400: "bad_request",
    401: "unauthenticated",
    403: "forbidden",
    404: "not_found",
    413: "payload_too_large",
    415: "unsupported_media_type",
    422: "invalid_data",
}


class ApiError(Exception):
    def __init__(self, status: int, message: str, code: str | None = None, detail: Any = None):
        self.status = status
        self.code = code or _CODES.get(status, "error")
        self.message = message
        self.detail = detail


def _body(code: str, message: str, detail: Any = None) -> dict[str, Any]:
    return {"code": code, "message": message, "detail": detail}


def validation_detail(errors: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        {"field": ".".join(str(p) for p in e.get("loc", ())), "error": e.get("msg")} for e in errors
    ]


def install_handlers(app: FastAPI) -> None:
    @app.exception_handler(ApiError)
    async def _api_error(_: Request, exc: ApiError) -> JSONResponse:
        headers = {"WWW-Authenticate": "Bearer"} if exc.status == 401 else None
        return JSONResponse(
            _body(exc.code, exc.message, exc.detail), status_code=exc.status, headers=headers
        )

    @app.exception_handler(StarletteHTTPException)
    async def _http_error(_: Request, exc: StarletteHTTPException) -> JSONResponse:
        code = _CODES.get(exc.status_code, "error")
        return JSONResponse(_body(code, str(exc.detail)), status_code=exc.status_code)

    @app.exception_handler(RequestValidationError)
    async def _validation(_: Request, exc: RequestValidationError) -> JSONResponse:
        return JSONResponse(
            _body("invalid_data", "La solicitud no cumple el contrato",
                  validation_detail(list(exc.errors()))),
            status_code=422,
        )
