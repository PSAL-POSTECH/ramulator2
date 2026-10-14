"""A pending refresh holds back the reads and writes it covers, and a per-bank one also the row openings beside it:
ACT-to-REFpb would let them starve it. Column accesses to other banks go on (JESD238 6.3.2.6), and two per-bank
refreshes are tRREFD apart."""
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


def test_per_bank_refreshes_are_trrefd_apart():
    dut = make_dut()
    dut.priority_send("REFpb", dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=0, Row=dut.ALL, Column=dut.ALL))
    dut.priority_send("REFpb", dut.addr_vec(PseudoChannel=0, BankGroup=1, Bank=0, Row=dut.ALL, Column=dut.ALL))
    history = dut.run_until_idle(max_ticks=256)

    dut.assert_commands(["REFpb", "REFpb"], history=history)
    assert history[1].clk - history[0].clk >= dut.timings["nRREFD"]


class RowMisses:
    """Reads of pseudo channel 0 that visit `banks` in turn, each to a row its bank has not opened yet."""

    def __init__(self, dut, banks):
        self.dut, self.banks, self.n = dut, banks, 0

    def fill(self):
        """Send reads until the read buffer refuses one; the refused read is sent again next time."""
        while True:
            bg, b = self.banks[self.n % len(self.banks)]
            try:
                self.dut.send_request("Read", self.dut.addr_vec(PseudoChannel=0, BankGroup=bg, Bank=b,
                                                                Row=self.n + 1, Column=0))
            except RuntimeError:
                return
            self.n += 1


def tick_until(dut, command, feed, max_ticks):
    """Tick with `feed` before each tick until `command` issues; return its clk."""
    for _ in range(max_ticks):
        feed()
        for c in dut.tick():
            if c.command == command:
                return c.clk
    raise AssertionError(f"no {command} within {max_ticks} ticks")


def test_activates_to_other_banks_do_not_starve_a_per_bank_refresh():
    """ACT->REFpb (tRRDS) outlasts ACT->ACT on HBM3, so a picker that keeps opening rows elsewhere in the pseudo
    channel pushes a pending REFpb back with every ACT; breaks if row-opening reads are not held while it waits."""
    dut = make_dut()
    open_row(dut, dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=0, Row=0, Column=0))
    others = [(bg, b) for bg in range(4) for b in range(4) if (bg, b) != (0, 0)]
    feed = RowMisses(dut, others).fill
    for _ in range(512):
        feed()
        dut.tick()

    queued = dut.history[-1].clk
    dut.priority_send("REFpb", dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=0, Row=dut.ALL, Column=dut.ALL))
    issued = tick_until(dut, "REFpb", feed, max_ticks=16384)

    assert issued - queued <= dut.timings["nRRDS"] + dut.timings["nRP"]


def test_per_bank_refresh_holds_back_only_row_openings_beside_it():
    """Reads to an open row of another bank go on while a REFpb waits; reads that open a row wait in its pseudo
    channel only. Breaks if a REFpb holds back the whole channel, or lets ACTs beside it through."""
    dut = make_dut()
    open_row(dut, dut.addr_vec(PseudoChannel=0, BankGroup=1, Bank=0, Row=0, Column=0))
    open_row(dut, dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=0, Row=0, Column=0))
    dut.priority_send("REFpb", dut.addr_vec(PseudoChannel=0, BankGroup=0, Bank=0, Row=dut.ALL, Column=dut.ALL))
    for col in range(1, 5):
        dut.send_request("Read", dut.addr_vec(PseudoChannel=0, BankGroup=1, Bank=0, Row=0, Column=col))
    dut.send_request("Read", dut.addr_vec(PseudoChannel=0, BankGroup=2, Bank=0, Row=0, Column=0))
    dut.send_request("Read", dut.addr_vec(PseudoChannel=1, BankGroup=2, Bank=0, Row=0, Column=0))
    history = dut.run_until_idle(max_ticks=1024)

    refpb = first(dut, history, "REFpb")
    assert first(dut, history, "RD", PseudoChannel=0, BankGroup=1) < refpb
    assert first(dut, history, "ACT", PseudoChannel=1) < refpb
    assert first(dut, history, "ACT", PseudoChannel=0, BankGroup=2) > refpb
