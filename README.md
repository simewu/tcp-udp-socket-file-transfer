# File Transfer Across Socket Protocols

The server responds to client file requests, for example, if the server has "SIGCOMM10-DataCenterTCP-2.pdf" then the client can request to download it. This is implemented on various socket protocols which are as follows.
- TCP
- UDP + ARQ stop-and-wait
- UDP + ARQ selective repeat

## Usage
Both the client and the server have a shell file. This automates the compilation + execution. The files are as follows.
- TCP: `./run_part1.sh`
- UDP: `./run_part2.sh`

For manual usage, for TCP, the arguments are as follows:
- `./TCP_Client serverAddress fileName`
- `./TCP_Server`

For UDP, the arguments are as follows:
- `./UDP_Client serverHostname fileName protocolType`
- `./UDP_Server loss_probability protocol_type`
