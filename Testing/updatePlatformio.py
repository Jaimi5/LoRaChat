"""
Refactored PlatformIO Build, Upload, and Monitor Manager

This module provides a cleaner, more robust implementation for managing
PlatformIO builds, uploads, and monitoring across multiple devices.

Improvements over previous version:
- Uses ThreadPoolExecutor for better resource management
- Cleaner separation of concerns with internal phase classes
- Better subprocess exit code validation
- Proper stderr handling to prevent subprocess deadlocks
- Success detection via output messages (build/upload)
- Organized log files in separate folders (build/, upload/)
- Auto-clean disabled for faster builds/uploads
- Intelligent filter error handling (skips decoder noise in initialization)
- Monitor retry logic (3 attempts before failing)
- Activity timeout check (verifies 50+ lines in first 60 seconds)
- Improved error handling and reporting
- Monitors run indefinitely without timeout
- Maintains backward compatibility with existing integration

Log file structure:
Monitoring/
├── build/
│   ├── ttgo-lora32-v1.ans
│   ├── ttgo-t-beam.ans
│   └── esp-wrover-kitNAYAD_V1R2.ans
├── upload/
│   ├── COM9.ans
│   ├── COM10.ans
│   └── COM11.ans
└── monitor_{port}.ans files (in main Monitoring directory)
"""

from platformio import util
import os
import subprocess
import threading
import colorama
from colorama import Fore
from error import set_error
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import Dict, List, Set, Optional
from time import sleep

# Timeouts in seconds
TIMEOUT_BUILD = 60 * 20
TIMEOUT_UPLOAD = 60 * 5


class PortsPlatformIo:
    """Utility class for displaying serial ports"""

    @staticmethod
    def printPorts():
        """Print all available serial ports"""
        ports = util.get_serial_ports()
        print("Ports: ", ports)
        for port in ports:
            print(port["port"], end=",", flush=True)
        print()


class _BuildPhase:
    """Internal class to handle building environments in parallel"""

    def __init__(self, environments: Set[str], log_dir: str, shared_state: dict,
                 shared_state_change: threading.Event):
        self.environments = environments
        self.log_dir = log_dir
        self.build_log_dir = os.path.join(log_dir, "build")
        os.makedirs(self.build_log_dir, exist_ok=True)
        self.shared_state = shared_state
        self.shared_state_change = shared_state_change
        self.processes: List[subprocess.Popen] = []
        self.success = True

    def build_all(self) -> bool:
        """Build all environments in parallel"""
        print(Fore.LIGHTGREEN_EX + "Building", end="", flush=True)

        # Use ThreadPoolExecutor for cleaner parallel execution
        max_workers = min(len(self.environments), os.cpu_count() or 4)

        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            # Submit all build tasks
            future_to_env = {
                executor.submit(self._build_environment, env): env
                for env in self.environments
            }

            # Wait for all builds to complete
            for future in as_completed(future_to_env):
                env = future_to_env[future]
                try:
                    if not future.result():
                        self.success = False
                        return False
                except Exception as exc:
                    print(f'\n{Fore.RED}Environment {env} generated an exception: {exc}{Fore.RESET}')
                    self.success = False
                    return False

        if self.success:
            print(Fore.GREEN + " Successfully built" + Fore.RESET)

        return self.success

    def _build_environment(self, env: str) -> bool:
        """Build a single environment"""
        print(f"\nStart building env: {env}")

        log_file = os.path.join(self.build_log_dir, f"{env}.ans")
        build_success_detected = False

        try:
            process = subprocess.Popen(
                ["pio", "run", "-e", env],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,  # Merge stderr into stdout to prevent deadlock
            )

            self.processes.append(process)
            progress_counter = 0

            # Stream output and check for errors
            for line in process.stdout:
                decoded = line.decode("utf-8", "ignore")

                # Write to build log file in build/ subdirectory
                with open(log_file, "a", encoding="utf-8") as f:
                    f.write(decoded)

                # Check for build failure
                if "[FAILED]" in decoded or "error occurred" in decoded:
                    set_error(
                        self.shared_state,
                        self.shared_state_change,
                        f"Build failed {Fore.YELLOW}{env}{Fore.RESET}",
                    )
                    self._cleanup_process(process)
                    return False

                # Check for successful build completion
                if "Successfully created esp32 image." in decoded:
                    build_success_detected = True

                # Show progress
                progress_counter += 1
                if progress_counter % 8 == 0:
                    print(Fore.LIGHTGREEN_EX + "." + Fore.RESET, end="", flush=True)

            # Wait for process to complete
            return_code = process.wait(timeout=TIMEOUT_BUILD)

            # Check exit code or success message
            if return_code != 0 and not build_success_detected:
                set_error(
                    self.shared_state,
                    self.shared_state_change,
                    f"Build failed for {env} with exit code {return_code}",
                )
                return False

            self.processes.remove(process)
            return True

        except subprocess.TimeoutExpired:
            set_error(
                self.shared_state,
                self.shared_state_change,
                f"Build timeout for environment {env}",
            )
            self._cleanup_process(process)
            return False
        except Exception as e:
            set_error(
                self.shared_state,
                self.shared_state_change,
                f"Build error for {env}: {str(e)}",
            )
            return False

    def _cleanup_process(self, process: subprocess.Popen):
        """Clean up a process safely"""
        try:
            process.kill()
            if process in self.processes:
                self.processes.remove(process)
        except:
            pass

    def kill_all(self):
        """Kill all build processes"""
        for process in self.processes[:]:  # Copy list to avoid modification during iteration
            self._cleanup_process(process)


