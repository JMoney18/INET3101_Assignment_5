# Assignment 5: C Socket File Transfer and AI Network Security Audit

## Problem Statement & Architecture

For this assignment, I built a client and server in C that use TCP sockets to transfer file contents. The client asks the user for a filename and sends it to the server. The server checks the request and returns the file contents if the file is valid and no larger than 255 bytes. For completed requests that cannot be fulfilled, it sends an error message.

I used ChatGPT to help write the code, explain the socket functions, and develop tests. We built the programs in small steps, and I compiled and tested each change on my Mac using VS Code.

### How the Server Works

The server creates an IPv4 TCP socket and listens on `127.0.0.1` using port `8080`. This address is the loopback address, so the client and server run on the same computer.

I used a loop to keep the server running after each request. It handles one client at a time, closes that client's socket when finished, and waits for the next connection. It looks for requested files in the directory where the server was started.

AI helped me build and understand the sequence of `socket()`, `bind()`, `listen()`, and `accept()`. I tested each part before adding file requests and responses.

### How the Client Works

The client asks for a filename before connecting. I originally had it connect first, but moved the prompt because the server could time out while the user was still typing. With AI's help, I changed the order and tested it by waiting about 30 seconds before entering a filename.

After the user enters a filename, the client connects to the server, sends the request, and prints the response in the terminal.

### Request and Response Format

Each connection handles one file request. The client sends the filename followed by a newline, which tells the server where the request ends.

TCP does not guarantee that a request arrives all at once. To handle this, the server reads one byte at a time until it receives the newline. Both programs also use loops when sending because `send()` might send fewer bytes than requested.

For a successful request, the server sends the file's contents and closes the connection. The client keeps receiving until the connection closes. Errors for completed requests start with `ERROR:`. Malformed or incomplete requests, such as filenames containing null bytes, are logged by the server and closed without a response.

AI helped generate a Python testing script that sends raw socket requests. I used it to check normal transfers, malformed requests, and whether the server could still handle a valid request after each attack.

### Port Management and Shutdown

Both programs use port `8080`. I added `SO_REUSEADDR` to the server so it can reuse the address after earlier connections when restarted.

The server has a five-second receive timeout to prevent an idle client from holding up other requests. This limits inactivity during each receive call, rather than the total time allowed for a request.

For shutdown, I added a `SIGINT` handler that sets a flag when Ctrl+C is pressed. The main program then closes the sockets and exits. Both programs ignore `SIGPIPE` so sending to a disconnected client returns an error instead of stopping the program.

AI helped me add these protections and plan the tests. I checked the results in the terminal, including clean shutdown and recovery after a client disconnected.

## Socket Security & Buffer Analysis

With AI's help, I added checks for unsafe filenames, oversized requests, and disconnected clients. I tested these checks manually and then added them to `fuzzer.py`.

### Request Framing and Buffer Limits

The server uses a 256-byte filename buffer. It allows up to 255 filename bytes and reserves the last byte for the null terminator that C strings need. Before storing another byte, it checks whether the buffer is full.

The server reads until it receives a newline instead of assuming one `recv()` call will contain the entire request. I tested this by sending a filename in separate pieces, and the server still returned the correct file.

Embedded null bytes are rejected because they could make C string functions treat only part of a request as the filename.

### File Size Checks

The server reads up to 256 bytes into its content buffer. If it reads a 256th byte, it rejects the file for exceeding the 255-byte limit.

I tested both sides of this limit: a 255-byte file was accepted, and a 256-byte file returned an error. File contents are sent using the number of bytes read, so the content buffer does not need a null terminator.

### Filename and Path Validation

The server allows letters, numbers, dots, underscores, and hyphens in filenames. It rejects empty names, names containing `..`, and other characters such as directory separators. This blocks requests like `../example.txt` and `/etc/passwd` before opening a file.

During testing, I found that filename validation alone did not stop symbolic links. A link with a valid filename could point outside the server's directory. With AI's help, I replaced `fopen()` with `open()` using `O_NOFOLLOW`, then used `fdopen()` to keep the existing file-reading code.

The server also checks the opened file with `fstat()` and accepts only regular files. `O_NONBLOCK` prevents opening a named pipe from making the server wait.

### Disconnects and Resource Cleanup

The server checks the return values from socket calls. A return value of zero from `recv()` means the client closed its sending side. A return value of `-1` indicates an error, which the server handles separately.

Both programs ignore `SIGPIPE` and handle interrupted sending and receiving calls. The server closes each client socket after processing its request, including failed requests. It also closes file resources after reading or when a file check fails.

### Testing Results and Limits

All 14 automated tests passed against the current server. An earlier version also passed 12 tests while compiled with AddressSanitizer and UndefinedBehaviorSanitizer, with no sanitizer reports shown during that run.

These results show how the server behaved for the tested inputs. The five-second receive timeout only limits inactivity: a client sending bytes slowly can keep a request open longer. The server also handles clients one at a time, so other clients must wait while a request is being processed.

## Pros & Cons of Solution

### Pros

