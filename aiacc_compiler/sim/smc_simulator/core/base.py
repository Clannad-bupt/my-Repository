import abc
from typing import Any, Dict, List, Optional

class Component(abc.ABC):
    """Base abstract class for all hardware components in the simulator."""
    
    def __init__(self, name: str):
        self.name = name
        self._next_state = {}
        self._current_state = {}

    @abc.abstractmethod
    def reset(self):
        """Reset component to initial state."""
        pass

    @abc.abstractmethod
    def evaluate(self, inputs: Dict[str, Any]):
        """
        Phase 1: Evaluate inputs and calculate next state.
        DO NOT update internal state here.
        """
        pass

    @abc.abstractmethod
    def tick(self):
        """
        Phase 2: Update internal state based on calculations from evaluate phase.
        """
        pass

class SMCSimulator:
    """Top-level simulator controller."""
    
    def __init__(self):
        self.cycle_count = 0
        self.components: List[Component] = []
        self.stop_requested = False
        
    def add_component(self, component: Component):
        self.components.append(component)
        
    def reset(self):
        self.cycle_count = 0
        self.stop_requested = False
        for comp in self.components:
            comp.reset()
            
    def step(self):
        """Execute one simulation cycle."""
        # Phase 1: Evaluate all components
        # In a real hardware simulation, we might need multiple delta cycles or careful ordering.
        # For this high-level sim, we assume inputs are available from previous cycle's tick.
        
        # We need a way to pass signals between components. 
        # For now, we'll let components pull from shared interconnects or registered outputs.
        # A simple approach is calling evaluate on all, then tick on all.
        
        # Note: Data dependencies between components need to be handled.
        # Usually: Read Registers -> Compute -> Write Next Registers
        
        for comp in self.components:
            # Inputs gathering would happen here or inside evaluate
            comp.evaluate({}) 
            
        # Phase 2: Update state
        for comp in self.components:
            comp.tick()
            
        self.cycle_count += 1

    def run(self, max_cycles: int = 1000):
        self.reset()
        print(f"Starting simulation for max {max_cycles} cycles...")
        while self.cycle_count < max_cycles and not self.stop_requested:
            self.step()
            # print(f"Cycle {self.cycle_count} completed.")
        print(f"Simulation ended at cycle {self.cycle_count}")