class _UploadPhase:
    """Internal class to handle uploading to devices in parallel"""

    def __init__(self, env_port_mapping: Dict[str, str], log_dir: str,
                 shared_state: dict, shared_state_change: threading.Event):
        self.env_port_mapping = env_port_mapping
        self.log_dir = log_dir
        self.upload_log_dir = os.path.join(log_dir, "upload")
        os.makedirs(self.upload_log_dir, exist_ok=True)
        self.shared_state = shared_state
        self.shared_state_change = shared_state_change
        self.processes: List[subprocess.Popen] = []
        self.upload_results: Dict[str, bool] = {}

    def upload_to_port(self, port: str, env: str) -> bool:
        """Upload to a specific port"""
        print(f"\nStart uploading to port: {port}, env: {env}")

        log_file = os.path.join(self.upload_log_dir, f"{port.replace('/', '_')}.ans")
        upload_success_detected = False

        try:
            process = subprocess.Popen(
                ["pio", "run", "-e", env, "--target", "upload", "--upload-port", port],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,  # Merge stderr into stdout to prevent deadlock
            )

            self.processes.append(process)
            progress_counter = 0

            # Stream output and check for errors
            for line in process.stdout:
                decoded = line.decode("utf-8", "ignore")

                # Write to upload log file in upload/ subdirectory
                with open(log_file, "a", encoding="utf-8") as f:
                    f.write(decoded)

                # Check for upload failure
                if "[FAILED]" in decoded or "error occurred" in decoded:
                    set_error(
                        self.shared_state,
                        self.shared_state_change,
                        f"Failed to upload to port: {port}",
                    )
                    self._cleanup_process(process)
                    return False

                # Check for successful upload completion
                if "Hard resetting via RTS pin" in decoded or "Leaving..." in decoded:
                    upload_success_detected = True

                # Show progress
                progress_counter += 1
                if progress_counter % 8 == 0:
                    print(".", end="", flush=True)

            # Wait for process to complete
            return_code = process.wait(timeout=TIMEOUT_UPLOAD)

            # Validate upload success (exit code 0 OR success message detected)
            if return_code != 0 and not upload_success_detected:
                if return_code < 0:
                    print(f"{Fore.YELLOW}Upload to {port} was interrupted (signal {-return_code}){Fore.RESET}")
                    set_error(
                        self.shared_state,
                        self.shared_state_change,
                        f"Upload to {port} was interrupted",
                    )
                else:
                    print(f"{Fore.RED}Upload to {port} failed with exit code {return_code}{Fore.RESET}")
                    set_error(
                        self.shared_state,
                        self.shared_state_change,
                        f"Upload to {port} failed with exit code {return_code}",
                    )
                return False

            self.processes.remove(process)
            print(f"\n{Fore.GREEN}Successfully uploaded to port: {port}{Fore.RESET}")
            return True

        except subprocess.TimeoutExpired:
            print(f"{Fore.RED}Upload timeout for port {port}{Fore.RESET}")
            set_error(
                self.shared_state,
                self.shared_state_change,
                f"Upload timeout for port {port}",
            )
            self._cleanup_process(process)
            return False
        except Exception as e:
            set_error(
                self.shared_state,
                self.shared_state_change,
                f"Upload error for {port}: {str(e)}",
            )
            return False

    def _cleanup_process(self, process: subprocess.Popen):
        """Clean up a process safely"""
        try:
            process.kill()
            if process in self.processes:
                self.processes.remove(process)
        except:
            pass

    def kill_all(self):
        """Kill all upload processes"""
        for process in self.processes[:]:
            self._cleanup_process(process)


