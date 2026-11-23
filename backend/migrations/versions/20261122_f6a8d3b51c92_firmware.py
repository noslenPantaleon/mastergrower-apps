"""firmware (OTA)

Revision ID: f6a8d3b51c92
Revises: e5f2c7a90d13
Create Date: 2026-11-22 21:00:00

"""
from collections.abc import Sequence

import sqlalchemy as sa
from alembic import op

revision: str = "f6a8d3b51c92"
down_revision: str | Sequence[str] | None = "e5f2c7a90d13"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.create_table(
        "firmware",
        sa.Column("id", sa.BigInteger().with_variant(sa.Integer(), "sqlite"), autoincrement=True,
                  nullable=False),
        sa.Column("kind", sa.String(length=8), nullable=False),
        sa.Column("version", sa.String(length=32), nullable=False),
        sa.Column("project", sa.String(length=32), nullable=False),
        sa.Column("idf_ver", sa.String(length=32), nullable=False),
        sa.Column("size", sa.Integer(), nullable=False),
        sa.Column("sha256", sa.String(length=64), nullable=False),
        sa.Column("path", sa.String(length=128), nullable=False),
        sa.Column("uploaded_by", sa.String(length=64), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.text("now()"),
                  nullable=False),
        sa.PrimaryKeyConstraint("id", name=op.f("pk_firmware")),
        sa.UniqueConstraint("kind", "version", name=op.f("uq_firmware_kind")),
    )
    op.create_index(op.f("ix_firmware_kind"), "firmware", ["kind"])


def downgrade() -> None:
    op.drop_index(op.f("ix_firmware_kind"), table_name="firmware")
    op.drop_table("firmware")
