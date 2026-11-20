"""nodes y events (B2)

Revision ID: b7d2f4a91c08
Revises: a1c3e5f70b21
Create Date: 2026-11-19 23:10:00

"""
from collections.abc import Sequence

import sqlalchemy as sa
from alembic import op
from sqlalchemy.dialects import postgresql

revision: str = "b7d2f4a91c08"
down_revision: str | Sequence[str] | None = "a1c3e5f70b21"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None

JSON = sa.JSON().with_variant(postgresql.JSONB(astext_type=sa.Text()), "postgresql")
TS = sa.DateTime(timezone=True)


def upgrade() -> None:
    op.create_table(
        "nodes",
        sa.Column("node_id", sa.String(length=32), nullable=False),
        sa.Column("kind", sa.String(length=8), nullable=False),
        sa.Column("camera_type", sa.String(length=32), nullable=True),
        sa.Column("firmware", sa.String(length=32), nullable=True),
        sa.Column("zone_id", sa.String(length=32), nullable=True),
        sa.Column("node_state", sa.String(length=16), nullable=True),
        sa.Column("last_seen", TS, nullable=True),
        sa.Column("status_ts", TS, nullable=True),
        sa.Column("last_status", JSON, nullable=True),
        sa.Column("created_at", TS, server_default=sa.text("now()"), nullable=False),
        sa.PrimaryKeyConstraint("node_id", name=op.f("pk_nodes")),
    )
    op.create_index(op.f("ix_nodes_kind"), "nodes", ["kind"], unique=False)
    op.create_index(op.f("ix_nodes_zone_id"), "nodes", ["zone_id"], unique=False)

    op.create_table(
        "events",
        sa.Column("id", sa.BigInteger().with_variant(sa.Integer(), "sqlite"), autoincrement=True,
                  nullable=False),
        sa.Column("node_id", sa.String(length=32), nullable=False),
        sa.Column("event_type", sa.String(length=32), nullable=False),
        sa.Column("severity", sa.String(length=8), nullable=False),
        sa.Column("event_state", sa.String(length=16), nullable=False),
        sa.Column("started_at", TS, nullable=False),
        sa.Column("acknowledged_at", TS, nullable=True),
        sa.Column("acknowledged_by", sa.String(length=64), nullable=True),
        sa.Column("resolved_at", TS, nullable=True),
        sa.Column("capture_id", sa.String(length=32), nullable=True),
        sa.Column("detail", JSON, nullable=True),
        sa.Column("received_at", TS, server_default=sa.text("now()"), nullable=False),
        sa.PrimaryKeyConstraint("id", name=op.f("pk_events")),
        sa.UniqueConstraint("node_id", "event_type", "started_at",
                            name="uq_events_node_id_event_type_started_at"),
    )
    op.create_index(op.f("ix_events_node_id"), "events", ["node_id"], unique=False)
    op.create_index(op.f("ix_events_event_state"), "events", ["event_state"], unique=False)
    op.create_index("ix_events_started_at_id", "events", ["started_at", "id"], unique=False)


def downgrade() -> None:
    op.drop_index("ix_events_started_at_id", table_name="events")
    op.drop_index(op.f("ix_events_event_state"), table_name="events")
    op.drop_index(op.f("ix_events_node_id"), table_name="events")
    op.drop_table("events")
    op.drop_index(op.f("ix_nodes_zone_id"), table_name="nodes")
    op.drop_index(op.f("ix_nodes_kind"), table_name="nodes")
    op.drop_table("nodes")