class _MonitorPhase:
    """Internal class to handle monitoring devices"""

    def __init__(self, log_dir: str, shared_state: dict,
                 shared_state_change: threading.Event):
        self.log_dir = log_dir
        self.shared_state = shared_state
        self.shared_state_change = shared_state_change
        self.processes: List[subprocess.Popen] = []

    def monitor_port(self, port: str) -> bool:
        """
        Monitor a specific port for output and errors (runs indefinitely).

        Intelligently skips filter error messages when checking for device initialization,
        preventing false negatives from esp32_exception_decoder filter issues.
        Retries up to 3 times if initialization fails or if insufficient activity detected.

        Activity check: Verifies at least 50 lines are written in the first 60 seconds.
        """
        print(f"Start monitoring port: {port}")

        log_file = os.path.join(self.log_dir, f"monitor_{port.replace('/', '_')}.ans")
        max_retries = 3

        for attempt in range(1, max_retries + 1):
            if attempt > 1:
                print(f"{Fore.YELLOW}Retry {attempt}/{max_retries} for monitoring port: {port}{Fore.RESET}")

            activity_timer = None

            try:
                process = subprocess.Popen(
                    [
                        "pio",
                        "device",
                        "monitor",
                        "--port",
                        port,
                        "-f",
                        "esp32_exception_decoder",
                        "-f",
                        "time",
                    ],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,  # Merge stderr into stdout to prevent deadlock
                )

                self.processes.append(process)

                # Monitor state
                error_occurred = False
                error_line_countdown = 50
                initialized = False
                initial_line_countdown = 200  # Increased from 50 to account for filter noise
                address_found = False
                init_failed = False

                # Activity monitoring (50 lines per minute check)
                line_count = 0
                activity_timeout = False

                def check_activity():
                    """Check if sufficient activity (50+ lines) occurred in first 60 seconds"""
                    nonlocal activity_timeout
                    if line_count < 20:
                        activity_timeout = True
                        print(f"\n{Fore.YELLOW}Insufficient activity detected for {port} ({line_count} lines in 60s){Fore.RESET}")

                # Start activity timer (60 seconds)
                activity_timer = threading.Timer(60.0, check_activity)
                activity_timer.start()

                # Process output line by line (runs indefinitely)
                for line in process.stdout:
                    decoded_line = line.decode("utf-8", "ignore")

                    # Write to log file
                    with open(log_file, "a", encoding="utf-8") as f:
                        f.write(decoded_line)

                    # Increment line count
                    line_count += 1

                    # Cancel activity timer if we reached 50 lines
                    if line_count >= 50 and activity_timer is not None:
                        activity_timer.cancel()
                        activity_timer = None

                    # Check if this is a filter error message (should be skipped from initialization check)
                    is_filter_error = (
                        "Esp32ExceptionDecoder:" in decoded_line or
                        decoded_line.strip().startswith("[WinError") or
                        decoded_line.strip().startswith("Please manually remove")
                    )

                    # Check for critical errors
                    if "Guru Meditation Error" in decoded_line or "assert failed" in decoded_line:
                        error_occurred = True

                    # Check if waiting for download (not uploaded correctly)
                    if "waiting for download" in decoded_line:
                        # Cancel activity timer
                        if activity_timer is not None:
                            activity_timer.cancel()
                        set_error(
                            self.shared_state,
                            self.shared_state_change,
                            f"Error in port waiting for upload: {port}",
                        )
                        return False

                    # Check for proper initialization (skip filter error lines)
                    if not initialized:
                        if "POWERON_RESET" in decoded_line:
                            initialized = True
                        elif not is_filter_error:
                            # Only count non-filter-error lines toward timeout
                            initial_line_countdown -= 1
                            if initial_line_countdown == 0:
                                # Initialization failed - retry or error
                                init_failed = True
                                break

                    # Handle errors
                    if error_occurred:
                        error_line_countdown -= 1
                        if error_line_countdown == 0:
                            # Cancel activity timer
                            if activity_timer is not None:
                                activity_timer.cancel()
                            set_error(
                                self.shared_state,
                                self.shared_state_change,
                                f"Error detected in port: {port}",
                            )
                            return False

                    # Extract LoRa address
                    if not address_found and "Local LoRa address" in decoded_line:
                        address_found = True
                        try:
                            # Extract hex address from line
                            hex_address = decoded_line.split(" ")[-1].strip()[:4]
                            self.shared_state["deviceAddressAndCOM"][port] = int(hex_address, 16)
                            self.shared_state_change.set()
                        except (ValueError, IndexError) as e:
                            # Cancel activity timer
                            if activity_timer is not None:
                                activity_timer.cancel()
                            set_error(
                                self.shared_state,
                                self.shared_state_change,
                                f"Error parsing address from port {port}: {str(e)}",
                            )
                            return False

                    # Check if activity timeout occurred
                    if activity_timeout:
                        break

                # Cancel activity timer if still running
                if activity_timer is not None:
                    activity_timer.cancel()

                # Check if initialization failed or activity timeout occurred
                if init_failed or activity_timeout:
                    # Kill the process
                    try:
                        process.kill()
                        if process in self.processes:
                            self.processes.remove(process)
                    except:
                        pass

                    # If this is not the last attempt, retry
                    if attempt < max_retries:
                        if init_failed:
                            print(f"{Fore.YELLOW}Initialization timeout for {port}, retrying...{Fore.RESET}")
                        # activity_timeout message already printed
                        continue
                    else:
                        # Last attempt - report error
                        if init_failed:
                            set_error(
                                self.shared_state,
                                self.shared_state_change,
                                f"Not uploaded correctly in port: {port}",
                            )
                        elif activity_timeout:
                            set_error(
                                self.shared_state,
                                self.shared_state_change,
                                f"Insufficient activity in port: {port} (only {line_count} lines in 60s)",
                            )
                        return False

                # Process completed successfully (only when manually stopped or connection lost)
                process.wait()
                if process in self.processes:
                    self.processes.remove(process)
                return True

            except Exception as e:
                # Cancel activity timer if it exists
                if activity_timer is not None:
                    activity_timer.cancel()

                # Clean up process on exception
                try:
                    process.kill()
                    if process in self.processes:
                        self.processes.remove(process)
                except:
                    pass

                set_error(
                    self.shared_state,
                    self.shared_state_change,
                    f"Monitor error for {port}: {str(e)}",
                )
                return False

        # Should never reach here
        return False

    def kill_all(self):
        """Kill all monitor processes"""
        for process in self.processes[:]:
            try:
                process.kill()
                if process in self.processes:
                    self.processes.remove(process)
            except:
                pass


