"""A registry of jobs that all succeed, for full-run tests."""

from tests.fake_jobs import OK, SEEN

ALL = (*OK, SEEN)
