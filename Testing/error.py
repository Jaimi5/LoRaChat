from datetime import datetime
from colorama import Fore


def set_error(shared_state, shared_state_change, error_message):
    print(Fore.RED + error_message + Fore.RESET)
    shared_state["error"] = True
    shared_state["error_message"] = error_message
    shared_state["error_time"] = datetime.now().strftime("%d/%m/%Y %H:%M:%S")
    shared_state_change.set()
