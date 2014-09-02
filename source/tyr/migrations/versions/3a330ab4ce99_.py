"""add created_at and updated_at on Jobs

Revision ID: 3a330ab4ce99
Revises: d8ce67cdcaf
Create Date: 2014-07-29 16:55:18.541664

"""

# revision identifiers, used by Alembic.
revision = '3a330ab4ce99'
down_revision = '3e6c7a8fbdb2'

from alembic import op
import sqlalchemy as sa


def upgrade():
    ### commands auto generated by Alembic - please adjust! ###
    op.add_column('job', sa.Column('created_at', sa.DateTime(), nullable=False, server_default=sa.text('CURRENT_TIMESTAMP')))
    op.add_column('job', sa.Column('updated_at', sa.DateTime(), nullable=True))
    ### end Alembic commands ###


def downgrade():
    ### commands auto generated by Alembic - please adjust! ###
    op.drop_column('job', 'updated_at')
    op.drop_column('job', 'created_at')
    ### end Alembic commands ###
