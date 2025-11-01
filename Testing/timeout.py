# Create a timeout using threads.
# If the timeout occurs, set the shared_state_change event.

import threading
import time


class Timeout:
    def __init__(self, timeout, shared_state_change, shared_state, error_message="Timeout"):
        self.timeout = timeout

        self.shared_state_change = shared_state_change

        self.shared_state = shared_state

        self.error_message = error_message

        self.thread = threading.Timer(timeout * 60, self.run)

        self.thread.start()

    def run(self):
        print(f"Timeout after {self.timeout} minutes, message: {self.error_message}")
        self.shared_state["error"] = True
        self.shared_state["error_message"] = self.error_message
        self.shared_state_change.set()

    def cancel(self):
        self.thread.cancel()
