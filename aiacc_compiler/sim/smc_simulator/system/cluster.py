import logging
from typing import Dict, List, Any, Optional
from dataclasses import dataclass, field

from smc_simulator.core.kernel import ParallelKernel
from aiacc_hardware.src.spec_loader import SpecLoader

@dataclass
class TransferRequest:
    src_cluster_id: int
    src_bank: int
    src_addr: int
    dst_cluster_id: int
    dst_bank: int
    dst_addr: int
    data: Optional[int] = None
    status: str = "PENDING" # PENDING, TRANSFERRING, COMPLETED, FAILED

class BusArbiter:
    """
    Simple Bus Arbiter for inter-cluster communication.
    
    Current Model:
    - Shared Bus (Single channel)
    - Throughput: 1 transfer per cycle
    - Latency: configurable (default 10 cycles)
    """
    def __init__(self, latency: int = 10):
        self.latency = latency
        self.queue: List[TransferRequest] = []
        self.active_transfers: List[Dict[str, Any]] = [] # {'req': req, 'cycles_left': n}
        self.logger = logging.getLogger("BusArbiter")

    def request_transfer(self, req: TransferRequest):
        self.queue.append(req)
        self.logger.info(f"[Bus] Request received: Cluster {req.src_cluster_id} -> Cluster {req.dst_cluster_id}")

    def tick(self) -> List[TransferRequest]:
        completed = []
        
        # 1. Process active transfers
        remaining_transfers = []
        for transfer in self.active_transfers:
            transfer['cycles_left'] -= 1
            if transfer['cycles_left'] <= 0:
                req = transfer['req']
                req.status = "COMPLETED"
                completed.append(req)
                self.logger.info(f"[Bus] Transfer completed: Cluster {req.src_cluster_id} -> Cluster {req.dst_cluster_id}")
            else:
                remaining_transfers.append(transfer)
        self.active_transfers = remaining_transfers

        # 2. Schedule new transfers if bus is free (assuming single channel for now)
        # For simplicity, allow infinite parallelism in this prototype unless constrained
        # Let's say max 1 new transfer per cycle initiates
        if self.queue:
            req = self.queue.pop(0)
            req.status = "TRANSFERRING"
            self.active_transfers.append({'req': req, 'cycles_left': self.latency})
            self.logger.info(f"[Bus] Started transfer: Cluster {req.src_cluster_id} -> Cluster {req.dst_cluster_id}")
            
        return completed

class SystemCluster:
    """
    Multi-Kernel System Cluster.
    Contains multiple ParallelKernel instances and a BusArbiter.
    """
    def __init__(self, num_kernels: int = 2):
        self.loader = SpecLoader()
        self.kernels: Dict[int, ParallelKernel] = {}
        for i in range(num_kernels):
            k = ParallelKernel(self.loader)
            k.logger = logging.getLogger(f"Kernel-{i}")
            self.kernels[i] = k
            
        self.bus = BusArbiter()
        self.cycle_count = 0
        self.logger = logging.getLogger("SystemCluster")

    def step(self):
        # 1. Step all kernels
        for k_id, kernel in self.kernels.items():
            # Kernels run their own internal logic
            # In a real system, kernels might block on IO. 
            # Here we just step them. 
            # Note: We need a mechanism for Kernel to initiate transfer.
            # For this prototype, we'll handle transfers externally or via special IO instruction emulation.
            try:
                kernel.step()
            except Exception as e:
                self.logger.error(f"Kernel {k_id} error: {e}")

        # 2. Step Bus
        completed_transfers = self.bus.tick()
        
        # 3. Handle completed transfers (Write to Destination Kernel)
        for req in completed_transfers:
            target_kernel = self.kernels.get(req.dst_cluster_id)
            if target_kernel:
                # Direct memory access to target UR (Backdoor write)
                # In real HW, this would go through a write port or DMA engine
                if req.data is None:
                    # If data wasn't pre-fetched, read it now (Magic instantaneous read if not modeled)
                    src_kernel = self.kernels.get(req.src_cluster_id)
                    req.data = src_kernel.read_ur_bank(req.src_bank, req.src_addr)
                
                # Write to destination
                # We need a way to write to UR without going through the pipeline if we want to simulate DMA
                # ParallelKernel doesn't expose raw storage write easily except load_hex.
                # Let's add a helper or access storage directly.
                target_kernel.ur_bank.storage[req.dst_bank][req.dst_addr] = req.data
                self.logger.info(f"[System] DMA Write: Kernel {req.dst_cluster_id} Bank {req.dst_bank} Addr {req.dst_addr} <= {req.data:x}")

        self.cycle_count += 1

    def transfer(self, src_id: int, src_bank: int, src_addr: int, dst_id: int, dst_bank: int, dst_addr: int):
        """Initiate a transfer (External DMA controller behavior)"""
        # Read data immediately for simplicity in this model, or defer it
        src_kernel = self.kernels.get(src_id)
        data = src_kernel.read_ur_bank(src_bank, src_addr)
        
        req = TransferRequest(
            src_cluster_id=src_id, src_bank=src_bank, src_addr=src_addr,
            dst_cluster_id=dst_id, dst_bank=dst_bank, dst_addr=dst_addr,
            data=data
        )
        self.bus.request_transfer(req)

    def run(self, cycles: int):
        self.logger.info(f"System Cluster Run for {cycles} cycles")
        for _ in range(cycles):
            self.step()