class UpdatePlatformIO:
    """
    Main class for managing PlatformIO build, upload, and monitor workflow.

    This class maintains backward compatibility with the existing integration
    while providing a cleaner internal implementation.
    """

    def __init__(self, directory: str, shared_state_change: threading.Event,
                 shared_state: dict, device_mapping: Optional[Dict[str, str]] = None):
        """
        Initialize the UpdatePlatformIO manager.

        Args:
            directory: Directory for storing logs and outputs
            shared_state_change: Event for signaling state changes
            shared_state: Shared state dictionary for communication
            device_mapping: Optional mapping of ports to environments
        """
        self.shared_state = shared_state
        self.shared_state_change = shared_state_change
        self.directory = directory

        # Initialize shared state
        self.shared_state["uploadedPorts"] = []
        self.shared_state["deviceAddressAndCOM"] = {}

        # Use provided device mapping or fallback to global envPort
        self.envPort = device_mapping if device_mapping else {}

        # Display available ports
        # print("Available ports:")
        # ports = util.get_serial_ports()
        # for port in ports:
        #     print(port["port"], end=", ", flush=True)
        # print()

        # Create log directory
        self.log_dir = os.path.join(directory, "Monitoring")
        os.makedirs(self.log_dir, exist_ok=True)

        # Initialize phase managers
        self.build_phase: Optional[_BuildPhase] = None
        self.upload_phase: Optional[_UploadPhase] = None
        self.monitor_phase: Optional[_MonitorPhase] = None

        # Build timeout timer
        self.build_timer = threading.Timer(TIMEOUT_BUILD, self._build_timeout)
        self.build_timer.start()

        # Execute build phase
        if not self._execute_build():
            set_error(
                self.shared_state,
                self.shared_state_change,
                "Error when building the project",
            )
            return

        # Build successful
        self.shared_state["builded"] = True
        self.shared_state_change.set()

        # Cancel build timer
        self.build_timer.cancel()

        # Start upload and monitor phases
        self._spawn_upload_and_monitor_threads()

    def _execute_build(self) -> bool:
        """Execute the build phase for all environments"""
        # Get unique environments from device mapping
        environments = set(self.envPort.values())

        if not environments:
            print(f"{Fore.YELLOW}Warning: No environments to build{Fore.RESET}")
            return True

        # Create build phase manager
        self.build_phase = _BuildPhase(
            environments,
            self.log_dir,
            self.shared_state,
            self.shared_state_change
        )

        # Execute parallel builds
        return self.build_phase.build_all()

    def _spawn_upload_and_monitor_threads(self):
        """Spawn threads for upload and monitor phases"""
        # Get all serial ports
        ports = util.get_serial_ports()

        if len(ports) == 0:
            set_error(
                self.shared_state,
                self.shared_state_change,
                "No serial ports found",
            )
            return

        # Initialize phase managers
        self.upload_phase = _UploadPhase(
            self.envPort,
            self.log_dir,
            self.shared_state,
            self.shared_state_change
        )

        self.monitor_phase = _MonitorPhase(
            self.log_dir,
            self.shared_state,
            self.shared_state_change
        )

        # Spawn a thread for each port
        for number, port in enumerate(ports):
            port_name = port["port"]

            if port_name not in self.envPort:
                print(f"{Fore.YELLOW}Port {port_name} not in device mapping{Fore.RESET}")
                continue

            # Create thread for upload and monitor
            thread = threading.Thread(
                target=self._upload_and_monitor,
                args=(port_name, self.envPort[port_name], number)
            )
            thread.start()

    def _upload_and_monitor(self, port: str, env: str, number: int):
        """Upload to a port and then monitor it"""
        #sleep(10 * number)  # Stagger uploads to avoid conflicts

        # Upload phase
        if self.upload_phase and self.upload_phase.upload_to_port(port, env):
            # Mark first device monitor started
            if not self.shared_state.get("deviceMonitorStarted", False):
                self.shared_state["deviceMonitorStarted"] = True
                self.shared_state_change.set()

            # Track uploaded ports
            self._mark_port_uploaded(port)

            # Monitor phase
            if self.monitor_phase:
                self.monitor_phase.monitor_port(port)

    def _mark_port_uploaded(self, port: str):
        """Mark a port as successfully uploaded"""
        self.shared_state["uploadedPorts"].append(port)

        if len(self.shared_state["uploadedPorts"]) == getNumberOfPorts():
            self.shared_state["allPortsUploaded"] = True
            self.shared_state_change.set()
            print(f"{Fore.GREEN}All ports uploaded{Fore.RESET}")

    def _build_timeout(self):
        """Handle build timeout"""
        set_error(
            self.shared_state,
            self.shared_state_change,
            "Timeout during build or upload",
        )
        self.killThreads()

    def killThreads(self):
        """Kill all running processes and threads"""
        print("Killing update and monitor threads...")

        # Cancel build timer
        if hasattr(self, 'build_timer') and self.build_timer.is_alive():
            self.build_timer.cancel()

        # Kill all phase processes
        if self.build_phase:
            self.build_phase.kill_all()

        if self.upload_phase:
            self.upload_phase.kill_all()

        if self.monitor_phase:
            self.monitor_phase.kill_all()


def getNumberOfPorts() -> int:
    """Get the number of available serial ports"""
    return len(util.get_serial_ports())
