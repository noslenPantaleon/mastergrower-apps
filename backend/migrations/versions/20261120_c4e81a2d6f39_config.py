"""config_global, config_node y config_revisions (B3)

Revision ID: c4e81a2d6f39
Revises: b7d2f4a91c08
Create Date: 2026-11-20 00:30:00

"""
from collections.abc import Sequence

import sqlalchemy as sa
from alembic import op
from sqlalchemy.dialects import postgresql

revision: str = "c4e81a2d6f39"
down_revision: str | Sequence[str] | None = "b7d2f4a91c08"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None

JSON = sa.JSON().with_variant(postgresql.JSONB(astext_type=sa.Text()), "postgresql")
TS = sa.DateTime(timezone=True)


def upgrade() -> None:
    op.create_table(
        "config_global",
        sa.Column("key", sa.String(length=64), nullable=False),
        sa.Column("value", JSON, nullable=True),
        sa.Column("updated_at", TS, server_default=sa.text("now()"), nullable=False),
        sa.PrimaryKeyConstraint("key", name=op.f("pk_config_global")),
    )
    op.create_table(
        "config_node",
        sa.Column("node_id", sa.String(length=32), nullable=False),
        sa.Column("key", sa.String(length=64), nullable=False),
        sa.Column("value", JSON, nullable=True),
        sa.Column("updated_at", TS, server_default=sa.text("now()"), nullable=False),
        sa.PrimaryKeyConstraint("node_id", "key", name=op.f("pk_config_node")),
    )
    op.create_table(
        "config_revisions",
        sa.Column("id", sa.BigInteger().with_variant(sa.Integer(), "sqlite"), autoincrement=True,
                  nullable=False),
        sa.Column("scope", sa.String(length=32), nullable=False),
        sa.Column("changed_by", sa.String(length=64), nullable=False),
        sa.Column("changes", JSON, nullable=False),
        sa.Column("created_at", TS, server_default=sa.text("now()"), nullable=False),
        sa.PrimaryKeyConstraint("id", name=op.f("pk_config_revisions")),
    )
    op.create_index(op.f("ix_config_revisions_scope"), "config_revisions", ["scope"], unique=False)


def downgrade() -> None:
    op.drop_index(op.f("ix_config_revisions_scope"), table_name="config_revisions")
    op.drop_table("config_revisions")
    op.drop_table("config_node")
    op.drop_table("config_global")