- **Simple design:** Handling one client at a time made the code easier to follow and test. Building it step by step with AI helped me understand how the socket functions fit together.
- **Bounded buffers:** The server checks filename length before storing bytes and rejects file contents larger than 255 bytes.
- **Path checks:** Filename validation blocks directory paths, and `O_NOFOLLOW` prevents the server from following symbolic links.
- **Recovery after errors:** The tests showed that the server could handle a valid request after malformed requests and client disconnects.
- **Repeatable tests:** The AI-assisted Python fuzzer creates its test files and checks responses automatically.

### Cons

- **One client at a time:** An active client holds up other requests. A threaded or process-based server could handle multiple clients at once, but would require more complicated resource management.
- **Inactivity timeout only:** A client sending a byte before each timeout can keep the server busy longer than five seconds. A total request deadline would provide a stronger limit.
- **Basic protocol:** The connection closing marks the end of the response. There is no separate status field, so file contents beginning with `ERROR:` could look like an error message.
- **Limited filenames and file sizes:** The filename rules reject spaces and subdirectories, and the 255-byte content limit makes this suitable for small files.
- **Local connections only:** The server binds to the loopback address. It also has no authentication or encryption.
- **Testing has limits:** Passing the fuzzer and sanitizer checks does not cover every possible input or timing condition.

## AI Fuzzing Reflection

### How I Used AI

I used ChatGPT to help write the C programs and generate the Python fuzzer. I asked to work step by step so I could compile each change, run it, and check the result before continuing. I also used AI to explain socket functions, buffer checks, signal handling, and errors that appeared during testing.

My requests included:

- “Review my server.c for buffer overflows, path traversal, and socket cleanup issues. Explain any problems and help me fix them one at a time.”
- “Help me improve fuzzer.py to test malformed TCP requests. Check that each test verifies the server’s response and that it still handles a normal request afterward.”
- “Explain how my server handles a filename arriving across multiple recv() calls and why the buffer needs space for a null terminator.”

The fuzzer was developed throughout the conversation rather than generated from one separate prompt. AI supplied each test, and I added and ran them individually before continuing.

### Fuzzer Tests

The script sends raw TCP requests directly to the server. It includes 14 tests covering:

- Normal file transfer.
- Missing files and the 255-byte file-size boundary.
- Relative, nested, and absolute path requests.
- Embedded null bytes.
- A 10,000-byte payload without a newline or null terminator.
- Immediate and partial-request disconnects.
- An idle client that triggers the receive timeout.
- Symbolic-link rejection.
- A filename sent in separate pieces.

Most tests also make a normal request afterward to check that the server can still serve files. Unexpected responses, connection failures during recovery, and socket timeouts cause the script to fail.

### Issues Found and Changes Made

One issue was the client connecting before asking for a filename. Once the server had a receive timeout, taking too long to type could cause the connection to close. I moved the prompt before the connection and verified that a request still worked after waiting about 30 seconds.

Testing also found that a valid-looking filename could be a symbolic link to a file outside the server's directory. The original file-opening code followed the link and returned its contents. I changed it to use `O_NOFOLLOW` and check for regular files with `fstat()`. The same request was then rejected.

A connection-reset test produced a “Broken pipe” error during sending. After adding `SIGPIPE` handling, the server handled the error and successfully served the next client.

Other improvements included a specific oversized-filename message, cleaner output during Ctrl+C shutdown, and increasing the client's request buffer to fit 255 filename bytes plus a newline and null terminator.

### Results and What I Learned

All 14 automated tests passed against the current server. An earlier version passed 12 tests with AddressSanitizer and UndefinedBehaviorSanitizer enabled, without sanitizer reports during that run. No memory crash was observed in those tests.

This assignment helped me understand that getting a normal socket connection working is only part of the task. TCP requests can arrive in pieces, clients can disconnect unexpectedly, and filenames need checks before they are used to open files. AI helped write and explain the code, but compiling and testing each step showed whether the changes behaved as expected.

## Building and Running

I developed and tested this project on macOS using VS Code, Apple Clang, and Python 3. The Python fuzzer uses only standard-library modules.

Run all commands from the assignment folder.

### Compile

```sh
clang -Wall -Wextra -Wpedantic server.c -o server
clang -Wall -Wextra -Wpedantic client.c -o client
```

### Start the Server

In the first terminal:

```sh
./server
```

The server listens on `127.0.0.1:8080`. Leave it running while using the client or fuzzer.

### Run the Automated Tests

In a second terminal:

```sh
python3 fuzzer.py
```

The fuzzer creates `example.txt`, `oversized.txt`, and `boundary.txt` in the current directory. It also creates a symbolic-link fixture pointing to a test file in the parent directory. These three sample files are overwritten when the script runs. The missing-file test requires that `missing.txt` does not exist.

A successful run prints 14 `PASS` lines. If a test fails, the script prints an exception explaining the failure.

### Run the Client

After the fuzzer has created the sample files:

```sh
./client
```

Enter `example.txt` when prompted. The expected file contents are:

```text
Hello from Assignment 5!
```

Other requests to try include `missing.txt`, `oversized.txt`, and `../example.txt`.

### Stop the Server

Press Ctrl+C in the server terminal. The server closes its sockets and prints a shutdown message.

### Run with Sanitizers

Stop any running server before starting the sanitized version:

```sh
clang -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer server.c -o server_sanitized
./server_sanitized
```

Then run `python3 fuzzer.py` in the second terminal. Check both the test results and the server terminal for sanitizer reports.