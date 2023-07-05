import os


class Timeout:
    def __init__(self, time, number, numOfTimeouts, recalculationTimeout):
        self.time = time
        self.number = number
        self.numOfTimeouts = numOfTimeouts
        self.recalculationTimeout = recalculationTimeout


class Connection:
    def __init__(self, sender, receiver, seqId):
        self.sender = sender
        self.receiver = receiver
        self.seqId = seqId
        self.timeoutsReceived = {}
        self.timeoutsSent = {}

    def __eq__(self, __value: object) -> bool:
        return (
            self.sender == __value.sender
            and self.receiver == __value.receiver
            and self.seqId == __value.seqId
        )

    def __hash__(self) -> int:
        return hash((self.sender, self.receiver, self.seqId))

    def addTimeoutReceived(self, time, number, numberOfTimeouts, recalculationTimeout):
        self.timeoutsReceived[time] = Timeout(
            time, number, numberOfTimeouts, recalculationTimeout
        )

    def addTimeoutSent(self, time, number, numberOfTimeouts, recalculationTimeout):
        self.timeoutsSent[time] = Timeout(
            time, number, numberOfTimeouts, recalculationTimeout
        )


class Monitoring:
    def __init__(self, fileName):
        self.fileName = fileName
        self.connections = {}
        self.address = self.findAddress()
        self.findConnections()

    def findAddress(self):
        with open(self.fileName, "r") as f:
            for line in f:
                if "Local LoRa address" in line:
                    # Split the line by spaces
                    splitLine = line.split()
                    # Get the address
                    address = splitLine[-1]
                    # Get the name of the device
                    addr = address.split(":")[-1]
                    return addr

    def findConnections(self):
        with open(self.fileName, "r") as f:
            for line in f:
                if "timeout reached" in line:
                    try:
                        # Find the source address after the first occurrence of the word "Src" and remove the last character
                        src = line.split("Src:", 1)[1].split()[0][:-1]
                        # Find the sequence ID after the first occurrence of the word "Seq"
                        seqId = line.split("Seq_Id:", 1)[1].split()[0][:-1]
                        # Find the time after the first occurrence of the word "Num:"
                        num = int(line.split("Num:", 1)[1].split()[0][:-1])
                        # transform the number to int
                        # Find the time after the first occurrence of the word "N.TimeOuts"
                        numberOfTimeouts = line.split("N.TimeOuts", 1)[1].split()[0]
                    except:
                        continue

                    time = line.split()[0]

                    if "Waiting Received Queue" in line:
                        source = src
                        receiver = self.address
                    else:
                        source = self.address
                        receiver = src

                    # Create a connection object
                    connection = Connection(source, receiver, seqId)

                    # Get recalculation timeout
                    nextLine = next(f)
                    if "Timeout recalculated to" in nextLine:
                        recalculationTimeout = nextLine.split()[-2]

                    # TODO: Only for this time
                    try:
                        nextLine = next(f)
                    except:
                        nextLine = ""

                    isFirstSyncResend = False
                    if "Adding packet to Q_SP" in nextLine:
                        isFirstSyncResend = True

                    # If the connection is not in the dictionary, add it
                    if connection not in self.connections:
                        self.connections[connection] = connection

                    if "Waiting Received Queue" in line:
                        # Add a timeout to the connection
                        self.connections[connection].addTimeoutReceived(
                            time, num, numberOfTimeouts, recalculationTimeout
                        )
                    else:
                        # Add a timeout to the connection
                        self.connections[connection].addTimeoutSent(
                            time,
                            num + (not isFirstSyncResend),
                            numberOfTimeouts,
                            recalculationTimeout,
                        )


def getMonitorsStatus(folderName):
    # for each file in the folder, create a Timeouts object and add it to the list
    monitorsList = []
    for file in os.listdir(folderName):
        if "monitor" in file:
            fileName = os.path.join(folderName, file)
            monitors = Monitoring(fileName)
            monitorsList.append(monitors)
    return monitorsList


def getMonitorStatusResults(folderName):
    monitorsList = getMonitorsStatus(folderName)
    totalMessagesResend = 0
    totalSyncResend = 0
    for monitor in monitorsList:
        # print("Address: ", monitor.address)
        # print("Number of connections: ", len(monitor.connections))
        # Calculate the number of timeouts received for each connection
        timeoutsReceived = 0
        timeoutsSent = 0
        maxTimeoutsReceived = 0
        maxTimeoutsSent = 0

        numOfResendsStartSequenceSend = 0

        for connection in monitor.connections:
            timeoutsReceived += len(connection.timeoutsReceived)
            timeoutsSent += len(connection.timeoutsSent)
            if len(connection.timeoutsReceived) > maxTimeoutsReceived:
                maxTimeoutsReceived = len(connection.timeoutsReceived)
            if len(connection.timeoutsSent) > maxTimeoutsSent:
                maxTimeoutsSent = len(connection.timeoutsSent)

            for timeout in connection.timeoutsSent.values():
                if timeout.number == 0:
                    numOfResendsStartSequenceSend += 1

        totalMessagesResend += timeoutsReceived
        totalSyncResend += numOfResendsStartSequenceSend

        # if timeoutsReceived != 0:
        #     print("Timeouts Received: ", timeoutsReceived)

        # if timeoutsSent != 0:
        #     print("Timeouts Sent: ", timeoutsSent)

        # if maxTimeoutsReceived != 0:
        #     print("Max Timeouts Received: ", maxTimeoutsReceived)

        # if maxTimeoutsSent != 0:
        #     print("Max Timeouts Sent: ", maxTimeoutsSent)

        # if numOfResendsStartSequenceSend != 0:
        #     print(
        #         "Number of resends of the start sequence sent: ",
        #         numOfResendsStartSequenceSend,
        #     )

        # print()

    # print("Total number of timeouts resend: ", totalMessagesResend)
    # print("Total number of sync resend: ", totalSyncResend)

    return {
        "totalMessagesResend": totalMessagesResend,
        "totalSyncResend": totalSyncResend,
    }


if __name__ == "__main__":
    folderName = "ZLargeNumberOfPacketsNoLimitRepeatTest2"

    results = []

    # for each folder in the directory, get the number of timeouts
    for folder in os.listdir(folderName):
        path = os.path.join(folderName, folder)
        if os.path.isdir(path):
            if "plots" in folder:
                continue
            path = os.path.join(path, "Monitoring")
            print(path)
            results.append(getMonitorStatusResults(path))

    print(results)