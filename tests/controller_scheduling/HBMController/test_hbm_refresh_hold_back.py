"""A pending refresh holds back only the reads and writes it covers (JESD238 6.3.2.6: a per-bank
refresh leaves accesses to other banks unaffected), on the HBM12 and HBM34 controllers."""

import pytest

import ramulator
import tests.controller_scheduling.harness as cs

pytestmark = pytest.mark.controller_scheduling


def _hbm12():
    dram = ramulator.dram.HBM2(org_preset="HBM2_4Gb", timing_preset="HBM2_2000Mbps")
    return cs.ControllerUnderTest.make_hbm12(dram)


def _hbm34():
    dram = ramulator.dram.HBM3(org_preset="HBM3_16Gb_4hi", timing_preset="HBM3_6400Mbps")
    return cs.ControllerUnderTest.make_hbm34(dram)


make_dut = pytest.mark.parametrize("make_dut", [_hbm12, _hbm34], ids=["HBM12", "HBM34"])


def open_row(dut, addr):
    """Read `addr` until its RD is issued, so its bank holds an open row."""
    dut.send_request("Read", addr)
    for _ in range(256):
        if any(c.command == "RD" for c in dut.tick()):
            return
    raise AssertionError("the read was never issued")


def first(dut, history, command, **levels):
    """The index in `history` of the first `command` whose address has these level values."""
    at = {dut.level_names.index(name): v for name, v in levels.items()}
    for i, item in enumerate(history):
        if item.command == command and all(item.addr_vec[lv] == v for lv, v in at.items()):
            return i
    raise AssertionError(f"no {command} with {levels} in {[c.command for c in history]}")


@make_dut
def test_per_bank_refresh_leaves_other_banks_unaffected(make_dut):
    dut = make_dut()
    open_row(dut, dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=0, Row=0, Column=0))
    dut.priority_send(
        "REFpb", dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=0, Row=dut.ALL, Column=dut.ALL)
    )
    dut.send_request("Read", dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=1, Row=0, Column=0))
    history = dut.run_until_idle(max_ticks=1024)

    assert first(dut, history, "ACT", PseudoChannel=0, Bank=1) < first(dut, history, "REFpb")


@make_dut
def test_all_bank_refresh_holds_back_its_pseudo_channel_only(make_dut):
    dut = make_dut()
    open_row(dut, dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=0, Row=0, Column=0))
    dut.priority_send(
        "REFab",
        dut.addr_vec(
            PseudoChannel=0,
            Sid=dut.ALL,
            BankGroup=dut.ALL,
            Bank=dut.ALL,
            Row=dut.ALL,
            Column=dut.ALL,
        ),
    )
    dut.send_request("Read", dut.addr_vec(PseudoChannel=0, BankGroup=1, Bank=0, Row=0, Column=0))
    dut.send_request("Read", dut.addr_vec(PseudoChannel=1, BankGroup=1, Bank=0, Row=0, Column=0))
    history = dut.run_until_idle(max_ticks=4096)

    refab = first(dut, history, "REFab")
    assert first(dut, history, "ACT", PseudoChannel=1) < refab
    assert first(dut, history, "ACT", PseudoChannel=0) > refab
