import os

import pytest

import convert
from romconv.core.errors import ConversionError


def test_defaults_to_cpu_count_and_all_jobs():
    assert convert.parse_args([]) == (os.cpu_count() or 1, [])


@pytest.mark.parametrize("args", [["-j", "4", "galaga"], ["-j4", "galaga"], ["galaga", "-j", "4"]])
def test_job_count_forms(args):
    assert convert.parse_args(args) == (4, ["galaga"])


def test_names_keep_order():
    assert convert.parse_args(["pacman", "-j2", "galaga"]) == (2, ["pacman", "galaga"])


@pytest.mark.parametrize("args", [["-j"], ["-j", "galaga"], ["-j0"], ["-jx"], ["-j", "-3"], ["--fast"], ["-x"]])
def test_bad_options_are_conversion_errors(args):
    with pytest.raises(ConversionError, match="usage"):
        convert.parse_args(args)


def test_j_does_not_swallow_the_next_name():
    with pytest.raises(ConversionError) as err:
        convert.parse_args(["-j", "galaga"])
    assert "'galaga'" in str(err.value)


def test_main_reports_unknown_name_and_bad_usage(capsys):
    assert convert.main(["-j1", "nosuchgame"]) == 1
    assert "unknown job 'nosuchgame'" in capsys.readouterr().err
    assert convert.main(["-j"]) == 1
    assert "usage" in capsys.readouterr().err
