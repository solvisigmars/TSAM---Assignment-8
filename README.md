# TSAM Assignment 8 — G_nammi

## 1. Compilation and Execution

### Compilation
make

### Running the Server
./tsamserver <port>

### Running the Client
./tsamclient 130.208.246.98 4049

## 2. Development and Testing Environment

- Development: macOS using VS Code
- Local: macos, using VS Code terminal to compile, run and test client and server
- Remote Testing: Linux on the university TSAM server, testing connections and communitcation with the other servers
## 3. Implemented Commands

### Client Commands
SENDMSG,<from>,<to>,<message> - Sends a message to the server which stores it destination

GETMSG,<username> Retrieves a stored message

STATUSREQ - Request information about stored messages

### Server Commands
HELO,<group> - Sends the command when we conenct to a server

SERVERS,<server list> - Send a list of servers that we are connected to

KEEPALIVE,<No. of messages> - Sends to a server to tell it how many messages we have for that server

GETMSGS,<GROUP_ID> - Rewuests stored messages for a group

SENDMSG,<FROM_GROUP_ID>,<TO_GROUP_ID>,<TIMESTAMP>,<message content> - Transfers a message between servers

STATUSREQ - Request a summary of stored messages

STATUSRESP,<name1>:<count1>,<name2>:<count2>,... - Responds with the number of messages stored for each group
### Additional Commands

GETMSG - Still is here from assignment 6

## 4. Server Behaviour
HELO - Identifies the connected peer and sends information about known servers using SERVERS.
SERVERS - Reads the list of known servers and attempts to establish connections to additional peers.
KEEPALIVE -	Receives information about pending messages. Your server also periodically sends keepalives with the number of messages waiting for each connected peer.
STATUSREQ -	Responds with STATUSRESP, containing the number of stored messages for each destination group, or EMPTY if none exist.
STATUSRESP -	Checks whether another server has messages for G_nammi and requests them using GETMSGS.
GETMSGS -	Checks the requesting peer's group name and sends its stored messages in SENDMSG format.
SENDMSG -	Receives a message and stores it for the destination group. Messages received for G_nammi are handled as local messages.
GETMSG -	Retained from Assignment 6. Retrieves one stored message for the local group.

When the server starts up it starts listening for incoming connections and attempts to connect to the initial instuctor server

When the server gets a new connection it exchanges HELO and SERVERS messages to identify new groups

When a server is disconnected, our server removes the disconnected server from active connections

Messages are stored in memory according to their destinaton groups. 

Our servers send KEEPALIVE messages every 60 seconds to advertise to other servers how many messages we have for them

Our server rejects invalid commands

Our server stores messages and sends them when teh destination peer requests them using GETMSG

Our server can retrieve messages from other grous to forward them to them


## 5. Expected Bonus Points and Evidence

[Use the assignment's exact grading criteria. For each point you claim, explain why you believe it is satisfied and identify the relevant server.log entries.]

| Grading criterion | Expected points | Evidence timestamp (UTC) | Explanation |
|---|---|---|---|
| [Criterion] | [Points] | [YYYY-MM-DDTHH:MM:SSZ] | [Explanation] |
| [Criterion] | [Points] | [YYYY-MM-DDTHH:MM:SSZ] | [Explanation] |
| [Criterion] | [Points] | [YYYY-MM-DDTHH:MM:SSZ] | [Explanation] |



## 6. Authorship and Sources

### Group Members

**Member 1:** [Sölvi Sigmarsson / solvi24]
1. Server connections and connection management
2. HELO command
3. SERVERS command
5. STATUSREQ and STATUSRESP commands

**Member 2:** [Anton Fannar Kjartansson / antonk24]
1. KEEPALIVE command
2. SENDMSG command
3. GETMSGS command


### Tools and External Sources
- VS Code
- GitHub
- ChatGPT - To explain the assignement better and give us a better understanding, helping us with c++ commands and helping us debug
