"""zones, node_tokens y nodes.registered_at (B4)

Revision ID: d9a3b6c18e47
Revises: c4e81a2d6f39
Create Date: 2026-11-22 10:00:00

"""
from collections.abc import Sequence

import sqlalchemy as sa
from alembic import op
from sqlalchemy.dialects import postgresql

revision: str = "d9a3b6c18e47"
down_revision: str | Sequence[str] | None = "c4e81a2d6f39"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None

JSON = sa.JSON().with_variant(postgresql.JSONB(astext_type=sa.Text()), "postgresql")
TS = sa.DateTime(timezone=True)


def upgrade() -> None:
    op.create_table(
        "zones",
        sa.Column("zone_id", sa.String(length=24), nullable=False),
        sa.Column("name", sa.String(length=64), nullable=False),
        sa.Column("grow_mode", sa.String(length=16), nullable=True),
        sa.Column("light_cycle", JSON, nullable=True),
        sa.Column("created_at", TS, server_default=sa.text("now()"), nullable=False),
        sa.Column("updated_at", TS, server_default=sa.text("now()"), nullable=False),
        sa.PrimaryKeyConstraint("zone_id", name=op.f("pk_zones")),
    )
    op.create_table(
        "node_tokens",
        sa.Column("id", sa.BigInteger().with_variant(sa.Integer(), "sqlite"), autoincrement=True,
                  nullable=False),
        sa.Column("node_id", sa.String(length=32), nullable=False),
        sa.Column("token_hash", sa.String(length=64), nullable=False),
        sa.Column("source", sa.String(length=8), nullable=False),
        sa.Column("created_by", sa.String(length=64), nullable=False),
        sa.Column("created_at", TS, server_default=sa.text("now()"), nullable=False),
        sa.Column("revoked_at", TS, nullable=True),
        sa.PrimaryKeyConstraint("id", name=op.f("pk_node_tokens")),
        sa.UniqueConstraint("token_hash", name=op.f("uq_node_tokens_token_hash")),
    )
    op.create_index(op.f("ix_node_tokens_node_id"), "node_tokens", ["node_id"], unique=False)

    op.add_column("nodes", sa.Column("registered_at", TS, nullable=True))
    # Zonas que ya informaban los nodos (B2 las adoptaba sin tabla): se crean para no perderlas
    op.execute("UPDATE nodes SET zone_id = left(zone_id, 24) WHERE zone_id IS NOT NULL")
    op.execute(
        "INSERT INTO zones (zone_id, name) SELECT DISTINCT zone_id, zone_id FROM nodes "
        "WHERE zone_id IS NOT NULL"
    )
    op.alter_column("nodes", "zone_id", type_=sa.String(length=24),
                    existing_type=sa.String(length=32))
    op.create_foreign_key(op.f("fk_nodes_zone_id_zones"), "nodes", "zones", ["zone_id"],
                          ["zone_id"], ondelete="RESTRICT")


def downgrade() -> None:
    op.drop_constraint(op.f("fk_nodes_zone_id_zones"), "nodes", type_="foreignkey")
    op.alter_column("nodes", "zone_id", type_=sa.String(length=32),
                    existing_type=sa.String(length=24))
    op.drop_column("nodes", "registered_at")
    op.drop_index(op.f("ix_node_tokens_node_id"), table_name="node_tokens")
    op.drop_table("node_tokens")
    op.drop_table("zones")
