"""A pending refresh holds back only the reads and writes it covers (JESD238 6.3.2.6: a per-bank refresh leaves
accesses to other banks unaffected)."""
import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def make_dut():
    dram = ramulator.dram.HBM3(org_preset="HBM3_8Gb", timing_preset="HBM3_6400Mbps")
    controller = ramulator.controller.HBM(
        scheduler=ramulator.scheduler.FRFCFS(),
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
        dram=dram,
        controller_plugins=[],
    )
    return cs.ControllerUnderTest(controller)


def open_row(dut, addr):
    """Read `addr` until its RD is issued, so its bank holds an open row."""
    dut.send_request("Read", addr)
    for _ in range(256):
        if any(c.command == "RD" for c in dut.tick()):
            return
    raise AssertionError("the read was never issued")


def first(dut, history, command, **levels):
    """The index in `history` of the first `command` whose address has the given level values."""
    at = {dut.level_names.index(name): v for name, v in levels.items()}
    for i, item in enumerate(history):
        if item.command == command and all(item.addr_vec[l] == v for l, v in at.items()):
            return i
    raise AssertionError(f"no {command} with {levels} in {[c.command for c in history]}")


def test_per_bank_refresh_leaves_other_banks_unaffected():
    dut = make_dut()
    open_row(dut, dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=0, Row=0, Column=0))
    dut.priority_send("REFpb", dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=0, Row=dut.ALL, Column=dut.ALL))
    dut.send_request("Read", dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=1, Row=0, Column=0))
    history = dut.run_until_idle(max_ticks=1024)

    assert first(dut, history, "ACT", PseudoChannel=0, Bank=1) < first(dut, history, "REFpb")


def test_all_bank_refresh_holds_back_its_pseudo_channel_only():
    dut = make_dut()
    open_row(dut, dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=0, Row=0, Column=0))
    dut.priority_send("REFab", dut.addr_vec(PseudoChannel=0, BankGroup=dut.ALL, Bank=dut.ALL, Row=dut.ALL,
                                            Column=dut.ALL))
    dut.send_request("Read", dut.addr_vec(PseudoChannel=0, BankGroup=1, Bank=0, Row=0, Column=0))
    dut.send_request("Read", dut.addr_vec(PseudoChannel=1, BankGroup=1, Bank=0, Row=0, Column=0))
    history = dut.run_until_idle(max_ticks=4096)

    refab = first(dut, history, "REFab")
    assert first(dut, history, "ACT", PseudoChannel=1) < refab
    assert first(dut, history, "ACT", PseudoChannel=0) > refab
