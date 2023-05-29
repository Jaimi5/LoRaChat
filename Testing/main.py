import os
import sys
import simulation
import simConfiguration
from time import sleep
from updatePlatformio import PortsPlatformIo


def main():
    if len(sys.argv) < 2:
        print("Please provide the name of the directory.")
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
            simConfigurations.append(
                simConfiguration.SimConfiguration(
                    os.path.join(directory, directoryName), directoryName
                )
            )

    for config in simConfigurations:
        retries = 0
        while True and retries < 3:
            directoryName = os.path.join(directory, config.getName())
            if retries > 0:
                directoryName = directoryName + str(retries)
                os.makedirs(directoryName, exist_ok=True)
                # Copy the file "simConfiguration.json" to the new directory
                config.copyConfiguration(directoryName)

            sim = simulation.Simulation(
                directoryName,
                noBuild,
                config.getName(),
            )

            if sim.error():
                print(
                    "Error in simulation " + config.getName(),
                    "Retrying number " + str(retries),
                )
                sleep(10)
                retries += 1
                continue

            break

    print("Successfully closed the program")


if __name__ == "__main__":
    main()
