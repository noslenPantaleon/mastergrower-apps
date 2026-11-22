"""stream_sessions (B5)

Revision ID: e5f2c7a90d13
Revises: d9a3b6c18e47
Create Date: 2026-11-22 14:00:00

"""
from collections.abc import Sequence

import sqlalchemy as sa
from alembic import op

revision: str = "e5f2c7a90d13"
down_revision: str | Sequence[str] | None = "d9a3b6c18e47"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None

TS = sa.DateTime(timezone=True)


def upgrade() -> None:
    op.create_table(
        "stream_sessions",
        sa.Column("session_id", sa.String(length=16), nullable=False),
        sa.Column("node_id", sa.String(length=32), nullable=False),
        sa.Column("requested_by", sa.String(length=64), nullable=False),
        sa.Column("resolution", sa.String(length=8), nullable=False),
        sa.Column("fps_max", sa.Integer(), nullable=False),
        sa.Column("ttl_s", sa.Integer(), nullable=False),
        sa.Column("stream_state", sa.String(length=12), nullable=False),
        sa.Column("end_reason", sa.String(length=40), nullable=True),
        sa.Column("token_hash", sa.String(length=64), nullable=False),
        sa.Column("frames", sa.Integer(), nullable=False),
        sa.Column("bytes", sa.BigInteger().with_variant(sa.Integer(), "sqlite"), nullable=False),
        sa.Column("started_at", TS, server_default=sa.text("now()"), nullable=False),
        sa.Column("expires_at", TS, nullable=False),
        sa.Column("ended_at", TS, nullable=True),
        sa.PrimaryKeyConstraint("session_id", name=op.f("pk_stream_sessions")),
    )
    op.create_index(op.f("ix_stream_sessions_node_id"), "stream_sessions", ["node_id"])
    op.create_index(op.f("ix_stream_sessions_stream_state"), "stream_sessions", ["stream_state"])


def downgrade() -> None:
    op.drop_index(op.f("ix_stream_sessions_stream_state"), table_name="stream_sessions")
    op.drop_index(op.f("ix_stream_sessions_node_id"), table_name="stream_sessions")
    op.drop_table("stream_sessions")
