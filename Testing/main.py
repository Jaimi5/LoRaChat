import os
import sys
import simulation
import simConfiguration
import shutil
from time import sleep
from updatePlatformio import PortsPlatformIo

# TODO: Add in the defaultConfigValues the environments to use and their respective ports
# TODO: Add the environments to the simulation to change the configuration files
# TODO: Only build the projects that the environment is in the list of environments to use


def main():
    if len(sys.argv) < 2:
        print("Please provide the name of the directory.")
        return

    if "-h" in sys.argv:
        print("Usage: python main.py [directory_name] [-nb] [-p]")
        print("directory_name: name of the directory to create")
        print("-nb: no build, don't build the project")
        print("-p: print the ports")
        print("For more information, please read the README.md file")
        return

    # TODO: Need to check the arguments
    noBuild = "-nb" in sys.argv

    ports = "-p" in sys.argv

    if ports:
        PortsPlatformIo.printPorts()
        return

    directory = sys.argv[1]
    # Use the directory_name in your code

    print("Directory name:", directory)

    overwrite = False

    if os.path.isdir(directory):
        print("Directory already exists")
        overwrite = input("Do you want to overwrite it? (y/n)") == "y"

    if not overwrite:
        os.makedirs(directory, exist_ok=True)

        numberOfSimulations = int(input("How many simulations do you want to run?"))

        simConfigurations = []

        for i in range(numberOfSimulations):
            print("Enter the values for the configuration:")
            while True:
                name = input("Enter name of the configuration: ")
                if name == "":
                    print("Name cannot be empty")
                    continue
                break

            # Create the directory for the simulation
            simulationFile = os.path.join(directory, name)
            os.makedirs(simulationFile, exist_ok=True)

            # Create the configuration file
            config = simConfiguration.SimConfiguration(simulationFile, name)
            config.createConfiguration()

            simConfigurations.append(config)

    else:
        # Get all the directories in the directory
        directories = [
            name
            for name in os.listdir(directory)
            if os.path.isdir(os.path.join(directory, name))
        ]

        simConfigurations = []

        for directoryName in directories:
            # For each directory, delete all the files but simConfiguration.json
            for file in os.listdir(os.path.join(directory, directoryName)):
                dir_file = os.path.join(directory, directoryName, file)
                if os.path.isdir(dir_file):
                    shutil.rmtree(dir_file)
                elif file != "simConfiguration.json":
                    os.remove(dir_file)

            simConfigurations.append(
                simConfiguration.SimConfiguration(
                    os.path.join(directory, directoryName), directoryName
                )
            )

    # Maximum number of retries per simulation (configurable)
    MAX_RETRIES = 5
    RETRY_DELAY = 15  # seconds

    try:
        for config in simConfigurations:
            retries = 0
            success = False

            while retries < MAX_RETRIES and not success:
                directoryName = os.path.join(directory, config.getName())

                # Only create retry directories if we're actually retrying
                if retries > 0:
                    directoryName = directoryName + "_retry" + str(retries)
                    os.makedirs(directoryName, exist_ok=True)
                    # Copy the file "simConfiguration.json" to the new directory
                    config.copyConfiguration(directoryName)
                    print(f"Retry {retries}/{MAX_RETRIES} for simulation: {config.getName()}")

                sim = simulation.Simulation(
                    directoryName,
                    noBuild,
                    config.getName(),
                )

                if sim.error():
                    retries += 1
                    if retries < MAX_RETRIES:
                        print(
                            f"Error in simulation {config.getName()}: {sim.shared_state.get('error_message', 'Unknown error')}"
                        )
                        print(f"Waiting {RETRY_DELAY} seconds before retry {retries}/{MAX_RETRIES}...")
                        sleep(RETRY_DELAY)
                    else:
                        print(
                            f"Simulation {config.getName()} failed after {MAX_RETRIES} attempts. Skipping."
                        )
                else:
                    success = True
                    print(f"Simulation {config.getName()} completed successfully!")
                    break

    except KeyboardInterrupt:
        print("\n⚠ Testing interrupted by user (Ctrl+C)")
        print("Cleaning up and exiting...")
        sys.exit(0)

    print("Successfully closed the program")


if __name__ == "__main__":
    main()
