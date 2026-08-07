import pytest

from pyrubik.table import DistanceTable


@pytest.fixture(scope="session")
def table() -> DistanceTable:
    """The full distance table, built once for the whole session."""
    return DistanceTable.build()
