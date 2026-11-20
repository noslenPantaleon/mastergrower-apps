"""captures

Revision ID: a1c3e5f70b21
Revises:
Create Date: 2026-11-17 01:38:05

"""
from collections.abc import Sequence

import sqlalchemy as sa
from alembic import op
from sqlalchemy.dialects import postgresql

revision: str = "a1c3e5f70b21"
down_revision: str | Sequence[str] | None = None
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None

JSON = sa.JSON().with_variant(postgresql.JSONB(astext_type=sa.Text()), "postgresql")


def upgrade() -> None:
    op.create_table(
        "captures",
        sa.Column("id", sa.BigInteger().with_variant(sa.Integer(), "sqlite"), autoincrement=True,
                  nullable=False),
        sa.Column("node_id", sa.String(length=32), nullable=False),
        sa.Column("timestamp", sa.DateTime(timezone=True), nullable=False),
        sa.Column("timestamp_approx", sa.Boolean(), nullable=False),
        sa.Column("received_at", sa.DateTime(timezone=True), server_default=sa.text("now()"),
                  nullable=False),
        sa.Column("camera_type", sa.String(length=32), nullable=False),
        sa.Column("trigger_type", sa.String(length=32), nullable=False),
        sa.Column("resolution", sa.String(length=8), nullable=False),
        sa.Column("width", sa.Integer(), nullable=True),
        sa.Column("height", sa.Integer(), nullable=True),
        sa.Column("priority", sa.String(length=8), nullable=False),
        sa.Column("seq", sa.Integer(), nullable=True),
        sa.Column("sensor_snapshot", JSON, nullable=True),
        sa.Column("local_analysis", JSON, nullable=True),
        sa.Column("server_analysis", JSON, nullable=True),
        sa.Column("upload_state", sa.String(length=16), nullable=False),
        sa.Column("image_path", sa.String(length=255), nullable=False),
        sa.Column("image_bytes", sa.Integer(), nullable=False),
        sa.Column("local_id", sa.String(length=64), nullable=True),
        sa.PrimaryKeyConstraint("id", name=op.f("pk_captures")),
        sa.UniqueConstraint("node_id", "local_id", name="uq_captures_node_id_local_id"),
    )
    op.create_index(op.f("ix_captures_node_id"), "captures", ["node_id"], unique=False)
    op.create_index(op.f("ix_captures_timestamp"), "captures", ["timestamp"], unique=False)
    op.create_index(op.f("ix_captures_trigger_type"), "captures", ["trigger_type"], unique=False)


def downgrade() -> None:
    op.drop_index(op.f("ix_captures_trigger_type"), table_name="captures")
    op.drop_index(op.f("ix_captures_timestamp"), table_name="captures")
    op.drop_index(op.f("ix_captures_node_id"), table_name="captures")
    op.drop_table("captures")
