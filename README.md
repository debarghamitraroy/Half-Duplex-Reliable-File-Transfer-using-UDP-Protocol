# Half-Duplex Reliable File Transfer over UDP Protocol

Implement naive flow control mechanism using **(a)** Stop and Wait Protocol and **(b)** Selective and Repeat Protocol. Transfer files (Text, Image, Audio, Video) using UDP protocol. If during the connection suddenly connection is terminated then you have start ones again, it simply resume the process not start from beginning.

Write a socket program in C for Multimodal File Transmission using UDP with Half-Duplex Stop and Wait protocol. The program/protocol should support the following properties/mechanism

1. The protocol will send any type of files

2. Each packet should consist of the file name, sequence number / acknowledgement number

3. A log file should be generated with some information like,

List of uncommon files in server and client which are to be transferred, Start time, If the connection is broken then the $\%$ of the file already uploaded, How many times connections were established during the complete transmission, End time (when the file is fully transmitted), How many packets are lost, How many time-outs are occurred, etc.

## Objective

Implement a reliable file transfer system over the UDP protocol. Since UDP itself is unreliable (packets may be lost, duplicated, or arrive out of order), you must design a mechanism to ensure reliability using techniques such as segmentation, sequencing, acknowledgments, and retransmission.

## Tasks

- ### File Segmentation

  Divide the file into fixed-size segments (e.g., $1$ KB each). Each segment should contain metadata:
  - Sequence Number
  - File Name / File Identifier
  - Data Payload

- ### Stop-and-Wait Protocol

  Implement a Stop-and-Wait ARQ (Automatic Repeat Request) mechanism:
  - Sender transmits one segment at a time.
  - Sender waits for an acknowledgment (ACK) from the receiver.
  - If the ACK is not received within a timeout, retransmit the segment.
  - Receiver sends ACK for each correctly received segment (use sequence number to handle duplicates).

- ### Acknowledgment Handling

  Receiver must send back an ACK containing:
  - Sequence Number of the successfully received segment
  - File identifier (to handle multiple file transfers, if extended later)

- ### Reassembly of File

  Receiver collects all the received segments in order. Write them back into the original file format after successful transfer.

---

## Solution

- Go to the [server][def10] directory and open the terminal.

- Run the [`server.c`][def8] file using the below command:

  ```bash
  cd server
  gcc server.c -o server
  ```

- Run the `server` file using the below command:

  ```bash
  ./server
  ```

- Now go to the [client][def11] directory and open another terminal and run the [`client.c`][def9] file using the below command:

  ```bash
  gcc client.c -o client
  ```

- Run the `client` file using the below command:

  ```bash
  ./client
  ```

- Now give the folder names that you want to be compared.
  - [`server`][def10] folder before transfer content.

    [![server folder][def12]][def12]

  - [`client`][def11] folder before transfer content.

    [![client folder][def13]][def13]

- The client is requested for the files.

  [![client terminal][def15]][def15]

- The server will transfer the files.

  [![Server terminal][def14]][def14]

- [`client`][def11] after transfer (added the files of folder2).

  [![client folder][def16]][def16]

---

> - Open [WireShark][def1] and click on `Loopback: lo` to capture the packets.
>
>   [![WireShark Capture][def2]][def2]
>
> - Now go to the [WireShark][def1] window and filter the packets by typing below command in the filter bar.
>
>   ```ini
>   udp.port == 8080
>   ```
>
> - Go to the directory and open the terminal.
> - Open another terminal and build the [`sender.c`][def3] and [`receiver.c`][def4] by running the below command:
>
>   ```bash
>   make
>   ```
>
> - Open the terminal and start the receiver (listen on all interfaces on `port 8080`, no simulated loss).
>
>   ```bash
>   ./receiver 127.0.0.1 8080 ./output 0
>   ```
>
> - Open another terminal and Run the sender (send [`test.txt`][def5] to receiver at `127.0.0.1:8080`; no simulated loss).
>
>   ```bash
>   ./sender 127.0.0.1 8080 ./output/test.txt 0
>   ```
>
> [![Data Folder][def6]][def6]
>
> - After completion, the receiver writes `received_test.txt`.
>
> [![Output Folder][def7]][def7]
>
> - **Testing Retransmissions**
> - Open a terminal and run the below command.
>
>   ```bash
>   ./sender 127.0.0.1 8080 ./data/test.txt 20
>   ```
>
> - Open another terminal and run below comman.
>
>   ```bash
>   ./receiver 0.0.0.0 8080 ./output 20
>   ```

[def1]: https://www.wireshark.org/
[def2]: ./images/img_01.png
[def3]: ./sender.c
[def4]: ./receiver.c
[def5]: ./data/test.txt
[def6]: ./images/img_02.png
[def7]: ./images/img_03.png
[def8]: ./server/server.c
[def9]: ./client/client.c
[def10]: ./server/
[def11]: ./client/
[def12]: ./images/img_04.png
[def13]: ./images/img_05.png
[def14]: ./images/img_06.png
[def15]: ./images/img_07.png
[def16]: ./images/img_08.png
